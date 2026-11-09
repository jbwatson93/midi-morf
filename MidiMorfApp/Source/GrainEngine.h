#pragma once

#include <JuceHeader.h>
#include "BreakLibrary.h"
#include <array>
#include <atomic>

//==============================================================================
// The sampler's GRAIN mode: the sample played as clouds of short grains.
//
// A cloud is a stream of grains around a point in the sample. Each finger on
// the waveform is one cloud (Touch clouds, set from the message thread), and
// each held note is another (Note clouds, pitched from the root, at POSITION).
//
// A cloud loops: its read point runs through a LOOP-long stretch starting
// where the finger is, at SCAN speed (1 = the sample's own speed, 0 = frozen
// on the spot, negative = backwards), so a finger held down plays one loop
// and dragging moves it. Every 1/DENSITY seconds the cloud starts a grain
// there: SIZE long, nudged by up to SPRAY, pitched by TRANSPOSE (plus up to
// JITTER semitones at random), panned by up to SPREAD, played backwards with
// REVERSE probability, shaped by WINDOW (a smooth Hann bell to a near-square
// Tukey). Lifting the finger fades the cloud out over RELEASE.
//
// A finger's height (0 bottom, 1 top) bends one setting, the Y axis: SIZE,
// pitch (±12 semitones), SPRAY or DENSITY.
//
// The grains are a fixed pool, so nothing is allocated on the audio thread;
// with every grain busy, a new one waits. The engine reads the sampler's
// current (trimmed) sample and renders into BreakPlayer's voice buffers, so
// TONE, the filter and GAIN apply as to every other mode.
//
// For drawing, the audio thread leaves where every grain and cloud is in
// atomics the page reads (positions 0..1 across the sample).
class GrainEngine
{
public:
    static constexpr int NumTouch = 5, NumNotes = 8, NumClouds = NumTouch + NumNotes, NumGrains = 96;
    enum YAxis { YSize = 0, YPitch, YSpray, YDensity };

    // Settings — any thread.
    struct Settings
    {
        std::atomic<float> density { 30.0f };   // grains per second
        std::atomic<float> sizeMs { 80.0f };
        std::atomic<float> spray { 0.1f };      // 0..1 → up to ±250 ms
        std::atomic<float> scan { 1.0f };       // -2..2
        std::atomic<float> loopMs { 300.0f };
        std::atomic<float> jitter { 0.0f };     // semitones
        std::atomic<float> spread { 0.5f };     // 0..1
        std::atomic<float> window { 0.0f };     // 0 Hann .. 1 near-square
        std::atomic<float> reverse { 0.0f };    // probability
        std::atomic<float> releaseMs { 300.0f };
        std::atomic<int>   yAxis { YSize };
        std::atomic<float> position { 0.25f };  // where notes play, 0..1
        std::atomic<bool>  freeze { false };    // clouds stop scanning
        std::atomic<float> tuneSemis { 0.0f };  // the sampler's TRANSPOSE + DETUNE
        std::atomic<float> velSense { 1.0f };
    } settings;

    GrainEngine()
    {
        for (int i = 0; i < WindowTable; ++i)
            rise[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * (float) i / (float) (WindowTable - 1));
        for (auto& g : grainView) g.store (-1.0f);
        for (auto* view : { &cloudView, &cloudEndView, &cloudAnchorView })
            for (auto& c : *view) c.store (-1.0f);
    }

    void prepare (double sampleRate)
    {
        deviceRate = sampleRate;
        for (auto& g : grains) g.active = false;
        for (auto& c : clouds) c = {};
    }

    //==========================================================================
    // Touch clouds — message thread. `x` and `y` are 0..1 across the sample
    // and up the waveform. Moving a held cloud just updates it.
    void touch (int id, bool down, float x, float y)
    {
        if (! juce::isPositiveAndBelow (id, NumTouch))
            return;
        auto& t = touches[(size_t) id];
        t.x.store (juce::jlimit (0.0f, 1.0f, x));
        t.y.store (juce::jlimit (0.0f, 1.0f, y));
        if (down && ! t.gate.load())
            t.generation.fetch_add (1);   // a fresh cloud: its loop starts here
        t.gate.store (down);
    }

