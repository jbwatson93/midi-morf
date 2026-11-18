#define JUCE_CORE_INCLUDE_JNI_HELPERS 1   // for the Wi-Fi multicast lock
#include "LinkSync.h"

// Android uses Ableton's open-source Link (GPLv2+, or Ableton's proprietary
// license for a closed release). It isn't in the repo: clone it into
// MidiMorfApp/ThirdParty/link (see LINK.md). Without it the stub in
// LinkSync.cpp is used. The .jucer's compile list is shared, so iOS sees
// this file too.
#if JUCE_ANDROID && __has_include(<ableton/Link.hpp>)

#include <ableton/Link.hpp>

namespace
{
    constexpr double Quantum = 4.0;   // one bar of 4/4: Link keeps bar lines in phase

    // LinkKit remembers its own settings on iOS; here we do.
    std::unique_ptr<juce::PropertiesFile> openSettings()
    {
        juce::PropertiesFile::Options options;
        options.applicationName = "MidiMorf";
        return std::make_unique<juce::PropertiesFile> (
            juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Link.settings"),
            options);
    }

    // Many phones drop multicast packets to save power unless an app holds
    // this lock, and Link finds its peers by multicast.
    class MulticastLock
    {
    public:
        ~MulticastLock() { set (false); }

        void set (bool on)
        {
            auto* env = juce::getEnv();
            if (env == nullptr || on == (lock != nullptr))
                return;

            if (! on)
            {
                call ("release");
                lock.clear();
                return;
            }

            const auto context = juce::getAppContext();
            if (context == nullptr)
                return;

            const juce::LocalRef<jclass> contextClass { env->GetObjectClass (context.get()) };
            const auto getSystemService = env->GetMethodID (contextClass, "getSystemService",
                                                            "(Ljava/lang/String;)Ljava/lang/Object;");
            const juce::LocalRef<jobject> wifi { env->CallObjectMethod (context.get(), getSystemService,
                                                                        juce::javaString ("wifi").get()) };
            if (juce::jniCheckHasExceptionOccurredAndClear() || wifi == nullptr)
                return;

            const juce::LocalRef<jclass> wifiClass { env->GetObjectClass (wifi.get()) };
            const auto create = env->GetMethodID (wifiClass, "createMulticastLock",
                                                  "(Ljava/lang/String;)Landroid/net/wifi/WifiManager$MulticastLock;");
            const juce::LocalRef<jobject> l { env->CallObjectMethod (wifi.get(), create,
                                                                     juce::javaString ("Midi-Morf Link").get()) };
            if (juce::jniCheckHasExceptionOccurredAndClear() || l == nullptr)
                return;

            lock = juce::GlobalRef (l);
            call ("acquire");
        }

    private:
        void call (const char* method)
        {
            auto* env = juce::getEnv();
            const juce::LocalRef<jclass> lockClass { env->GetObjectClass (lock.get()) };
            env->CallVoidMethod (lock.get(), env->GetMethodID (lockClass, method, "()V"));
            juce::jniCheckHasExceptionOccurredAndClear();
        }

        juce::GlobalRef lock;
    };

    // Android has no Ableton pane, so this stands in for it: the two switches
    // Ableton's guidelines ask for, named the way they ask, and who's there.
    class SettingsPanel : public juce::Component, private juce::Timer
    {
    public:
        SettingsPanel (std::function<bool()> isOn, std::function<void (bool)> setOn,
                       std::function<bool()> isSync, std::function<void (bool)> setSync,
                       std::function<int()> peers)
            : getOn (std::move (isOn)), getSync (std::move (isSync)), getPeers (std::move (peers))
        {
            enable.onClick = [this, setOn = std::move (setOn)] { setOn (enable.getToggleState()); refresh(); };
            sync.onClick   = [this, setSync = std::move (setSync)] { setSync (sync.getToggleState()); };
            status.setJustificationType (juce::Justification::centredLeft);
            for (auto* c : std::initializer_list<juce::Component*> { &enable, &sync, &status })
                addAndMakeVisible (c);
            refresh();
            startTimerHz (4);
            setSize (280, 132);
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (12, 8);
            enable.setBounds (r.removeFromTop (38));
            sync.setBounds (r.removeFromTop (38));
            status.setBounds (r);
        }

    private:
        void timerCallback() override { refresh(); }

        void refresh()
        {
            enable.setToggleState (getOn(), juce::dontSendNotification);
            sync.setToggleState (getSync(), juce::dontSendNotification);
            sync.setEnabled (getOn());

            juce::String text = "Ableton Link: Disabled";
            if (getOn())
            {
                const int n = getPeers();
                text = n == 0 ? juce::String ("No other apps connected")
                              : "Connected to " + juce::String (n) + (n == 1 ? " app" : " apps");
            }
            status.setText (text, juce::dontSendNotification);
        }

        juce::ToggleButton enable { "Ableton Link" }, sync { "Start Stop Sync" };
        juce::Label status;
        std::function<bool()> getOn, getSync;
        std::function<int()> getPeers;
    };
}

