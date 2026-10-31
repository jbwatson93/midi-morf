#pragma once

#include <JuceHeader.h>
#include <vector>

// Test-only loops (Tools/make_dev_breaks.py): Debug builds, never Release.
#if JUCE_DEBUG && __has_include("DevBreaks.h")
 #include "DevBreaks.h"
 #define MIDIMORF_DEV_BREAKS 1
#endif

//==============================================================================
// A drum loop for the slice sequencer to cut up in Local mode.
//
// Slices are measured in beats, not as N equal parts of the file: slice i
// starts i steps into the loop at the loop's own tempo, and plays at native
// speed. Picking a break leaves the project's tempo alone: the slices warp
// the loop to it without repitching (see BreakPlayer), and at the loop's own
// tempo the untouched pattern reproduces it exactly.
struct Break
{
    juce::String name;
    juce::AudioBuffer<float> audio;   // always stereo
    double sampleRate = 44100.0;
    double beats      = 4.0;          // loop length in quarter notes
    int    gridBars   = 0;            // slice grid to open on; 0 = the whole loop

    double lengthSeconds() const { return audio.getNumSamples() / sampleRate; }
    double bpm() const           { return beats * 60.0 / juce::jmax (0.001, lengthSeconds()); }
    int    bars() const          { return juce::jmax (1, juce::roundToInt (beats / 4.0)); }
};

//==============================================================================
// Built-in breaks come first as recorded loops bundled in BinaryData (played
// by JB, so they ship), then synthesised ones, rendered at load time in a
// few milliseconds each — every synthesised pattern here is original.
namespace BreakLibrary
{
    static constexpr double RenderRate = 44100.0;

    // Per-break drum voicing.
    struct Kit
    {
        float kickHigh  = 160.0f;   // Hz at the start of the pitch drop
        float kickLow   = 50.0f;    // Hz it settles to
        float kickDecay = 0.30f;    // s
        float snareTune = 190.0f;   // Hz, body
        float snareDecay = 0.16f;   // s, noise tail
        float tone      = 9000.0f;  // bus low-pass, Hz — lower = dustier
        float drive     = 1.6f;     // bus saturation
        float room      = 0.12f;    // reverb wet
    };

    // One bar of 16ths per lane. '.' nothing, 'g' ghost, 'x' normal, 'X' accent.
    struct Style
    {
        const char* name;
        double bpm;
        float swing;   // 0..1, delays every second 16th (0.5 ≈ triplet feel)
        const char* kick;
        const char* snare;
        const char* hat;
        const char* openHat;
        const char* ride;
        const char* clap;
        Kit kit;
    };

    inline const std::vector<Style>& styles()
    {
        static const std::vector<Style> s = {
            { "Soul Shuffle", 96.0, 0.25f,
              "X.....x.X.....x.", "....X..g.g..X..g", "xgxgxgxgxgxgxg..", "..............x.", "", "",
              { 150.0f, 52.0f, 0.32f, 200.0f, 0.15f, 8000.0f, 1.8f, 0.14f } },
            { "Boom Bap", 90.0, 0.35f,
              "X......x.xX.....", "....X.......X...", "x.x.x.x.x.x.x.x.", "", "", "",
              { 140.0f, 46.0f, 0.42f, 180.0f, 0.20f, 6500.0f, 2.4f, 0.10f } },
            { "Jungle Roller", 168.0, 0.0f,
              "X.x.......xX....", "....X..g.X.gX..g", "", "", "x.x.x.x.x.x.x.x.", "",
              { 170.0f, 55.0f, 0.22f, 220.0f, 0.13f, 10000.0f, 2.0f, 0.16f } },
            { "Big Beat", 128.0, 0.0f,
              "X..x..X...X..x..", "....X.......X...", "x...x...x...x...", "..x...x...x...x.", "", "....x.......x...",
              { 155.0f, 48.0f, 0.36f, 185.0f, 0.18f, 11000.0f, 2.8f, 0.18f } },
            { "Lazy Hop", 84.0, 0.5f,
              "X.......x.X....x", "....X.......X...", "x.xgx.xgx.xgx.xg", "", "", "",
              { 130.0f, 44.0f, 0.45f, 175.0f, 0.22f, 5500.0f, 2.0f, 0.12f } },
            { "Two-Step", 132.0, 0.3f,
              "X......x..X.....", "....X..g....X.g.", "gxgxgxgxgxgxgxgx", "", "", "....x.......x...",
              { 160.0f, 50.0f, 0.28f, 210.0f, 0.12f, 12000.0f, 1.6f, 0.15f } },
            { "Disco Break", 118.0, 0.0f,
              "X...X...X...X...", "....X.......X...", "x.g.x.g.x.g.x.g.", "..x...x...x...x.", "", "....x.......x...",
              { 150.0f, 50.0f, 0.30f, 195.0f, 0.16f, 12000.0f, 1.4f, 0.16f } },
            { "Ride Funk", 104.0, 0.15f,
              "X..x.....xX.....", "....X..g.g..X.g.", "", "", "X.x.X.x.X.x.X.x.", "",
              { 145.0f, 50.0f, 0.34f, 205.0f, 0.15f, 9000.0f, 1.8f, 0.14f } },
            { "Half-Time", 140.0, 0.0f,
              "X.....x...x.....", "........X.......", "x.x.x.x.x.x.x.xx", "......x.........", "", "........x.......",
              { 145.0f, 42.0f, 0.50f, 180.0f, 0.24f, 10000.0f, 2.2f, 0.20f } },
        };
        return s;
    }

