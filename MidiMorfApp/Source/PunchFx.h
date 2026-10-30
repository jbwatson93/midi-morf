#pragma once

#include <JuceHeader.h>
#include "TrackRack.h"
#include <array>
#include <atomic>
#include <functional>
#include <vector>

//==============================================================================
// Punch-in effects on the master bus — what Morf FX plays in Local mode.
//
// Four slots, each holding one effect. Holding a slot's button brings its
// effect in; letting go takes it out. Both fade over a few milliseconds, so
// a punch never clicks. Slots run in series, 1 to 4. The XY pad shapes every
// active effect: each maps X and Y to its two main controls.
//
// Delay Throw and Reverb Wash are sends: holding feeds them, and their tails
// ring on after release. The rest are inserts: dry when released.
//
// Timing follows the local clock's tempo (getBpm). Divisions run from the
// moment of the punch, so they lock to the tempo, not to the bar.
//
// Threading: the slot, active and XY setters are any-thread atomics; the
// audio thread reads them in process(). With nothing held and no tail
// ringing, process() leaves the buffer untouched — bit-exact.
class PunchFx : public MasterProcessor
{
public:
    enum Effect { Filter = 0, Stutter, TapeStop, DelayThrow, ReverbWash, Bitcrush, TranceGate, VinylBrake, NumEffects };
    static constexpr int NumSlots = 4;

    static const juce::StringArray& effectNames()
    {
        static const juce::StringArray names { "Filter", "Stutter", "Tape Stop", "Delay Throw",
                                               "Reverb Wash", "Bitcrush", "Trance Gate", "Vinyl Brake" };
        return names;
    }

    // What X and Y do, for the page to label the sliders. Y labels are short:
    // they sit on the narrow vertical slider.
    static std::pair<juce::String, juce::String> axisNames (int effect)
    {
        switch (effect)
        {
            case Filter:     return { "Low-pass  |  High-pass", "Res" };
            case Stutter:    return { "Repeat 1/4 > 1/32", "Mix" };
            case TapeStop:   return { "Stop time", "Curve" };
            case DelayThrow: return { "Time 1/16 > 1/4", "Fdbk" };
            case ReverbWash: return { "Size", "Wet" };
            case Bitcrush:   return { "Bits", "Rate" };
            case TranceGate: return { "Rate 1/4 > 1/32", "Depth" };
            case VinylBrake: return { "Pitch drop", "Time" };
            default:         return { "X", "Y" };
        }
    }

    PunchFx()
    {
        const int defaults[NumSlots] = { Filter, Stutter, TapeStop, DelayThrow };
        for (int i = 0; i < NumSlots; ++i)
            slots[(size_t) i].effect.store (defaults[i]);
    }

    void setTempoSource (std::function<double()> bpmSource) { getBpm = std::move (bpmSource); }

    //==========================================================================
    // Any thread.
    void setSlotEffect (int slot, int effect)
    {
        if (juce::isPositiveAndBelow (slot, NumSlots) && juce::isPositiveAndBelow (effect, (int) NumEffects))
            slots[(size_t) slot].effect.store (effect);
    }

    int  getSlotEffect (int slot) const { return juce::isPositiveAndBelow (slot, NumSlots) ? slots[(size_t) slot].effect.load() : 0; }
    void setActive (int slot, bool on)  { if (juce::isPositiveAndBelow (slot, NumSlots)) slots[(size_t) slot].active.store (on); }
    bool isActive (int slot) const      { return juce::isPositiveAndBelow (slot, NumSlots) && slots[(size_t) slot].active.load(); }

    void setXY (float x, float y)
    {
        padX.store (juce::jlimit (0.0f, 1.0f, x));
        padY.store (juce::jlimit (0.0f, 1.0f, y));
    }

    void setX (float x) { padX.store (juce::jlimit (0.0f, 1.0f, x)); }
    void setY (float y) { padY.store (juce::jlimit (0.0f, 1.0f, y)); }
    float getX() const  { return padX.load(); }
    float getY() const  { return padY.load(); }

    void releaseAll()
    {
        for (auto& s : slots)
            s.active.store (false);
    }

