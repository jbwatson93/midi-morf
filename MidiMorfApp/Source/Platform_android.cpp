#define JUCE_CORE_INCLUDE_JNI_HELPERS 1   // for configureWindow
#include "Platform.h"

// The .jucer's compile list is shared by every exporter, so iOS sees this file.
#if JUCE_ANDROID

#include "AccelerometerManager.h"

#include <android/looper.h>
#include <android/sensor.h>

namespace Platform
{
    bool isTablet()
    {
        // Android's own line for a tablet: 600 dp on the short side. JUCE's
        // logical pixels are dp here.
        if (const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
            return juce::jmin (display->totalArea.getWidth(), display->totalArea.getHeight()) >= 600;
        return false;
    }

    // Plenty of phones have no notch, so no inset at all.
    bool needsRailGutter() { return true; }

    void requestMicrophone (std::function<void (bool)> done)
    {
        juce::RuntimePermissions::request (juce::RuntimePermissions::recordAudio, std::move (done));
    }

    juce::String microphoneSettingsPath() { return "Settings > Apps > MidiMorfApp > Permissions"; }

    // The public Documents folder is restricted under scoped storage, and
    // anything left there outlives the app. This one needs no permission.
    juce::File documentsDirectory()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Documents");
    }

    // The picker returns a content:// URL whose last part is a document id, not
    // a name, and audio readers choose their format by the extension.
    juce::String importedFileName (const juce::URL& url)
    {
        return juce::AndroidDocument::fromDocument (url).getInfo().getName();
    }

    // No CoreAudio here, so only what JUCE decodes itself.
    juce::String importableAudioPatterns()
    {
        return "*.wav;*.aif;*.aiff;*.flac;*.ogg";
    }

    juce::String importableAudioNames() { return "WAV, AIFF, FLAC, OGG"; }

    void configureAudioDevice (juce::AudioDeviceManager& deviceManager)
    {
        auto* device = deviceManager.getCurrentAudioDevice();
        if (device == nullptr)
            return;

        auto setup = deviceManager.getAudioDeviceSetup();

        // Left alone, JUCE takes the lowest rate from 44.1k up. Phones mix at
        // 48k, so anything else misses the low-latency path.
        if (device->getAvailableSampleRates().contains (48000.0))
            setup.sampleRate = 48000.0;

        // JUCE's default is 40 ms with a low-latency path and 100 ms without,
        // too slow to play pads with. Phones without one keep their 100 ms.
        constexpr int lowLatencyDefaultLimit = 2400, target = 480;   // 50 ms, 10 ms at 48k
        if (device->getDefaultBufferSize() < lowLatencyDefaultLimit)
        {
            for (const auto size : device->getAvailableBufferSizes())
            {
                if (size >= target)
                {
                    setup.bufferSize = size;
                    break;
                }
            }
        }

        const auto error = deviceManager.setAudioDeviceSetup (setup, true);
        if (error.isNotEmpty())
            DBG ("Platform: couldn't configure the audio device: " << error);
    }

    // A stream left open keeps running, silent, and costs battery.
    bool closesAudioDeviceInBackground() { return true; }

    // A menu taking focus makes Android recalculate the window's insets, and
    // unless told otherwise it then squeezes the app clear of the camera cutout,
    // with the bare window showing beside it. JUCE only does this below API 30.
    void configureWindow()
    {
        auto* env = juce::getEnv();
        const auto activity = juce::getMainActivity();
        if (activity == nullptr)
            return;

        const juce::LocalRef<jclass> activityClass { env->GetObjectClass (activity.get()) };
        const auto getWindow = env->GetMethodID (activityClass, "getWindow", "()Landroid/view/Window;");
        const juce::LocalRef<jobject> window { env->CallObjectMethod (activity.get(), getWindow) };
        if (window == nullptr)
            return;

        const juce::LocalRef<jclass> windowClass { env->GetObjectClass (window.get()) };

        if (juce::getAndroidSDKVersion() >= 30)
            env->CallVoidMethod (window.get(),
                                 env->GetMethodID (windowClass, "setDecorFitsSystemWindows", "(Z)V"),
                                 (jboolean) false);

        // Whatever the app doesn't cover shows black, not the theme's white.
        const juce::LocalRef<jobject> decorView { env->CallObjectMethod (window.get(),
            env->GetMethodID (windowClass, "getDecorView", "()Landroid/view/View;")) };
        const juce::LocalRef<jclass> viewClass { env->GetObjectClass (decorView.get()) };
        env->CallVoidMethod (decorView.get(),
                             env->GetMethodID (viewClass, "setBackgroundColor", "(I)V"),
                             (jint) 0xff000000);
    }

