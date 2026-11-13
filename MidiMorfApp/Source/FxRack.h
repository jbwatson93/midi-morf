#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <memory>
#include <vector>
#include "SynthFilter.h"

//==============================================================================
// FxRack — the synth's effects, as a rack of modules in any order: add one,
// take one out, drag it to a new place, bypass it, use the same effect twice.
// Replaces the fixed distortion → delay → reverb bus (MasterFx); a patch saved
// with that bus becomes a rack of the same three, so it sounds the same.
//
// Each module (FxUnit) declares its knobs as a list of Params, which is all
// the UI and the patch need to know about it. Knob values are atomics set on
// the message thread and read by the audio thread.
//
// Threading: the module list is changed on the message thread only. Every
// change builds a new Chain (the modules in order) and publishes it with one
// atomic pointer store; the audio thread reads whichever chain is live. Old
// chains, and the modules only they still hold, are kept for half a second
// before being freed, the same way WavetableEngine retires tables, so a block
// in flight never touches freed memory.
class FxUnit
{
public:
    struct Param
    {
        juce::String id, name;
        float min, max, def;
        float skewMid = 0.0f;   // 0: linear
        float step    = 0.01f;
    };

    // What the synth's mod matrix sends the FX, once per block. Each kind of
    // module picks out what applies to it.
    struct MatrixMods
    {
        float distDrive = 0.0f, delayMix = 0.0f, delayTimeMs = 0.0f, reverbWet = 0.0f;
    };

    static constexpr int MaxParams = 8;

    FxUnit (juce::String type, juce::String displayName, std::vector<Param> ps, juce::StringArray modeNames = {})
        : typeId (std::move (type)), name (std::move (displayName)), params (std::move (ps)), modes (std::move (modeNames))
    {
        jassert ((int) params.size() <= MaxParams);
        for (int i = 0; i < (int) params.size(); ++i)
        {
            values[i].store (params[(size_t) i].def);
            if (params[(size_t) i].id == "mix")
                mixIndex = i;
        }
    }

    virtual ~FxUnit() = default;

    const juce::String typeId, name;
    const std::vector<Param> params;
    const juce::StringArray modes;

    std::atomic<bool> enabled { true };
    std::atomic<int>  mode { 0 };

    int   indexOf (const juce::String& id) const
    {
        for (int i = 0; i < (int) params.size(); ++i)
            if (params[(size_t) i].id == id) return i;
        return -1;
    }
    float getValue (int i) const          { return values[i].load(); }
    void  setValue (int i, float v)       { const auto& p = params[(size_t) i]; values[i].store (juce::jlimit (p.min, p.max, v)); }
    void  setValue (const juce::String& id, float v) { if (auto i = indexOf (id); i >= 0) setValue (i, v); }

    //==========================================================================
    // Audio thread.
    virtual void prepare (double sampleRate) = 0;
    virtual void applyMatrix (const MatrixMods&) { modOffset.fill (0.0f); }

    // L and R are always both there. `dryL/R` is scratch space the size of
    // the block, for the mix.
    void process (float* L, float* R, int n, float* dryL, float* dryR)
    {
        if (! enabled.load())
        {
            lastMix = -1.0f;
            return;
        }

        const float mix = mixIndex >= 0 ? get (mixIndex) : 1.0f;
        if (mix < 0.999f || lastMix < 0.999f)
        {
            std::copy (L, L + n, dryL);
            std::copy (R, R + n, dryR);
        }

        processWet (L, R, n);

        // Ramp the mix across the block so turning the knob doesn't click.
        const float from = lastMix < 0.0f ? mix : lastMix;
        if (mix < 0.999f || from < 0.999f)
        {
            for (int i = 0; i < n; ++i)
            {
                const float m = from + (mix - from) * (float) (i + 1) / (float) n;
                L[i] = dryL[i] + (L[i] - dryL[i]) * m;
                R[i] = dryR[i] + (R[i] - dryR[i]) * m;
            }
        }
        lastMix = mix;
    }

protected:
    // The knob plus whatever the mod matrix adds, clamped to the knob's range.
    float get (int i) const
    {
        const auto& p = params[(size_t) i];
        return juce::jlimit (p.min, p.max, values[i].load() + modOffset[(size_t) i]);
    }

    virtual void processWet (float* L, float* R, int n) = 0;

