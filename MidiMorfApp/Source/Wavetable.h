#pragma once

#include <JuceHeader.h>
#include <vector>
#include <complex>
#include <memory>
#include <map>

//==============================================================================
// Wavetable — a stack of single-cycle frames, each pre-rendered at several
// band-limited "mip" levels so high notes don't alias.
//
// Everything is built from spectra: a frame is described by its first 1024
// harmonics, and mip level k keeps only harmonics up to 1024 >> k. Level 0
// is the full table (fine for low notes); level 10 is a pure sine. The voice
// picks the level whose top harmonic still sits below Nyquist for the note
// it's playing (see levelForIncrement).
//
// Frames are capped at MaxFrames to bound memory: 64 frames × 11 levels ×
// 2049 floats ≈ 5.8 MB. Only the tables an engine currently has selected are
// kept in memory — they're cheap to rebuild (a few ms) when picked again.
class Wavetable
{
public:
    static constexpr int FrameSize    = 2048;
    static constexpr int FrameOrder   = 11;            // log2 (FrameSize)
    static constexpr int NumHarmonics = FrameSize / 2; // 1024
    static constexpr int NumLevels    = FrameOrder;    // 11: 1024 … 1 harmonics
    static constexpr int MaxFrames    = 64;

    using Spectrum = std::vector<std::complex<float>>; // bins 0..NumHarmonics

    // Build from per-frame spectra. Bin h is harmonic h (bin 0 / DC ignored).
    // Bins are in juce::dsp::FFT's real-forward layout at FrameSize points, so
    // a unit sine fundamental is bin 1 = (0, -FrameSize/2). Absolute scale
    // doesn't matter much — the table is peak-normalised after building.
    Wavetable (juce::String tableName, const std::vector<Spectrum>& spectra)
        : name (std::move (tableName))
    {
        numFrames = juce::jlimit (1, MaxFrames, (int) spectra.size());
        data.assign ((size_t) (NumLevels * numFrames * Stride), 0.0f);

        juce::dsp::FFT fft (FrameOrder);
        std::vector<float> work ((size_t) FrameSize * 2);

        for (int f = 0; f < numFrames; ++f)
        {
            // Evenly pick source frames when there are more than MaxFrames.
            const auto& src = spectra[(size_t) (spectra.size() <= (size_t) MaxFrames
                                                    ? f
                                                    : (f * ((int) spectra.size() - 1)) / (MaxFrames - 1))];

            for (int level = 0; level < NumLevels; ++level)
            {
                const int maxHarm = NumHarmonics >> level;
                auto* bins = reinterpret_cast<std::complex<float>*> (work.data());
                std::fill (work.begin(), work.end(), 0.0f);

                for (int h = 1; h <= maxHarm && h < (int) src.size(); ++h)
                {
                    bins[h] = src[(size_t) h];
                    bins[FrameSize - h] = std::conj (src[(size_t) h]);
                }

                fft.performRealOnlyInverseTransform (work.data());

                float* dst = frameData (level, f);
                for (int i = 0; i < FrameSize; ++i)
                    dst[i] = work[(size_t) i];
                dst[FrameSize] = dst[0]; // guard sample for interpolation wrap
            }
        }

        // Normalise the whole table by the peak of the full-bandwidth level so
        // frames keep their relative loudness but the loudest one hits ±1.
        float peak = 0.0f;
        for (int f = 0; f < numFrames; ++f)
        {
            const float* p = frameData (0, f);
            for (int i = 0; i < FrameSize; ++i)
                peak = juce::jmax (peak, std::abs (p[i]));
        }
        if (peak > 1.0e-6f)
            juce::FloatVectorOperations::multiply (data.data(), 1.0f / peak, (int) data.size());
    }

    const juce::String& getName() const noexcept { return name; }
    int getNumFrames() const noexcept { return numFrames; }

    // Mip level for a phase increment (cycles per sample): the lowest level
    // whose top harmonic stays under Nyquist.
    static int levelForIncrement (double inc) noexcept
    {
        if (inc <= 0.0) return 0;
        int level = (int) std::ceil (std::log2 (inc * 2.0 * NumHarmonics));
        return juce::jlimit (0, NumLevels - 1, level);
    }