    void releaseAllTouches()
    {
        for (auto& t : touches)
            t.gate.store (false);
    }

    bool isTouchHeld (int id) const { return juce::isPositiveAndBelow (id, NumTouch) && touches[(size_t) id].gate.load(); }

    //==========================================================================
    // Notes — audio thread.
    void noteOn (int note, int rootNote, float velocity)
    {
        auto* c = &clouds[NumTouch];
        for (int i = NumTouch; i < NumClouds; ++i)
            if (clouds[(size_t) i].amp <= 0.0f && ! clouds[(size_t) i].gate) { c = &clouds[(size_t) i]; break; }
            else if (clouds[(size_t) i].age > c->age) c = &clouds[(size_t) i];   // else steal the oldest

        *c = {};
        c->gate = true;
        c->note = note;
        c->pitch = (float) (note - rootNote);
        c->velocity = velocityGain (velocity);
        c->anchor = settings.position.load();
        c->countdown = 0.0;   // first grain straight away
    }

    void noteOff (int note)
    {
        for (int i = NumTouch; i < NumClouds; ++i)
            if (clouds[(size_t) i].gate && clouds[(size_t) i].note == note)
                clouds[(size_t) i].gate = false;
    }

    // Audio thread: everything stops (STOP, panic).
    void cutAll()
    {
        for (auto& g : grains) g.active = false;
        for (auto& c : clouds) c = {};
    }

    //==========================================================================
    // Audio thread. Adds into L and R; true if anything sounded.
    bool render (const Break& b, float* L, float* R, int n)
    {
        if (&b != lastBreak)
        {
            cutAll();
            lastBreak = &b;
        }

        const int len = b.audio.getNumSamples();
        if (len < 4)
            return false;

        syncTouches();

        bool any = false;
        for (auto& c : clouds)
            any |= c.gate || c.amp > 0.0f;
        for (auto& g : grains)
            any |= g.active;
        if (! any)
        {
            publish (len);
            return false;
        }

        const float* srcL = b.audio.getReadPointer (0);
        const float* srcR = b.audio.getReadPointer (1);
        const double srcPerOut = b.sampleRate / deviceRate;
        const double wrap = len - 1;

        const float attackStep = 1.0f / (float) (0.01 * deviceRate);
        const float releaseStep = 1.0f / (float) juce::jmax (1.0, settings.releaseMs.load() * 0.001 * deviceRate);
        const double loopLen = juce::jlimit (16.0, (double) len, settings.loopMs.load() * 0.001 * b.sampleRate);
        const double scanStep = settings.freeze.load() ? 0.0 : settings.scan.load() * srcPerOut;

        for (int i = 0; i < n; ++i)
        {
            // The clouds: envelopes, scanning, and new grains.
            for (auto& c : clouds)
            {
                if (! c.gate && c.amp <= 0.0f)
                    continue;

                c.amp = c.gate ? juce::jmin (1.0f, c.amp + attackStep) : juce::jmax (0.0f, c.amp - releaseStep);
                c.anchor += (c.target - c.anchor) * 0.0015f;   // a drag glides (~15 ms)
                c.scanPos += scanStep;
                if (c.scanPos >= loopLen) c.scanPos -= loopLen;
                if (c.scanPos < 0.0)      c.scanPos += loopLen;
                ++c.age;

                if ((c.countdown -= 1.0) <= 0.0 && c.amp > 0.0f)
                {
                    spawn (c, b, len, srcPerOut);
                    c.countdown += deviceRate / juce::jmax (0.5f, density (c)) * (0.9 + 0.2 * random01());
                }
            }

            // The grains.
            float l = 0.0f, r = 0.0f;
            for (auto& g : grains)
            {
                if (! g.active)
                    continue;

                const float w = windowAt ((float) (g.age / g.length), g.shape) * g.gain;
                const int i0 = juce::jlimit (0, len - 2, (int) g.pos);
                const float t = (float) (g.pos - i0);
                const float sl = srcL[i0] + t * (srcL[i0 + 1] - srcL[i0]);
                const float sr = srcR[i0] + t * (srcR[i0 + 1] - srcR[i0]);
                l += w * g.panL * sl;
                r += w * g.panR * sr;

                // Grains wrap round the sample.
                g.pos += g.rate;
                if (g.pos >= wrap)     g.pos -= wrap;
                else if (g.pos < 0.0)  g.pos += wrap;
                if ((g.age += 1.0) >= g.length)
                    g.active = false;
            }
            L[i] += l;
            if (R != nullptr) R[i] += r;
        }

        publish (len);
        return true;
    }

