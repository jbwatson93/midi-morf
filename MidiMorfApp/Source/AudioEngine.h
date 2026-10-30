#pragma once

#include <JuceHeader.h>
#include "WavetableEngine.h"
#include "LocalSequencers.h"

//==============================================================================
// Owns the iOS audio device, the track sources and the track rack. The device
// plays LocalSequencers, which renders the rack and runs the sequencers'
// Local-mode clock in between. Stereo out, no input.
//
// Tracks for now: T1 the main synth (the controllers), T2 a second synth with
// a bass patch (the step sequencer), T3 the break player (the slice
// sequencer), T4 empty.
class AudioEngine
{
public:
    AudioEngine()
    {
        bass.setState (bassPatch());
        rack.setSource (0, &synth);
        rack.setSource (LocalSequencers::DefaultStepTrack, &bass);
        rack.setSource (LocalSequencers::DefaultSliceTrack, &breaks);
    }

    ~AudioEngine()
    {
        shutdown();
    }

    // First call: initialise the audio device (iOS session creation happens
    // here under the hood). Subsequent calls just re-attach the callback so
    // foreground/background cycles don't keep tearing down the audio session
    // (which on iOS sometimes returns paramErr -50 if the session is in flux).
    void start()
    {
        if (running)
            return;

        if (! deviceInitialised)
        {
            auto error = deviceManager.initialiseWithDefaultDevices (0, 2);
            if (error.isNotEmpty())
            {
                DBG ("AudioEngine: device init failed: " << error);
                return;
            }
            deviceInitialised = true;
        }

        sourcePlayer.setSource (&sequencers);
        deviceManager.addAudioCallback (&sourcePlayer);
        running = true;
    }

    // Detach the audio callback but keep the iOS audio session alive. Avoids
    // the closeAudioDevice → reopen churn that was causing AudioQueueNew -50
    // on resume.
    void stop()
    {
        if (! running)
            return;

        deviceManager.removeAudioCallback (&sourcePlayer);
        sourcePlayer.setSource (nullptr);
        running = false;
    }

    // Full teardown — only called from MainComponent destructor / app shutdown.
    void shutdown()
    {
        stop();
        if (deviceInitialised)
        {
            deviceManager.closeAudioDevice();
            deviceInitialised = false;
        }
    }

    WavetableEngine& getSynth() noexcept { return synth; }
    WavetableEngine& getBassSynth() noexcept { return bass; }

    // Tilt is a global mod source: every synth hears it.
    void setTilt (float x, float y)
    {
        for (auto* s : { &synth, &bass })
        {
            s->setAccelX (x);
            s->setAccelY (y);
        }
    }

    // Any thread. Hard stop on every track — for suspend and panic.
    void allNotesOff()
    {
        rack.postAllNotesOffEverywhere();
        sequencers.stopAll();
    }
    LocalSequencers& getSequencers() noexcept { return sequencers; }
    TrackRack& getRack() noexcept             { return rack; }

    // 0..1 of the callback's time budget, averaged by JUCE.
    double getCpuUsage() const { return deviceManager.getCpuUsage(); }

private:
    // A mono-ish acid bass: saw plus a square an octave down, a resonant
    // ladder snapped shut by a fast filter envelope, velocity on cutoff so
    // the step page's VEL lane accents. Dry — no delay or reverb muddying the
    // low end.
    static juce::ValueTree bassPatch()
    {
        juce::ValueTree t (WavetableEngine::stateType);
        t.setProperty ("osc0_tableName", "Saw", nullptr);
        t.setProperty ("osc0_level", 1.0f, nullptr);
        t.setProperty ("osc0_semi", 0, nullptr);
        t.setProperty ("osc0_fine", 0.0f, nullptr);
        t.setProperty ("osc1_tableName", "Square", nullptr);
        t.setProperty ("osc1_level", 0.45f, nullptr);
        t.setProperty ("osc1_semi", -12, nullptr);
        t.setProperty ("osc1_fine", 4.0f, nullptr);
        t.setProperty ("unison", 1, nullptr);
        t.setProperty ("sub", 0.35f, nullptr);
        t.setProperty ("drive", 0.35f, nullptr);
        t.setProperty ("gain", 0.36f, nullptr);
        t.setProperty ("cutoff", 420.0f, nullptr);
        t.setProperty ("reso", 2.4f, nullptr);
        t.setProperty ("filtEnv", 3200.0f, nullptr);
        t.setProperty ("envPos", 0.0f, nullptr);
        t.setProperty ("amp",  "0.002,0.3,0.6,0.09", nullptr);
        t.setProperty ("fenv", "0.001,0.16,0.0,0.1", nullptr);
        t.setProperty ("lfoDepth", 0.0f, nullptr);
        t.setProperty ("routeSrc_0", WavetableEngine::ModSrcVelocity, nullptr);
        t.setProperty ("routeDest_0", WavetableEngine::ModDestCutoff, nullptr);
        t.setProperty ("routeAmount_0", 0.35f, nullptr);
        t.setProperty ("routeSrc_1", WavetableEngine::ModSrcModWheel, nullptr);
        t.setProperty ("routeDest_1", WavetableEngine::ModDestResonance, nullptr);
        t.setProperty ("routeAmount_1", 0.3f, nullptr);
        for (int i = 2; i < WavetableEngine::NumRoutingSlots; ++i)
        {
            t.setProperty ("routeSrc_" + juce::String (i), WavetableEngine::ModSrcOff, nullptr);
            t.setProperty ("routeDest_" + juce::String (i), WavetableEngine::ModDestOff, nullptr);
        }
        t.setProperty ("distOn", false, nullptr);
        t.setProperty ("delayOn", false, nullptr);
        t.setProperty ("revOn", false, nullptr);
        return t;
    }

    // Voice budgets: chords and strums with long releases on T1; the step
    // sequencer plays one note per step on T2.
    WavetableEngine synth { 24 };
    WavetableEngine bass { 8 };
    BreakSource breaks;
    TrackRack rack;
    LocalSequencers sequencers { rack, breaks };
    juce::AudioDeviceManager deviceManager;
    juce::AudioSourcePlayer sourcePlayer;
    bool running = false;
    bool deviceInitialised = false;
};
