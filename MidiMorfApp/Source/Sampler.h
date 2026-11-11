#pragma once

#include "Platform.h"

#include <JuceHeader.h>
#include "BreakPlayer.h"
#include "WavetablePresets.h"
#include "GrainEngine.h"
#include <atomic>

//==============================================================================
// The sampler as a track source. One sample, four ways to play it:
//
//   • Slice     — the slice sequencer's breaks: notes from the slice root up
//                 pick slices, one-shots at native speed.
//   • Keys      — the whole sample across the keyboard from a root note, with
//                 an ADSR.
//   • One-shot  — every note plays the whole sample at its own pitch.
//   • Grain     — clouds of grains: one per finger on the waveform, one per
//                 held note (GrainEngine). The G* params are its settings.
//
// The sample is a built-in break, an imported audio file, or a note rendered
// from one of the synth's factory presets (so "Bell Keys" can become a
// sampled instrument). Every one of those is ours or the user's.
//
// Edits never touch the loaded sample (the "source"): START/END trim it, BARS
// says how long the trimmed part is (and so its tempo), REVERSE and the fades
// shape it, and the player gets a fresh copy built from those. The playback
// settings (Param) go straight to the player, Simpler style.
class SamplerSource : public SoundSource,
                      private BreakPlayer::ExtraVoices
{
public:
    enum Mode { Slice = 0, Keys = 1, OneShot = 2, Grain = 3 };

    // What's cut from the source. Times in seconds; end < 0 is the end.
    struct Edit
    {
        double start = 0.0, end = -1.0;
        double beats = 0.0;               // the trimmed sample's length; 0 = the source's
        bool   reverse = false;
        float  fadeIn = 0.0f, fadeOut = 0.0f;   // ms

        bool operator== (const Edit& o) const
        {
            return start == o.start && end == o.end && beats == o.beats && reverse == o.reverse
                && fadeIn == o.fadeIn && fadeOut == o.fadeOut;
        }
    };

    enum Param { Transpose = 0, Detune, Warp, GrainMs, Speed, Gain, VelSense, Loop,
                 FilterType, Cutoff, Resonance,
                 GDensity, GSize, GSpray, GScan, GLoop, GJitter, GSpread, GWindow, GReverse,
                 GRelease, GYAxis, GPosition, GHold, GFreeze, NumParams };

    struct ParamInfo { const char* key; float min, max, def; };
    static const ParamInfo& paramInfo (Param p)
    {
        static const ParamInfo info[NumParams] = {
            { "transpose", -24.0f, 24.0f, 0.0f },     // semitones
            { "detune",    -50.0f, 50.0f, 0.0f },     // cents
            { "warp",       0.0f,  2.0f,  0.0f },     // BreakPlayer::Warp
            { "grain",      5.0f,  250.0f, 40.0f },   // ms
            { "speed",      0.25f, 4.0f,  1.0f },
            { "gain",     -24.0f, 12.0f,  0.0f },     // dB
            { "vel",        0.0f,  1.0f,  1.0f },
            { "loop",       0.0f,  1.0f,  0.0f },
            { "ftype",      0.0f,  3.0f,  0.0f },     // BreakPlayer::FilterType
            { "cutoff",    20.0f, 20000.0f, 20000.0f },
            { "res",        0.0f,  1.0f,  0.2f },
            { "gDensity",   1.0f,  100.0f, 30.0f },   // grains per second
            { "gSize",      5.0f,  500.0f, 80.0f },   // ms
            { "gSpray",     0.0f,  1.0f,  0.1f },
            { "gScan",     -2.0f,  2.0f,  1.0f },     // x the sample's own speed
            { "gLoop",     10.0f,  2000.0f, 300.0f }, // ms
            { "gJitter",    0.0f,  12.0f, 0.0f },     // semitones
            { "gSpread",    0.0f,  1.0f,  0.5f },
            { "gWindow",    0.0f,  1.0f,  0.0f },
            { "gReverse",   0.0f,  1.0f,  0.0f },
            { "gRelease",   5.0f,  4000.0f, 300.0f }, // ms
            { "gYAxis",     0.0f,  3.0f,  0.0f },     // GrainEngine::YAxis
            { "gPos",       0.0f,  1.0f,  0.25f },
            { "gHold",      0.0f,  1.0f,  0.0f },
            { "gFreeze",    0.0f,  1.0f,  0.0f },
        };
        return info[p];
    }

    static const juce::StringArray& warpNames()
    {
        static const juce::StringArray n { "Beats", "Stretch", "Repitch" };
        return n;
    }

    static const juce::StringArray& yAxisNames()
    {
        static const juce::StringArray n { "Size", "Pitch", "Spray", "Density" };
        return n;
    }

    static const juce::StringArray& filterNames()
    {
        static const juce::StringArray n { "Off", "Low-pass", "Band-pass", "High-pass" };
        return n;
    }

    SamplerSource()
    {
        for (int p = 0; p < NumParams; ++p)
            params[(size_t) p].store (paramInfo ((Param) p).def);
        applyParams();
        breaks.setExtraVoices (this);
    }

    static const juce::StringArray& modeNames()
    {
        static const juce::StringArray n { "Slice", "Keys", "One-shot", "Grain" };
        return n;
    }

    BreakPlayer& player() noexcept { return breaks; }
    GrainEngine& grains() noexcept { return grain; }

    //==========================================================================
    // The sample — message thread. Each returns the new sample, or nullptr.
    const Break* setBuiltInBreak (int index)
    {
        auto b = BreakLibrary::generate (index);
        if (b == nullptr)
            return nullptr;

        origin = { Origin::BuiltIn, BreakLibrary::builtInNames()[index], {} };
        return setSource (std::move (b));
    }

    const Break* importBreak (const juce::File& file, juce::String& error)
    {
        auto b = BreakLibrary::importFile (file, error);
        if (b == nullptr)
            return nullptr;

        origin = { Origin::File, file.getFileNameWithoutExtension(), file };
        return setSource (std::move (b));
    }

    // A mic recording (SampleRecorder::stop). Saved as Documents/Recordings/
    // Recording N.wav and loaded like an import, so it's remembered the same
    // way and shows up in the Files app.
    const Break* loadRecording (const juce::AudioBuffer<float>& audio, double sampleRate, juce::String& error)
    {
        auto dir = Platform::documentsDirectory().getChildFile ("Recordings");
        dir.createDirectory();
        int number = 1;
        while (dir.getChildFile ("Recording " + juce::String (number) + ".wav").exists())
            ++number;
        const auto file = dir.getChildFile ("Recording " + juce::String (number) + ".wav");

        {
            std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
            juce::WavAudioFormat wav;
            auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (sampleRate)
                                                                                     .withNumChannels (1)
                                                                                     .withBitsPerSample (24));
            if (writer == nullptr || ! writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples()))
            {
                error = "Couldn't save the recording";
                file.deleteFile();
                return nullptr;
            }
        }

        return importBreak (file, error);
    }

    // One note (middle C) of a factory synth preset, rendered offline — held
    // 2 s, then 1 s of release. Becomes the sample, with its root at C.
    const Break* renderFromPreset (const juce::String& presetName)
    {
        const auto* preset = WavetablePresets::findFactory (presetName);
        if (preset == nullptr)
            return nullptr;

        constexpr double rate = 48000.0;
        constexpr int held = (int) (rate * 2.0), total = (int) (rate * 3.0), block = 512;

        WavetableEngine synth (4);
        synth.prepareToPlay (block, rate);
        synth.setState (preset->state);

        auto b = std::make_unique<Break>();
        b->name = presetName;
        b->sampleRate = rate;
        b->beats = 4.0;
        b->audio.setSize (2, total);
        b->audio.clear();

        synth.noteOn (60, 0.8f);
        for (int pos = 0; pos < total; pos += block)
        {
            if (pos == (held / block) * block)
                synth.noteOff (60);
            synth.getNextAudioBlock (juce::AudioSourceChannelInfo (&b->audio, pos, juce::jmin (block, total - pos)));
        }

        // Bring it up to a healthy level: the synth's own gain is set for
        // mixing several voices.
        const float peak = b->audio.getMagnitude (0, total);
        if (peak > 1.0e-4f)
            b->audio.applyGain (0.7f / peak);

        origin = { Origin::Preset, presetName, {} };
        setRootNote (60);
        return setSource (std::move (b));
    }

    //==========================================================================
    // Editing — message thread.
    const Break* getSource() const { return source.get(); }
    const Edit&  getEdit() const   { return edit; }

    // Applies an edit: the player gets a new sample built from the source.
    const Break* setEdit (const Edit& e)
    {
        edit = e;
        rebuild();
        return breaks.getBreak();
    }

    void  setParam (Param p, float v)
    {
        const auto& info = paramInfo (p);
        params[(size_t) p].store (juce::jlimit (info.min, info.max, v));
        applyParams();
    }
    float getParam (Param p) const { return params[(size_t) p].load(); }

    const Break* getBreak() const        { return breaks.getBreak(); }
    juce::String getSampleName() const   { return origin.name; }
    bool isFromPreset() const            { return origin.kind == Origin::Preset; }

    int getBuiltInBreakIndex() const
    {
        return origin.kind == Origin::BuiltIn ? BreakLibrary::builtInNames().indexOf (origin.name) : -1;
    }

    juce::File getImportedBreak() const { return origin.kind == Origin::File ? origin.file : juce::File(); }

    //==========================================================================
    // Playing — any thread.
    // Leaving Grain mode stops its clouds (held ones too), so they can't come
    // back when it returns.
    void setMode (Mode m)
    {
        if (mode.exchange ((int) m) == Grain && m != Grain)
        {
            grain.releaseAllTouches();
            grainCut.store (true);
        }
    }
    Mode getMode() const       { return (Mode) mode.load(); }
    void setRootNote (int n)   { root.store (juce::jlimit (0, 127, n)); }
    int  getRootNote() const   { return root.load(); }

    void setEnvelope (float a, float d, float s, float r)
    {
        attack.store (juce::jlimit (0.0f, 5.0f, a));
        decay.store (juce::jlimit (0.0f, 5.0f, d));
        sustain.store (juce::jlimit (0.0f, 1.0f, s));
        release.store (juce::jlimit (0.0f, 8.0f, r));
    }

    juce::ADSR::Parameters getEnvelope() const { return { attack.load(), decay.load(), sustain.load(), release.load() }; }

    // Audio thread: where slices start and how long they are, from the slice
    // sequencer.
    void setSliceMapping (int rootNote, double stepBeats, double secondsPerBeat)
    {
        sliceRoot = rootNote;
        sliceStepBeats = stepBeats;
        sliceSecondsPerBeat = secondsPerBeat;
    }

    //==========================================================================
    // SoundSource
    juce::String typeId() const override { return "sampler"; }

    void prepare (double sampleRate, int) override
    {
        breaks.prepare (sampleRate);
        grain.prepare (sampleRate);
    }

    void render (juce::AudioBuffer<float>& buffer, int start, int num) override
    {
        if (grainCut.exchange (false))
            grain.cutAll();
        if (releasePending.exchange (false))
        {
            breaks.releaseAll();
            for (int i = 0; i < 128; ++i)
                grain.noteOff (i);
        }

        float* L = buffer.getNumChannels() > 0 ? buffer.getWritePointer (0, start) : nullptr;
        float* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1, start) : nullptr;
        if (L != nullptr) juce::FloatVectorOperations::clear (L, num);
        if (R != nullptr) juce::FloatVectorOperations::clear (R, num);
        breaks.render (L, R, num);
    }

    void noteOnAudio (int, int note, int velocity127) override
    {
        const float vel = (float) velocity127 / 127.0f;
        switch (getMode())
        {
            case Slice:   breaks.trigger (note - sliceRoot, sliceStepBeats, sliceSecondsPerBeat, vel); break;
            case Keys:    breaks.startKey (note, vel, std::pow (2.0, (note - root.load()) / 12.0), false, getEnvelope()); break;
            case OneShot: breaks.startKey (note, vel, 1.0, true, {}); break;
            case Grain:   grain.noteOn (note, root.load(), vel); break;
        }
    }

    // Slices and one-shots ignore note-offs; STOP and panic cut them with
    // allNotesOff.
    void noteOffAudio (int, int note) override
    {
        if (getMode() == Keys)
            breaks.stopKey (note);
        else if (getMode() == Grain)
            grain.noteOff (note);
    }

    // Any thread: the voices aren't atomic, so this flags the audio thread.
    // Held grain clouds let go too.
    void allNotesOff() override
    {
        releasePending.store (true);
        grain.releaseAllTouches();
    }

    //==========================================================================
    static inline const juce::Identifier stateType { "Sampler" };

    juce::ValueTree getState() const override
    {
        juce::ValueTree t (stateType);
        t.setProperty ("mode", mode.load(), nullptr);
        t.setProperty ("root", root.load(), nullptr);
        t.setProperty ("env", juce::String (attack.load()) + "," + juce::String (decay.load()) + ","
                              + juce::String (sustain.load()) + "," + juce::String (release.load()), nullptr);
        t.setProperty ("break",  origin.kind == Origin::BuiltIn ? origin.name : juce::String(), nullptr);
        t.setProperty ("file",   origin.file.getFullPathName(), nullptr);
        t.setProperty ("preset", origin.kind == Origin::Preset ? origin.name : juce::String(), nullptr);
        t.setProperty ("edit", juce::String (edit.start, 6) + "," + juce::String (edit.end, 6) + "," + juce::String (edit.beats, 4) + ","
                               + juce::String ((int) edit.reverse) + "," + juce::String (edit.fadeIn) + "," + juce::String (edit.fadeOut), nullptr);
        for (int p = 0; p < NumParams; ++p)
            t.setProperty (paramInfo ((Param) p).key, params[(size_t) p].load(), nullptr);
        return t;
    }

    // A missing imported file falls back to the first built-in break; a
    // sampler that never had a sample stays empty.
    void setState (const juce::ValueTree& t) override
    {
        setMode ((Mode) juce::jlimit (0, 3, (int) t.getProperty ("mode", (int) Slice)));
        setRootNote (t.getProperty ("root", 60));
        for (int p = 0; p < NumParams; ++p)
            setParam ((Param) p, (float) t.getProperty (paramInfo ((Param) p).key, paramInfo ((Param) p).def));

        auto env = juce::StringArray::fromTokens (t["env"].toString(), ",", "");
        if (env.size() == 4)
            setEnvelope (env[0].getFloatValue(), env[1].getFloatValue(), env[2].getFloatValue(), env[3].getFloatValue());

        const juce::String path = t["file"].toString(), preset = t["preset"].toString(), name = t["break"].toString();
        if (path.isEmpty() && preset.isEmpty() && name.isEmpty())
            return;   // never had a sample; it gets one when first used

        juce::String error;
        if (preset.isNotEmpty() && renderFromPreset (preset) != nullptr)
            setRootNote (t.getProperty ("root", 60));   // rendering resets it
        else if (path.isNotEmpty() && juce::File (path).existsAsFile() && importBreak (juce::File (path), error) != nullptr)
            {}
        else
            setBuiltInBreak (juce::jmax (0, BreakLibrary::indexForName (name)));

        auto e = juce::StringArray::fromTokens (t["edit"].toString(), ",", "");
        if (e.size() == 6)
            setEdit ({ e[0].getDoubleValue(), e[1].getDoubleValue(), e[2].getDoubleValue(),
                       e[3].getIntValue() != 0, e[4].getFloatValue(), e[5].getFloatValue() });
    }

