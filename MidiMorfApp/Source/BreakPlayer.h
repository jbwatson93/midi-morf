#pragma once

#include <JuceHeader.h>
#include "BreakLibrary.h"
#include "TrackRack.h"
#include <array>
#include <vector>

//==============================================================================
// Plays slices of the current Break for the slice sequencer in Local mode.
//
// Like Simpler's slice mode, a slice is a one-shot: it plays at native speed
// to the end of its own region, whatever the gate does, and only the next
// slice (or STOP) cuts it short. The sequencer is monophonic, so a new slice
// chokes the old one with a 2 ms fade; a small voice pool lets fades overlap.
//
// setBreak() is message-thread; everything else runs on the audio thread.
class BreakPlayer
{
public:
    // Swaps in a new loop. The old one is kept alive for half a second so the
    // audio thread can never be caught reading freed memory.
    void setBreak (std::unique_ptr<Break> b)
    {
        const auto now = juce::Time::getMillisecondCounter();
        retired.erase (std::remove_if (retired.begin(), retired.end(),
                                       [now] (const Retired& r) { return now - r.when > 500; }),
                       retired.end());

        current.store (b.get());
        if (owned != nullptr)
            retired.push_back ({ std::move (owned), now });
        owned = std::move (b);
    }

    const Break* getBreak() const { return owned.get(); }

    void prepare (double sampleRate)
    {
        deviceRate = sampleRate;
        scratchL.assign (2048, 0.0f);
        scratchR.assign (2048, 0.0f);
        for (auto& v : voices)
            v.active = false;
        ic1[0] = ic1[1] = ic2[0] = ic2[1] = 0.0f;
    }

    // Ribbon targets — any thread.
    void setPitchSemitones (float semis) { pitchSemis.store (juce::jlimit (-24.0f, 24.0f, semis)); }
    void setTone (float t)               { tone.store (juce::jlimit (0.0f, 1.0f, t)); }

    //==========================================================================
    // Audio thread.

    // `slice` counts steps of `stepBeats` quarter notes into the loop, wrapping
    // if the grid is longer than the loop.
    void trigger (int slice, double stepBeats, float velocity)
    {
        const Break* b = syncBreak();
        if (b == nullptr || slice < 0)
            return;

        const int n = b->audio.getNumSamples();
        const double startBeats = std::fmod (slice * stepBeats, b->beats);

        for (auto& v : voices)
            if (v.active && ! v.releasing)
                v.release (fadeSamples (fadeSeconds));

        Voice* v = &voices[0];
        for (auto& cand : voices)
            if (! cand.active) { v = &cand; break; }

        v->active  = true;
        v->slice   = slice;
        // Whole-sample start, rounded up the same way the clock rounds a
        // tick up to the next sample. A fractional start would interpolate
        // (dull) every odd slice for a timing shift of ~10 µs.
        v->pos     = juce::jlimit (0.0, (double) (n - 1), std::ceil (startBeats / b->beats * n - 1.0e-6));
        v->end     = v->pos + stepBeats / b->beats * n;
        v->gain    = juce::jlimit (0.0f, 1.0f, velocity);
        v->env     = 0.0f;
        v->envStep = 1.0f / fadeSamples (fadeSeconds);
        v->releasing = false;
    }

    void releaseAll()
    {
        for (auto& v : voices)
            if (v.active)
                v.release (fadeSamples (0.004));
    }

    // Adds into L/R.
    void render (float* L, float* R, int numSamples)
    {
        const Break* b = syncBreak();
        if (b == nullptr || L == nullptr)
            return;

        while (numSamples > 0)
        {
            const int n = juce::jmin (numSamples, (int) scratchL.size());
            renderChunk (*b, L, R, n);
            L += n;
            if (R != nullptr) R += n;
            numSamples -= n;
        }
    }

private:
    struct Voice
    {
        bool   active = false, releasing = false;
        int    slice = -1;
        double pos = 0.0, end = 0.0;
        float  gain = 1.0f, env = 0.0f, envStep = 0.0f;

        void release (float samples)
        {
            releasing = true;
            envStep = -juce::jmax (env, 0.001f) / juce::jmax (1.0f, samples);
        }
    };

    struct Retired { std::unique_ptr<Break> b; juce::uint32 when; };

    // Slice-edge crossfade. Short enough to keep transients sharp, long
    // enough that a cut never clicks.
    static constexpr double fadeSeconds = 0.002;

    float fadeSamples (double seconds) const { return (float) (seconds * deviceRate); }

    // A new break invalidates every playing voice's position.
    const Break* syncBreak()
    {
        const Break* b = current.load();
        if (b != lastBreak)
        {
            for (auto& v : voices)
                v.active = false;
            lastBreak = b;
        }
        return b;
    }