    //==========================================================================
    // MasterProcessor — audio thread.
    void prepare (double sampleRate, int) override
    {
        sr = sampleRate;
        ringLen = (int) (sr * 12.0);                          // tape stop / brake history
        ring.setSize (2, ringLen);
        ring.clear();
        writePos = 0;

        attackCoef  = (float) std::exp (-1.0 / (0.004 * sr));  // punch in over ~4 ms
        releaseCoef = (float) std::exp (-1.0 / (0.012 * sr));  // out over ~12 ms
        edge = juce::jmax (8, (int) (0.0015 * sr));            // stutter loop edge fade

        for (auto& s : slots)
            s.prepare (sr);

        xs.reset (sr, 0.03);
        ys.reset (sr, 0.03);
        xs.setCurrentAndTargetValue (padX.load());
        ys.setCurrentAndTargetValue (padY.load());
    }

    void process (juce::AudioBuffer<float>& buffer, int start, int num) override
    {
        if (buffer.getNumChannels() < 2 || ringLen == 0)
            return;

        float* L = buffer.getWritePointer (0, start);
        float* R = buffer.getWritePointer (1, start);

        // The history ring always records, so a punch can reach back.
        for (int i = 0; i < num; ++i)
        {
            ring.setSample (0, (writePos + i) % ringLen, L[i]);
            ring.setSample (1, (writePos + i) % ringLen, R[i]);
        }

        bool anything = false;
        for (auto& s : slots)
            anything |= s.active.load() || s.amount > 0.0f || s.tailLive;
        if (! anything)
        {
            writePos = (writePos + num) % ringLen;
            xs.setCurrentAndTargetValue (padX.load());
            ys.setCurrentAndTargetValue (padY.load());
            return;   // untouched
        }

        const double bpm = juce::jlimit (40.0, 240.0, getBpm ? getBpm() : 120.0);
        const double beat = sr * 60.0 / bpm;
        xs.setTargetValue (padX.load());
        ys.setTargetValue (padY.load());

        for (int i = 0; i < num; ++i)
        {
            const float x = xs.getNextValue(), y = ys.getNextValue();
            const int pos = (writePos + i) % ringLen;
            float l = L[i], r = R[i];

            for (auto& s : slots)
                s.tick (*this, l, r, x, y, beat, pos);

            L[i] = l;
            R[i] = r;
        }

        writePos = (writePos + num) % ringLen;
    }

private:
    //==========================================================================
    // X in four zones: 1/4, 1/8, 1/16, 1/32 notes, as beats.
    static double division (float x)
    {
        const int zone = juce::jlimit (0, 3, (int) (x * 4.0f));
        return 1.0 / (double) (1 << zone);   // 1, 0.5, 0.25, 0.125 beats
    }

    float ringRead (int ch, double p) const
    {
        while (p < 0) p += ringLen;
        const int i0 = (int) p % ringLen, i1 = (i0 + 1) % ringLen;
        const float f = (float) (p - std::floor (p));
        const float a = ring.getSample (ch, i0), b = ring.getSample (ch, i1);
        return a + f * (b - a);
    }

    //==========================================================================
    // Topology-preserving SVF (Zavalishin): low and high outputs, per channel.
    struct Svf
    {
        float ic1[2] {}, ic2[2] {};
        void reset() { ic1[0] = ic1[1] = ic2[0] = ic2[1] = 0.0f; }

        float run (int ch, float v0, float g, float k, bool high)
        {
            const float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
            const float v3 = v0 - ic2[ch];
            const float v1 = a1 * ic1[ch] + a2 * v3;
            const float v2 = ic2[ch] + a2 * ic1[ch] + a3 * v3;
            ic1[ch] = 2.0f * v1 - ic1[ch];
            ic2[ch] = 2.0f * v2 - ic2[ch];
            return high ? v0 - k * v1 - v2 : v2;
        }
    };

    struct Slot
    {
        std::atomic<int>  effect { 0 };
        std::atomic<bool> active { false };

