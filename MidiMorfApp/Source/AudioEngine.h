#pragma once

#include <JuceHeader.h>
#include "ActiveSynth.h"

//==============================================================================
// Owns the iOS audio device + both built-in engines (classic subtractive and
// wavetable). ActiveSynth picks which one plays; it's what the AudioSourcePlayer
// renders into the system AudioDeviceManager. Stereo out, no input.
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

        sourcePlayer.setSource (&activeSynth);
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

    SynthEngine& getSynth() noexcept { return synth; }
    WavetableEngine& getWavetable() noexcept { return wavetable; }
    ActiveSynth& getActiveSynth() noexcept { return activeSynth; }

private:
    SynthEngine synth;
    WavetableEngine wavetable;
    ActiveSynth activeSynth { synth, wavetable };
    juce::AudioDeviceManager deviceManager;
    juce::AudioSourcePlayer sourcePlayer;
    bool running = false;
    bool deviceInitialised = false;
};
