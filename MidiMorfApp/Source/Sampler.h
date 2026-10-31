#pragma once

#include <JuceHeader.h>
#include "BreakPlayer.h"
#include "WavetablePresets.h"
#include <atomic>

//==============================================================================
// The sampler as a track source. One sample, three ways to play it:
//
//   • Slice     — the slice sequencer's breaks: notes from the slice root up
//                 pick slices, one-shots at native speed.
//   • Keys      — the whole sample across the keyboard from a root note, with
//                 an ADSR.
//   • One-shot  — every note plays the whole sample at its own pitch.
//
// The sample is a built-in break, an imported audio file, or a note rendered
// from one of the synth's factory presets (so "Bell Keys" can become a
// sampled instrument). Every one of those is ours or the user's.
class SamplerSource : public SoundSource
{
public:
    enum Mode { Slice = 0, Keys = 1, OneShot = 2 };

    static const juce::StringArray& modeNames()
    {
        static const juce::StringArray n { "Slice", "Keys", "One-shot" };
        return n;
    }

    BreakPlayer& player() noexcept { return breaks; }

    //==========================================================================
    // The sample — message thread. Each returns the new sample, or nullptr.
    const Break* setBuiltInBreak (int index)
    {
        auto b = BreakLibrary::generate (index);
        if (b == nullptr)
            return nullptr;

        origin = { Origin::BuiltIn, BreakLibrary::builtInNames()[index], {} };
        breaks.setBreak (std::move (b));
        return breaks.getBreak();
    }

    const Break* importBreak (const juce::File& file, juce::String& error)
    {
        auto b = BreakLibrary::importFile (file, error);
        if (b == nullptr)
            return nullptr;

        origin = { Origin::File, file.getFileNameWithoutExtension(), file };
        breaks.setBreak (std::move (b));
        return breaks.getBreak();
    }

    // A mic recording (SampleRecorder::stop). Saved as Documents/Recordings/
    // Recording N.wav and loaded like an import, so it's remembered the same
    // way and shows up in the Files app.
    const Break* loadRecording (const juce::AudioBuffer<float>& audio, double sampleRate, juce::String& error)
    {
        auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Recordings");
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
        breaks.setBreak (std::move (b));
        return breaks.getBreak();
    }

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
    void setMode (Mode m)      { mode.store ((int) m); }
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

    void prepare (double sampleRate, int) override { breaks.prepare (sampleRate); }

    void render (juce::AudioBuffer<float>& buffer, int start, int num) override
    {
        if (releasePending.exchange (false))
            breaks.releaseAll();

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
        }
    }

    // Slices and one-shots ignore note-offs; STOP and panic cut them with
    // allNotesOff.
    void noteOffAudio (int, int note) override
    {
        if (getMode() == Keys)
            breaks.stopKey (note);
    }

    // Any thread: the voices aren't atomic, so this flags the audio thread.
    void allNotesOff() override { releasePending.store (true); }

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
        return t;
    }

    // A missing imported file falls back to the first built-in break; a
    // sampler that never had a sample stays empty.
    void setState (const juce::ValueTree& t) override
    {
        setMode ((Mode) juce::jlimit (0, 2, (int) t.getProperty ("mode", (int) Slice)));
        setRootNote (t.getProperty ("root", 60));

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
    }

private:
    struct Origin
    {
        enum Kind { None, BuiltIn, File, Preset };
        Kind kind = None;
        juce::String name;
        juce::File file;
    };

    BreakPlayer breaks;
    Origin origin;   // message thread

    std::atomic<int> mode { Slice }, root { 60 };
    std::atomic<float> attack { 0.002f }, decay { 0.3f }, sustain { 1.0f }, release { 0.3f };
    std::atomic<bool> releasePending { false };

    int sliceRoot = 36;          // audio thread
    double sliceStepBeats = 0.25;
    double sliceSecondsPerBeat = 0.0;   // the project's tempo, to warp slices to
};