    std::array<float, MaxParams> modOffset {};

private:
    std::atomic<float> values[MaxParams];
    int   mixIndex = -1;
    float lastMix = -1.0f;
};

//==============================================================================
// The modules.
namespace Fx
{
    // One-pole low-pass, for tone controls and feedback damping.
    struct OnePole
    {
        float z = 0.0f;
        inline float process (float x, float a) { z += a * (x - z); return z; }
        static float coeff (float hz, double sr) { return 1.0f - std::exp (-juce::MathConstants<float>::twoPi * hz / (float) sr); }
    };

    // Takes off the DC an asymmetric shaper leaves behind (~10 Hz).
    struct DcBlocker
    {
        float x1 = 0.0f, y1 = 0.0f;
        inline float process (float x) { const float y = x - x1 + 0.9987f * y1; x1 = x; y1 = y; return y; }
    };

    // Plain biquad, RBJ cookbook coefficients. Computed on the audio thread
    // only when a knob moves, with no allocation.
    struct Biquad
    {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        float z1[2] {}, z2[2] {};

        inline float process (int ch, float x)
        {
            const float y = b0 * x + z1[ch];
            z1[ch] = b1 * x - a1 * y + z2[ch];
            z2[ch] = b2 * x - a2 * y;
            return y;
        }

        void set (double nb0, double nb1, double nb2, double na0, double na1, double na2)
        {
            b0 = (float) (nb0 / na0); b1 = (float) (nb1 / na0); b2 = (float) (nb2 / na0);
            a1 = (float) (na1 / na0); a2 = (float) (na2 / na0);
        }

        enum Kind { LowShelf, Peak, HighShelf };

        void design (Kind kind, double sr, double hz, double q, double gainDb)
        {
            const double A  = std::pow (10.0, gainDb / 40.0);
            const double w0 = juce::MathConstants<double>::twoPi * juce::jlimit (10.0, sr * 0.45, hz) / sr;
            const double cw = std::cos (w0), sw = std::sin (w0);
            const double alpha = sw / (2.0 * q);
            const double rootA = 2.0 * std::sqrt (A) * alpha;

            switch (kind)
            {
                case Peak:
                    set (1 + alpha * A, -2 * cw, 1 - alpha * A, 1 + alpha / A, -2 * cw, 1 - alpha / A);
                    break;
                case LowShelf:
                    set (A * ((A + 1) - (A - 1) * cw + rootA), 2 * A * ((A - 1) - (A + 1) * cw),
                         A * ((A + 1) - (A - 1) * cw - rootA),
                         (A + 1) + (A - 1) * cw + rootA, -2 * ((A - 1) + (A + 1) * cw),
                         (A + 1) + (A - 1) * cw - rootA);
                    break;
                case HighShelf:
                    set (A * ((A + 1) + (A - 1) * cw + rootA), -2 * A * ((A - 1) + (A + 1) * cw),
                         A * ((A + 1) + (A - 1) * cw - rootA),
                         (A + 1) - (A - 1) * cw + rootA, 2 * ((A - 1) - (A + 1) * cw),
                         (A + 1) - (A - 1) * cw - rootA);
                    break;
            }
        }
    };

    //==========================================================================
    // DISTORTION. SOFT at the old bus's curve, so migrated patches match.
    class Distortion : public FxUnit
    {
    public:
        enum Mode { Soft, Hard, Tube, Fold };

        Distortion()
            : FxUnit ("distortion", "Distortion",
                      { { "drive", "DRIVE", 0.0f, 1.0f, 0.4f },
                        { "tone",  "TONE",  0.0f, 1.0f, 1.0f },
                        { "mix",   "MIX",   0.0f, 1.0f, 1.0f } },
                      { "Soft", "Hard", "Tube", "Fold" }) {}

        void prepare (double sr) override { sampleRate = sr; }

        void applyMatrix (const MatrixMods& m) override
        {
            FxUnit::applyMatrix (m);
            modOffset[0] = m.distDrive;
        }

    protected:
        void processWet (float* L, float* R, int n) override
        {
            const float drive = get (0), tone = get (1);
            const int m = mode.load();
            if (m == Soft && drive <= 0.001f)
                return;   // the old bus skipped itself at zero drive

            const bool toneOn = tone < 0.999f;
            const float a = OnePole::coeff (800.0f * std::exp2 (tone * 5.0f), sampleRate);

            float* ch[2] = { L, R };
            for (int c = 0; c < 2; ++c)
            {
                float* x = ch[c];
                for (int i = 0; i < n; ++i)
                {
                    float y = shape (m, drive, x[i]);
                    if (m == Tube || m == Fold) y = dc[c].process (y);
                    if (toneOn) y = lp[c].process (y, a);
                    x[i] = y;
                }
            }
        }