    //==========================================================================
    // For drawing — any thread. -1 where nothing is.
    float grainPosition (int i) const  { return grainView[(size_t) i].load(); }
    float grainLevel (int i) const     { return grainLevelView[(size_t) i].load(); }
    float cloudPosition (int i) const  { return cloudView[(size_t) i].load(); }
    float cloudLevel (int i) const     { return cloudLevelView[(size_t) i].load(); }
    float cloudLoopEnd (int i) const   { return cloudEndView[(size_t) i].load(); }
    float cloudAnchor (int i) const    { return cloudAnchorView[(size_t) i].load(); }

private:
    static constexpr int WindowTable = 512;

    struct Touch
    {
        std::atomic<bool> gate { false };
        std::atomic<float> x { 0.0f }, y { 0.5f };
        std::atomic<int> generation { 0 };
    };

    struct Cloud
    {
        bool gate = false;
        int note = -1, generation = 0;
        float amp = 0.0f, pitch = 0.0f, velocity = 1.0f, y = 0.5f;
        float anchor = 0.0f, target = 0.0f;   // 0..1; the anchor glides to the target
        double scanPos = 0.0, countdown = 0.0;
        juce::int64 age = 0;
    };

    struct Grain
    {
        bool active = false;
        double pos = 0.0, rate = 1.0, age = 0.0, length = 1.0;
        float gain = 0.0f, panL = 1.0f, panR = 1.0f, shape = 0.0f;
        int cloud = 0;
    };

    // Touch clouds follow their fingers. A new touch (a new generation)
    // starts its loop at the finger, without gliding there.
    void syncTouches()
    {
        for (int i = 0; i < NumTouch; ++i)
        {
            auto& t = touches[(size_t) i];
            auto& c = clouds[(size_t) i];
            const int gen = t.generation.load();
            const bool gate = t.gate.load();
            const float x = t.x.load();
            if (gate && (gen != c.generation || (c.amp <= 0.0f && ! c.gate)))
            {
                c = {};
                c.generation = gen;
                c.anchor = c.target = x;
            }
            c.gate = gate;
            c.target = x;
            c.y = t.y.load();
        }
        // Notes follow POSITION.
        const float pos = settings.position.load();
        for (int i = NumTouch; i < NumClouds; ++i)
            clouds[(size_t) i].target = pos;
    }

    float yBend (const Cloud& c, int axis) const
    {
        const bool touch = (&c - clouds.data()) < NumTouch;
        return touch && settings.yAxis.load() == axis ? c.y - 0.5f : 0.0f;   // -0.5..0.5
    }

    float density (const Cloud& c) const { return settings.density.load() * std::pow (2.0f, yBend (c, YDensity) * 4.0f); }