// Link calls back on its own thread; everything the app hears goes through
// the message thread, as LinkKit's callbacks do on iOS.
struct LinkSync::Impl : private juce::AsyncUpdater
{
    Impl()
    {
        enabled.store (settings->getBoolValue ("enabled", false));
        startStopSync.store (settings->getBoolValue ("startStopSync", false));
        link.enableStartStopSync (startStopSync.load());

        link.setNumPeersCallback ([this] (std::size_t n) { peers.store ((int) n); triggerAsyncUpdate(); });
        link.setTempoCallback ([this] (double) { triggerAsyncUpdate(); });
        link.setStartStopCallback ([this] (bool playing)
        {
            remotePlaying.store (playing);
            startStopPending.store (true);
            triggerAsyncUpdate();
        });
    }

    ~Impl()
    {
        link.enable (false);
        cancelPendingUpdate();
    }

    // Link runs while the app is in front and the user has it on.
    void apply()
    {
        const bool on = active && enabled.load();
        multicast.set (on);
        link.enable (on);
        if (! on)
            peers.store (0);
        changed();
    }

    void handleAsyncUpdate() override
    {
        if (startStopPending.exchange (false) && owner->onStartStop)
            owner->onStartStop (remotePlaying.load());
        changed();
    }

    void changed() { if (owner->onStateChanged) owner->onStateChanged(); }

    LinkSync* owner = nullptr;
    std::unique_ptr<juce::PropertiesFile> settings = openSettings();
    MulticastLock multicast;
    bool active = false;

    std::atomic<bool> enabled { false }, startStopSync { false };
    std::atomic<bool> remotePlaying { false }, startStopPending { false };
    std::atomic<int> peers { 0 };
    std::atomic<double> pendingTempo { 0.0 };   // 0 = none
    std::atomic<int64_t> latencyMicros { 0 };

    // Audio thread, between beginBlock and endBlock.
    std::optional<ableton::Link::SessionState> state;
    std::chrono::microseconds blockTime {};
    bool dirty = false;

    // Last, so it stops calling back before the rest goes.
    ableton::Link link { 120.0 };
};

LinkSync::LinkSync() : impl (std::make_unique<Impl>()) { impl->owner = this; }
LinkSync::~LinkSync() = default;

bool LinkSync::isAvailable()           { return true; }
bool LinkSync::isEnabled() const       { return impl->enabled.load(); }
int  LinkSync::numPeers() const        { return impl->peers.load(); }
void LinkSync::setActive (bool a)      { impl->active = a; impl->apply(); }
void LinkSync::requestTempo (double b) { impl->pendingTempo.store (b); }

void LinkSync::setOutputLatencySeconds (double s)
{
    impl->latencyMicros.store ((int64_t) std::llround (juce::jmax (0.0, s) * 1.0e6));
}

void LinkSync::showSettings (juce::Component& from)
{
    auto& d = *impl;
    auto panel = std::make_unique<SettingsPanel> (
        [&d] { return d.enabled.load(); },
        [&d] (bool on)
        {
            d.enabled.store (on);
            d.settings->setValue ("enabled", on);
            d.settings->saveIfNeeded();
            d.apply();
        },
        [&d] { return d.startStopSync.load(); },
        [&d] (bool on)
        {
            d.startStopSync.store (on);
            d.link.enableStartStopSync (on);
            d.settings->setValue ("startStopSync", on);
            d.settings->saveIfNeeded();
            d.changed();
        },
        [&d] { return d.peers.load(); });

    juce::CallOutBox::launchAsynchronously (std::move (panel), from.getScreenBounds(), nullptr);
}

//==============================================================================
// Audio thread.

// Oboe gives no buffer time, so beginBlock reads Link's own clock instead.
void LinkSync::setBufferHostTimeNs (uint64_t) {}

bool LinkSync::beginBlock (Block& b)
{
    auto& d = *impl;
    if (! d.enabled.load() || ! d.link.isEnabled())
        return false;

    d.blockTime = d.link.clock().micros() + std::chrono::microseconds (d.latencyMicros.load());
    d.state.emplace (d.link.captureAudioSessionState());
    d.dirty = false;

    if (const double t = d.pendingTempo.exchange (0.0); t > 0.0)
    {
        d.state->setTempo (t, d.blockTime);
        d.dirty = true;
    }

    b.beat           = d.state->beatAtTime (d.blockTime, Quantum);
    b.bpm            = d.state->tempo();
    b.peers          = d.peers.load();
    b.startStopSync  = d.startStopSync.load();
    b.sessionPlaying = d.state->isPlaying();
    return true;
}

void LinkSync::requestBeatAtBlockStart (double beat)
{
    auto& d = *impl;
    if (! d.state) return;
    d.state->requestBeatAtTime (beat, d.blockTime, Quantum);
    d.dirty = true;
}

void LinkSync::setPlayingAtBeat (bool playing, double beat)
{
    auto& d = *impl;
    if (! d.state || ! d.startStopSync.load()) return;
    d.state->setIsPlaying (playing, d.state->timeAtBeat (beat, Quantum));
    d.dirty = true;
}

void LinkSync::endBlock()
{
    auto& d = *impl;
    if (d.state && d.dirty)
        d.link.commitAudioSessionState (*d.state);
    d.state.reset();
}

#endif