    private:
        static float shape (int m, float drive, float x)
        {
            switch (m)
            {
                case Hard:
                    return juce::jlimit (-1.0f, 1.0f, x * (1.0f + drive * 12.0f));
                case Tube:
                {
                    // Bias one way, so the two halves of the wave clip differently.
                    const float k = 1.0f + drive * 8.0f;
                    return (std::tanh (k * x + 0.3f) - std::tanh (0.3f)) / std::tanh (k);
                }
                case Fold:
                    return std::sin (x * (1.0f + drive * 6.0f) * juce::MathConstants<float>::halfPi);
                default:
                {
                    const float k = 1.0f + drive * 8.0f;
                    return std::tanh (x * k) / std::tanh (k);
                }
            }
        }

        double sampleRate = 44100.0;
        OnePole lp[2];
        DcBlocker dc[2];
    };

    //==========================================================================
    // DELAY. MONO is the old bus's delay (fed from the mid, heard in both);
    // PING-PONG bounces between the sides. TONE darkens each repeat.
    class Delay : public FxUnit
    {
    public:
        enum Mode { Mono, PingPong };

        Delay()
            : FxUnit ("delay", "Delay",
                      { { "time", "TIME", 5.0f, 2000.0f, 350.0f, 250.0f, 1.0f },
                        { "fb",   "FB",   0.0f, 0.95f,  0.4f },
                        { "tone", "TONE", 0.0f, 1.0f,   1.0f },
                        { "mix",  "MIX",  0.0f, 1.0f,   0.2f } },
                      { "Mono", "Ping-Pong" }) {}

        void prepare (double sr) override
        {
            sampleRate = sr;
            for (auto& b : buf) b.assign ((size_t) (sr * 2.0), 0.0f);
            writePos = 0;
        }

        void applyMatrix (const MatrixMods& m) override
        {
            FxUnit::applyMatrix (m);
            modOffset[0] = m.delayTimeMs;
            modOffset[3] = m.delayMix;
        }

    protected:
        void processWet (float* L, float* R, int n) override
        {
            if (buf[0].empty()) return;

            const int size = (int) buf[0].size();
            const int delay = juce::jlimit (1, size - 1, (int) (get (0) * 0.001f * (float) sampleRate));
            const float fb = get (1), tone = get (2);
            const bool toneOn = tone < 0.999f;
            const float a = OnePole::coeff (600.0f * std::exp2 (tone * 5.0f), sampleRate);
            const bool ping = mode.load() == PingPong;

            for (int i = 0; i < n; ++i)
            {
                int readPos = writePos - delay;
                if (readPos < 0) readPos += size;

                float dl = buf[0][(size_t) readPos], dr = buf[1][(size_t) readPos];
                if (toneOn) { dl = damp[0].process (dl, a); dr = damp[1].process (dr, a); }

                const float in = (L[i] + R[i]) * 0.5f;
                if (ping)
                {
                    buf[0][(size_t) writePos] = in + dr * fb;
                    buf[1][(size_t) writePos] = dl * fb;
                    L[i] = dl; R[i] = dr;
                }
                else
                {
                    buf[0][(size_t) writePos] = in + dl * fb;
                    L[i] = R[i] = dl;
                }
                writePos = (writePos + 1) % size;
            }
        }

    private:
        double sampleRate = 44100.0;
        std::vector<float> buf[2];
        int writePos = 0;
        OnePole damp[2];
    };

    //==========================================================================
    // REVERB — juce::Reverb with the old bus's wet/dry law, so migrated
    // patches match. It mixes itself, so it has WET instead of MIX.
    class Reverb : public FxUnit
    {
    public:
        Reverb()
            : FxUnit ("reverb", "Reverb",
                      { { "wet",   "WET",   0.0f, 1.0f, 0.25f },
                        { "size",  "SIZE",  0.0f, 1.0f, 0.6f },
                        { "damp",  "DAMP",  0.0f, 1.0f, 0.4f },
                        { "width", "WIDTH", 0.0f, 1.0f, 1.0f } }) {}

