#pragma once

#include <JuceHeader.h>
#include "WavetableEngine.h"
#include "LocalSequencers.h"
#include "PunchFx.h"
#include "WavetablePresets.h"
#include "SampleRecorder.h"
#include "DrumSynth.h"

//==============================================================================
// Owns the iOS audio device, the track sources and the track rack. The device
// plays LocalSequencers, which renders the rack and runs the sequencers'
// Local-mode clock in between. Stereo out; the mic comes on only to record
// a sample.
//
// Each track has its own synth, sampler and drum synth, and plays one of them
// (or nothing). Defaults: T1 Synth, T2 Synth with the Acid Bass preset, T3
// Sampler slicing the Amen, T4 Drums.
class AudioEngine
{
public:
    // Values are saved in projects. Sampler was "Breaks" before it played
    // keys and one-shots too.
    enum class Sound { Empty = 0, Synth = 1, Sampler = 2, Drums = 3 };

    AudioEngine()
    {
        for (int t = 0; t < TrackRack::NumTracks; ++t)
        {
            synths[(size_t) t] = std::make_unique<WavetableEngine> (t == 0 ? 24 : 12);
            rack.addToPool (synths[(size_t) t].get());
            samplers[(size_t) t] = std::make_unique<SamplerSource>();
            rack.addToPool (samplers[(size_t) t].get());
            drums[(size_t) t] = std::make_unique<DrumSource>();
            rack.addToPool (drums[(size_t) t].get());
        }
        samplers[2]->setBuiltInBreak (0);   // the Amen, for the slicer

        punch.setTempoSource ([this] { return sequencers.getBpm(); });
        rack.setMasterProcessor (&punch);
        rack.setTempoSource ([this] { return sequencers.getBpm(); });

        if (auto* acid = WavetablePresets::findFactory ("Acid Bass"))
            synths[1]->setState (acid->state);
        setTrackSound (0, Sound::Synth);
        setTrackSound (1, Sound::Synth);
        setTrackSound (2, Sound::Sampler);
        setTrackSound (3, Sound::Drums);
        soundNames[1] = "Acid Bass";
    }

    //==========================================================================
    // Track sounds — message thread.

    void setTrackSound (int track, Sound sound)
    {
        if (! juce::isPositiveAndBelow (track, TrackRack::NumTracks) || sounds[(size_t) track] == sound)
            return;

        // A sampler picked for the first time starts with the Amen rather than silence.
        if (sound == Sound::Sampler && samplers[(size_t) track]->getBreak() == nullptr)
            samplers[(size_t) track]->setBuiltInBreak (0);

        sounds[(size_t) track] = sound;
        rack.setSource (track, sound == Sound::Synth   ? (SoundSource*) synths[(size_t) track].get()
                             : sound == Sound::Sampler ? (SoundSource*) samplers[(size_t) track].get()
                             : sound == Sound::Drums   ? (SoundSource*) drums[(size_t) track].get()
                                                       : nullptr);
    }

    Sound getTrackSound (int track) const
    {
        return juce::isPositiveAndBelow (track, TrackRack::NumTracks) ? sounds[(size_t) track] : Sound::Empty;
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

        setMicOn (false);   // no mic in the background
        deviceManager.removeAudioCallback (&sourcePlayer);
        sourcePlayer.setSource (nullptr);
        running = false;
    }

    // Message thread. Opens or closes the first input channel and attaches the
    // recorder to it. Opening it the first time is when iOS asks for the mic;
    // the outputs keep playing (the device restarts, a brief gap).
    bool setMicOn (bool on)
    {
        if (micOn == on)
            return true;
        if (! deviceInitialised)
            return false;

        auto setup = deviceManager.getAudioDeviceSetup();
        // The device was opened output-only, so it has no input device named;
        // without one JUCE silently drops the input channels. Name the default
        // input (on iOS, the same "iOS Audio" device). It stays named when the
        // mic goes off, so only the channels change after the first time.
        if (setup.inputDeviceName.isEmpty())
            if (auto* type = deviceManager.getCurrentDeviceTypeObject())
                setup.inputDeviceName = type->getDeviceNames (true)[juce::jmax (0, type->getDefaultDeviceIndex (true))];
        setup.useDefaultInputChannels = false;
        setup.inputChannels.clear();
        if (on)
            setup.inputChannels.setBit (0);

        const auto error = deviceManager.setAudioDeviceSetup (setup, true);
        if (error.isNotEmpty())
        {
            DBG ("AudioEngine: mic " << (on ? "on" : "off") << " failed: " << error);
            return false;
        }

        if (on) deviceManager.addAudioCallback (&recorder);
        else    deviceManager.removeAudioCallback (&recorder);
        micOn = on;
        return true;
    }