    // Read one sample. phase 0..1, position 0..1 across frames. Linear interp
    // within a frame and between the two neighbouring frames.
    float read (int level, double phase, float position) const noexcept
    {
        const float framePos = position * (float) (numFrames - 1);
        const int   f0       = juce::jlimit (0, numFrames - 1, (int) framePos);
        const int   f1       = juce::jmin (f0 + 1, numFrames - 1);
        const float fFrac    = framePos - (float) f0;

        const double idx  = phase * FrameSize;
        const int    i0   = juce::jlimit (0, FrameSize - 1, (int) idx);
        const float  frac = (float) (idx - i0);

        const float* a = frameData (level, f0);
        const float* b = frameData (level, f1);
        const float sa = a[i0] + frac * (a[i0 + 1] - a[i0]);
        const float sb = b[i0] + frac * (b[i0 + 1] - b[i0]);
        return sa + fFrac * (sb - sa);
    }

    // Full-bandwidth frame for drawing in the UI.
    const float* getDisplayFrame (int frame) const noexcept
    {
        return frameData (0, juce::jlimit (0, numFrames - 1, frame));
    }

private:
    static constexpr int Stride = FrameSize + 1;

    float* frameData (int level, int frame) noexcept
    {
        return data.data() + ((size_t) level * (size_t) numFrames + (size_t) frame) * Stride;
    }
    const float* frameData (int level, int frame) const noexcept
    {
        return data.data() + ((size_t) level * (size_t) numFrames + (size_t) frame) * Stride;
    }

    juce::String name;
    int numFrames = 1;
    std::vector<float> data;
};

//==============================================================================
// WavetableLibrary — the built-in tables (generated in code, no assets) plus
// Serum-style .wav import. Built-ins are described by an index; call build()
// to materialise one.
//
// The first NumAnalogWaves built-ins are the classic analog shapes as
// single-frame tables. They go through the same band-limited mip levels, so
// they're alias-free at every pitch with no PolyBLEP needed, and an
// oscillator can flip between "analog" and "wavetable" just by picking a
// different table. Patches store tables by name, so this list can be
// reordered or extended freely.
namespace WavetableLibrary
{
    using Spectrum = Wavetable::Spectrum;

    static constexpr int BuiltInFrames  = 32;
    static constexpr int NumAnalogWaves = 4;

    inline const juce::StringArray& builtInNames()
    {
        static const juce::StringArray names {
            "Sine", "Triangle", "Saw", "Square",
            "Morph", "PWM", "Harmonics", "Vocal", "FM",
            "Sync", "Fold", "Digital", "Organ", "Glass"
        };
        return names;
    }

    inline bool isAnalog (int index) { return index >= 0 && index < NumAnalogWaves; }

    // -1 if there's no built-in by that name.
    inline int indexForName (const juce::String& name) { return builtInNames().indexOf (name); }

    //--------------------------------------------------------------------------
    // Helpers

    // Spectrum from harmonic amplitudes, amps[h] for h = 1..N. Sine phase by
    // default; cosine phase for shapes whose series is written in cosines
    // (a pulse built from sines comes out as a pair of spikes).
    inline Spectrum additive (const std::vector<float>& amps, bool cosinePhase = false)
    {
        // A unit sine at FrameSize points has bin value (0, -FrameSize/2); a
        // unit cosine has (FrameSize/2, 0).
        constexpr float binScale = Wavetable::FrameSize * 0.5f;
        Spectrum s ((size_t) Wavetable::NumHarmonics + 1);
        for (size_t h = 1; h < amps.size() && h <= (size_t) Wavetable::NumHarmonics; ++h)
            s[h] = cosinePhase ? std::complex<float> (amps[h] * binScale, 0.0f)
                               : std::complex<float> (0.0f, -amps[h] * binScale);
        return s;
    }