        void prepare (double sr) override { reverb.setSampleRate (sr); reverb.reset(); last = {}; last.roomSize = -1.0f; }

        void applyMatrix (const MatrixMods& m) override
        {
            FxUnit::applyMatrix (m);
            modOffset[0] = m.reverbWet;
        }

    protected:
        void processWet (float* L, float* R, int n) override
        {
            juce::Reverb::Parameters p;
            p.wetLevel = get (0);
            p.dryLevel = 1.0f - p.wetLevel;
            p.roomSize = get (1);
            p.damping  = get (2);
            p.width    = get (3);
            if (std::abs (p.wetLevel - last.wetLevel) > 0.001f || std::abs (p.roomSize - last.roomSize) > 0.001f
                || std::abs (p.damping - last.damping) > 0.001f || std::abs (p.width - last.width) > 0.001f)
            {
                reverb.setParameters (p);
                last = p;
            }
            reverb.processStereo (L, R, n);
        }

    private:
        juce::Reverb reverb;
        juce::Reverb::Parameters last;
    };

    //==========================================================================
    // CHORUS and FLANGER: one modulated delay per side, the LFOs a quarter
    // cycle apart for width. The delay swings around DELAY by up to 95% of it,
    // so it never reaches zero.
    class ModDelay : public FxUnit
    {
    public:
        ModDelay (juce::String type, juce::String displayName, float rate, float depth, float delayMs, float fb, float mix)
            : FxUnit (std::move (type), std::move (displayName),
                      { { "rate",  "RATE",  0.05f, 5.0f,  rate,  1.0f },
                        { "depth", "DEPTH", 0.0f,  1.0f,  depth },
                        { "delay", "DELAY", 1.0f,  25.0f, delayMs, 6.0f, 0.1f },
                        { "fb",    "FB",   -0.95f, 0.95f, fb },
                        { "mix",   "MIX",   0.0f,  1.0f,  mix } }) {}

        void prepare (double sr) override
        {
            sampleRate = sr;
            for (auto& b : buf) b.assign ((size_t) (sr * 0.06) + 4, 0.0f);
            writePos = 0;
        }

    protected:
        void processWet (float* L, float* R, int n) override
        {
            if (buf[0].empty()) return;

            const int size = (int) buf[0].size();
            const float centre = get (2) * 0.001f * (float) sampleRate;
            const float swing = 0.95f * get (1) * centre;
            const float fb = get (3);
            const double inc = get (0) / sampleRate;
            float* ch[2] = { L, R };

            for (int i = 0; i < n; ++i)
            {
                for (int c = 0; c < 2; ++c)
                {
                    const double ph = phase + (c == 1 ? 0.25 : 0.0);
                    const float d = juce::jlimit (1.0f, (float) size - 2.0f,
                                                  centre + swing * (float) std::sin (ph * juce::MathConstants<double>::twoPi));
                    float rp = (float) writePos - d;
                    if (rp < 0.0f) rp += (float) size;
                    const int i0 = (int) rp;
                    const int i1 = (i0 + 1) % size;
                    const float frac = rp - (float) i0;
                    auto& b = buf[c];
                    const float delayed = b[(size_t) i0] + (b[(size_t) i1] - b[(size_t) i0]) * frac;
                    b[(size_t) writePos] = ch[c][i] + delayed * fb;
                    ch[c][i] = delayed;
                }
                writePos = (writePos + 1) % size;
                phase += inc;
                if (phase >= 1.0) phase -= 1.0;
            }
        }

    private:
        double sampleRate = 44100.0, phase = 0.0;
        std::vector<float> buf[2];
        int writePos = 0;
    };

    struct Chorus  : ModDelay { Chorus()  : ModDelay ("chorus",  "Chorus",  0.8f,  0.35f, 8.0f, 0.0f, 0.5f) {} };
    struct Flanger : ModDelay { Flanger() : ModDelay ("flanger", "Flanger", 0.25f, 0.6f,  2.0f, 0.6f, 0.5f) {} };

