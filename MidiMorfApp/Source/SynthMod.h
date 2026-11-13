#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <cmath>
#include <vector>

//==============================================================================
// SynthMod — the synth's modulation building blocks that aren't the voice
// itself: drawable LFO shapes, LFO settings, and the tempo divisions an LFO
// can sync to.
//
// An LFO's shape is a list of points (Serum-style): x 0..1 across one cycle,
// y -1..1, and a curve per segment that bends the line between a point and
// the next one. Two points may share an x, which makes a vertical step. The
// first point is always at x = 0 and the last at x = 1.
//
// The audio thread never reads the points. Each edit bakes the shape into a
// TableSize-point table, and the LFO swaps between two tables with an atomic
// index, so the voice only ever does a table lookup.
namespace SynthMod
{
    struct Point
    {
        float x = 0.0f, y = 0.0f;
        float curve = 0.0f;   // -1..1: the segment from this point to the next bows down/up
    };

    using Shape = std::vector<Point>;

    // The bend of one segment: t 0..1 in, 0..1 out. Positive curves rise
    // late (ease-in), negative early (ease-out).
    inline float bend (float t, float curve)
    {
        if (std::abs (curve) < 0.001f) return t;
        const float e = std::exp2 (curve * 3.0f);   // 1/8 .. 8
        return curve > 0.0f ? std::pow (t, e) : 1.0f - std::pow (1.0f - t, 1.0f / e);
    }

    inline float valueAt (const Shape& s, float x)
    {
        if (s.empty()) return 0.0f;
        if (x <= s.front().x) return s.front().y;
        for (size_t i = 0; i + 1 < s.size(); ++i)
        {
            const auto& a = s[i];
            const auto& b = s[i + 1];
            if (x <= b.x)
            {
                const float w = b.x - a.x;
                if (w <= 1.0e-6f) return b.y;
                return a.y + (b.y - a.y) * bend ((x - a.x) / w, a.curve);
            }
        }
        return s.back().y;
    }

    // Keeps a shape valid after an edit: sorted, inside the box, ends pinned.
    inline void tidy (Shape& s)
    {
        for (auto& p : s)
        {
            p.x = juce::jlimit (0.0f, 1.0f, p.x);
            p.y = juce::jlimit (-1.0f, 1.0f, p.y);
            p.curve = juce::jlimit (-1.0f, 1.0f, p.curve);
        }
        std::stable_sort (s.begin(), s.end(), [] (const Point& a, const Point& b) { return a.x < b.x; });
        if (s.empty() || s.front().x > 0.0f) s.insert (s.begin(), { 0.0f, s.empty() ? 0.0f : s.front().y, 0.0f });
        if (s.back().x < 1.0f) s.push_back ({ 1.0f, s.front().y, 0.0f });
        s.front().x = 0.0f;
        s.back().x = 1.0f;
    }

    //==========================================================================
    // Shape presets. The first four are the old LFO's waves, in its order, so
    // a patch's lfoWave index maps straight across.
    inline juce::StringArray presetNames()
    {
        return { "Sine", "Triangle", "Saw", "Square", "Ramp Down", "Exp Up", "Steps", "Gate 1/2", "Gate 3/4", "Bounce", "Random" };
    }

    inline Shape preset (int index)
    {
        switch (index)
        {
            case 0:  return { { 0.0f, 0.0f, -0.55f }, { 0.25f, 1.0f, 0.55f }, { 0.5f, 0.0f, -0.55f }, { 0.75f, -1.0f, 0.55f }, { 1.0f, 0.0f, 0.0f } };
            case 1:  return { { 0.0f, 0.0f }, { 0.25f, 1.0f }, { 0.75f, -1.0f }, { 1.0f, 0.0f } };
            case 2:  return { { 0.0f, -1.0f }, { 1.0f, 1.0f } };
            case 3:  return { { 0.0f, 1.0f }, { 0.5f, 1.0f }, { 0.5f, -1.0f }, { 1.0f, -1.0f } };
            case 4:  return { { 0.0f, 1.0f }, { 1.0f, -1.0f } };
            case 5:  return { { 0.0f, -1.0f, 0.7f }, { 1.0f, 1.0f } };
            case 6:  return { { 0.0f, -1.0f }, { 0.25f, -1.0f }, { 0.25f, -0.33f }, { 0.5f, -0.33f }, { 0.5f, 0.33f },
                              { 0.75f, 0.33f }, { 0.75f, 1.0f }, { 1.0f, 1.0f } };
            case 7:  return { { 0.0f, 1.0f }, { 0.5f, 1.0f }, { 0.5f, -1.0f }, { 1.0f, -1.0f } };
            case 8:  return { { 0.0f, 1.0f }, { 0.75f, 1.0f }, { 0.75f, -1.0f }, { 1.0f, -1.0f } };
            case 9:  return { { 0.0f, 1.0f, 0.6f }, { 0.5f, -1.0f, -0.6f }, { 1.0f, 1.0f } };
            case 10:
            {
                juce::Random r (1234);
                Shape s;
                for (int i = 0; i < 8; ++i)
                {
                    const float y = r.nextFloat() * 2.0f - 1.0f;
                    s.push_back ({ (float) i / 8.0f, y });
                    s.push_back ({ (float) (i + 1) / 8.0f, y });
                }
                return s;
            }
            default: return preset (0);
        }
    }