    // Spectrum of an arbitrary single cycle, rendered by fn(x), x in [0,1).
    // Rendered 4× oversampled so hard edges (sync, steps) fold far less than
    // they would at 2048 points; only the first 1024 harmonics are kept.
    inline Spectrum fromFunction (const std::function<float (double)>& fn)
    {
        constexpr int order = Wavetable::FrameOrder + 2;
        constexpr int n     = 1 << order;
        juce::dsp::FFT fft (order);
        std::vector<float> work ((size_t) n * 2, 0.0f);

        for (int i = 0; i < n; ++i)
            work[(size_t) i] = fn ((double) i / n);

        fft.performRealOnlyForwardTransform (work.data(), true);
        auto* bins = reinterpret_cast<std::complex<float>*> (work.data());

        // JUCE's inverse divides by N, so a forward/inverse round trip at the
        // two sizes needs the 2048/n ratio applied here.
        const float scale = (float) Wavetable::FrameSize / (float) n;
        Spectrum s ((size_t) Wavetable::NumHarmonics + 1);
        for (int h = 1; h <= Wavetable::NumHarmonics; ++h)
            s[(size_t) h] = bins[h] * scale;
        return s;
    }

    // Spectrum of a single cycle stored as raw samples (any length).
    inline Spectrum fromSamples (const float* samples, int length)
    {
        return fromFunction ([samples, length] (double x)
        {
            double idx = x * length;
            int i0 = (int) idx;
            int i1 = (i0 + 1) % length;
            float frac = (float) (idx - i0);
            return samples[i0] + frac * (samples[i1] - samples[i0]);
        });
    }

    inline float lerp (float a, float b, float t) { return a + t * (b - a); }

    //--------------------------------------------------------------------------
    // Analog shapes: one frame each. Index 0..NumAnalogWaves-1.
    inline Spectrum analogWave (int index)
    {
        constexpr int H = Wavetable::NumHarmonics;
        constexpr double pi = juce::MathConstants<double>::pi;
        std::vector<float> amps ((size_t) H + 1, 0.0f);
        for (int h = 1; h <= H; ++h)
        {
            switch (index)
            {
                case 0:  amps[(size_t) h] = h == 1 ? 1.0f : 0.0f; break;                          // Sine
                case 1:  amps[(size_t) h] = h % 2 == 1 ? ((h / 2) % 2 == 0 ? 1.0f : -1.0f)        // Triangle
                                                          * 8.0f / (float) (pi * pi * h * h) : 0.0f; break;
                case 2:  amps[(size_t) h] = -2.0f / (float) (pi * h); break;                      // Saw, rising like the classic osc
                default: amps[(size_t) h] = h % 2 == 1 ? 4.0f / (float) (pi * h) : 0.0f; break;   // Square
            }
        }
        return additive (amps);
    }