    //==========================================================================
    // PHASER — juce::dsp::Phaser run fully wet; the notches come from MIX
    // blending it with the dry signal.
    class Phaser : public FxUnit
    {
    public:
        Phaser()
            : FxUnit ("phaser", "Phaser",
                      { { "rate",  "RATE",  0.05f, 5.0f,    0.5f, 1.0f },
                        { "depth", "DEPTH", 0.0f,  1.0f,    0.7f },
                        { "freq",  "FREQ",  100.0f, 4000.0f, 800.0f, 800.0f, 1.0f },
                        { "fb",    "FB",   -0.9f,  0.9f,    0.4f },
                        { "mix",   "MIX",   0.0f,  1.0f,    0.5f } }) {}

        void prepare (double sr) override
        {
            phaser.prepare ({ sr, (juce::uint32) 512, 2 });
            phaser.setMix (1.0f);
            phaser.reset();
        }

    protected:
        void processWet (float* L, float* R, int n) override
        {
            phaser.setRate (get (0));
            phaser.setDepth (get (1));
            phaser.setCentreFrequency (get (2));
            phaser.setFeedback (get (3));
            float* chans[2] = { L, R };
            juce::dsp::AudioBlock<float> block (chans, 2, (size_t) n);
            phaser.process (juce::dsp::ProcessContextReplacing<float> (block));
        }

    private:
        juce::dsp::Phaser<float> phaser;
    };

    //==========================================================================
    // EQ — low shelf at 150 Hz, a sweepable bell, high shelf at 6 kHz.
    class Eq : public FxUnit
    {
    public:
        Eq()
            : FxUnit ("eq", "EQ",
                      { { "low",  "LOW",  -15.0f, 15.0f, 0.0f, 0.0f, 0.1f },
                        { "mid",  "MID",  -15.0f, 15.0f, 0.0f, 0.0f, 0.1f },
                        { "freq", "FREQ", 200.0f, 8000.0f, 1000.0f, 1000.0f, 1.0f },
                        { "high", "HIGH", -15.0f, 15.0f, 0.0f, 0.0f, 0.1f } }) {}

        void prepare (double sr) override { sampleRate = sr; last.fill (1.0e9f); }

    protected:
        void processWet (float* L, float* R, int n) override
        {
            const std::array<float, 4> now { get (0), get (1), get (2), get (3) };
            if (now != last)
            {
                bands[0].design (Biquad::LowShelf,  sampleRate, 150.0,  0.707, now[0]);
                bands[1].design (Biquad::Peak,      sampleRate, now[2], 1.0,   now[1]);
                bands[2].design (Biquad::HighShelf, sampleRate, 6000.0, 0.707, now[3]);
                last = now;
            }

            for (int i = 0; i < n; ++i)
                for (auto& b : bands)
                {
                    L[i] = b.process (0, L[i]);
                    R[i] = b.process (1, R[i]);
                }
        }

    private:
        double sampleRate = 44100.0;
        Biquad bands[3];
        std::array<float, 4> last {};
    };

    //==========================================================================
    // COMPRESSOR — feed-forward, stereo-linked peak detector, hard knee.
    class Compressor : public FxUnit
    {
    public:
        Compressor()
            : FxUnit ("compressor", "Compressor",
                      { { "thresh",  "THRESH", -48.0f, 0.0f,  -18.0f, 0.0f,   0.1f },
                        { "ratio",   "RATIO",   1.0f,  20.0f,  4.0f,  4.0f,   0.1f },
                        { "attack",  "ATTACK",  0.5f,  100.0f, 10.0f, 10.0f,  0.1f },
                        { "release", "RELEASE", 10.0f, 1000.0f, 120.0f, 120.0f, 1.0f },
                        { "gain",    "GAIN",    0.0f,  24.0f,  0.0f,  0.0f,   0.1f },
                        { "mix",     "MIX",     0.0f,  1.0f,   1.0f } }) {}

        void prepare (double sr) override { sampleRate = sr; envDb = 0.0f; }

    protected:
        void processWet (float* L, float* R, int n) override
        {
            const float thresh = get (0), ratio = get (1);
            const float att = std::exp (-1.0f / (0.001f * get (2) * (float) sampleRate));
            const float rel = std::exp (-1.0f / (0.001f * get (3) * (float) sampleRate));
            const float makeup = juce::Decibels::decibelsToGain (get (4));
            const float slope = 1.0f - 1.0f / ratio;

            for (int i = 0; i < n; ++i)
            {
                const float peak = juce::jmax (std::abs (L[i]), std::abs (R[i]), 1.0e-6f);
                const float over = juce::jmax (0.0f, juce::Decibels::gainToDecibels (peak) - thresh);
                const float target = over * slope;                    // dB of gain reduction wanted
                const float coeff = target > envDb ? att : rel;
                envDb = target + coeff * (envDb - target);
                const float g = juce::Decibels::decibelsToGain (-envDb) * makeup;
                L[i] *= g;
                R[i] *= g;
            }
        }