    SampleRecorder& getRecorder() noexcept { return recorder; }

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

    // T1's synth — the one the original Synth page patch belongs to.
    WavetableEngine& getSynth() noexcept { return *synths[0]; }

    // A track's own synth, whether or not the track is playing it.
    WavetableEngine& getTrackSynth (int track) noexcept { return *synths[(size_t) juce::jlimit (0, TrackRack::NumTracks - 1, track)]; }

    // A track's own sampler, whether or not the track is playing it.
    SamplerSource& getTrackSampler (int track) noexcept { return *samplers[(size_t) juce::jlimit (0, TrackRack::NumTracks - 1, track)]; }

    // A track's own drum synth, whether or not the track is playing it.
    DrumSource& getTrackDrums (int track) noexcept { return *drums[(size_t) juce::jlimit (0, TrackRack::NumTracks - 1, track)]; }

    // Tilt is a global mod source: every synth hears it.
    void setTilt (float x, float y)
    {
        for (auto& s : synths)
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
    PunchFx& getPunchFx() noexcept            { return punch; }

    // What a track's sound is called in the UI: for a synth, the preset it
    // was loaded from (or saved as).
    juce::String getSoundName (int track) const
    {
        switch (getTrackSound (track))
        {
            case Sound::Synth:  return soundNames[(size_t) track];
            case Sound::Sampler: return samplers[(size_t) track]->getSampleName();
            case Sound::Drums:   return drums[(size_t) track]->getKitName();
            default:            return "Empty";
        }
    }

    // The track synth's preset name, whatever the track is playing now.
    juce::String getSynthName (int track) const
    {
        return juce::isPositiveAndBelow (track, TrackRack::NumTracks) ? soundNames[(size_t) track] : juce::String();
    }

    void setSoundName (int track, const juce::String& name)
    {
        if (juce::isPositiveAndBelow (track, TrackRack::NumTracks) && name.isNotEmpty())
            soundNames[(size_t) track] = name;
    }

    // The track's synth if it's playing one, else nullptr.
    WavetableEngine* getWavetable (int track)
    {
        return getTrackSound (track) == Sound::Synth ? synths[(size_t) track].get() : nullptr;
    }

    // 0..1 of the callback's time budget, averaged by JUCE.
    double getCpuUsage() const { return deviceManager.getCpuUsage(); }

private:
    // Voice budgets: chords and strums with long releases on T1, 12 elsewhere.
    // Voices cost nothing until they sound.
    std::array<std::unique_ptr<WavetableEngine>, TrackRack::NumTracks> synths;
    std::array<Sound, TrackRack::NumTracks> sounds { Sound::Empty, Sound::Empty, Sound::Empty, Sound::Empty };
    // T1 keeps whatever patch an older install had, so it isn't called Init.
    std::array<juce::String, TrackRack::NumTracks> soundNames { "Synth", "Acid Bass", "Init", "Init" };
    std::array<std::unique_ptr<SamplerSource>, TrackRack::NumTracks> samplers;
    std::array<std::unique_ptr<DrumSource>, TrackRack::NumTracks> drums;
    PunchFx punch;
    TrackRack rack;
    LocalSequencers sequencers { rack };
    juce::AudioDeviceManager deviceManager;
    juce::AudioSourcePlayer sourcePlayer;
    SampleRecorder recorder;
    bool running = false;
    bool micOn = false;
    bool deviceInitialised = false;
};
