#pragma once

#include "Platform.h"

#include <JuceHeader.h>
#include "BreakPlayer.h"
#include "TrackRack.h"
#include "WavetablePresets.h"
#include <atomic>

//==============================================================================
// What the Sampler and Grain sounds share: one loaded sample and the edits
// cut from it, played through a BreakPlayer (which also gives both its TONE,
// filter and GAIN).
//
// The sample is a built-in break, an imported audio file, a mic recording or
// a note rendered from one of the synth's factory presets (so "Bell Keys" can
// become a sampled instrument). Every one of those is ours or the user's.
//
// Edits never touch the loaded sample (the "source"): START/END trim it, BARS
// says how long the trimmed part is (and so its tempo), REVERSE and the fades
// shape it, and the player gets a fresh copy built from those.
class SampleSource : public SoundSource
{
public:
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

    BreakPlayer& player() noexcept { return breaks; }

    // BreakPlayer::FilterType, as the pages name them.
    static const juce::StringArray& filterNames()
    {
        static const juce::StringArray n { "Off", "Low-pass", "Band-pass", "High-pass" };
        return n;
    }

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

    const Break* getBreak() const        { return breaks.getBreak(); }
    juce::String getSampleName() const   { return origin.name; }
    bool isFromPreset() const            { return origin.kind == Origin::Preset; }

    int getBuiltInBreakIndex() const
    {
        return origin.kind == Origin::BuiltIn ? BreakLibrary::builtInNames().indexOf (origin.name) : -1;
    }

    juce::File getImportedBreak() const { return origin.kind == Origin::File ? origin.file : juce::File(); }

    // The note that plays the sample at its own pitch — any thread.
    void setRootNote (int n)   { root.store (juce::jlimit (0, 127, n)); }
    int  getRootNote() const   { return root.load(); }

protected:
    // The sample's part of a saved state: where it came from, the root and
    // the edit.
    void writeSample (juce::ValueTree& t) const
    {
        t.setProperty ("root", root.load(), nullptr);
        t.setProperty ("break",  origin.kind == Origin::BuiltIn ? origin.name : juce::String(), nullptr);
        t.setProperty ("file",   origin.file.getFullPathName(), nullptr);
        t.setProperty ("preset", origin.kind == Origin::Preset ? origin.name : juce::String(), nullptr);
        t.setProperty ("edit", juce::String (edit.start, 6) + "," + juce::String (edit.end, 6) + "," + juce::String (edit.beats, 4) + ","
                               + juce::String ((int) edit.reverse) + "," + juce::String (edit.fadeIn) + "," + juce::String (edit.fadeOut), nullptr);
    }

    // A missing imported file falls back to the first built-in break; a
    // source that never had a sample stays empty.
    void readSample (const juce::ValueTree& t)
    {
        setRootNote (t.getProperty ("root", 60));

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

    BreakPlayer breaks;

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

    Origin origin;   // message thread
    std::unique_ptr<Break> source;   // as loaded; the player has the edited copy
    Edit edit;
    std::atomic<int> root { 60 };
};