    // Wavetable generators. Each returns BuiltInFrames spectra, t = 0..1
    // across frames. Index is into the wavetables only (0 = Morph).
    inline std::vector<Spectrum> generate (int index)
    {
        constexpr int F = BuiltInFrames;
        constexpr int H = Wavetable::NumHarmonics;
        constexpr double twoPi = juce::MathConstants<double>::twoPi;
        constexpr double pi    = juce::MathConstants<double>::pi;
        std::vector<Spectrum> frames;
        frames.reserve (F);

        for (int f = 0; f < F; ++f)
        {
            const float t = (float) f / (float) (F - 1);
            std::vector<float> amps ((size_t) H + 1, 0.0f);

            switch (index)
            {
                case 0: // Morph: sine → triangle → saw → square
                {
                    for (int h = 1; h <= H; ++h)
                    {
                        float sine = (h == 1) ? 1.0f : 0.0f;
                        float tri  = (h % 2 == 1) ? ((h / 2) % 2 == 0 ? 1.0f : -1.0f) * 8.0f / (float) (pi * pi * h * h) : 0.0f;
                        float saw  = 2.0f / (float) (pi * h);
                        float sqr  = (h % 2 == 1) ? 4.0f / (float) (pi * h) : 0.0f;
                        float seg  = t * 3.0f;
                        amps[(size_t) h] = seg < 1.0f ? lerp (sine, tri, seg)
                                         : seg < 2.0f ? lerp (tri, saw, seg - 1.0f)
                                                      : lerp (saw, sqr, seg - 2.0f);
                    }
                    frames.push_back (additive (amps));
                    break;
                }

                case 1: // PWM: 50% → 3% pulse
                {
                    float duty = lerp (0.5f, 0.03f, t);
                    for (int h = 1; h <= H; ++h)
                        amps[(size_t) h] = 2.0f / (float) (pi * h) * (float) std::sin (pi * h * duty);
                    frames.push_back (additive (amps, true));
                    break;
                }

                case 2: // Harmonics: partials fade in one at a time up to 48
                {
                    float top = 1.0f + t * 47.0f;
                    for (int h = 1; h <= 48; ++h)
                    {
                        float fade = juce::jlimit (0.0f, 1.0f, top - (float) h + 1.0f);
                        amps[(size_t) h] = fade / std::sqrt ((float) h);
                    }
                    frames.push_back (additive (amps));
                    break;
                }

                case 3: // Vocal: buzz through A → E → I → O → U formants
                {
                    // Formant centres in Hz, relative to a ~110 Hz glottal pitch.
                    static const float formants[5][3] = {
                        { 800, 1150, 2900 }, { 400, 1600, 2700 }, { 350, 2300, 3000 },
                        { 450,  800, 2830 }, { 325,  700, 2530 }
                    };
                    float seg = t * 4.0f;
                    int   v0  = juce::jmin (3, (int) seg);
                    float vf  = seg - (float) v0;
                    for (int h = 1; h <= H; ++h)
                    {
                        float hz = 110.0f * (float) h, a = 0.0f;
                        for (int k = 0; k < 3; ++k)
                        {
                            float fc = lerp (formants[v0][k], formants[v0 + 1][k], vf);
                            float bw = 80.0f + fc * 0.06f;
                            float d  = (hz - fc) / bw;
                            a += std::exp (-0.5f * d * d) / (float) (k + 1);
                        }
                        amps[(size_t) h] = a / std::sqrt ((float) h) + (h == 1 ? 0.3f : 0.0f);
                    }
                    frames.push_back (additive (amps));
                    break;
                }

                case 4: // FM: 3:1 modulator, index 0 → 6
                {
                    double ix = t * 6.0;
                    frames.push_back (fromFunction ([ix] (double x)
                    {
                        return (float) std::sin (twoPi * x + ix * std::sin (twoPi * 3.0 * x));
                    }));
                    break;
                }

                case 5: // Sync: hard-synced saw, slave ratio 1 → 8
                {
                    double ratio = 1.0 + t * 7.0;
                    frames.push_back (fromFunction ([ratio] (double x)
                    {
                        double p = x * ratio;
                        return (float) (2.0 * (p - std::floor (p)) - 1.0);
                    }));
                    break;
                }

                case 6: // Fold: sine through a wavefolder, gain 1 → 6
                {
                    double gain = 1.0 + t * 5.0;
                    frames.push_back (fromFunction ([gain] (double x)
                    {
                        return (float) std::sin (gain * std::sin (twoPi * x) * pi * 0.5);
                    }));
                    break;
                }

                case 7: // Digital: saw quantised from 32 steps down to 2
                {
                    double steps = std::round (32.0 * std::pow (2.0 / 32.0, (double) t));
                    frames.push_back (fromFunction ([steps] (double x)
                    {
                        double s = std::floor (x * steps) / (steps - 1.0);
                        return (float) (2.0 * s - 1.0);
                    }));
                    break;
                }

                case 8: // Organ: drawbar registrations morphing flute → full
                {
                    // Drawbar harmonics: 16' 5⅓' 8' 4' 2⅔' 2' 1⅗' 1⅓' 1' as
                    // multiples of the 16' fundamental.
                    static const int   bars[9]  = { 1, 3, 2, 4, 6, 8, 10, 12, 16 };
                    static const float soft[9]  = { 1.0f, 0, 0.8f, 0, 0, 0, 0, 0, 0 };
                    static const float full[9]  = { 0.8f, 0.6f, 1.0f, 0.8f, 0.6f, 0.7f, 0.4f, 0.5f, 0.6f };
                    for (int b = 0; b < 9; ++b)
                        amps[(size_t) bars[b]] += lerp (soft[b], full[b], t);
                    frames.push_back (additive (amps));
                    break;
                }

                default: // Glass: seeded inharmonic-ish bell spectra, crossfaded
                {
                    auto spectrumFor = [] (int seed, std::vector<float>& out)
                    {
                        juce::Random r (seed);
                        for (int h = 1; h <= 64; ++h)
                            out[(size_t) h] = (r.nextFloat() < 0.3f ? r.nextFloat() : 0.0f) / std::sqrt ((float) h);
                        out[1] = 1.0f;
                    };
                    std::vector<float> a ((size_t) H + 1, 0.0f), b ((size_t) H + 1, 0.0f);
                    float seg = t * 3.0f;
                    int   s0  = juce::jmin (2, (int) seg);
                    spectrumFor (1000 + s0, a);
                    spectrumFor (1001 + s0, b);
                    for (int h = 1; h <= H; ++h)
                        amps[(size_t) h] = lerp (a[(size_t) h], b[(size_t) h], seg - (float) s0);
                    frames.push_back (additive (amps));
                    break;
                }
            }
        }
        return frames;
    }

