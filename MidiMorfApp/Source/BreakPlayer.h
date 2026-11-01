#pragma once

#include <JuceHeader.h>
#include "BreakLibrary.h"
#include "TrackRack.h"
#include <array>
#include <vector>

//==============================================================================
// Plays the sampler's current sample (a Break) two ways.
//
// Slices, for the slice sequencer: like Simpler's slice mode, a slice is a
// one-shot that plays at native speed to the end of its own region, whatever
// the gate does, and only the next slice (or STOP) cuts it short. The
// sequencer is monophonic, so a new slice chokes the old one with a 2 ms
// fade; a small voice pool lets fades overlap.
//
// Slices also warp the loop to the project's tempo: each slice lasts one
// step at the project tempo. How it fills the step is the WARP mode:
//   • Beats   — native speed, like Ableton's Beats mode. Faster than the loop,
//               a slice fades out early; slower, it loops the back half of its
//               own region (crossfaded) until its step is up.
//   • Stretch — granular time-stretch at the slice's own pitch, in the spirit
//               of the old Akai samplers: fixed-size grains overlapped with no
//               waveform matching, so slowing a loop down gives that phasey,
//               metallic jungle-timestretch grain. GRAIN sets the grain size.
//   • Repitch — plays the slice faster or slower to fit, like a turntable.
// At the loop's own tempo (and pitch) none of this happens and the loop plays
// back unchanged.
//
// Keys: the whole sample, polyphonic, pitched from a root note — with an
// ADSR (Keys mode) or as a plain one-shot (One-shot mode). These read with
// 4-point Hermite interpolation, which keeps pitched-up samples cleaner than
// linear; slices stay linear and at native speed, so a break is unchanged.
// SPEED below or above 1 plays keys through the same grains, changing their
// length without their pitch.
//
// After the voices: the TONE ribbon's low-pass, then the sampler's filter.
//
// setBreak() is message-thread; everything else runs on the audio thread.
class BreakPlayer
{
public:
    enum Warp { Beats = 0, Stretch = 1, Repitch = 2 };
    enum FilterType { FilterOff = 0, LowPass = 1, BandPass = 2, HighPass = 3 };

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
        for (auto& k : keys)
        {
            k.active = false;
            k.adsr.setSampleRate (sampleRate);
        }
        ic1[0] = ic1[1] = ic2[0] = ic2[1] = 0.0f;
        fz1[0] = fz1[1] = fz2[0] = fz2[1] = 0.0f;
    }

    // Ribbon targets — any thread.
    void setPitchSemitones (float semis) { pitchSemis.store (juce::jlimit (-24.0f, 24.0f, semis)); }
    void setTone (float t)               { tone.store (juce::jlimit (0.0f, 1.0f, t)); }

    // The sampler's playback settings — any thread.
    void setTune (float semis)          { tuneSemis.store (juce::jlimit (-48.0f, 48.0f, semis)); }
    void setWarp (Warp w)               { warp.store ((int) w); }
    void setGrainMs (float ms)          { grainMs.store (juce::jlimit (5.0f, 250.0f, ms)); }
    void setSpeed (float s)             { speed.store (juce::jlimit (0.25f, 4.0f, s)); }
    void setGain (float g)              { gain.store (juce::jlimit (0.0f, 4.0f, g)); }
    void setVelocitySense (float s)     { velSense.store (juce::jlimit (0.0f, 1.0f, s)); }
    void setLoop (bool l)               { loopKeys.store (l); }
    void setFilter (FilterType t, float cutoffHz, float resonance)
    {
        filterType.store ((int) t);
        filterCutoff.store (juce::jlimit (20.0f, 20000.0f, cutoffHz));
        filterRes.store (juce::jlimit (0.0f, 1.0f, resonance));
    }

    //==========================================================================
    // Audio thread.

    // `slice` counts steps of `stepBeats` quarter notes into the loop, wrapping
    // if the grid is longer than the loop. `secondsPerBeat` is the project's
    // tempo; 0 plays the slice at the loop's own length.
    void trigger (int slice, double stepBeats, double secondsPerBeat, float velocity)
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
        v->gain    = velocityGain (velocity);
        v->env     = 0.0f;
        v->envStep = 1.0f / fadeSamples (fadeSeconds);
        v->releasing = false;
        v->fadeFrom  = -1.0;

        // Warp: how long this step lasts at the project's tempo, against how
        // long the slice is at its own speed and pitch. Within a millisecond,
        // play it as is.
        const double region = v->end - v->pos;
        const double native = region / sliceRate (*b);
        const double wanted = stepBeats * secondsPerBeat * deviceRate;
        v->life = secondsPerBeat > 0.0 && std::abs (wanted - native) > deviceRate * 0.001 ? wanted : -1.0;
        v->warp = v->life >= 0.0 ? (Warp) warp.load() : Beats;
        v->loopLen = juce::jmax (region * 0.5, b->sampleRate * loopFadeSeconds * 2.0);
        // Repitch speeds the slice up or down for the tempo only; TRANSPOSE
        // and the pitch ribbon still pitch it on top.
        v->rateMul = v->warp == Repitch ? region * deviceRate / b->sampleRate / wanted : 1.0;
        v->t = 0.0;
        v->hop = hopSamples();
        v->stretch = v->life > 0.0 ? region / v->life : 0.0;   // source samples per output sample
    }

    // A keyed note: the whole sample from the start, pitched by `ratio`
    // (2^(semitones/12) from the root). With `oneShot` it ignores note-off
    // and plays to the end; otherwise `envelope` shapes it.
    void startKey (int note, float velocity, double ratio, bool oneShot, const juce::ADSR::Parameters& envelope)
    {
        const Break* b = syncBreak();
        if (b == nullptr)
            return;

        // Polyphony is MaxKeys; the pool has spares so a stolen voice can fade
        // out (~3 ms) while the new note starts, instead of being cut.
        int sounding = 0;
        Key* oldest = nullptr;
        for (auto& cand : keys)
            if (cand.active && ! cand.stolen)
            {
                ++sounding;
                if (oldest == nullptr || cand.age > oldest->age) oldest = &cand;
            }
        if (sounding >= MaxKeys && oldest != nullptr)
        {
            oldest->stolen = true;
            oldest->oneShot = false;
            oldest->adsr.setParameters ({ 0.001f, 0.0f, 1.0f, 0.003f });
            oldest->adsr.noteOff();
        }

        // A free slot, else (every spare still fading) the oldest fading one.
        Key* k = nullptr;
        for (auto& cand : keys)
            if (! cand.active) { k = &cand; break; }
        if (k == nullptr)
            for (auto& cand : keys)
                if (k == nullptr || cand.age > k->age) k = &cand;

        k->active  = true;
        k->stolen  = false;
        k->note    = note;
        k->oneShot = oneShot;
        k->pos     = 0.0;
        k->ratio   = ratio;
        k->gain    = velocityGain (velocity);
        k->age     = 0;
        k->fadeIn  = 0.0f;   // a stolen voice restarts from silence, no click
        k->t       = 0.0;
        k->hop     = hopSamples();
        k->speed   = speed.load();
        k->adsr.setParameters (oneShot ? juce::ADSR::Parameters { 0.001f, 0.0f, 1.0f, 0.004f } : envelope);
        k->adsr.reset();
        k->adsr.noteOn();
    }

    void stopKey (int note)
    {
        for (auto& k : keys)
            if (k.active && k.note == note && ! k.oneShot)
                k.adsr.noteOff();
    }

    void releaseAll()
    {
        for (auto& v : voices)
            if (v.active)
                v.release (fadeSamples (0.004));
        for (auto& k : keys)
            if (k.active)
            {
                k.oneShot = false;
                k.adsr.setParameters ({ 0.001f, 0.0f, 1.0f, 0.004f });
                k.adsr.noteOff();
            }
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

        // Warping. `life` counts down the output samples left in the step
        // (negative: not warped). Looping back (Beats), the old read head
        // `fadeFrom` fades out as `pos` fades in (`loopMix` 0..1). Repitch
        // reads `rateMul` faster; Stretch reads grains: `t` output samples in,
        // a grain every `hop`, each starting `stretch` source samples per
        // output sample further on.
        Warp   warp = Beats;
        double life = -1.0, loopLen = 0.0, fadeFrom = -1.0, rateMul = 1.0;
        double t = 0.0, hop = 1.0, stretch = 0.0;
        float  loopMix = 1.0f, loopMixStep = 0.0f;

        void release (float samples)
        {
            releasing = true;
            envStep = -juce::jmax (env, 0.001f) / juce::jmax (1.0f, samples);
        }
    };

    struct Key
    {
        bool   active = false, oneShot = false, stolen = false;
        int    note = -1;
        double pos = 0.0, ratio = 1.0;
        float  gain = 1.0f, fadeIn = 1.0f;
        juce::int64 age = 0;
        juce::ADSR adsr;

        // Grains, when SPEED isn't 1 (see Voice).
        double t = 0.0, hop = 1.0;
        float  speed = 1.0f;
    };

    struct Retired { std::unique_ptr<Break> b; juce::uint32 when; };

    // Slice-edge crossfade. Short enough to keep transients sharp, long
    // enough that a cut never clicks.
    static constexpr double fadeSeconds = 0.002;

    // The crossfade where a warped slice loops back.
    static constexpr double loopFadeSeconds = 0.005;

    float fadeSamples (double seconds) const { return (float) (seconds * deviceRate); }

    // Grains overlap by half, so a grain starts every half grain.
    double hopSamples() const { return juce::jmax (16.0, grainMs.load() * 0.001 * deviceRate * 0.5); }

    float velocityGain (float velocity) const
    {
        const float s = velSense.load();
        return juce::jlimit (0.0f, 1.0f, 1.0f - s * (1.0f - velocity));
    }

    double pitchRatio() const { return std::pow (2.0, (pitchSemis.load() + tuneSemis.load()) / 12.0); }

    // Source samples per output sample for a slice: the sample rate and the
    // pitch (ribbon plus TRANSPOSE).
    double sliceRate (const Break& b) const { return b.sampleRate / deviceRate * pitchRatio(); }

    // A new break invalidates every playing voice's position.
    const Break* syncBreak()
    {
        const Break* b = current.load();
        if (b != lastBreak)
        {
            for (auto& v : voices)
                v.active = false;
            for (auto& k : keys)
                k.active = false;
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
        const double rate = sliceRate (b);
        bool any = false;

        for (auto& v : voices)
        {
            if (! v.active)
                continue;

            any = true;
            const double loopFade = b.sampleRate * loopFadeSeconds;
            for (int i = 0; i < n; ++i)
            {
                if (v.life >= 0.0 && ! v.releasing)
                {
                    // Warped: the step is up — fade out where it is.
                    if ((v.life -= 1.0) <= fadeSamples (fadeSeconds))
                        v.release (fadeSamples (fadeSeconds));
                    // Repitch, pitched up past the end of its region: done.
                    else if (v.warp == Repitch && v.pos >= v.end)
                        v.release (fadeSamples (fadeSeconds));
                    // Beats, with time left at the end of the region: loop
                    // its back half, crossfading so the old head ends at `end`.
                    else if (v.warp == Beats && v.fadeFrom < 0.0 && v.pos >= v.end - loopFade)
                    {
                        v.fadeFrom = v.pos;
                        v.pos -= v.loopLen;
                        v.loopMix = 0.0f;
                        v.loopMixStep = (float) (rate / loopFade);
                    }
                }
                // Past its own region it fades out over the same few ms the
                // next slice fades in, reading on into the loop — so the
                // untouched pattern crossfades audio with itself, seamlessly.
                else if (! v.releasing && v.pos >= v.end)
                    v.release (fadeSamples (fadeSeconds));

                v.env += v.envStep;
                if (v.env >= 1.0f && ! v.releasing) { v.env = 1.0f; v.envStep = 0.0f; }
                if (v.env <= 0.0f)
                {
                    v.active = false;
                    break;
                }

                const float g = v.env * v.gain;
                if (v.warp == Stretch)
                {
                    // Grains never read past the end of the slice's region:
                    // once the stretch gets there, the last grain repeats.
                    const double lastStart = juce::jmax (v.pos, v.end - 2.0 * v.hop * rate);
                    float l, r;
                    grains (srcL, srcR, len, v.t, v.hop, rate,
                            [&] (double j) { return juce::jmin (v.pos + j * v.hop * v.stretch, lastStart); }, l, r);
                    scratchL[(size_t) i] += g * l;
                    scratchR[(size_t) i] += g * r;
                    v.t += 1.0;
                    continue;
                }

                if (v.fadeFrom < 0.0)
                {
                    scratchL[(size_t) i] += g * linear (srcL, len, v.pos);
                    scratchR[(size_t) i] += g * linear (srcR, len, v.pos);
                }
                else
                {
                    // Equal-power: the two heads read different audio.
                    const float in = std::sin (v.loopMix * juce::MathConstants<float>::halfPi);
                    const float out = std::cos (v.loopMix * juce::MathConstants<float>::halfPi);
                    scratchL[(size_t) i] += g * (in * linear (srcL, len, v.pos) + out * linear (srcL, len, v.fadeFrom));
                    scratchR[(size_t) i] += g * (in * linear (srcR, len, v.pos) + out * linear (srcR, len, v.fadeFrom));
                    v.fadeFrom += rate;
                    if ((v.loopMix += v.loopMixStep) >= 1.0f)
                        v.fadeFrom = -1.0;
                }
                v.pos += rate * v.rateMul;
            }
        }

        any |= renderKeys (b, srcL, srcR, len, n);

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

        // The sampler's own filter.
        if (const int type = filterType.load(); type != FilterOff)
        {
            const float fc = juce::jmin (filterCutoff.load(), (float) deviceRate * 0.45f);
            const float g  = std::tan (juce::MathConstants<float>::pi * fc / (float) deviceRate);
            const float k  = 2.0f - 1.9f * filterRes.load();   // 2 (flat) .. 0.1 (ringing)
            const float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
            float* chans[2] = { scratchL.data(), scratchR.data() };

            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < n; ++i)
                {
                    const float x  = chans[ch][i];
                    const float v3 = x - fz2[ch];
                    const float v1 = a1 * fz1[ch] + a2 * v3;
                    const float v2 = fz2[ch] + a2 * fz1[ch] + a3 * v3;
                    fz1[ch] = 2.0f * v1 - fz1[ch];
                    fz2[ch] = 2.0f * v2 - fz2[ch];
                    chans[ch][i] = type == LowPass ? v2 : type == BandPass ? v1 : x - k * v1 - v2;
                }
        }

        const float level = gain.load();
        if (level != 1.0f)
        {
            juce::FloatVectorOperations::multiply (scratchL.data(), level, n);
            juce::FloatVectorOperations::multiply (scratchR.data(), level, n);
        }

        juce::FloatVectorOperations::add (L, scratchL.data(), n);
        juce::FloatVectorOperations::add (R != nullptr ? R : L, scratchR.data(), n);
    }

    // Granular read: at output sample `t`, grain j started at j * hop, reads
    // from `startOf (j)` at `rate`, and fades in over one hop and out over
    // the next. Grains k and k-1 overlap, so their linear fades sum to one.
    // The first grain starts at full level so the attack isn't softened.
    template <typename StartOf>
    static void grains (const float* srcL, const float* srcR, int len, double t, double hop, double rate,
                        StartOf startOf, float& l, float& r)
    {
        const double k = std::floor (t / hop);
        const float  phase = (float) (t / hop - k);
        const double a = startOf (k) + (t - k * hop) * rate;
        if (k < 1.0)
        {
            l = linear (srcL, len, a);
            r = linear (srcR, len, a);
            return;
        }
        const double p = startOf (k - 1.0) + (t - (k - 1.0) * hop) * rate;
        l = phase * linear (srcL, len, a) + (1.0f - phase) * linear (srcL, len, p);
        r = phase * linear (srcR, len, a) + (1.0f - phase) * linear (srcR, len, p);
    }

    static float linear (const float* d, int len, double pos)
    {
        const int i0 = (int) pos % len;
        const int i1 = (i0 + 1) % len;
        const float frac = (float) (pos - std::floor (pos));
        return d[i0] + frac * (d[i1] - d[i0]);
    }

    static float hermite (const float* d, int len, double pos)
    {
        const int i1 = (int) pos;
        const float t = (float) (pos - i1);
        const float y0 = d[juce::jmax (0, i1 - 1)], y1 = d[i1],
                    y2 = d[juce::jmin (len - 1, i1 + 1)], y3 = d[juce::jmin (len - 1, i1 + 2)];
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * t + c2) * t + c1) * t + y1;
    }

    // Keyed voices into the scratch buffers, after the slices. True if any played.
    bool renderKeys (const Break& b, const float* srcL, const float* srcR, int len, int n)
    {
        bool any = false;
        const double base = b.sampleRate / deviceRate * pitchRatio();
        const double timeBase = b.sampleRate / deviceRate;   // SPEED moves through the sample at this, unpitched
        const float fadeStep = 1.0f / fadeSamples (fadeSeconds);
        const double tail = fadeSamples (fadeSeconds);   // fade out before the sample runs out
        const bool loop = loopKeys.load();

        for (auto& k : keys)
        {
            if (! k.active)
                continue;

            any = true;
            const double rate = base * k.ratio;
            for (int i = 0; i < n; ++i)
            {
                float env = 1.0f;
                float l, r;

                if (std::abs (k.speed - 1.0f) > 0.001f)
                {
                    // Grains: the grain starts move at SPEED, each grain reads
                    // at the note's pitch. Looping wraps the starts.
                    const double span = juce::jmax (1.0, len - 2.0 - 2.0 * k.hop * rate);
                    const double at = k.t * timeBase * k.speed;
                    if ((! loop && at >= span) || ! k.adsr.isActive())
                    {
                        k.active = false;
                        break;
                    }
                    env = k.adsr.getNextSample() * k.gain;
                    if (! loop)
                    {
                        const double left = (span - at) / (timeBase * k.speed);
                        if (left < tail) env *= (float) (left / tail);
                    }
                    grains (srcL, srcR, len, k.t, k.hop, rate,
                            [&] (double j) { const double s = j * k.hop * timeBase * k.speed; return loop ? std::fmod (s, span) : juce::jmin (s, span); },
                            l, r);
                    k.t += 1.0;
                }
                else
                {
                    if (k.pos >= len - 2)
                    {
                        if (loop) k.pos -= len - 2;
                        else { k.active = false; break; }
                    }
                    if (! k.adsr.isActive())
                    {
                        k.active = false;
                        break;
                    }
                    env = k.adsr.getNextSample() * k.gain;
                    if (! loop)
                    {
                        const double left = (len - 2 - k.pos) / rate;
                        if (left < tail) env *= (float) (left / tail);
                    }
                    l = hermite (srcL, len, k.pos);
                    r = hermite (srcR, len, k.pos);
                    k.pos += rate;
                }

                if (k.fadeIn < 1.0f) { k.fadeIn = juce::jmin (1.0f, k.fadeIn + fadeStep); env *= k.fadeIn; }
                scratchL[(size_t) i] += env * l;
                scratchR[(size_t) i] += env * r;
            }
            k.age += n;
        }
        return any;
    }

    std::unique_ptr<Break> owned;
    std::atomic<const Break*> current { nullptr };
    std::vector<Retired> retired;

    double deviceRate = 48000.0;
    const Break* lastBreak = nullptr;
    std::array<Voice, 6> voices;
    static constexpr int MaxKeys = 12;
    std::array<Key, MaxKeys + 4> keys;   // + spares for stolen voices to fade out in
    std::vector<float> scratchL, scratchR;
    float ic1[2] {}, ic2[2] {}, fz1[2] {}, fz2[2] {};
    std::atomic<float> pitchSemis { 0.0f }, tone { 1.0f };

    std::atomic<float> tuneSemis { 0.0f }, grainMs { 40.0f }, speed { 1.0f }, gain { 1.0f }, velSense { 1.0f };
    std::atomic<float> filterCutoff { 20000.0f }, filterRes { 0.2f };
    std::atomic<int>   warp { Beats }, filterType { FilterOff };
    std::atomic<bool>  loopKeys { false };
};