        // Audio thread.
        int   running = -1;          // the effect the state below belongs to
        bool  wasActive = false;
        float amount = 0.0f;         // 0..1 punch envelope
        bool  tailLive = false;      // a send effect still ringing

        // Filter
        Svf svf;
        float cutoffSm = 1000.0f;
        int   coefCount = 0;
        float g = 0.1f, k = 1.0f;
        bool  high = false;

        // Stutter
        juce::AudioBuffer<float> loop;
        int loopLen = 0, loopPos = 0;
        double loopDiv = 0;

        // Tape stop / vinyl brake
        double readPos = 0, speed = 1.0, elapsed = 0;

        // Delay throw
        juce::AudioBuffer<float> delay;
        int delayWrite = 0;
        float delaySm = 0.0f, damp[2] {};
        int   inFlight = 0;   // samples until the last thing sent comes out

        // Reverb wash
        juce::Reverb reverb;
        float tailEnergy = 0.0f;

        // Bitcrush
        float held[2] {};
        float holdCount = 0.0f;

        // Trance gate
        double gatePhase = 0;
        float  gateSm = 1.0f;

        double rate = 48000.0;

        void prepare (double sr)
        {
            rate = sr;
            loop.setSize (2, (int) (sr * 1.6) + 16);   // a beat at 40 BPM
            delay.setSize (2, (int) (sr * 2.1));
            delay.clear();
            reverb.setSampleRate (sr);
            running = -1;
            amount = 0.0f;
            tailLive = false;
        }

        void resetFor (int fx)
        {
            running = fx;
            svf.reset();
            loopLen = loopPos = 0;
            readPos = 0; speed = 1.0; elapsed = 0;
            delay.clear(); delayWrite = 0; delaySm = 0; damp[0] = damp[1] = 0; inFlight = 0;
            reverb.reset(); tailEnergy = 0;
            held[0] = held[1] = 0; holdCount = 0;
            gatePhase = 0; gateSm = 1.0f;
            tailLive = false;
            wasActive = false;   // a held button re-punches the new effect
        }

        void tick (PunchFx& fx, float& l, float& r, float x, float y, double beat, int pos)
        {
            const int want = effect.load();
            const bool on = active.load();

            if (want != running)
            {
                if (amount > 0.0f || tailLive)
                {
                    // Fade the old effect out before switching.
                    amount *= fx.releaseCoef;
                    if (amount < 1.0e-4f) amount = 0.0f;
                    if (amount == 0.0f) tailLive = false;
                }
                if (amount == 0.0f && ! tailLive)
                    resetFor (want);
                else
                {
                    apply (fx, l, r, x, y, beat, pos, false);
                    return;
                }
            }

            const bool rising = on && ! wasActive;
            wasActive = on;
            if (rising)
                onPunch (fx, x, beat, pos);

            amount = on ? 1.0f - (1.0f - amount) * fx.attackCoef
                        : amount * fx.releaseCoef;
            if (amount < 1.0e-5f && ! on)
                amount = 0.0f;

            if (amount == 0.0f && ! tailLive)
                return;   // dry, untouched

            apply (fx, l, r, x, y, beat, pos, on);
        }

        void onPunch (PunchFx& fx, float x, double beat, int pos)
        {
            switch (running)
            {
                case Stutter:     capture (fx, x, beat, pos); break;
                case TapeStop:
                case VinylBrake:  readPos = pos; speed = 1.0; elapsed = 0; break;
                case TranceGate:  gatePhase = 0; gateSm = 1.0f; break;
                case Filter:      svf.reset(); break;
                default: break;
            }
        }

        // The most recent division of audio becomes the loop.
        void capture (PunchFx& fx, float x, double beat, int pos)
        {
            loopDiv = division (x);
            loopLen = juce::jlimit (16, loop.getNumSamples(), (int) std::round (beat * loopDiv));
            for (int i = 0; i < loopLen; ++i)
            {
                const int src = (pos - loopLen + i + fx.ringLen) % fx.ringLen;
                loop.setSample (0, i, fx.ring.getSample (0, src));
                loop.setSample (1, i, fx.ring.getSample (1, src));
            }
            loopPos = 0;
        }

