#include "LinkSync.h"

#if JUCE_IOS && __has_include(<ABLLink.h>)

#import <UIKit/UIKit.h>
#import <AVFoundation/AVFoundation.h>
#include <ABLLink.h>
#include <ABLLinkSettingsViewController.h>
#include <mach/mach_time.h>

namespace
{
    constexpr double Quantum = 4.0;   // one bar of 4/4: Link keeps bar lines in phase

    UIViewController* topViewController()
    {
        UIViewController* top = nil;
        for (UIScene* scene in UIApplication.sharedApplication.connectedScenes)
            if ([scene isKindOfClass: UIWindowScene.class])
                for (UIWindow* w in ((UIWindowScene*) scene).windows)
                    if (w.isKeyWindow)
                        top = w.rootViewController;
        while (top.presentedViewController != nil)
            top = top.presentedViewController;
        return top;
    }
}

struct LinkSync::Impl
{
    Impl()
    {
        mach_timebase_info_data_t tb;
        mach_timebase_info (&tb);
        ticksPerNs = (double) tb.denom / (double) tb.numer;

        link = ABLLinkNew (120.0);
        enabled.store (ABLLinkIsEnabled (link));
        startStopSync.store (ABLLinkIsStartStopSyncEnabled (link));
        peers.store ((int) ABLLinkNumPeers (link));

        ABLLinkSetIsEnabledCallback (link, [] (bool on, void* c)
        {
            auto* self = static_cast<Impl*> (c);
            self->enabled.store (on);
            self->changed();
        }, this);
        ABLLinkSetIsStartStopSyncEnabledCallback (link, [] (bool on, void* c)
        {
            auto* self = static_cast<Impl*> (c);
            self->startStopSync.store (on);
            self->changed();
        }, this);
        ABLLinkSetNumPeersCallback (link, [] (size_t n, void* c)
        {
            auto* self = static_cast<Impl*> (c);
            self->peers.store ((int) n);
            self->changed();
        }, this);
        ABLLinkSetSessionTempoCallback (link, [] (double, void* c)
        {
            static_cast<Impl*> (c)->changed();
        }, this);
        ABLLinkSetStartStopCallback (link, [] (bool playing, void* c)
        {
            auto* self = static_cast<Impl*> (c);
            if (self->owner->onStartStop) self->owner->onStartStop (playing);
        }, this);
    }

    ~Impl() { ABLLinkDelete (link); }

    void changed() { if (owner->onStateChanged) owner->onStateChanged(); }

    LinkSync* owner = nullptr;
    ABLLinkRef link = nullptr;
    double ticksPerNs = 1.0;

    std::atomic<bool> enabled { false }, startStopSync { false };
    std::atomic<int> peers { 0 };
    std::atomic<double> pendingTempo { 0.0 };   // 0 = none
    std::atomic<uint64_t> bufferNs { 0 }, latencyTicks { 0 };

    // Audio thread, between beginBlock and endBlock.
    ABLLinkSessionStateRef state = nullptr;
    uint64_t blockHostTime = 0;
    bool dirty = false;
};

LinkSync::LinkSync() : impl (std::make_unique<Impl>()) { impl->owner = this; }
LinkSync::~LinkSync() = default;

bool LinkSync::isAvailable()          { return true; }
bool LinkSync::isEnabled() const      { return impl->enabled.load(); }
int  LinkSync::numPeers() const       { return impl->peers.load(); }
void LinkSync::setActive (bool a)     { ABLLinkSetActive (impl->link, a); }
void LinkSync::requestTempo (double b) { impl->pendingTempo.store (b); }

void LinkSync::setOutputLatencySeconds (double s)
{
    impl->latencyTicks.store ((uint64_t) std::llround (juce::jmax (0.0, s) * 1.0e9 * impl->ticksPerNs));
}

// Ableton's own pane, in a navigation bar with Done, as a sheet.
void LinkSync::showSettings()
{
    auto* top = topViewController();
    if (top == nil)
        return;

    auto* settings = [ABLLinkSettingsViewController instance: impl->link];
    auto* nav = [[UINavigationController alloc] initWithRootViewController: settings];
    __weak UINavigationController* weakNav = nav;
    settings.navigationItem.rightBarButtonItem =
        [[UIBarButtonItem alloc] initWithBarButtonSystemItem: UIBarButtonSystemItemDone
                                               primaryAction: [UIAction actionWithHandler: ^(UIAction*)
                                               {
                                                   [weakNav dismissViewControllerAnimated: YES completion: nil];
                                               }]];
    nav.modalPresentationStyle = UIModalPresentationFormSheet;
    [top presentViewController: nav animated: YES completion: nil];
}

//==============================================================================
// Audio thread.

void LinkSync::setBufferHostTimeNs (uint64_t ns) { impl->bufferNs.store (ns); }

bool LinkSync::beginBlock (Block& b)
{
    auto& d = *impl;
    if (! d.enabled.load())
        return false;

    const auto ns = d.bufferNs.load();
    d.blockHostTime = (ns != 0 ? (uint64_t) std::llround ((double) ns * d.ticksPerNs) : mach_absolute_time())
                    + d.latencyTicks.load();
    d.state = ABLLinkCaptureAudioSessionState (d.link);
    d.dirty = false;

    if (const double t = d.pendingTempo.exchange (0.0); t > 0.0)
    {
        ABLLinkSetTempo (d.state, t, d.blockHostTime);
        d.dirty = true;
    }

    b.beat           = ABLLinkBeatAtTime (d.state, d.blockHostTime, Quantum);
    b.bpm            = ABLLinkGetTempo (d.state);
    b.peers          = d.peers.load();
    b.startStopSync  = d.startStopSync.load();
    b.sessionPlaying = ABLLinkIsPlaying (d.state);
    return true;
}

void LinkSync::requestBeatAtBlockStart (double beat)
{
    auto& d = *impl;
    if (d.state == nullptr) return;
    ABLLinkRequestBeatAtTime (d.state, beat, d.blockHostTime, Quantum);
    d.dirty = true;
}

void LinkSync::setPlayingAtBeat (bool playing, double beat)
{
    auto& d = *impl;
    if (d.state == nullptr || ! d.startStopSync.load()) return;
    ABLLinkSetIsPlaying (d.state, playing, ABLLinkTimeAtBeat (d.state, beat, Quantum));
    d.dirty = true;
}

void LinkSync::endBlock()
{
    auto& d = *impl;
    if (d.state != nullptr && d.dirty)
        ABLLinkCommitAudioSessionState (d.link, d.state);
    d.state = nullptr;
}

#endif