    inline std::unique_ptr<Wavetable> build (int index)
    {
        index = juce::jlimit (0, builtInNames().size() - 1, index);
        if (isAnalog (index))
            return std::make_unique<Wavetable> (builtInNames()[index], std::vector<Spectrum> { analogWave (index) });
        return std::make_unique<Wavetable> (builtInNames()[index], generate (index - NumAnalogWaves));
    }

    // Built-in tables are read-only once built, so every synth that has the
    // same one selected shares a single copy — Morph is ~5.8 MB. The cache
    // only holds weak references: a table nobody uses is freed, as before.
    // Message thread.
    inline std::shared_ptr<const Wavetable> shared (int index)
    {
        static std::map<int, std::weak_ptr<const Wavetable>> cache;
        index = juce::jlimit (0, builtInNames().size() - 1, index);

        if (auto existing = cache[index].lock())
            return existing;

        std::shared_ptr<const Wavetable> table = build (index);
        cache[index] = table;
        return table;
    }

    //--------------------------------------------------------------------------
    // .wav import. Serum (and Vital, Bitwig, etc.) write concatenated single
    // cycles, usually 2048 samples each, with the frame size in a "clm "
    // chunk ("<!>2048 ..."). Without that chunk we assume 2048; a file shorter
    // than one frame is treated as a single cycle.

    inline int readClmFrameSize (const juce::File& file)
    {
        juce::MemoryBlock mb;
        if (! file.loadFileAsData (mb) || mb.getSize() < 12)
            return 0;

        auto* bytes = static_cast<const char*> (mb.getData());
        const size_t size = mb.getSize();

        for (size_t pos = 12; pos + 8 <= size;)
        {
            auto chunkSize = (size_t) juce::ByteOrder::littleEndianInt (bytes + pos + 4);
            if (std::memcmp (bytes + pos, "clm ", 4) == 0)
            {
                juce::String text (bytes + pos + 8, juce::jmin (chunkSize, size - pos - 8));
                return text.fromFirstOccurrenceOf ("<!>", false, false).getIntValue();
            }
            pos += 8 + chunkSize + (chunkSize & 1);
        }
        return 0;
    }

    // Returns nullptr (and an error message) if the file can't be used.
    inline std::unique_ptr<Wavetable> importWav (const juce::File& file, juce::String& error)
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        if (reader == nullptr)
        {
            error = "Couldn't read " + file.getFileName();
            return nullptr;
        }

        const int length = (int) juce::jmin<juce::int64> (reader->lengthInSamples, 1 << 22);
        if (length < 16)
        {
            error = file.getFileName() + " is too short";
            return nullptr;
        }

        juce::AudioBuffer<float> buf (1, length);
        reader->read (&buf, 0, length, 0, true, false); // left/mono only

        int frameSize = readClmFrameSize (file);
        if (frameSize <= 0)
            frameSize = Wavetable::FrameSize;
        if (length < frameSize)
            frameSize = length;

        const int total = length / frameSize;
        const int count = juce::jmin (total, Wavetable::MaxFrames);
        std::vector<Spectrum> spectra;
        spectra.reserve ((size_t) count);
        for (int i = 0; i < count; ++i)
        {
            int src = (count == total) ? i : (i * (total - 1)) / (count - 1);
            spectra.push_back (fromSamples (buf.getReadPointer (0, src * frameSize), frameSize));
        }

        return std::make_unique<Wavetable> (file.getFileNameWithoutExtension(), spectra);
    }
}