private:
    struct Origin
    {
        enum Kind { None, BuiltIn, File, Preset };
        Kind kind = None;
        juce::String name;
        juce::File file;
    };

    // A newly loaded sample starts unedited.
    const Break* setSource (std::unique_ptr<Break> b)
    {
        source = std::move (b);
        edit = {};
        rebuild();
        return breaks.getBreak();
    }

    // The player's sample: the source trimmed, reversed and faded.
    void rebuild()
    {
        if (source == nullptr)
            return;

        const auto& src = *source;
        const int n = src.audio.getNumSamples();
        const int minLength = juce::jmin (n, (int) (src.sampleRate * 0.02));
        const int s0 = juce::jlimit (0, n - minLength, (int) std::lround (edit.start * src.sampleRate));
        const int s1 = juce::jlimit (s0 + minLength, n, edit.end < 0.0 ? n : (int) std::lround (edit.end * src.sampleRate));
        const int len = s1 - s0;

        auto b = std::make_unique<Break>();
        b->name = src.name;
        b->sampleRate = src.sampleRate;
        b->beats = edit.beats > 0.0 ? edit.beats : src.beats;
        b->gridBars = (edit.beats > 0.0 || s0 > 0 || s1 < n) ? 0 : src.gridBars;
        b->audio.setSize (2, len);
        for (int ch = 0; ch < 2; ++ch)
            b->audio.copyFrom (ch, 0, src.audio, ch, s0, len);

        if (edit.reverse)
            b->audio.reverse (0, len);

        const int in  = juce::jmin (len, (int) (edit.fadeIn * 0.001 * src.sampleRate));
        const int out = juce::jmin (len, (int) (edit.fadeOut * 0.001 * src.sampleRate));
        if (in > 0)  b->audio.applyGainRamp (0, in, 0.0f, 1.0f);
        if (out > 0) b->audio.applyGainRamp (len - out, out, 1.0f, 0.0f);

        breaks.setBreak (std::move (b));
    }

    void applyParams()
    {
        breaks.setTune (getParam (Transpose) + getParam (Detune) * 0.01f);
        breaks.setWarp ((BreakPlayer::Warp) juce::roundToInt (getParam (Warp)));
        breaks.setGrainMs (getParam (GrainMs));
        breaks.setSpeed (getParam (Speed));
        breaks.setGain (juce::Decibels::decibelsToGain (getParam (Gain)));
        breaks.setVelocitySense (getParam (VelSense));
        breaks.setLoop (getParam (Loop) >= 0.5f);
        breaks.setFilter ((BreakPlayer::FilterType) juce::roundToInt (getParam (FilterType)), getParam (Cutoff), getParam (Resonance));

        auto& g = grain.settings;
        g.density   = getParam (GDensity);
        g.sizeMs    = getParam (GSize);
        g.spray     = getParam (GSpray);
        g.scan      = getParam (GScan);
        g.loopMs    = getParam (GLoop);
        g.jitter    = getParam (GJitter);
        g.spread    = getParam (GSpread);
        g.window    = getParam (GWindow);
        g.reverse   = getParam (GReverse);
        g.releaseMs = getParam (GRelease);
        g.yAxis     = juce::roundToInt (getParam (GYAxis));
        g.position  = getParam (GPosition);
        g.freeze    = getParam (GFreeze) >= 0.5f;
        g.tuneSemis = getParam (Transpose) + getParam (Detune) * 0.01f;
        g.velSense  = getParam (VelSense);
    }

    // BreakPlayer::ExtraVoices: the clouds, in Grain mode.
    bool renderVoices (const Break& b, float* L, float* R, int n) override
    {
        return getMode() == Grain && grain.render (b, L, R, n);
    }

    BreakPlayer breaks;
    GrainEngine grain;
    Origin origin;   // message thread
    std::unique_ptr<Break> source;   // as loaded; the player has the edited copy
    Edit edit;
    std::array<std::atomic<float>, NumParams> params;

    std::atomic<int> mode { Slice }, root { 60 };
    std::atomic<float> attack { 0.002f }, decay { 0.3f }, sustain { 1.0f }, release { 0.3f };
    std::atomic<bool> releasePending { false }, grainCut { false };

    int sliceRoot = 36;          // audio thread
    double sliceStepBeats = 0.25;
    double sliceSecondsPerBeat = 0.0;   // the project's tempo, to warp slices to
};
