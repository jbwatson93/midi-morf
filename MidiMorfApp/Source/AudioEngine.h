#pragma once

#include <JuceHeader.h>
#include "WavetableEngine.h"
#include "LocalSequencers.h"

//==============================================================================
// Owns the iOS audio device, the track sources and the track rack. The device
// plays LocalSequencers, which renders the rack and runs the sequencers'
// Local-mode clock in between. Stereo out, no input.
//
// Each track has its own synth, used when the track's sound is Synth; the
// one break player can sit on any one track. Defaults: T1 Synth, T2 Synth
// with a bass patch, T3 Breaks, T4 empty.
class AudioEngine
{
public:
    enum class Sound { Empty = 0, Synth = 1, Breaks = 2 };

    AudioEngine()
    {
        for (int t = 0; t < TrackRack::NumTracks; ++t)
        {
            synths[(size_t) t] = std::make_unique<WavetableEngine> (t == 0 ? 24 : 12);
            rack.addToPool (synths[(size_t) t].get());
        }
        rack.addToPool (&breaks);

        synths[1]->setState (bassPatch());
        setTrackSound (0, Sound::Synth);
        setTrackSound (1, Sound::Synth);
        setTrackSound (2, Sound::Breaks);
        soundNames[1] = "Bass";
    }

    //==========================================================================
    // Track sounds — message thread.

    // Breaks can only be on one track: putting it here empties its old track.
    void setTrackSound (int track, Sound sound)
    {
        if (! juce::isPositiveAndBelow (track, TrackRack::NumTracks) || sounds[(size_t) track] == sound)
            return;

        if (sound == Sound::Breaks)
            for (int t = 0; t < TrackRack::NumTracks; ++t)
                if (t != track && sounds[(size_t) t] == Sound::Breaks)
                {
                    sounds[(size_t) t] = Sound::Empty;
                    rack.setSource (t, nullptr);
                }

        sounds[(size_t) track] = sound;
        rack.setSource (track, sound == Sound::Synth  ? (SoundSource*) synths[(size_t) track].get()
                             : sound == Sound::Breaks ? (SoundSource*) &breaks
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

    // T1's synth — the one the original Synth page patch belongs to.
    WavetableEngine& getSynth() noexcept { return *synths[0]; }

    // A track's own synth, whether or not the track is playing it.
    WavetableEngine& getTrackSynth (int track) noexcept { return *synths[(size_t) juce::jlimit (0, TrackRack::NumTracks - 1, track)]; }

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

    // What a track's sound is called in the UI. Presets will name the synths
    // later; until then T2's starts as "Bass".
    juce::String getSoundName (int track) const
    {
        switch (getTrackSound (track))
        {
            case Sound::Synth:  return soundNames[(size_t) track];
            case Sound::Breaks: return "Breaks";
            default:            return "Empty";
        }
    }

    // The track's synth if it's playing one, else nullptr.
    WavetableEngine* getWavetable (int track)
    {
        return getTrackSound (track) == Sound::Synth ? synths[(size_t) track].get() : nullptr;
    }

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

    // Voice budgets: chords and strums with long releases on T1, 12 elsewhere.
    // Voices cost nothing until they sound.
    std::array<std::unique_ptr<WavetableEngine>, TrackRack::NumTracks> synths;
    std::array<Sound, TrackRack::NumTracks> sounds { Sound::Empty, Sound::Empty, Sound::Empty, Sound::Empty };
    std::array<juce::String, TrackRack::NumTracks> soundNames { "Synth", "Synth", "Synth", "Synth" };
    BreakSource breaks;
    TrackRack rack;
    LocalSequencers sequencers { rack, breaks };
    juce::AudioDeviceManager deviceManager;
    juce::AudioSourcePlayer sourcePlayer;
    bool running = false;
    bool deviceInitialised = false;
};