    private:
        double sampleRate = 44100.0;
        float envDb = 0.0f;
    };

    //==========================================================================
    // FILTER — the synth's state-variable filter as an effect.
    class Filter : public FxUnit
    {
    public:
        Filter()
            : FxUnit ("filter", "Filter",
                      { { "cutoff", "CUTOFF", 20.0f, 20000.0f, 2000.0f, 1000.0f, 1.0f },
                        { "reso",   "RESO",   0.0f,  SynthFilter::MaxReso, 0.6f },
                        { "mix",    "MIX",    0.0f,  1.0f, 1.0f } },
                      { "Low 12", "Low 24", "High 12", "High 24", "Band", "Notch" }) {}

        void prepare (double sr) override
        {
            sampleRate = sr;
            for (auto& c : ch) c.reset();
            smoothedCutoff = -1.0f;
        }

    protected:
        void processWet (float* L, float* R, int n) override
        {
            static constexpr int typeFor[] = { SynthFilter::LP12, SynthFilter::LP24, SynthFilter::HP12,
                                               SynthFilter::HP24, SynthFilter::BP,   SynthFilter::Notch };
            const int type = typeFor[juce::jlimit (0, 5, mode.load())];

            // Glide the cutoff a little each block, in octaves, so a knob
            // sweep doesn't step.
            const float target = get (0);
            smoothedCutoff = smoothedCutoff < 0.0f ? target
                                                   : smoothedCutoff * std::pow (target / smoothedCutoff, 0.5f);
            const auto c = SynthFilter::makeCoeffs (type, smoothedCutoff, get (1), sampleRate);

            for (int i = 0; i < n; ++i)
            {
                L[i] = ch[0].process (type, c, L[i]);
                R[i] = ch[1].process (type, c, R[i]);
            }
        }

    private:
        double sampleRate = 44100.0;
        SynthFilter::Channel ch[2];
        float smoothedCutoff = -1.0f;
    };

    //==========================================================================
    // In the order the ADD menu lists them.
    inline juce::StringArray typeIds()
    {
        return { "distortion", "chorus", "flanger", "phaser", "delay", "reverb", "eq", "compressor", "filter" };
    }

    inline std::shared_ptr<FxUnit> create (const juce::String& type)
    {
        if (type == "distortion") return std::make_shared<Distortion>();
        if (type == "chorus")     return std::make_shared<Chorus>();
        if (type == "flanger")    return std::make_shared<Flanger>();
        if (type == "phaser")     return std::make_shared<Phaser>();
        if (type == "delay")      return std::make_shared<Delay>();
        if (type == "reverb")     return std::make_shared<Reverb>();
        if (type == "eq")         return std::make_shared<Eq>();
        if (type == "compressor") return std::make_shared<Compressor>();
        if (type == "filter")     return std::make_shared<Filter>();
        return nullptr;
    }

    inline juce::String displayName (const juce::String& type)
    {
        auto unit = create (type);
        return unit != nullptr ? unit->name : type;
    }
}

//==============================================================================
class FxRack
{
public:
    static constexpr int MaxUnits = 8;

    FxRack() { publish(); }

    //==========================================================================
    // Audio thread (prepare is called before rendering starts).
    void prepare (double sr)
    {
        sampleRate.store (sr);
        if (auto* c = live.load())
            for (auto& u : c->units)
                u->prepare (sr);
    }

    void setModulated (const FxUnit::MatrixMods& m) { mods = m; }

    void process (float* L, float* R, int n)
    {
        if (L == nullptr) return;
        auto* chain = live.load();
        if (chain == nullptr || chain->units.empty()) return;

        // Modules are stereo. A mono buffer is run as a stereo pair and
        // folded back down.
        const bool mono = (R == nullptr || R == L);
        for (int done = 0; done < n; done += Chunk)
        {
            const int len = juce::jmin (Chunk, n - done);
            float* l = L + done;
            float* r = mono ? monoR : R + done;
            if (mono) std::copy (l, l + len, monoR);

            for (auto& u : chain->units)
            {
                u->applyMatrix (mods);
                u->process (l, r, len, dryL, dryR);
            }

            if (mono)
                for (int i = 0; i < len; ++i)
                    l[i] = (l[i] + r[i]) * 0.5f;
        }
    }