    // Recorded loops in BinaryData. Listed only once Projucer has bundled the
    // file, so a build from a stale project just doesn't offer them.
    struct Recorded { const char* name; const char* resource; double beats; int gridBars; };

    inline std::vector<Recorded> recorded()
    {
        static const Recorded all[] = {
            { "Amen Break",      "Amen140_wav",         8.0, 1 },   // 2 bars, 140 BPM; opens on 16 steps of bar 1
            { "Think",           "Think111_wav",        4.0, 1 },   // 1 bar, 111 BPM
            { "Funky Drummer",   "FunkyDrummer98_wav",  4.0, 1 },   // 1 bar, 98 BPM
            { "Funky Drummer 2", "FunkyDrummer98b_wav", 4.0, 1 },   // 1 bar, 98 BPM, a second take
            { "Impeach",         "Impeach94_wav",       4.0, 1 },   // 1 bar, 94 BPM
            { "Synthetic Sub",   "SyntheticSub95_wav",  8.0, 1 },   // 2 bars, 95 BPM
            { "Jungle Jazz",     "JungleJazz110_wav",   4.0, 1 },   // 1 bar, 110 BPM
            { "Who Is She",      "WhoIsShe101_wav",     4.0, 1 },   // 1 bar, 101 BPM
        };

        std::vector<Recorded> present;
        for (auto& r : all)
        {
            int size = 0;
            if (BinaryData::getNamedResource (r.resource, size) != nullptr)
                present.push_back (r);
        }
        return present;
    }

    inline juce::StringArray builtInNames()
    {
        juce::StringArray names;
        for (auto& r : recorded())
            names.add (r.name);
        for (auto& s : styles())
            names.add (s.name);
       #if MIDIMORF_DEV_BREAKS
        for (auto& d : DevBreaks::items)
            names.add (d.name);
       #endif
        return names;
    }

    inline int indexForName (const juce::String& name)
    {
        return builtInNames().indexOf (name);
    }

    //==========================================================================
    namespace detail
    {
        constexpr double twoPi = juce::MathConstants<double>::twoPi;

        struct OnePole   // low-pass; high-pass is input minus this
        {
            float a = 0.0f, z = 0.0f;
            void setCutoff (float hz) { a = (float) std::exp (-twoPi * hz / RenderRate); }
            float lp (float x) { z = x + a * (z - x); return z; }
            float hp (float x) { return x - lp (x); }
        };

        inline float velocityFor (char c)
        {
            switch (c)
            {
                case 'X': return 1.0f;
                case 'x': return 0.78f;
                case 'g': return 0.3f;
                default:  return 0.0f;
            }
        }

