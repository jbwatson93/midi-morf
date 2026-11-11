#pragma once

#include <JuceHeader.h>
#include "TrackRack.h"
#include <array>
#include <atomic>

//==============================================================================
// Halo — sympathetic strings for the whole band.
//
// Every note held on a Halo track tunes a string: a bank of resonators at its
// harmonics. The strings aren't plucked by the player so much as set ringing
// by the other tracks: the rack hands Halo what they just played (before
// their faders), and that sound is the bow. Hold a chord and the drums ring
// in it; a hi-hat becomes shimmer, a kick a low bloom, a break a pad that
// breathes in its rhythm. Mute the drums and they still drive the strings
// unheard, so the groove plays the chord.
//
//   LISTEN  which tracks drive it (all the others, or one) and how hard
//   PLUCK   each new note also plucks its string, so Halo sounds on its own
//   BOW     a soft noise bow under held strings: a drone with nothing else on
//   DECAY   how long a held string rings; DAMP how fast a let-go one stops
//   BRIGHT  how much of the upper harmonics ring (and how long)
//   METAL   stretches the harmonics: string, then bell, then gong
//   WIDTH   spreads the harmonics across the stereo field
//   LATCH   strings stay tuned after the keys come up, until the next chord
//
// Each string is a bank of two-pole resonators, so nothing is allocated on
// the audio thread. A soft clipper keeps a hard-driven Halo in bounds.
class HaloSource : public SoundSource
{
public:
    static constexpr int NumVoices = 8, NumPartials = 8;

    enum Param { Drive = 0, Listen, Pluck, Bow, Decay, Damp, Bright, Metal, Width, Level, Latch, NumParams };

    struct ParamInfo { const char* key; float min, max, def; };
    static const ParamInfo& paramInfo (Param p)
    {
        static const ParamInfo info[NumParams] = {
            { "drive",  0.0f,  2.0f,  1.0f },
            { "listen", 0.0f,  4.0f,  0.0f },    // 0 every other track, 1..4 one track
            { "pluck",  0.0f,  1.0f,  0.5f },
            { "bow",    0.0f,  1.0f,  0.0f },
            { "decay",  0.2f,  12.0f, 3.0f },    // s, held
            { "damp",   0.05f, 3.0f,  0.4f },    // s, let go
            { "bright", 0.0f,  1.0f,  0.5f },
            { "metal",  0.0f,  1.0f,  0.0f },
            { "width",  0.0f,  1.0f,  0.6f },
            { "level", -24.0f, 12.0f, 0.0f },    // dB
            { "latch",  0.0f,  1.0f,  0.0f },
        };
        return info[p];
    }

    HaloSource()
    {
        for (int p = 0; p < NumParams; ++p)
            params[(size_t) p].store (paramInfo ((Param) p).def);
        for (auto& e : energyView) e.store (0.0f);
        for (auto& n : noteView) n.store (-1);
    }

    void  setParam (Param p, float v) { const auto& i = paramInfo (p); params[(size_t) p].store (juce::jlimit (i.min, i.max, v)); }
    float getParam (Param p) const    { return params[(size_t) p].load(); }

    // For the page: each string's note (-1 when idle) and how hard it rings.
    int   voiceNote (int v) const   { return noteView[(size_t) v].load(); }
    float voiceEnergy (int v) const { return energyView[(size_t) v].load(); }

    //==========================================================================
    // SoundSource
    juce::String typeId() const override { return "halo"; }

    void prepare (double sampleRate, int) override
    {
        rate = sampleRate;
        for (auto& v : voices) v = {};
        dcIn = dcOut = 0.0f;
        held = 0;
    }

    bool listensToTracks() const override { return true; }

    // Audio thread, just before render: the excitation for this block.
    void hearTracks (const float* const* tracks, int numTracks, int num) override
    {
        const int listen = juce::roundToInt (getParam (Listen));
        const int n = juce::jmin (num, (int) excite.size());
        std::fill (excite.begin(), excite.begin() + n, 0.0f);
        for (int t = 0; t < numTracks; ++t)
            if (tracks[t] != nullptr && (listen == 0 || listen == t + 1))
                for (int i = 0; i < n; ++i)
                    excite[(size_t) i] += tracks[t][i];
        heardSamples = n;
    }

    void render (juce::AudioBuffer<float>& buffer, int start, int num) override
    {
        float* L = buffer.getWritePointer (0, start);
        float* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1, start) : nullptr;
        juce::FloatVectorOperations::clear (L, num);
        if (R != nullptr) juce::FloatVectorOperations::clear (R, num);

        if (releasePending.exchange (false))
        {
            for (auto& v : voices)
                letGo (v);
            held = 0;   // STOP sends no note-offs
        }

        // LATCH turned off: what it was holding lets go.
        const bool latch = getParam (Latch) >= 0.5f;
        if (wasLatched && ! latch)
            for (auto& v : voices)
                letGo (v);
        wasLatched = latch;

