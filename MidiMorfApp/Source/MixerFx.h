#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <vector>

//==============================================================================
// The mixer's effects: a small insert chain per track, and the two shared
// send buses (delay, reverb) every track can feed.
//
// Parameters are atomics set from the message thread; the DSP runs on the
// audio thread inside TrackRack::render. Everything at its neutral setting is
// skipped, so an untouched mix is exactly the sum of the tracks.

//==============================================================================
// Per track: FILTER (one knob — left low-passes, right high-passes, centre
// is off), DRIVE (tanh) and CRUSH (fewer bits and a lower sample rate).
class TrackInsert
{
public:
    std::atomic<float> filter { 0.0f };   // -1..+1
    std::atomic<float> drive { 0.0f };    // 0..1
    std::atomic<float> crush { 0.0f };    // 0..1

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        for (auto& s : svf) s = {};
        holdL = holdR = 0.0f;
        holdCount = 0.0f;
    }

    bool isNeutral() const
    {
        return std::abs (filter.load()) < 0.01f && drive.load() < 0.001f && crush.load() < 0.001f;
    }

    void process (float* L, float* R, int n)
    {
        const float f = filter.load(), d = drive.load(), c = crush.load();

        // An SVF per side: low-pass sweeping down from 20 kHz as the knob
        // goes left, high-pass up from 20 Hz as it goes right.
        if (std::abs (f) >= 0.01f)
        {
            const bool highPass = f > 0.0f;
            const double hz = highPass ? 20.0 * std::pow (400.0, (double) f)           // 20 Hz .. 8 kHz
                                       : 20000.0 * std::pow (0.0075, (double) -f);     // 20 kHz .. 150 Hz
            const float g = (float) std::tan (juce::MathConstants<double>::pi * juce::jmin (hz, sr * 0.45) / sr);
            const float k = 1.0f / 0.9f;   // a little resonance
            for (int side = 0; side < 2; ++side)
            {
                float* x = side == 0 ? L : R;
                if (x == nullptr || (side == 1 && R == L)) continue;
                auto& s = svf[(size_t) side];
                for (int i = 0; i < n; ++i)
                {
                    const float hp = (x[i] - (k + g) * s.z1 - s.z2) / (1.0f + g * (g + k));
                    const float bp = g * hp + s.z1;
                    const float lp = g * bp + s.z2;
                    s.z1 = g * hp + bp;
                    s.z2 = g * bp + lp;
                    x[i] = highPass ? hp : lp;
                }
            }
        }

        if (d >= 0.001f)
        {
            const float k = 1.0f + d * 9.0f, norm = 1.0f / std::tanh (k);
            for (int i = 0; i < n; ++i)
            {
                L[i] = std::tanh (L[i] * k) * norm;
                if (R != nullptr && R != L) R[i] = std::tanh (R[i] * k) * norm;
            }
        }

        if (c >= 0.001f)
        {
            const float levels = std::pow (2.0f, 12.0f - 9.0f * c);    // 12 bits down to 3
            const float hold = 1.0f + c * c * 15.0f;                     // keep each sample up to 16x
            for (int i = 0; i < n; ++i)
            {
                if ((holdCount -= 1.0f) <= 0.0f)
                {
                    holdCount += hold;
                    holdL = std::round (L[i] * levels) / levels;
                    holdR = R != nullptr ? std::round (R[i] * levels) / levels : holdL;
                }
                L[i] = holdL;
                if (R != nullptr && R != L) R[i] = holdR;
            }
        }
    }

private:
    struct Svf { float z1 = 0.0f, z2 = 0.0f; };
    std::array<Svf, 2> svf {};
    double sr = 48000.0;
    float holdL = 0.0f, holdR = 0.0f, holdCount = 0.0f;
};

//==============================================================================
// The shared returns. Tracks add into the two inputs (post-fader, times their
// send); process() adds the wet returns into the mix. A stereo ping-pong
// delay synced to the tempo, and a reverb.
class SendBuses
{
public:
    // Delay time in beats: 1/4, dotted 1/8, 1/8, 1/16.
    static const juce::StringArray& delayTimeNames()
    {
        static const juce::StringArray n { "1/4", "1/8 dotted", "1/8", "1/16" };
        return n;
    }

    std::atomic<int>   delayTime { 1 };
    std::atomic<float> delayFeedback { 0.45f };   // 0..0.9
    std::atomic<float> reverbSize { 0.6f };       // 0..1
    std::atomic<float> delayReturn { 1.0f }, reverbReturn { 1.0f };

    void prepare (double sampleRate, int maxBlock)
    {
        sr = sampleRate;
        for (auto& b : delayLine) b.assign ((size_t) (sampleRate * 2.5), 0.0f);
        writePos = 0;
        input.setSize (4, maxBlock);   // delay L/R, reverb L/R
        input.clear();
        reverb.setSampleRate (sampleRate);
        reverb.reset();
        tail = 0;
    }

    float* delayIn (int ch)  { return input.getWritePointer (ch); }
    float* reverbIn (int ch) { return input.getWritePointer (2 + ch); }

    // Before the tracks add in.
    void clearInputs (int n) { input.clear (0, n); }

    // A send was used this block: keep processing until the tails die.
    void touched() { tail = (int) (sr * 12.0); }
    bool isIdle() const { return tail <= 0; }

    void process (float* L, float* R, int n, double bpm)
    {
        if (tail <= 0)
            return;
        tail -= n;

        // Delay: ping-pong, the left input feeding the left line and the
        // feedback crossing sides.
        static const double beats[4] = { 1.0, 0.75, 0.5, 0.25 };
        const int len = (int) delayLine[0].size();
        const int delay = juce::jlimit (1, len - 1, (int) (60.0 / juce::jmax (40.0, bpm) * beats[juce::jlimit (0, 3, delayTime.load())] * sr));
        const float fb = juce::jlimit (0.0f, 0.9f, delayFeedback.load()), dRet = delayReturn.load();
        const float* inL = input.getReadPointer (0);
        const float* inR = input.getReadPointer (1);
        for (int i = 0; i < n; ++i)
        {
            int rp = writePos - delay;
            if (rp < 0) rp += len;
            const float outL = delayLine[0][(size_t) rp], outR = delayLine[1][(size_t) rp];
            delayLine[0][(size_t) writePos] = inL[i] + outR * fb;
            delayLine[1][(size_t) writePos] = inR[i] + outL * fb;
            writePos = (writePos + 1) % len;
            L[i] += outL * dRet;
            if (R != nullptr) R[i] += outR * dRet;
        }

        // Reverb: fully wet.
        juce::Reverb::Parameters p;
        p.roomSize = juce::jlimit (0.0f, 1.0f, reverbSize.load());
        p.damping = 0.45f;
        p.wetLevel = 1.0f;
        p.dryLevel = 0.0f;
        p.width = 1.0f;
        reverb.setParameters (p);
        float* rvL = input.getWritePointer (2);
        float* rvR = input.getWritePointer (3);
        reverb.processStereo (rvL, rvR, n);
        const float rRet = reverbReturn.load() * 0.6f;
        for (int i = 0; i < n; ++i)
        {
            L[i] += rvL[i] * rRet;
            if (R != nullptr) R[i] += rvR[i] * rRet;
        }
    }

private:
    double sr = 48000.0;
    std::array<std::vector<float>, 2> delayLine;
    int writePos = 0, tail = 0;
    juce::AudioBuffer<float> input;
    juce::Reverb reverb;
};
