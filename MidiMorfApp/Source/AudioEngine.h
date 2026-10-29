#pragma once

#include <JuceHeader.h>
#include "WavetableEngine.h"
#include "LocalSequencers.h"

//==============================================================================
// Owns the iOS audio device + the built-in synth. The device plays
// LocalSequencers, which renders the synth plus the break player and runs the
// sequencers' Local-mode clock in between. Stereo out, no input.
class AudioEngine
{
public:
    AudioEngine() = default;

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
    LocalSequencers& getSequencers() noexcept { return sequencers; }

private:
    WavetableEngine synth;
    LocalSequencers sequencers { synth };
    juce::AudioDeviceManager deviceManager;
    juce::AudioSourcePlayer sourcePlayer;
    bool running = false;
    bool deviceInitialised = false;
};