        if (std::none_of (voices.begin(), voices.end(), [] (const Voice& v) { return v.active; }))
        {
            heardSamples = 0;
            publish();
            return;
        }

        updateCoefficients();

        const float drive = getParam (Drive), bow = getParam (Bow) * 1.2f;
        const float level = juce::Decibels::decibelsToGain (getParam (Level)) * MakeUp;
        const float listenStep = 1.0f / (float) (0.03 * rate);   // a string starts or stops listening over 30 ms

        for (int i = 0; i < num; ++i)
        {
            // The band, DC-blocked: the bow for every string.
            const float in = i < heardSamples ? excite[(size_t) i] * drive : 0.0f;
            dcOut = in - dcIn + 0.995f * dcOut;
            dcIn = in;
            const float noise = bow > 0.0f ? bow * (random01() * 2.0f - 1.0f) : 0.0f;

            float l = 0.0f, r = 0.0f;
            for (auto& v : voices)
            {
                if (! v.active)
                    continue;

                // Held strings hear the band (and the bow); a let-go one
                // stops listening and dies away.
                v.listening = v.gate ? juce::jmin (1.0f, v.listening + listenStep) : juce::jmax (0.0f, v.listening - listenStep);
                const float x = v.listening * (dcOut + noise);

                for (int k = 0; k < v.partials; ++k)
                {
                    auto& p = v.p[(size_t) k];
                    const float y = p.g * x + p.a1 * p.y1 - p.a2 * p.y2;
                    p.y2 = p.y1;
                    p.y1 = y;
                    l += y * p.panL;
                    r += y * p.panR;
                    v.energy += y * y;
                }
            }

            L[i] = softClip (l * level);
            if (R != nullptr) R[i] = softClip (r * level);
        }
        heardSamples = 0;

        // A let-go string that has faded frees its voice.
        for (auto& v : voices)
        {
            if (v.active && ! v.gate && v.listening <= 0.0f && v.energy / (float) num < 1.0e-9f)
                v.active = false;
            v.shownEnergy = v.energy / (float) num;
            v.energy = 0.0f;
        }
        publish();
    }

    void noteOnAudio (int channel, int note, int velocity127) override
    {
        const bool latch = getParam (Latch) >= 0.5f;
        if (latch && held == 0)
            for (auto& v : voices)
                letGo (v);   // a new chord re-tunes the latched strings
        ++held;

        Voice* v = nullptr;
        for (auto& c : voices)
            if (c.active && c.note == note) { v = &c; break; }   // the same string again
        if (v == nullptr)
            for (auto& c : voices)
                if (! c.active) { v = &c; break; }
        if (v == nullptr)
        {
            // Steal the quietest let-go string, else the oldest held one.
            for (auto& c : voices)
                if (! c.gate && (v == nullptr || c.shownEnergy < v->shownEnergy))
                    v = &c;
            if (v == nullptr)
            {
                v = &voices[0];
                for (auto& c : voices)
                    if (c.age < v->age)
                        v = &c;
            }
        }

        const bool fresh = ! v->active || v->note != note;
        if (fresh)
        {
            *v = {};
            v->note = note;
        }
        v->channel = channel;
        v->freq = 440.0 * std::pow (2.0, (note - 69 + bends[(size_t) juce::jlimit (0, 16, channel)]) / 12.0);
        v->active = true;
        v->gate = true;
        v->age = ++ages;
        v->strike += getParam (Pluck) * (float) velocity127 / 127.0f;
        v->dirty = true;
    }

    void noteOffAudio (int, int note) override
    {
        held = juce::jmax (0, held - 1);
        if (getParam (Latch) >= 0.5f)
            return;
        for (auto& v : voices)
            if (v.active && v.note == note)
                letGo (v);
    }

    void allNotesOff() override { releasePending.store (true); }

    // A held string on the channel retunes; it keeps ringing as it goes, so
    // the bend is heard as a glide.
    void pitchBendAudio (int channel, float semis) override
    {
        bends[(size_t) juce::jlimit (0, 16, channel)] = semis;
        for (auto& v : voices)
            if (v.active && v.gate && v.channel == channel)
            {
                v.freq = 440.0 * std::pow (2.0, (v.note - 69 + semis) / 12.0);
                v.dirty = true;
            }
    }

    //==========================================================================
    static inline const juce::Identifier stateType { "Halo" };

    juce::ValueTree getState() const override
    {
        juce::ValueTree t (stateType);
        for (int p = 0; p < NumParams; ++p)
            t.setProperty (paramInfo ((Param) p).key, params[(size_t) p].load(), nullptr);
        return t;
    }

    void setState (const juce::ValueTree& t) override
    {
        for (int p = 0; p < NumParams; ++p)
            setParam ((Param) p, (float) t.getProperty (paramInfo ((Param) p).key, paramInfo ((Param) p).def));
    }