    //==========================================================================
    // Message thread.
    int size() const                { return (int) units.size(); }
    FxUnit* get (int i) const       { return juce::isPositiveAndBelow (i, size()) ? units[(size_t) i].get() : nullptr; }
    bool isFull() const             { return size() >= MaxUnits; }

    // Returns the new module's index, or -1.
    int add (const juce::String& type, int at = -1)
    {
        if (isFull()) return -1;
        auto unit = Fx::create (type);
        if (unit == nullptr) return -1;
        unit->prepare (sampleRate.load());

        at = juce::isPositiveAndNotGreaterThan (at, size()) ? at : size();
        units.insert (units.begin() + at, std::move (unit));
        publish();
        return at;
    }

    void remove (int i)
    {
        if (! juce::isPositiveAndBelow (i, size())) return;
        units.erase (units.begin() + i);
        publish();
    }

    void move (int from, int to)
    {
        if (! juce::isPositiveAndBelow (from, size())) return;
        to = juce::jlimit (0, size() - 1, to);
        if (from == to) return;
        auto unit = units[(size_t) from];
        units.erase (units.begin() + from);
        units.insert (units.begin() + to, std::move (unit));
        publish();
    }

    void clear()
    {
        units.clear();
        publish();
    }

    //==========================================================================
    // The rack as a patch stores it: one child per module, in order.
    static inline const juce::Identifier stateType { "FxRack" };
    static inline const juce::Identifier unitType  { "Fx" };

    juce::ValueTree getState() const
    {
        juce::ValueTree t (stateType);
        for (auto& u : units)
        {
            juce::ValueTree c (unitType);
            c.setProperty ("type", u->typeId, nullptr);
            c.setProperty ("on",   u->enabled.load(), nullptr);
            if (! u->modes.isEmpty())
                c.setProperty ("mode", u->mode.load(), nullptr);
            for (int i = 0; i < (int) u->params.size(); ++i)
                c.setProperty (u->params[(size_t) i].id, u->getValue (i), nullptr);
            t.appendChild (c, nullptr);
        }
        return t;
    }

    void setState (const juce::ValueTree& t)
    {
        std::vector<std::shared_ptr<FxUnit>> next;
        for (auto c : t)
        {
            if (! c.hasType (unitType) || (int) next.size() >= MaxUnits) continue;
            auto unit = Fx::create (c["type"].toString());
            if (unit == nullptr) continue;   // a module this version doesn't have
            unit->prepare (sampleRate.load());
            unit->enabled.store ((bool) c.getProperty ("on", true));
            unit->mode.store (juce::jlimit (0, juce::jmax (0, unit->modes.size() - 1), (int) c.getProperty ("mode", 0)));
            for (int i = 0; i < (int) unit->params.size(); ++i)
                unit->setValue (i, (float) (double) c.getProperty (unit->params[(size_t) i].id, unit->params[(size_t) i].def));
            next.push_back (std::move (unit));
        }
        units = std::move (next);
        publish();
    }

private:
    static constexpr int Chunk = 256;

    struct Chain { std::vector<std::shared_ptr<FxUnit>> units; };
    struct Retired { std::unique_ptr<Chain> chain; juce::uint32 when; };

    void publish()
    {
        const auto now = juce::Time::getMillisecondCounter();
        retired.erase (std::remove_if (retired.begin(), retired.end(),
                                       [now] (const Retired& r) { return now - r.when > 500; }),
                       retired.end());

        auto next = std::make_unique<Chain>();
        next->units = units;
        live.store (next.get());
        if (current != nullptr)
            retired.push_back ({ std::move (current), now });
        current = std::move (next);
    }

    std::vector<std::shared_ptr<FxUnit>> units;   // message thread's view
    std::unique_ptr<Chain> current;
    std::vector<Retired> retired;
    std::atomic<Chain*> live { nullptr };
    std::atomic<double> sampleRate { 44100.0 };

    FxUnit::MatrixMods mods;
    float dryL[Chunk] {}, dryR[Chunk] {}, monoR[Chunk] {};
};