    void renderChunk (const Break& b, float* L, float* R, int n)
    {
        std::fill (scratchL.begin(), scratchL.begin() + n, 0.0f);
        std::fill (scratchR.begin(), scratchR.begin() + n, 0.0f);

        const int len = b.audio.getNumSamples();
        const float* srcL = b.audio.getReadPointer (0);
        const float* srcR = b.audio.getReadPointer (1);
        const double rate = b.sampleRate / deviceRate * std::pow (2.0, pitchSemis.load() / 12.0);
        bool any = false;

        for (auto& v : voices)
        {
            if (! v.active)
                continue;

            any = true;
            for (int i = 0; i < n; ++i)
            {
                // Past its own region it fades out over the same few ms the
                // next slice fades in, reading on into the loop — so the
                // untouched pattern crossfades audio with itself, seamlessly.
                if (! v.releasing && v.pos >= v.end)
                    v.release (fadeSamples (fadeSeconds));

                v.env += v.envStep;
                if (v.env >= 1.0f && ! v.releasing) { v.env = 1.0f; v.envStep = 0.0f; }
                if (v.env <= 0.0f)
                {
                    v.active = false;
                    break;
                }

                const int i0 = (int) v.pos % len;
                const int i1 = (i0 + 1) % len;
                const float frac = (float) (v.pos - std::floor (v.pos));
                const float g = v.env * v.gain;
                scratchL[(size_t) i] += g * (srcL[i0] + frac * (srcL[i1] - srcL[i0]));
                scratchR[(size_t) i] += g * (srcR[i0] + frac * (srcR[i1] - srcR[i0]));
                v.pos += rate;
            }
        }

        if (! any)
            return;

        // TONE ribbon: state-variable low-pass, wide open at the top.
        const float t = tone.load();
        if (t < 0.999f)
        {
            const float fc = juce::jmin (120.0f * std::pow (2.0f, t * 7.4f), (float) deviceRate * 0.45f);
            const float g  = std::tan (juce::MathConstants<float>::pi * fc / (float) deviceRate);
            const float k  = 1.1f;
            const float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
            float* chans[2] = { scratchL.data(), scratchR.data() };

            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < n; ++i)
                {
                    const float v3 = chans[ch][i] - ic2[ch];
                    const float v1 = a1 * ic1[ch] + a2 * v3;
                    const float v2 = ic2[ch] + a2 * ic1[ch] + a3 * v3;
                    ic1[ch] = 2.0f * v1 - ic1[ch];
                    ic2[ch] = 2.0f * v2 - ic2[ch];
                    chans[ch][i] = v2;
                }
        }

        juce::FloatVectorOperations::add (L, scratchL.data(), n);
        juce::FloatVectorOperations::add (R != nullptr ? R : L, scratchR.data(), n);
    }

    std::unique_ptr<Break> owned;
    std::atomic<const Break*> current { nullptr };
    std::vector<Retired> retired;

    double deviceRate = 48000.0;
    const Break* lastBreak = nullptr;
    std::array<Voice, 6> voices;
    std::vector<float> scratchL, scratchR;
    float ic1[2] {}, ic2[2] {};
    std::atomic<float> pitchSemis { 0.0f }, tone { 1.0f };
};

//==============================================================================
// The break player as a track source. Slices sit chromatically up from a
// root note, as in Simpler; the slice sequencer sets that root and the step
// length with setSliceMapping() before it fires notes.
class BreakSource : public SoundSource
{
public:
    BreakPlayer& player() noexcept { return breaks; }

    //==========================================================================
    // The loop — message thread. Returns the new break, or nullptr.
    const Break* setBuiltInBreak (int index)
    {
        auto b = BreakLibrary::generate (index);
        if (b == nullptr)
            return nullptr;

        breakIndex = index;
        importedFile = juce::File();
        breaks.setBreak (std::move (b));
        return breaks.getBreak();
    }

    const Break* importBreak (const juce::File& file, juce::String& error)
    {
        auto b = BreakLibrary::importFile (file, error);
        if (b == nullptr)
            return nullptr;

        breakIndex = -1;
        importedFile = file;
        breaks.setBreak (std::move (b));
        return breaks.getBreak();
    }

    const Break* getBreak() const        { return breaks.getBreak(); }
    int  getBuiltInBreakIndex() const    { return breakIndex; }
    juce::File getImportedBreak() const  { return importedFile; }

    // Audio thread.
    void setSliceMapping (int rootNote, double stepBeats)
    {
        sliceRoot = rootNote;
        sliceStepBeats = stepBeats;
    }

    //==========================================================================
    // SoundSource
    juce::String typeId() const override { return "breaks"; }

    void prepare (double sampleRate, int) override { breaks.prepare (sampleRate); }

    void render (juce::AudioBuffer<float>& buffer, int start, int num) override
    {
        float* L = buffer.getNumChannels() > 0 ? buffer.getWritePointer (0, start) : nullptr;
        float* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1, start) : nullptr;
        if (L != nullptr) juce::FloatVectorOperations::clear (L, num);
        if (R != nullptr) juce::FloatVectorOperations::clear (R, num);
        breaks.render (L, R, num);
    }

    // Slices are one-shots, so note-offs are ignored; STOP and panic cut
    // them with allNotesOff.
    void noteOnAudio (int, int note, int velocity127) override
    {
        breaks.trigger (note - sliceRoot, sliceStepBeats, (float) velocity127 / 127.0f);
    }

    void noteOffAudio (int, int) override {}

    // Audio thread only, unlike the synth's: the voices aren't atomic.
    void allNotesOff() override { breaks.releaseAll(); }

    juce::ValueTree getState() const override
    {
        juce::ValueTree t (stateType);
        t.setProperty ("break", breakIndex >= 0 ? BreakLibrary::builtInNames()[breakIndex] : juce::String(), nullptr);
        t.setProperty ("file", importedFile.getFullPathName(), nullptr);
        return t;
    }

    // An imported loop that has gone missing falls back to the first built-in.
    void setState (const juce::ValueTree& t) override
    {
        const juce::String path = t["file"].toString();
        juce::String error;
        if (path.isNotEmpty() && juce::File (path).existsAsFile() && importBreak (juce::File (path), error) != nullptr)
            return;

        setBuiltInBreak (juce::jmax (0, BreakLibrary::indexForName (t["break"].toString())));
    }

    static inline const juce::Identifier stateType { "Breaks" };

private:
    BreakPlayer breaks;
    int breakIndex = -1;
    juce::File importedFile;
    int sliceRoot = 36;
    double sliceStepBeats = 0.25;
};