        // Every voice writes mono into `out` from `start`, never past `maxLen`.
        inline void kick (float* out, int start, int maxLen, float vel, const Kit& k, juce::Random& rng)
        {
            const int len = juce::jmin (maxLen, (int) (k.kickDecay * 6.0 * RenderRate));
            double phase = 0.0;
            for (int i = 0; i < len; ++i)
            {
                const double t = i / RenderRate;
                const double f = k.kickLow + (k.kickHigh - k.kickLow) * std::exp (-t / 0.03);
                phase += twoPi * f / RenderRate;
                const float body  = (float) (std::sin (phase) * std::exp (-t / k.kickDecay));
                const float click = (rng.nextFloat() * 2.0f - 1.0f) * (float) std::exp (-t / 0.0015) * 0.35f;
                out[start + i] += vel * std::tanh (1.4f * (body + click));
            }
        }

        inline void snare (float* out, int start, int maxLen, float vel, const Kit& k, juce::Random& rng)
        {
            const int len = juce::jmin (maxLen, (int) (k.snareDecay * 6.0 * RenderRate));
            OnePole hp, lp;
            hp.setCutoff (1200.0f);
            lp.setCutoff (8000.0f);
            for (int i = 0; i < len; ++i)
            {
                const double t = i / RenderRate;
                const float body = (float) ((std::sin (twoPi * k.snareTune * t) * 0.6
                                           + std::sin (twoPi * k.snareTune * 1.74 * t) * 0.4)
                                          * std::exp (-t / 0.055));
                const float noise = lp.lp (hp.hp (rng.nextFloat() * 2.0f - 1.0f))
                                    * (float) std::exp (-t / k.snareDecay);
                out[start + i] += vel * (body * 0.55f + noise * 1.3f);
            }
        }

        // 808-style metallic source: six detuned squares through a high-pass.
        inline void metal (float* out, int start, int len, float vel, float decay,
                           float freqScale, float hpHz, float level)
        {
            static const float freqs[6] = { 205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f };
            OnePole hp1, hp2;
            hp1.setCutoff (hpHz);
            hp2.setCutoff (hpHz);
            for (int i = 0; i < len; ++i)
            {
                const double t = i / RenderRate;
                float s = 0.0f;
                for (float f : freqs)
                    s += std::fmod (t * f * freqScale, 1.0) < 0.5 ? 1.0f : -1.0f;
                s = hp2.hp (hp1.hp (s / 6.0f));
                // 5 ms fade at the end so a choked hat never clicks.
                const float fade = juce::jmin (1.0f, (float) (len - i) / (float) (0.005 * RenderRate));
                out[start + i] += vel * level * s * (float) std::exp (-t / decay) * fade;
            }
        }

        inline void clap (float* out, int start, int maxLen, float vel, juce::Random& rng)
        {
            const int len = juce::jmin (maxLen, (int) (0.6 * RenderRate));
            OnePole hp, lp;
            hp.setCutoff (900.0f);
            lp.setCutoff (2600.0f);
            for (int i = 0; i < len; ++i)
            {
                const double t = i / RenderRate;
                double env = std::exp (-juce::jmax (0.0, t - 0.02) / 0.11) * 0.6;
                for (double burst : { 0.0, 0.009, 0.018 })
                    if (t >= burst)
                        env += std::exp (-(t - burst) / 0.004);
                out[start + i] += vel * 0.9f * lp.lp (hp.hp (rng.nextFloat() * 2.0f - 1.0f)) * (float) env;
            }
        }

        // Equal-power pan of a mono lane into the stereo mix.
        inline void addPanned (juce::AudioBuffer<float>& mix, const std::vector<float>& lane, float pan)
        {
            const float angle = (pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
            mix.addFrom (0, 0, lane.data(), (int) lane.size(), std::cos (angle));
            mix.addFrom (1, 0, lane.data(), (int) lane.size(), std::sin (angle));
        }
    }

    inline std::unique_ptr<Break> fromReader (std::unique_ptr<juce::AudioFormatReader>, const juce::String&, double, juce::String&);

