#pragma once

#include <JuceHeader.h>
#include "SynthEngine.h"

//==============================================================================
// Owns the iOS audio device + a SynthEngine. Wires synth output through an
// AudioSourcePlayer into the system AudioDeviceManager. Stereo out, no input.
class AudioEngine
{
public:
    AudioEngine() = default;

    ~AudioEngine()
    {
        stop();
    }

    // Initialises the audio device and starts the audio thread. Safe to call
    // multiple times.
    void start()
    {
        if (running)
            return;

        // Stereo out, no input. JUCE configures the iOS audio session under the
        // hood when initialising for output.
        auto error = deviceManager.initialiseWithDefaultDevices (0, 2);
        if (error.isNotEmpty())
        {
            DBG ("AudioEngine: device init failed: " << error);
            return;
        }

        sourcePlayer.setSource (&synth);
        deviceManager.addAudioCallback (&sourcePlayer);
        running = true;
    }

    void stop()
    {
        if (! running)
            return;

        deviceManager.removeAudioCallback (&sourcePlayer);
        sourcePlayer.setSource (nullptr);
        deviceManager.closeAudioDevice();
        running = false;
    }

    SynthEngine& getSynth() noexcept { return synth; }

private:
    SynthEngine synth;
    juce::AudioDeviceManager deviceManager;
    juce::AudioSourcePlayer sourcePlayer;
    bool running = false;
};
