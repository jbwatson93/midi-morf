#pragma once

#include <JuceHeader.h>

//==============================================================================
// Ableton Link: a shared tempo, bar line and (if the user switches it on)
// start/stop with other apps on the same Wi-Fi, Live included. Link sends no
// notes, only where the beat is, so Wi-Fi jitter doesn't reach the groove:
// each device plays its own sound on its own clock, locked to the same beat.
//
// LocalSequencers asks where the beat is at the start of every audio block
// and lines its clock up with it (see LocalSequencers::followLink).
//
// iOS uses Ableton's LinkKit (LinkSync_ios.mm). The SDK isn't in the repo
// (its license forbids passing it on), so a checkout without it, and every
// other platform, gets the stub in LinkSync.cpp: isAvailable() is false and
// Link never turns on.
//
// Threading: setters, getters and callbacks are message thread, except the
// "audio thread" section.
class LinkSync
{
public:
    LinkSync();
    ~LinkSync();

    // False when the app was built without LinkKit.
    static bool isAvailable();

    //==========================================================================
    // Message thread.

    // The user turns Link on and off in Ableton's own settings pane.
    bool isEnabled() const;
    int  numPeers() const;
    void showSettings();

    // Called when the app goes to and from the background. Link stays
    // active in the background only while something is playing.
    void setActive (bool active);

    // A tempo change from the app, sent to the session on the next block.
    void requestTempo (double bpm);

    // Hardware output latency, so the beat Link reports is the one heard.
    void setOutputLatencySeconds (double seconds);

    // Another app started or stopped the session (start/stop sync on).
    std::function<void (bool playing)> onStartStop;
    // Enabled, peers or tempo changed: worth repainting.
    std::function<void()> onStateChanged;

    //==========================================================================
    // Audio thread.

    struct Block
    {
        double beat = 0.0;        // at the first sample of the block, as heard
        double bpm  = 120.0;
        int    peers = 0;
        bool   startStopSync = false;
        bool   sessionPlaying = false;   // only meaningful with startStopSync
    };

    // The device callback's time for this buffer, before the block renders.
    void setBufferHostTimeNs (uint64_t ns);

    // False when Link is off. Otherwise fills `b` and keeps the session
    // state until endBlock().
    bool beginBlock (Block& b);

    // Map `beat` to the first sample of this block (only when alone: with
    // peers Link keeps the session's bar line, which is what we want).
    void requestBeatAtBlockStart (double beat);

    // Our transport started or stopped at `beat` (sent only with start/stop sync).
    void setPlayingAtBeat (bool playing, double beat);

    void endBlock();

private:
    struct Impl;
    std::unique_ptr<Impl> impl;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LinkSync)
};
