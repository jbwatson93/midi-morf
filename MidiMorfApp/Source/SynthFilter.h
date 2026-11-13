#pragma once

#include <JuceHeader.h>
#include <cmath>
#include <complex>

//==============================================================================
// SynthFilter — the voice filter, and the plain state-variable filter the FX
// rack's FILTER module reuses.
//
// LADDER is the synth's original 4-pole ladder, kept exactly so patches made
// with it sound the same. The rest are zero-delay-feedback state-variable
// filters (Andy Simper's trapezoidal SVF): tuned to the knob, stable at full
// resonance, and cheap enough to retune every few samples. The 24 dB modes
// are two SVF stages, the first a plain Butterworth, the second carrying the
// resonance.
//
// Resonance is 0..4 across all types (the ladder's own range); the SVFs map
// it onto damping.
namespace SynthFilter
{
    enum Type
    {
        Ladder = 0,
        LP24, LP12, HP24, HP12, BP, Notch,
        NumTypes
    };

    // Menu names, indexed by Type. Append only — patches store the index.
    inline juce::StringArray typeNames()
    {
        return { "Ladder", "Low 24", "Low 12", "High 24", "High 12", "Band", "Notch" };
    }

    static constexpr float MaxReso = 4.0f;

    //==========================================================================
    struct Coeffs
    {
        float g = 0.0f;         // ladder: one-pole gain; SVF: tan (pi fc / sr)
        float k = 2.0f;         // SVF damping of the resonant stage
        float a1 = 1.0f, a2 = 0.0f, a3 = 0.0f;
        float b1 = 1.0f, b2 = 0.0f, b3 = 0.0f;   // the Butterworth first stage of the 24s
        float reso = 0.0f;      // ladder feedback
    };

    inline float dampingFor (float reso)
    {
        // k = 2 is no resonance; 0.04 is a Q of 25, ringing but not screaming.
        return 2.0f - 1.96f * juce::jlimit (0.0f, 1.0f, reso / MaxReso);
    }

    inline Coeffs makeCoeffs (int type, float cutoffHz, float reso, double sampleRate)
    {
        Coeffs c;
        const float fc = juce::jlimit (20.0f, (float) (sampleRate * 0.45), cutoffHz);
        if (type == Ladder)
        {
            c.g = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * fc / (float) sampleRate);
            c.reso = juce::jlimit (0.0f, MaxReso, reso);
            return c;
        }

        c.g = std::tan (juce::MathConstants<float>::pi * fc / (float) sampleRate);
        c.k = dampingFor (reso);
        c.a1 = 1.0f / (1.0f + c.g * (c.g + c.k));
        c.a2 = c.g * c.a1;
        c.a3 = c.g * c.a2;

        const float kb = juce::MathConstants<float>::sqrt2;
        c.b1 = 1.0f / (1.0f + c.g * (c.g + kb));
        c.b2 = c.g * c.b1;
        c.b3 = c.g * c.b2;
        return c;
    }

    //==========================================================================
    // One SVF stage. Returns low, band and high at once; the caller picks.
    struct Svf
    {
        float ic1 = 0.0f, ic2 = 0.0f;

        void reset() { ic1 = ic2 = 0.0f; }

        inline void tick (float v0, float a1, float a2, float a3, float k,
                          float& low, float& band, float& high)
        {
            const float v3 = v0 - ic2;
            const float v1 = a1 * ic1 + a2 * v3;
            const float v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2.0f * v1 - ic1;
            ic2 = 2.0f * v2 - ic2;
            low = v2; band = v1; high = v0 - k * v1 - v2;
        }
    };

    //==========================================================================
    // One channel of the voice filter.
    struct Channel
    {
        float ladder[4] {};
        Svf first, second;

        void reset()
        {
            for (auto& s : ladder) s = 0.0f;
            first.reset();
            second.reset();
        }

        inline float process (int type, const Coeffs& c, float x)
        {
            float low, band, high;
            switch (type)
            {
                case Ladder:
                {
                    float* s = ladder;
                    const float in = x - c.reso * s[3];
                    s[0] += c.g * (in   - s[0]);
                    s[1] += c.g * (s[0] - s[1]);
                    s[2] += c.g * (s[1] - s[2]);
                    s[3] += c.g * (s[2] - s[3]);
                    return s[3];
                }
                case LP24:
                    first.tick (x, c.b1, c.b2, c.b3, juce::MathConstants<float>::sqrt2, low, band, high);
                    second.tick (low, c.a1, c.a2, c.a3, c.k, low, band, high);
                    return low;
                case HP24:
                    first.tick (x, c.b1, c.b2, c.b3, juce::MathConstants<float>::sqrt2, low, band, high);
                    second.tick (high, c.a1, c.a2, c.a3, c.k, low, band, high);
                    return high;
                case LP12:
                    second.tick (x, c.a1, c.a2, c.a3, c.k, low, band, high);
                    return low;
                case HP12:
                    second.tick (x, c.a1, c.a2, c.a3, c.k, low, band, high);
                    return high;
                case BP:
                    second.tick (x, c.a1, c.a2, c.a3, c.k, low, band, high);
                    return band * c.k;   // unity at the peak, whatever the resonance
                case Notch:
                    second.tick (x, c.a1, c.a2, c.a3, c.k, low, band, high);
                    return low + high;
                default:
                    return x;
            }
        }
    };

    //==========================================================================
    // The analog response of each type at `hz`, for drawing the curve. The
    // ladder's is four one-poles in a feedback loop; the SVFs are the
    // second-order prototypes they're built from.
    inline float magnitude (int type, float hz, float cutoffHz, float reso)
    {
        using C = std::complex<float>;
        const float w = hz / juce::jmax (1.0f, cutoffHz);
        const C s (0.0f, w);

        auto lowpass2  = [&s] (float k) { return 1.0f / (s * s + k * s + 1.0f); };
        auto highpass2 = [&s] (float k) { return (s * s) / (s * s + k * s + 1.0f); };
        const float k  = dampingFor (reso);
        const float kb = juce::MathConstants<float>::sqrt2;

        C h;
        switch (type)
        {
            case Ladder:
            {
                const C g = 1.0f / (1.0f + s);
                const C g4 = g * g * g * g;
                h = g4 / (1.0f + juce::jlimit (0.0f, MaxReso, reso) * g4);
                break;
            }
            case LP24:  h = lowpass2 (kb) * lowpass2 (k);   break;
            case HP24:  h = highpass2 (kb) * highpass2 (k); break;
            case LP12:  h = lowpass2 (k);                   break;
            case HP12:  h = highpass2 (k);                  break;
            case BP:    h = (k * s) / (s * s + k * s + 1.0f); break;
            case Notch: h = (s * s + 1.0f) / (s * s + k * s + 1.0f); break;
            default:    h = 1.0f; break;
        }
        return std::abs (h);
    }
}