    // Renders three passes of the bar and keeps the middle one, so the reverb
    // and the last hits' tails wrap into the start and the loop is seamless.
    inline std::unique_ptr<Break> generate (int index)
    {
        using namespace detail;

        const auto rec = recorded();
        if (juce::isPositiveAndBelow (index, (int) rec.size()))
        {
            const auto& r = rec[(size_t) index];
            int size = 0;
            const char* data = BinaryData::getNamedResource (r.resource, size);
            juce::String error;
            auto b = fromReader (std::unique_ptr<juce::AudioFormatReader> (juce::WavAudioFormat().createReaderFor (
                                     new juce::MemoryInputStream (data, (size_t) size, false), true)),
                                 r.name, r.beats, error);
            if (b != nullptr)
                b->gridBars = r.gridBars;
            return b;
        }
        index -= (int) rec.size();

        const auto& all = styles();
       #if MIDIMORF_DEV_BREAKS
        if (const int dev = index - (int) all.size(); juce::isPositiveAndBelow (dev, (int) std::size (DevBreaks::items)))
        {
            const auto& d = DevBreaks::items[dev];
            juce::String error;
            return fromReader (std::unique_ptr<juce::AudioFormatReader> (juce::WavAudioFormat().createReaderFor (
                                   new juce::MemoryInputStream (d.data, d.size, false), true)),
                               d.name, d.beats, error);
        }
       #endif
        if (! juce::isPositiveAndBelow (index, (int) all.size()))
            return nullptr;

        const auto& st = all[(size_t) index];
        const auto& kit = st.kit;
        const int barLen  = (int) std::round (4.0 * 60.0 / st.bpm * RenderRate);
        const int stepLen = barLen / 16;
        const int total   = barLen * 3;

        juce::Random rng (0x5EED + index);   // same break every launch
        std::vector<float> kickL ((size_t) total), snareL ((size_t) total), hatL ((size_t) total),
                           rideL ((size_t) total), clapL ((size_t) total);

        auto lane = [] (const char* p, int step) { return (p != nullptr && (int) std::strlen (p) > step) ? p[step] : '.'; };

        // Push and velocity wobble are fixed per step, so all three passes
        // are identical and the kept middle bar joins itself exactly.
        int   push[16];
        float wobble[16];
        for (int step = 0; step < 16; ++step)
        {
            push[step]   = (int) (rng.nextFloat() * 0.003 * RenderRate);
            wobble[step] = 0.92f + rng.nextFloat() * 0.16f;
        }

        // Hits only ever land late (swing and a few ms of push), never early,
        // so a slice that starts on the grid never cuts off its own hit.
        auto hitTime = [&] (int pass, int step)
        {
            int t = pass * barLen + step * stepLen + push[step];
            if (step % 2 == 1)
                t += (int) (st.swing * 0.66f * stepLen);
            return t;
        };

        int currentStep = 0;
        auto humanise = [&] (float v) { return v * wobble[currentStep]; };

        for (int pass = 0; pass < 3; ++pass)
        {
            for (int step = 0; step < 16; ++step)
            {
                currentStep = step;

                if (float v = velocityFor (lane (st.kick, step)); v > 0.0f)
                {
                    const int t = hitTime (pass, step);
                    kick (kickL.data(), t, total - t, humanise (v), kit, rng);
                }

                if (float v = velocityFor (lane (st.snare, step)); v > 0.0f)
                {
                    const int t = hitTime (pass, step);
                    snare (snareL.data(), t, total - t, humanise (v), kit, rng);
                }

                if (float v = velocityFor (lane (st.clap, step)); v > 0.0f)
                {
                    const int t = hitTime (pass, step);
                    clap (clapL.data(), t, total - t, humanise (v), rng);
                }

                if (float v = velocityFor (lane (st.ride, step)); v > 0.0f)
                {
                    const int t = hitTime (pass, step);
                    metal (rideL.data(), t, juce::jmin (total - t, (int) (1.2 * RenderRate)),
                           humanise (v), 0.6f, 1.9f, 5000.0f, 0.5f);
                }

                // Closed and open hats share a lane: any hat chokes the open one.
                const float open   = velocityFor (lane (st.openHat, step));
                const float closed = open > 0.0f ? 0.0f : velocityFor (lane (st.hat, step));

                if (open > 0.0f || closed > 0.0f)
                {
                    const int t = hitTime (pass, step);
                    int next = total;
                    for (int s = step + 1; s < 48; ++s)
                    {
                        const int st16 = s % 16;
                        if (velocityFor (lane (st.hat, st16)) > 0.0f || velocityFor (lane (st.openHat, st16)) > 0.0f)
                        {
                            next = pass * barLen + s * stepLen;
                            break;
                        }
                    }

                    const float decay = open > 0.0f ? 0.28f : 0.045f;
                    const int len = juce::jmin (juce::jmax (0, next - t), total - t, (int) (decay * 6.0f * RenderRate));
                    metal (hatL.data(), t, len, humanise (open > 0.0f ? open : closed),
                           decay, 1.0f, 7000.0f, open > 0.0f ? 0.55f : 0.7f);
                }
            }
        }

        juce::AudioBuffer<float> mix (2, total);
        mix.clear();
        addPanned (mix, kickL,  0.0f);
        addPanned (mix, snareL, 0.05f);
        addPanned (mix, clapL,  -0.1f);
        addPanned (mix, hatL,   0.3f);
        addPanned (mix, rideL,  -0.35f);

        // Bus: dull it down, glue with saturation, then a small room.
        const float norm = 1.0f / std::tanh (kit.drive);
        for (int ch = 0; ch < 2; ++ch)
        {
            OnePole lp;
            lp.setCutoff (kit.tone);
            auto* d = mix.getWritePointer (ch);
            for (int i = 0; i < total; ++i)
                d[i] = std::tanh (lp.lp (d[i]) * kit.drive * 0.8f) * norm;
        }

        juce::Reverb reverb;
        juce::Reverb::Parameters rp;
        rp.roomSize = 0.3f;
        rp.damping  = 0.6f;
        rp.wetLevel = kit.room;
        rp.dryLevel = 1.0f;
        rp.width    = 0.8f;
        reverb.setSampleRate (RenderRate);
        reverb.setParameters (rp);
        reverb.processStereo (mix.getWritePointer (0), mix.getWritePointer (1), total);

        auto out = std::make_unique<Break>();
        out->name       = st.name;
        out->sampleRate = RenderRate;
        out->beats      = 4.0;
        out->audio.setSize (2, barLen);
        for (int ch = 0; ch < 2; ++ch)
            out->audio.copyFrom (ch, 0, mix, ch, barLen, barLen);

        const float peak = out->audio.getMagnitude (0, barLen);
        if (peak > 0.0f)
            out->audio.applyGain (0.8f / peak);

        return out;
    }