    void spawn (const Cloud& c, const Break& b, int len, double srcPerOut)
    {
        Grain* g = nullptr;
        for (auto& cand : grains)
            if (! cand.active) { g = &cand; break; }
        if (g == nullptr)
            return;

        const double sizeMs = juce::jlimit (5.0, 1000.0, (double) settings.sizeMs.load() * std::pow (2.0, yBend (c, YSize) * 4.0));
        const float spray = juce::jlimit (0.0f, 1.0f, settings.spray.load() + yBend (c, YSpray) * 2.0f);
        const double sprayed = spray * 0.25 * b.sampleRate * (2.0 * random01() - 1.0);

        const float semis = settings.tuneSemis.load() + c.pitch + yBend (c, YPitch) * 24.0f
                          + settings.jitter.load() * (float) (2.0 * random01() - 1.0);
        const double rate = srcPerOut * std::pow (2.0, semis / 12.0);
        const bool backwards = random01() < settings.reverse.load();

        g->active = true;
        g->age = 0.0;
        g->length = juce::jmax (8.0, sizeMs * 0.001 * deviceRate);
        g->rate = backwards ? -rate : rate;
        // Centred on the cloud's read point, so a grain's middle is where the
        // cloud is (a backwards grain starts at its end).
        const double centre = c.anchor * (len - 1) + c.scanPos + sprayed;
        g->pos = centre - g->rate * g->length * 0.5;
        g->pos = std::fmod (g->pos, (double) (len - 1));
        if (g->pos < 0.0) g->pos += len - 1;
        g->shape = settings.window.load();
        g->cloud = (int) (&c - clouds.data());

        // Overlapping grains add up: keep a dense cloud near one grain's level.
        const double overlap = juce::jmax (1.0, density (c) * sizeMs * 0.001);
        g->gain = c.amp * c.velocity * (float) (1.0 / std::sqrt (overlap));

        const float pan = settings.spread.load() * (float) (2.0 * random01() - 1.0);
        g->panL = juce::jmin (1.0f, 1.0f - pan);
        g->panR = juce::jmin (1.0f, 1.0f + pan);
    }

    // Tukey window: a raised-cosine rise and fall over `alpha` of the grain
    // (1 = Hann), flat between.
    float windowAt (float p, float shape) const
    {
        const float alpha = 1.0f - 0.9f * shape;
        const float half = alpha * 0.5f;
        float u = p < half ? p / half : p > 1.0f - half ? (1.0f - p) / half : 1.0f;
        u = juce::jlimit (0.0f, 1.0f, u);
        return rise[(size_t) (u * (WindowTable - 1))];
    }

    float velocityGain (float velocity) const
    {
        const float s = settings.velSense.load();
        return juce::jlimit (0.0f, 1.0f, 1.0f - s * (1.0f - velocity));
    }

    double random01()
    {
        // xorshift32: cheap, and plenty random for grains.
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        return (double) rng / 4294967296.0;
    }

    void publish (int len)
    {
        const double loopLen = settings.loopMs.load() * 0.001 * (lastBreak != nullptr ? lastBreak->sampleRate : 48000.0);
        for (int i = 0; i < NumGrains; ++i)
        {
            const auto& g = grains[(size_t) i];
            grainView[(size_t) i].store (g.active ? (float) (g.pos / len) : -1.0f);
            grainLevelView[(size_t) i].store (g.active ? windowAt ((float) (g.age / g.length), g.shape) * g.gain : 0.0f);
        }
        for (int i = 0; i < NumClouds; ++i)
        {
            const auto& c = clouds[(size_t) i];
            const bool on = c.gate || c.amp > 0.0f;
            const float at = c.anchor + (float) (c.scanPos / len);
            cloudView[(size_t) i].store (on ? at - std::floor (at) : -1.0f);
            cloudLevelView[(size_t) i].store (on ? c.amp : 0.0f);
            cloudEndView[(size_t) i].store (on ? juce::jmin (1.0f, c.anchor + (float) (loopLen / len)) : -1.0f);
            cloudAnchorView[(size_t) i].store (on ? c.anchor : -1.0f);
        }
    }

    double deviceRate = 48000.0;
    const Break* lastBreak = nullptr;   // audio thread
    std::array<Touch, NumTouch> touches;
    std::array<Cloud, NumClouds> clouds;
    std::array<Grain, NumGrains> grains;
    std::array<float, WindowTable> rise {};
    juce::uint32 rng = 0x9E3779B9u;

    std::array<std::atomic<float>, NumGrains> grainView, grainLevelView;
    std::array<std::atomic<float>, NumClouds> cloudView, cloudLevelView, cloudEndView, cloudAnchorView;
};