private:
    struct Partial
    {
        float a1 = 0.0f, a2 = 0.0f, g = 0.0f, y1 = 0.0f, y2 = 0.0f, panL = 1.0f, panR = 1.0f;
    };

    struct Voice
    {
        bool active = false, gate = false, dirty = true;
        int note = -1, partials = 0, channel = 1;
        double freq = 440.0;
        float strike = 0.0f;                  // a pluck waiting to be struck
        float listening = 0.0f, energy = 0.0f, shownEnergy = 0.0f;
        juce::uint32 age = 0;
        std::array<Partial, NumPartials> p {};
    };

    // DAMP from now on.
    static void letGo (Voice& v)
    {
        if (v.active && v.gate)
        {
            v.gate = false;
            v.dirty = true;
        }
    }

    // Recomputed when a string is struck or let go, or a setting moves; the
    // ringing state (y1, y2) carries on, so nothing clicks.
    void updateCoefficients()
    {
        const float sig = getParam (Decay) + 7.0f * getParam (Damp) + 13.0f * getParam (Bright)
                        + 17.0f * getParam (Metal) + 19.0f * getParam (Width);
        const bool settingsMoved = sig != lastSignature;
        lastSignature = sig;

        const float bright = getParam (Bright), width = getParam (Width);
        const double stretch = 0.02 * getParam (Metal) * getParam (Metal);
        const double tilt = 2.2 - 2.0 * bright;

        for (auto& v : voices)
        {
            if (! v.active || ! (v.dirty || settingsMoved))
                continue;
            v.dirty = false;

            const double t60 = v.gate ? getParam (Decay) : getParam (Damp);
            v.partials = 0;
            for (int k = 0; k < NumPartials; ++k)
            {
                const int n = k + 1;
                const double f = v.freq * n * std::sqrt (1.0 + stretch * n * n);
                if (f > rate * 0.45)
                    break;

                const double w = juce::MathConstants<double>::twoPi * f / rate;
                // Upper harmonics die sooner, less so when BRIGHT.
                const double t = t60 / (1.0 + (0.25 - 0.2 * bright) * (n - 1));
                const double r = std::exp (-6.907755 / (juce::jmax (0.01, t) * rate));
                auto& p = v.p[(size_t) k];
                p.a1 = (float) (2.0 * r * std::cos (w));
                p.a2 = (float) (r * r);
                // Unity gain at resonance, times the harmonic's level. A
                // long-ringing string catches a narrower band of the bow, so
                // it gets some of that back.
                const double catchUp = std::sqrt (juce::jlimit (1.0, 40.0, t / 0.2));
                p.g = (float) ((1.0 - r) * 2.0 * std::sin (w) * catchUp / std::pow ((double) n, tilt));
                const float pan = width * (k % 2 == 0 ? -0.6f : 0.6f) * (0.5f + 0.5f * (float) k / NumPartials);
                p.panL = juce::jmin (1.0f, 1.0f - pan);
                p.panR = juce::jmin (1.0f, 1.0f + pan);
                v.partials = k + 1;

                // A pluck strikes every harmonic at once, at its level, with
                // a phase that sets it ringing as a sine from zero.
                if (v.strike > 0.0f)
                {
                    const float amp = v.strike * PluckLevel / (float) std::pow ((double) n, tilt);
                    p.y1 += amp * (float) std::sin (w);
                }
            }
            v.strike = 0.0f;
        }
    }

    // Level set so a held chord rung by a drum loop sits near a synth pad,
    // and a pluck near a plucked synth note.
    static constexpr float MakeUp = 3.0f, PluckLevel = 0.35f;

    static float softClip (float x)
    {
        // Linear to about ±0.5, then rounding off towards ±1.
        const float a = std::abs (x);
        return a < 0.5f ? x : std::copysign (0.5f + (a - 0.5f) / (1.0f + (a - 0.5f) * 2.0f), x);
    }

    float random01()
    {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        return (float) rng / 4294967296.0f;
    }

    void publish()
    {
        for (int v = 0; v < NumVoices; ++v)
        {
            const auto& voice = voices[(size_t) v];
            noteView[(size_t) v].store (voice.active ? voice.note : -1);
            energyView[(size_t) v].store (voice.active ? voice.shownEnergy : 0.0f);
        }
    }

    std::array<std::atomic<float>, NumParams> params;
    std::atomic<bool> releasePending { false };

    // Audio thread.
    double rate = 48000.0;
    std::array<Voice, NumVoices> voices {};
    std::array<float, 17> bends {};   // each channel's bend, semitones
    std::array<float, 4096> excite {};
    int heardSamples = 0, held = 0;
    bool wasLatched = false;
    juce::uint32 ages = 0, rng = 0x2545F491u;
    float dcIn = 0.0f, dcOut = 0.0f, lastSignature = -1.0f;

    std::array<std::atomic<int>, NumVoices> noteView;
    std::array<std::atomic<float>, NumVoices> energyView;
};