    //==========================================================================
    // A user loop is assumed to be a power-of-two number of bars — whichever
    // puts it at a plausible tempo. The shortest wins, so a 2 s loop is 1 bar
    // at 120. Only a guess for files not trimmed to whole bars.
    inline double estimateBeats (double seconds)
    {
        double best = 4.0, bestDist = 1.0e9;
        for (double beats : { 4.0, 8.0, 16.0, 32.0, 64.0 })
        {
            const double bpm = beats * 60.0 / seconds;
            if (bpm >= 75.0 && bpm <= 180.0)
                return beats;
            const double dist = std::abs (bpm - 120.0);
            if (dist < bestDist) { bestDist = dist; best = beats; }
        }
        return best;
    }

    // beats <= 0 means guess it from the length.
    inline std::unique_ptr<Break> fromReader (std::unique_ptr<juce::AudioFormatReader> reader,
                                              const juce::String& name, double beats, juce::String& error)
    {
        if (reader == nullptr)
        {
            error = "Couldn't read " + name;
            return nullptr;
        }

        const double maxSeconds = 30.0;
        const auto length = (int) juce::jmin<juce::int64> (reader->lengthInSamples,
                                                           (juce::int64) (maxSeconds * reader->sampleRate));
        if (length < (int) (0.25 * reader->sampleRate))
        {
            error = name + " is too short to slice";
            return nullptr;
        }

        auto out = std::make_unique<Break>();
        out->name       = name;
        out->sampleRate = reader->sampleRate;
        out->audio.setSize (2, length);
        reader->read (&out->audio, 0, length, 0, true, true);
        if (reader->numChannels < 2)
            out->audio.copyFrom (1, 0, out->audio, 0, 0, length);

        out->beats = beats > 0.0 ? beats : estimateBeats (out->lengthSeconds());

        const float peak = out->audio.getMagnitude (0, length);
        if (peak > 0.0f && peak < 0.5f)
            out->audio.applyGain (0.8f / peak);   // lift quiet files, never clip loud ones

        return out;
    }

    inline std::unique_ptr<Break> importFile (const juce::File& file, juce::String& error)
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        return fromReader (std::unique_ptr<juce::AudioFormatReader> (formats.createReaderFor (file)),
                           file.getFileNameWithoutExtension(), 0.0, error);
    }
}