    // Without this JUCE draws on the CPU, and the hardware canvas it copies to
    // ignores dirty rects, so any small repaint redraws the whole screen.
    static std::unique_ptr<juce::OpenGLContext> openGL;

    void attachRenderer (juce::Component& mainComponent)
    {
        openGL = std::make_unique<juce::OpenGLContext>();
        openGL->attachTo (mainComponent);
    }

    void detachRenderer()
    {
        openGL = nullptr;
    }
}

//==============================================================================
struct AccelerometerManager::Impl
{
    ASensorManager* sensorManager = nullptr;
    const ASensor* sensor = nullptr;
    ASensorEventQueue* queue = nullptr;
    juce::Desktop::DisplayOrientation orientation = juce::Desktop::rotatedAntiClockwise;
    int eventsUntilOrientationCheck = 0;
    Listener** listener = nullptr;

    static int onEvents (int, int, void* data)
    {
        auto& self = *static_cast<Impl*> (data);
        ASensorEvent event;
        while (ASensorEventQueue_getEvents (self.queue, &event, 1) > 0)
        {
            // Asking the display goes through Java, and a flip is rare.
            if (--self.eventsUntilOrientationCheck <= 0)
            {
                self.orientation = juce::Desktop::getInstance().getCurrentOrientation();
                self.eventsUntilOrientationCheck = 30;
            }

            // Android reports m/s^2 pointing up; iOS reports units of g pointing
            // down, and the tilt code expects iOS's.
            const float x = -event.acceleration.x / ASENSOR_STANDARD_GRAVITY;
            const float y = -event.acceleration.y / ASENSOR_STANDARD_GRAVITY;

            // Sensor axes follow the device's natural way up, while the screen
            // can sit in either landscape: a quarter turn from natural on a
            // phone, natural on most tablets. iOS's frame is the phone's first.
            if (*self.listener != nullptr)
            {
                switch (self.orientation)
                {
                    case juce::Desktop::rotatedClockwise: (*self.listener)->tiltUpdated (-x, -y); break;
                    case juce::Desktop::upright:          (*self.listener)->tiltUpdated (y, -x);  break;
                    case juce::Desktop::upsideDown:       (*self.listener)->tiltUpdated (-y, x);  break;
                    default:                              (*self.listener)->tiltUpdated (x, y);   break;
                }
            }
        }
        return 1;   // keep listening
    }
};

AccelerometerManager::AccelerometerManager() : impl (std::make_unique<Impl>()) {}
AccelerometerManager::~AccelerometerManager() { stop(); }

void AccelerometerManager::start()
{
    if (impl->queue != nullptr)
        return;

    auto* looper = ALooper_forThread();
    impl->sensorManager = ASensorManager_getInstanceForPackage ("com.retrorelic.midimorfapp");
    if (looper == nullptr || impl->sensorManager == nullptr)
        return;

    impl->sensor = ASensorManager_getDefaultSensor (impl->sensorManager, ASENSOR_TYPE_GRAVITY);
    if (impl->sensor == nullptr)
        impl->sensor = ASensorManager_getDefaultSensor (impl->sensorManager, ASENSOR_TYPE_ACCELEROMETER);
    if (impl->sensor == nullptr)
    {
        DBG ("AccelerometerManager: no gravity sensor");
        return;
    }

    impl->listener = &listener;
    impl->queue = ASensorManager_createEventQueue (impl->sensorManager, looper, ALOOPER_POLL_CALLBACK, Impl::onEvents, impl.get());
    if (impl->queue == nullptr)
        return;

    ASensorEventQueue_enableSensor (impl->queue, impl->sensor);
    ASensorEventQueue_setEventRate (impl->queue, impl->sensor, 1000000 / 60);
}

void AccelerometerManager::stop()
{
    if (impl->queue != nullptr)
    {
        ASensorEventQueue_disableSensor (impl->queue, impl->sensor);
        ASensorManager_destroyEventQueue (impl->sensorManager, impl->queue);
        impl->queue = nullptr;
    }
}

#endif