        void apply (PunchFx& fx, float& l, float& r, float x, float y, double beat, int pos, bool on)
        {
            const float dryL = l, dryR = r;
            float wl = l, wr = r;

            switch (running)
            {
                case Filter:
                {
                    // X: left half sweeps a low-pass down from fully open,
                    // right half a high-pass up; the middle is open.
                    if (--coefCount <= 0)
                    {
                        coefCount = 16;
                        high = x > 0.5f;
                        const float t = high ? (x - 0.5f) * 2.0f : (0.5f - x) * 2.0f;
                        const float target = high ? 20.0f * std::pow (400.0f, t)       // 20 Hz .. 8 kHz
                                                  : 20000.0f * std::pow (0.004f, t);   // 20 kHz .. 80 Hz
                        cutoffSm += 0.35f * (target - cutoffSm);
                        const float fc = juce::jlimit (20.0f, (float) fx.sr * 0.45f, cutoffSm);
                        g = std::tan (juce::MathConstants<float>::pi * fc / (float) fx.sr);
                        k = 1.0f / (0.6f + y * 7.4f);
                    }
                    wl = svf.run (0, l, g, k, high);
                    wr = svf.run (1, r, g, k, high);
                    break;
                }

                case Stutter:
                {
                    if (loopLen <= 0) break;
                    // A new division takes over at the loop's end, in time.
                    if (loopPos == 0 && division (x) != loopDiv && on)
                        capture (fx, x, beat, pos);

                    float e = 1.0f;
                    if (loopPos < fx.edge)                 e = (float) loopPos / (float) fx.edge;
                    else if (loopPos > loopLen - fx.edge)  e = (float) (loopLen - loopPos) / (float) fx.edge;
                    wl = loop.getSample (0, loopPos) * e;
                    wr = loop.getSample (1, loopPos) * e;
                    loopPos = (loopPos + 1) % loopLen;

                    const float mix = 0.55f + 0.45f * y;
                    wl = dryL + mix * (wl - dryL);
                    wr = dryR + mix * (wr - dryR);
                    break;
                }

                case TapeStop:
                case VinylBrake:
                {
                    const bool brake = running == VinylBrake;
                    // Tape: slows to a stop over X (80 ms .. 2 s), Y bends the
                    // curve. Vinyl: slows to a lower speed over Y and holds.
                    const double dur    = brake ? 0.05 + y * 0.95 : 0.08 + x * 1.9;
                    const double target = brake ? 1.0 - x * 0.9 : 0.0;
                    const double t      = juce::jmin (1.0, elapsed / (dur * fx.sr));
                    const double shape  = brake ? t : std::pow (t, 1.0 + 2.0 * y);
                    speed = 1.0 + (target - 1.0) * shape;
                    elapsed += 1.0;

                    // How far behind the live signal, across the ring's wrap.
                    // Never further than the ring holds.
                    double lag = pos - readPos;
                    if (lag < 0) lag += fx.ringLen;
                    if (lag > fx.ringLen - 4096)
                        readPos = pos;

                    const float fade = (float) juce::jmin (1.0, speed * 8.0);   // no DC at a standstill
                    wl = fx.ringRead (0, readPos) * fade;
                    wr = fx.ringRead (1, readPos) * fade;
                    readPos += speed;
                    if (readPos >= fx.ringLen) readPos -= fx.ringLen;
                    break;
                }

                case DelayThrow:
                {
                    static const double times[4] = { 0.25, 0.5, 0.75, 1.0 };   // 1/16, 1/8, dotted 1/8, 1/4
                    const int zone = juce::jlimit (0, 3, (int) (x * 4.0f));
                    const float target = (float) juce::jmin ((double) delay.getNumSamples() - 4, beat * times[zone]);
                    delaySm = delaySm <= 0.0f ? target : delaySm + 0.0005f * (target - delaySm);
                    const float fb = y * 0.85f;

                    const int n = delay.getNumSamples();
                    double rp = delayWrite - delaySm;
                    while (rp < 0) rp += n;
                    const int i0 = (int) rp % n, i1 = (i0 + 1) % n;
                    const float f = (float) (rp - std::floor (rp));
                    float echo[2];
                    for (int ch = 0; ch < 2; ++ch)
                    {
                        const float a = delay.getSample (ch, i0), b = delay.getSample (ch, i1);
                        echo[ch] = a + f * (b - a);
                        damp[ch] += 0.45f * (echo[ch] - damp[ch]);   // darker each repeat
                    }

                    // Send: only while held. Echoes ping-pong across the sides.
                    const float sendL = dryL * amount, sendR = dryR * amount;
                    const float inL = sendL + fb * damp[1], inR = sendR + fb * damp[0];
                    delay.setSample (0, delayWrite, inL);
                    delay.setSample (1, delayWrite, inR);
                    delayWrite = (delayWrite + 1) % n;

                    // Whatever just went in is still in flight for a whole
                    // delay time, silent at the output until it lands.
                    if (juce::jmax (std::abs (inL), std::abs (inR)) > 1.0e-5f)
                        inFlight = (int) delaySm + 64;
                    else if (inFlight > 0)
                        --inFlight;

                    l = dryL + echo[0];
                    r = dryR + echo[1];
                    trackTail (echo[0], echo[1], fx);
                    return;
                }

                case ReverbWash:
                {
                    juce::Reverb::Parameters p;
                    p.roomSize   = 0.55f + 0.44f * x;
                    p.damping    = 0.35f;
                    p.wetLevel   = 1.0f;
                    p.dryLevel   = 0.0f;
                    p.width      = 1.0f;
                    p.freezeMode = 0.0f;
                    if (std::abs (p.roomSize - lastRoom) > 0.01f) { reverb.setParameters (p); lastRoom = p.roomSize; }

                    float sl = dryL * amount, sr2 = dryR * amount;
                    reverb.processStereo (&sl, &sr2, 1);
                    const float wet = 0.35f + 0.65f * y;
                    l = dryL + sl * wet;
                    r = dryR + sr2 * wet;
                    trackTail (sl, sr2, fx);
                    return;
                }

                case Bitcrush:
                {
                    const float bits = 16.0f - x * 13.0f;                     // 16 .. 3 bits
                    const float step = std::pow (2.0f, 1.0f - bits);
                    const float hold = 1.0f + y * 39.0f;                      // rate / 1 .. / 40
                    if ((holdCount += 1.0f) >= hold)
                    {
                        holdCount -= hold;
                        held[0] = std::round (l / step) * step;
                        held[1] = std::round (r / step) * step;
                    }
                    wl = held[0];
                    wr = held[1];
                    break;
                }

                case TranceGate:
                {
                    const double period = beat * division (x);
                    gatePhase += 1.0 / period;
                    if (gatePhase >= 1.0) gatePhase -= 1.0;
                    const float open = gatePhase < 0.5 ? 1.0f : 0.0f;
                    gateSm += 0.02f * (open - gateSm);                         // ~1 ms edges
                    const float depth = 0.4f + 0.6f * y;
                    const float gain = 1.0f - depth * (1.0f - gateSm);
                    wl = l * gain;
                    wr = r * gain;
                    break;
                }

                default: break;
            }

            l = dryL + amount * (wl - dryL);
            r = dryR + amount * (wr - dryR);
        }

        // A send stays live until its tail has died away.
        void trackTail (float a, float b, PunchFx& fx)
        {
            tailEnergy = juce::jmax (std::abs (a), std::abs (b), tailEnergy * fx.tailDecay);
            tailLive = amount > 0.0f || tailEnergy > 1.0e-5f || inFlight > 0;
        }

        float lastRoom = -1.0f;
    };

    std::array<Slot, NumSlots> slots;
    std::atomic<float> padX { 0.5f }, padY { 0.5f };
    juce::SmoothedValue<float> xs, ys;
    std::function<double()> getBpm;

    double sr = 48000.0;
    juce::AudioBuffer<float> ring;
    int ringLen = 0, writePos = 0, edge = 64;
    float attackCoef = 0.99f, releaseCoef = 0.99f;
    const float tailDecay = 0.9995f;
};
