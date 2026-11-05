#include "Platform.h"
#include "AccelerometerManager.h"

#import <UIKit/UIKit.h>
#import <AVFoundation/AVFoundation.h>
#import <CoreMotion/CoreMotion.h>

namespace Platform
{
    bool isTablet()
    {
        return [UIDevice currentDevice].userInterfaceIdiom == UIUserInterfaceIdiomPad;
    }

    // iPhone's notch inset is already enough; the iPad's corners aren't.
    bool needsRailGutter() { return isTablet(); }

    void requestMicrophone (std::function<void (bool)> done)
    {
        JUCE_BEGIN_IGNORE_DEPRECATION_WARNINGS
        [[AVAudioSession sharedInstance] requestRecordPermission: ^(BOOL granted)
        {
            juce::MessageManager::callAsync ([done, granted] { done (granted); });
        }];
        JUCE_END_IGNORE_DEPRECATION_WARNINGS
    }

    juce::String microphoneSettingsPath() { return "Settings > Midi-Morf > Microphone"; }

    juce::File documentsDirectory()
    {
        return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    }

    juce::String importedFileName (const juce::URL& url)
    {
        return juce::URL::removeEscapeChars (url.getFileName());
    }

    juce::String importableAudioPatterns()
    {
        return "*.wav;*.aif;*.aiff;*.mp3;*.m4a;*.aac;*.caf;*.flac";
    }

    juce::String importableAudioNames() { return "WAV, AIFF, MP3, M4A"; }

    // The rate is pinned by JUCE_IOS_AUDIO_EXPLICIT_SAMPLERATES in the .jucer.
    void configureAudioDevice (juce::AudioDeviceManager&) {}

    // Reopening on resume can fail with AudioQueueNew -50 while the session is
    // in flux, so iOS keeps it open.
    bool closesAudioDeviceInBackground() { return false; }

    void configureWindow() {}

    // CoreGraphics honours dirty rects, so iOS stays on JUCE's default.
    void attachRenderer (juce::Component&) {}
    void detachRenderer() {}
}

//==============================================================================
struct AccelerometerManager::Impl
{
    CMMotionManager* motionManager = nil;
};

AccelerometerManager::AccelerometerManager() : impl (std::make_unique<Impl>()) {}
AccelerometerManager::~AccelerometerManager() { stop(); }

void AccelerometerManager::start()
{
    if (impl->motionManager != nil)
        return;

    impl->motionManager = [[CMMotionManager alloc] init];

    if (! impl->motionManager.deviceMotionAvailable)
    {
        DBG ("AccelerometerManager: device motion not available");
        impl->motionManager = nil;
        return;
    }

    impl->motionManager.deviceMotionUpdateInterval = 1.0 / 60.0;

    [impl->motionManager startDeviceMotionUpdatesToQueue:[NSOperationQueue mainQueue]
        withHandler:^(CMDeviceMotion* motion, NSError* error)
        {
            if (error != nil || motion == nil)
                return;

            // gravity gives us a nice filtered tilt vector
            // x: left/right tilt (-1 to 1)
            // y: forward/back tilt (-1 to 1)
            float tiltX = (float) motion.gravity.x;
            float tiltY = (float) motion.gravity.y;

            if (listener != nullptr)
                listener->tiltUpdated (tiltX, tiltY);
        }];

    DBG ("AccelerometerManager: started");
}

void AccelerometerManager::stop()
{
    if (impl->motionManager != nil)
    {
        [impl->motionManager stopDeviceMotionUpdates];
        impl->motionManager = nil;
        DBG ("AccelerometerManager: stopped");
    }
}