    inline juce::String toString (const Shape& s)
    {
        juce::StringArray parts;
        for (auto& p : s)
            parts.add (juce::String (p.x, 4) + "," + juce::String (p.y, 4) + "," + juce::String (p.curve, 3));
        return parts.joinIntoString (";");
    }

    inline Shape fromString (const juce::String& text)
    {
        Shape s;
        for (auto& part : juce::StringArray::fromTokens (text, ";", ""))
        {
            auto v = juce::StringArray::fromTokens (part, ",", "");
            if (v.size() >= 2)
                s.push_back ({ v[0].getFloatValue(), v[1].getFloatValue(), v.size() > 2 ? v[2].getFloatValue() : 0.0f });
        }
        if (s.size() < 2) return preset (0);
        tidy (s);
        return s;
    }

    //==========================================================================
    // Tempo divisions, in beats per cycle.
    inline juce::StringArray divisionNames()
    {
        return { "1/32", "1/16T", "1/16", "1/8T", "1/16D", "1/8", "1/4T", "1/8D", "1/4", "1/2T", "1/4D", "1/2", "1 bar", "2 bars", "4 bars", "8 bars" };
    }

    inline double divisionBeats (int index)
    {
        static const double beats[] = { 0.125, 1.0 / 6.0, 0.25, 1.0 / 3.0, 0.375, 0.5, 2.0 / 3.0, 0.75,
                                        1.0, 4.0 / 3.0, 1.5, 2.0, 4.0, 8.0, 16.0, 32.0 };
        return beats[juce::jlimit (0, (int) std::size (beats) - 1, index)];
    }

    //==========================================================================
    enum LfoMode { Free = 0, Retrigger = 1, Envelope = 2 };

    inline juce::StringArray modeNames() { return { "Free", "Retrig", "Env" }; }

    class Lfo
    {
    public:
        static constexpr int TableSize = 1024;

        Lfo() { setShape (preset (0)); }

        // Message thread.
        void setShape (Shape s)
        {
            tidy (s);
            shape = std::move (s);
            const int next = 1 - live.load();
            auto& t = tables[(size_t) next];
            for (int i = 0; i < TableSize; ++i)
                t[(size_t) i] = valueAt (shape, (float) i / (float) TableSize);
            t[TableSize] = t[0];
            live.store (next);
        }

        const Shape& getShape() const { return shape; }

        // Audio thread: phase 0..1.
        float read (double phase) const noexcept
        {
            const auto& t = tables[(size_t) live.load()];
            const double idx = phase * TableSize;
            const int i0 = juce::jlimit (0, TableSize - 1, (int) idx);
            const float f = (float) (idx - i0);
            return t[(size_t) i0] + f * (t[(size_t) i0 + 1] - t[(size_t) i0]);
        }

        // Plain settings, written by the UI and read by the audio thread a
        // block at a time, like the rest of the engine's knobs.
        float rate = 1.0f;        // Hz, when not synced
        bool  sync = false;
        int   division = 8;       // 1/4
        int   mode = Free;
        float rise = 0.0f;        // seconds to fade in after the delay (per note)
        float delay = 0.0f;       // seconds before it starts (per note)
        float phase = 0.0f;       // 0..1 start phase
        float depth = 1.0f;       // scales the output

        // Cycles per second at `bpm`.
        double hz (double bpm) const
        {
            return sync ? bpm / 60.0 / divisionBeats (division) : (double) rate;
        }

        void reset()
        {
            setShape (preset (0));
            rate = 1.0f; sync = false; division = 8; mode = Free;
            rise = delay = phase = 0.0f; depth = 1.0f;
        }

    private:
        Shape shape;
        std::array<std::array<float, TableSize + 1>, 2> tables {};
        std::atomic<int> live { 0 };
    };
}
