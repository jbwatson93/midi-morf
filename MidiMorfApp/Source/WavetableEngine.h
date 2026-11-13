#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <memory>
#include <vector>
#include "Wavetable.h"
#include "SynthFilter.h"
#include "SynthMod.h"
#include "FxRack.h"
#include "TrackRack.h"

//==============================================================================
// WtMod — the synth's modulation sources and destinations, voice modes and
// oscillator warps. Every list is append-only: patches store the indices.
struct WtMod
{
    enum ModSource
    {
        ModSrcOff      = 0,
        ModSrcModWheel = 1,
        ModSrcVelocity = 2,
        ModSrcAccelX   = 3,
        ModSrcAccelY   = 4,
        ModSrcLFO      = 5,    // LFO 1
        ModSrcLFO2     = 6,
        ModSrcLFO3     = 7,
        ModSrcLFO4     = 8,
        ModSrcEnv1     = 9,    // the amp envelope
        ModSrcEnv2     = 10,   // the filter envelope
        ModSrcEnv3     = 11,
        ModSrcNote     = 12,   // -1..1 across ±5 octaves of middle C
        ModSrcRandom   = 13,   // a new value -1..1 every note
        ModSrcMacro1   = 14,
        ModSrcMacro2   = 15,
        ModSrcMacro3   = 16,
        ModSrcMacro4   = 17,
        NumModSources  = 18
    };

    enum ModDest
    {
        ModDestOff       = 0,
        ModDestPosA      = 1,
        ModDestPosB      = 2,
        ModDestCutoff    = 3,
        ModDestResonance = 4,
        ModDestPitch     = 5,
        ModDestGain      = 6,
        ModDestReverbMix = 7,
        ModDestDelayMix  = 8,
        ModDestDistDrive = 9,
        ModDestDelayTime = 10,
        ModDestLevelA    = 11,
        ModDestLevelB    = 12,
        ModDestPitchA    = 13,
        ModDestPitchB    = 14,
        ModDestSub       = 15,
        ModDestNoise     = 16,
        ModDestDetune    = 17,
        ModDestDrive     = 18,
        ModDestEnvAmount = 19,
        ModDestWarpA     = 20,
        ModDestWarpB     = 21,
        ModDestPan       = 22,
        ModDestLFO1Rate  = 23,
        ModDestLFO2Rate  = 24,
        ModDestLFO3Rate  = 25,
        ModDestLFO4Rate  = 26,
        ModDestCutoff2   = 27,
        ModDestResonance2 = 28,
        NumModDests      = 29
    };

    static juce::StringArray sourceNames()
    {
        return { "Off", "Mod Wheel", "Velocity", "Tilt X", "Tilt Y", "LFO 1", "LFO 2", "LFO 3", "LFO 4",
                 "Env 1 (Amp)", "Env 2 (Filter)", "Env 3", "Note", "Random", "Macro 1", "Macro 2", "Macro 3", "Macro 4" };
    }

    // Short names, for the source strip and knob menus.
    static juce::StringArray sourceShortNames()
    {
        return { "-", "MW", "VEL", "TILT X", "TILT Y", "LFO 1", "LFO 2", "LFO 3", "LFO 4",
                 "ENV 1", "ENV 2", "ENV 3", "NOTE", "RAND", "M1", "M2", "M3", "M4" };
    }

    static juce::StringArray destNames()
    {
        return { "Off", "OSC A Position", "OSC B Position", "Filter Cutoff", "Resonance",
                 "Pitch", "Gain", "Reverb Mix", "Delay Mix", "Dist Drive", "Delay Time",
                 "OSC A Level", "OSC B Level", "OSC A Pitch", "OSC B Pitch", "Sub Level", "Noise Level",
                 "Unison Detune", "Drive", "Filter Env", "OSC A Warp", "OSC B Warp", "Pan",
                 "LFO 1 Rate", "LFO 2 Rate", "LFO 3 Rate", "LFO 4 Rate", "Filter 2 Cutoff", "Filter 2 Reso" };
    }

    enum Curve { CurveLinear = 0, CurveExp = 1, CurveLog = 2, NumCurves = 3 };
    static juce::StringArray curveNames() { return { "Linear", "Exp", "Log" }; }

    enum VoiceMode { Poly = 0, Mono = 1, Legato = 2 };
    static juce::StringArray voiceModeNames() { return { "Poly", "Mono", "Legato" }; }

    // How the two filters combine when both are on.
    enum FilterRouting { FiltersSerial = 0, FiltersParallel = 1 };
    static juce::StringArray filterRoutingNames() { return { "Serial", "Parallel" }; }

    // Which sources go through the filters (the rest bypass them).
    enum FilterInput { FilterOscA = 1, FilterOscB = 2, FilterSub = 4, FilterNoise = 8, FilterAll = 15 };

    enum Warp { WarpOff = 0, WarpSync, WarpBendUp, WarpBendDown, WarpPwm, WarpMirror, WarpQuantize, WarpFM, NumWarps };
    static juce::StringArray warpNames() { return { "Off", "Sync", "Bend +", "Bend -", "PWM", "Mirror", "Quantize", "FM" }; }

    static constexpr int NumLfos = 4;
    static constexpr int NumMacros = 4;
    static constexpr int NumRoutingSlots = 16;

    struct Route
    {
        int   source = ModSrcOff, dest = ModDestOff;
        float amount = 0.0f;       // -1..1
        int   aux = ModSrcOff;     // scales the amount when set
        int   curve = CurveLinear;
        bool  enabled = true;

        bool isActive() const { return enabled && source != ModSrcOff && dest != ModDestOff && amount != 0.0f; }
    };

    static float shapeSource (float s, int curve)
    {
        switch (curve)
        {
            case CurveExp: return s * std::abs (s);
            case CurveLog: return std::copysign (std::sqrt (std::abs (s)), s);
            default:       return s;
        }
    }

    // Is this source the same for every voice (computed once per block)?
    static bool isGlobalSource (int s)
    {
        return s == ModSrcModWheel || s == ModSrcAccelX || s == ModSrcAccelY
            || (s >= ModSrcMacro1 && s <= ModSrcMacro4);
    }
};

//==============================================================================
// Everything a voice needs for one block, filled in once by the engine and
// read by every voice through a pointer (audio thread only).
struct WtBlockParams
{
    struct Osc
    {
        const Wavetable* table = nullptr;
        float level = 1.0f, position = 0.0f, semis = 0.0f;
        int   warp = WtMod::WarpOff;
        float warpAmount = 0.0f;
    };

    struct LfoBlock
    {
        const SynthMod::Lfo* lfo = nullptr;
        double hz = 1.0;          // before per-voice rate modulation
        double freePhase = 0.0;   // where a Free LFO is at the block's start
        int    mode = SynthMod::Free;
        float  rise = 0.0f, delay = 0.0f, phase = 0.0f, depth = 1.0f;
    };

    Osc   osc[2];
    float sub = 0.0f, noise = 0.0f;
    int   unison = 1;
    float detune = 0.0f;

    int   filterType = SynthFilter::Ladder;
    bool  filterOn = true;
    float cutoff = 2500.0f, reso = 0.8f, envAmount = 0.0f, filterVel = 0.0f, keytrack = 0.0f;
    bool  filter2On = false;
    int   filter2Type = SynthFilter::LP12;
    float cutoff2 = 2000.0f, reso2 = 0.5f, envAmount2 = 0.0f;
    int   filterRouting = WtMod::FiltersSerial;
    int   filterInputs = WtMod::FilterAll;

    float envToPos = 0.0f, drive = 0.0f, gain = 0.15f;
    float glide = 0.0f;       // seconds

    std::array<WtMod::Route, WtMod::NumRoutingSlots> routes;
    int   numRoutes = 0;
    float global[WtMod::NumModSources] {};
    LfoBlock lfo[WtMod::NumLfos];

    double sampleRate = 44100.0;
    int    blockStart = 0;    // the block's first sample in the buffer, for Free LFO phase

    // The last note any voice started, for portamento (written by voices).
    float lastNote = -1.0f;
};

//==============================================================================
// WavetableVoice: two wavetable oscillators (stereo unison, warp) + sine sub +
// noise → tanh drive → stereo filter (SynthFilter) → amp envelope.
//
// Modulation runs per voice, every ControlRate samples: each voice works out
// its own sources (its velocity, note, envelopes, LFOs started on its note)
// and runs the whole mod matrix, so two notes can be modulated differently.
//
// Cutoff moves in octaves, not Hz: the envelope, keytracking and the mod
// matrix all multiply the knob's frequency.
class WavetableVoice : public juce::SynthesiserVoice, private WtMod
{
public:
    static constexpr int MaxUnison = 7;

    // ENV at ±1 moves the cutoff this many octaves at the envelope's peak.
    static constexpr float EnvOctaves = 6.0f;
    // A mod-matrix route at ±1 moves the cutoff this many octaves.
    static constexpr float MatrixCutoffOctaves = 5.0f;

    bool canPlaySound (juce::SynthesiserSound*) override { return true; }

    // A pitch wheel of 0..16383 covers this many semitones each way, so a
    // fretboard's slide reaches two octaves.
    static constexpr float BendRange = 48.0f;
    static float wheelToSemis (int wheel) { return (float) (wheel - 8192) / 8192.0f * BendRange; }

    void setParams (WtBlockParams* p) { params = p; }

    void startNote (int midiNoteNumber, float velocity,
                    juce::SynthesiserSound*, int pitchWheel) override
    {
        level = velocity;
        noteNumber = midiNoteNumber;
        randomValue = random.nextFloat() * 2.0f - 1.0f;
        bendTarget = bendNow = wheelToSemis (pitchWheel);
        drift = (random.nextFloat() * 2.0f - 1.0f) * 0.015f;   // ±1.5 cents

        // Portamento: glide from wherever the last note was.
        targetSemis = (float) midiNoteNumber;
        currentSemis = (params != nullptr && params->glide > 0.0005f && params->lastNote >= 0.0f)
                     ? params->lastNote : targetSemis;
        if (params != nullptr) params->lastNote = targetSemis;

        // Random start phases keep unison voices from phasing identically on
        // every note; a single voice always starts at 0 so plucks stay punchy.
        const int u = params != nullptr ? params->unison : 1;
        for (int i = 0; i < MaxUnison; ++i)
        {
            phaseA[i] = u > 1 ? random.nextDouble() : 0.0;
            phaseB[i] = u > 1 ? random.nextDouble() : 0.0;
            lastA[i] = lastB[i] = 0.0f;
        }
        subPhase = 0.0;
        restartModulation();

        filterL.reset();
        filterR.reset();
        filter2L.reset();
        filter2R.reset();
        countdown = 0;
        ampAdsr.noteOn();
        filterAdsr.noteOn();
        env3.noteOn();
    }

    // Mono and legato: move this voice to a new note without a new voice.
    // Mono retriggers the envelopes and LFOs; legato just glides.
    void glideTo (int midiNoteNumber, float velocity, bool retrigger)
    {
        noteNumber = midiNoteNumber;
        targetSemis = (float) midiNoteNumber;
        if (params != nullptr)
        {
            if (params->glide <= 0.0005f) currentSemis = targetSemis;
            params->lastNote = targetSemis;
        }
        if (retrigger)
        {
            level = velocity;
            randomValue = random.nextFloat() * 2.0f - 1.0f;
            restartModulation();
            ampAdsr.noteOn();
            filterAdsr.noteOn();
            env3.noteOn();
        }
    }

    void stopNote (float, bool allowTailOff) override
    {
        if (allowTailOff)
        {
            ampAdsr.noteOff();
            filterAdsr.noteOff();
            env3.noteOff();
        }
        else
        {
            clearCurrentNote();
        }
    }

    void pitchWheelMoved (int wheel) override { bendTarget = wheelToSemis (wheel); }
    void controllerMoved (int, int) override {}

    void prepareToPlay (double sampleRate)
    {
        currentSampleRate = sampleRate;
        ampAdsr.setSampleRate (sampleRate);
        filterAdsr.setSampleRate (sampleRate);
        env3.setSampleRate (sampleRate);
    }

    void setAmpADSR (const juce::ADSR::Parameters& p)    { ampAdsr.setParameters (p); }
    void setFilterADSR (const juce::ADSR::Parameters& p) { filterAdsr.setParameters (p); }
    void setEnv3ADSR (const juce::ADSR::Parameters& p)   { env3.setParameters (p); }

    // What this voice's sources and destinations were at its last control
    // tick: the engine uses the newest voice's for the FX and the UI.
    const float* getLastSources() const noexcept { return src; }
    const float* getLastMods() const noexcept    { return mods; }
    float getCurrentCutoff (int filter = 0) const noexcept { return filter == 0 ? currentCutoff : currentCutoff2; }
    float getPosition (int osc) const noexcept   { return osc == 0 ? posA : posB; }

    void renderNextBlock (juce::AudioBuffer<float>& outputBuffer,
                          int startSample, int numSamples) override
    {
        if (! isVoiceActive() || params == nullptr)
            return;

        const auto& p = *params;
        const bool useA = p.osc[0].table != nullptr;
        const bool useB = p.osc[1].table != nullptr;
        // An oscillator FM'd by the other needs the other running, even
        // when it's turned all the way down.
        const bool fmFromB = p.osc[0].warp == WarpFM, fmFromA = p.osc[1].warp == WarpFM;

        float* outL = outputBuffer.getWritePointer (0);
        float* outR = outputBuffer.getNumChannels() > 1 ? outputBuffer.getWritePointer (1) : nullptr;

        for (int sample = startSample; sample < startSample + numSamples; ++sample)
        {
            ampValue  = ampAdsr.getNextSample();
            env2Value = filterAdsr.getNextSample();
            env3Value = env3.getNextSample();

            if (--countdown <= 0)
            {
                countdown = ControlRate;
                controlTick (p, sample);
            }

            // Each source goes to the filtered path (f) or around it (d).
            float fl = 0.0f, fr = 0.0f, dl = 0.0f, dr = 0.0f;
            const int inputs = p.filterInputs;
            for (int u = 0; u < unison; ++u)
            {
                float a = 0.0f, b = 0.0f;
                if (useA && (levelA > 0.0001f || fmFromA))
                {
                    const double ph = warpPhase (p.osc[0].warp, warpA, phaseA[u], lastB[u]);
                    const float v = p.osc[0].table->read (mipA, ph, posA);
                    lastA[u] = v;
                    a = v * levelA;
                    phaseA[u] += incA * ratio[u];
                    if (phaseA[u] >= 1.0) phaseA[u] -= 1.0;
                }
                if (useB && (levelB > 0.0001f || fmFromB))
                {
                    const double ph = warpPhase (p.osc[1].warp, warpB, phaseB[u], lastA[u]);
                    const float v = p.osc[1].table->read (mipB, ph, posB);
                    lastB[u] = v;
                    b = v * levelB;
                    phaseB[u] += incB * ratio[u];
                    if (phaseB[u] >= 1.0) phaseB[u] -= 1.0;
                }
                const float f = ((inputs & FilterOscA) ? a : 0.0f) + ((inputs & FilterOscB) ? b : 0.0f);
                const float d = a + b - f;
                fl += f * gainL[u]; fr += f * gainR[u];
                dl += d * gainL[u]; dr += d * gainR[u];
            }

            if (subLevel > 0.0001f)
            {
                const float sub = std::sin ((float) subPhase * juce::MathConstants<float>::twoPi) * subLevel;
                if (inputs & FilterSub) { fl += sub; fr += sub; } else { dl += sub; dr += sub; }
                subPhase += incSub;
                if (subPhase >= 1.0) subPhase -= 1.0;
            }

            if (noiseLevel > 0.0001f)
            {
                const float nl = nextNoise() * noiseLevel, nr = nextNoise() * noiseLevel;
                if (inputs & FilterNoise) { fl += nl; fr += nr; } else { dl += nl; dr += nr; }
            }

            fl *= level; fr *= level;

            if (driveAmount > 0.0001f)
            {
                const float k = 1.0f + driveAmount * 6.0f;
                const float norm = 1.0f / std::tanh (k);
                fl = std::tanh (fl * k) * norm;
                fr = std::tanh (fr * k) * norm;
            }

            if (p.filterOn && p.filter2On)
            {
                if (p.filterRouting == FiltersSerial)
                {
                    fl = filter2L.process (p.filter2Type, coeffs2, filterL.process (p.filterType, coeffs, fl));
                    fr = filter2R.process (p.filter2Type, coeffs2, filterR.process (p.filterType, coeffs, fr));
                }
                else
                {
                    fl = 0.5f * (filterL.process (p.filterType, coeffs, fl) + filter2L.process (p.filter2Type, coeffs2, fl));
                    fr = 0.5f * (filterR.process (p.filterType, coeffs, fr) + filter2R.process (p.filter2Type, coeffs2, fr));
                }
            }
            else if (p.filterOn)
            {
                fl = filterL.process (p.filterType, coeffs, fl);
                fr = filterR.process (p.filterType, coeffs, fr);
            }
            else if (p.filter2On)
            {
                fl = filter2L.process (p.filter2Type, coeffs2, fl);
                fr = filter2R.process (p.filter2Type, coeffs2, fr);
            }

            const float l = fl + dl * level, r = fr + dr * level;

            const float amp = ampValue * voiceGain;
            if (outR != nullptr)
            {
                outL[sample] += l * amp * panL;
                outR[sample] += r * amp * panR;
            }
            else
            {
                outL[sample] += (l + r) * 0.5f * amp;
            }

            if (! ampAdsr.isActive())
            {
                clearCurrentNote();
                break;
            }
        }
    }

private:
    // Modulation, pitch and filter coefficients are recomputed this often
    // (samples): fast enough that a snappy envelope stays smooth, slow
    // enough that the matrix, tan() and exp2() don't cost much across 24
    // voices on 4 tracks.
    static constexpr int ControlRate = 16;

    void restartModulation()
    {
        noteSamples = 0;
        if (params != nullptr)
            for (int k = 0; k < NumLfos; ++k)
                lfoPhase[k] = params->lfo[k].phase;
        for (auto& m : mods) m = 0.0f;
    }

    void controlTick (const WtBlockParams& p, int sample)
    {
        const double sr = p.sampleRate;
        const double tickSeconds = ControlRate / sr;

        // ── Sources ────────────────────────────────────────────────────────
        for (int s = 0; s < NumModSources; ++s)
            src[s] = p.global[s];
        src[ModSrcVelocity] = level;
        src[ModSrcNote]     = (float) (noteNumber - 60) / 60.0f;
        src[ModSrcRandom]   = randomValue;
        src[ModSrcEnv1]     = ampValue;
        src[ModSrcEnv2]     = env2Value;
        src[ModSrcEnv3]     = env3Value;

        const double seconds = (double) noteSamples / sr;
        for (int k = 0; k < NumLfos; ++k)
        {
            const auto& lb = p.lfo[k];
            if (lb.lfo == nullptr) { src[ModSrcLFO + k] = 0.0f; continue; }

            // LFO rate is a destination too: last tick's value.
            const double hz = lb.hz * std::exp2 (mods[ModDestLFO1Rate + k] * 4.0f);
            double ph;
            if (lb.mode == SynthMod::Free)
            {
                ph = lb.freePhase + lb.hz * (double) (sample - p.blockStart) / sr;
                ph -= std::floor (ph);
            }
            else
            {
                ph = lfoPhase[k];
                lfoPhase[k] += hz * tickSeconds;
                if (lb.mode == SynthMod::Envelope) lfoPhase[k] = juce::jmin (lfoPhase[k], 0.99999);
                else                               lfoPhase[k] -= std::floor (lfoPhase[k]);
            }

            float fade = 1.0f;
            if (seconds < lb.delay)          fade = 0.0f;
            else if (lb.rise > 0.0001f)      fade = (float) juce::jmin (1.0, (seconds - lb.delay) / lb.rise);
            src[ModSrcLFO + k] = lb.lfo->read (ph) * lb.depth * fade;
        }
        noteSamples += ControlRate;

        // ── Matrix ─────────────────────────────────────────────────────────
        for (auto& m : mods) m = 0.0f;
        for (int i = 0; i < p.numRoutes; ++i)
        {
            const auto& rt = p.routes[(size_t) i];
            float v = shapeSource (src[rt.source], rt.curve) * rt.amount;
            if (rt.aux != ModSrcOff) v *= src[rt.aux];
            mods[rt.dest] += v;
        }

        // ── Pitch ──────────────────────────────────────────────────────────
        if (p.glide > 0.0005f)
            currentSemis += (targetSemis - currentSemis) * (float) (1.0 - std::exp (-tickSeconds / (p.glide * 0.35)));
        else
            currentSemis = targetSemis;
        bendNow += (bendTarget - bendNow) * 0.25f;

        const float semis = currentSemis + bendNow + mods[ModDestPitch] * 7.0f + drift;
        const double base = 440.0 * std::exp2 ((semis - 69.0) / 12.0) / sr;
        incA = base * std::exp2 ((p.osc[0].semis + mods[ModDestPitchA] * 24.0f) / 12.0f);
        incB = base * std::exp2 ((p.osc[1].semis + mods[ModDestPitchB] * 24.0f) / 12.0f);
        incSub = base * 0.5;

        // ── Unison spread, equal-power pan ─────────────────────────────────
        unison = juce::jlimit (1, MaxUnison, p.unison);
        const float detune = juce::jlimit (0.0f, 100.0f, p.detune + mods[ModDestDetune] * 50.0f);
        const float unisonNorm = 1.0f / std::sqrt ((float) unison);
        for (int u = 0; u < unison; ++u)
        {
            const float spread = unison > 1 ? ((float) u / (float) (unison - 1)) * 2.0f - 1.0f : 0.0f;
            ratio[u] = std::exp2 ((double) (spread * detune) / 1200.0);
            const float pan = (spread + 1.0f) * 0.25f * juce::MathConstants<float>::pi; // 0..π/2
            gainL[u] = std::cos (pan) * juce::MathConstants<float>::sqrt2 * unisonNorm;
            gainR[u] = std::sin (pan) * juce::MathConstants<float>::sqrt2 * unisonNorm;
        }

        // One mip level per oscillator, chosen for its widest-detuned unison
        // voice (and for SYNC, the faster read) so nothing aliases.
        warpA = juce::jlimit (0.0f, 1.0f, p.osc[0].warpAmount + mods[ModDestWarpA]);
        warpB = juce::jlimit (0.0f, 1.0f, p.osc[1].warpAmount + mods[ModDestWarpB]);
        const double maxRatio = juce::jmax (ratio[0], ratio[unison - 1]);
        auto speedUp = [] (int warp, float amt) { return warp == WarpSync ? 1.0 + amt * 7.0 : 1.0; };
        mipA = Wavetable::levelForIncrement (incA * maxRatio * speedUp (p.osc[0].warp, warpA));
        mipB = Wavetable::levelForIncrement (incB * maxRatio * speedUp (p.osc[1].warp, warpB));

        // ── Levels, position ───────────────────────────────────────────────
        posA = juce::jlimit (0.0f, 1.0f, p.osc[0].position + mods[ModDestPosA] + p.envToPos * env2Value);
        posB = juce::jlimit (0.0f, 1.0f, p.osc[1].position + mods[ModDestPosB] + p.envToPos * env2Value);
        levelA = juce::jlimit (0.0f, 1.0f, p.osc[0].level + mods[ModDestLevelA]);
        levelB = juce::jlimit (0.0f, 1.0f, p.osc[1].level + mods[ModDestLevelB]);
        subLevel = juce::jlimit (0.0f, 1.0f, p.sub + mods[ModDestSub]);
        noiseLevel = juce::jlimit (0.0f, 1.0f, p.noise + mods[ModDestNoise]);
        driveAmount = juce::jlimit (0.0f, 1.0f, p.drive + mods[ModDestDrive]);
        voiceGain = juce::jlimit (0.0f, 0.6f, p.gain + mods[ModDestGain] * 0.2f);

        const float pan = juce::jlimit (-1.0f, 1.0f, mods[ModDestPan]);
        panL = juce::jmin (1.0f, 1.0f - pan);
        panR = juce::jmin (1.0f, 1.0f + pan);

        // ── Filters ────────────────────────────────────────────────────────
        // VEL and KEY are shared; each filter has its own ENV amount.
        const float velScale = 1.0f - p.filterVel + p.filterVel * level;
        const float keyOctaves = p.keytrack * (float) (noteNumber - 60) / 12.0f;
        if (p.filterOn)
        {
            const float envAmt = juce::jlimit (-1.0f, 1.0f, p.envAmount + mods[ModDestEnvAmount]);
            const float octaves = mods[ModDestCutoff] * MatrixCutoffOctaves + keyOctaves
                                + envAmt * EnvOctaves * velScale * env2Value;
            const float reso = juce::jlimit (0.0f, SynthFilter::MaxReso, p.reso + mods[ModDestResonance] * SynthFilter::MaxReso);
            currentCutoff = juce::jlimit (20.0f, 20000.0f, p.cutoff * std::exp2 (octaves));
            coeffs = SynthFilter::makeCoeffs (p.filterType, currentCutoff, reso, sr);
        }
        if (p.filter2On)
        {
            const float octaves = mods[ModDestCutoff2] * MatrixCutoffOctaves + keyOctaves
                                + p.envAmount2 * EnvOctaves * velScale * env2Value;
            const float reso = juce::jlimit (0.0f, SynthFilter::MaxReso, p.reso2 + mods[ModDestResonance2] * SynthFilter::MaxReso);
            currentCutoff2 = juce::jlimit (20.0f, 20000.0f, p.cutoff2 * std::exp2 (octaves));
            coeffs2 = SynthFilter::makeCoeffs (p.filter2Type, currentCutoff2, reso, sr);
        }
    }

    // Reads a table at a bent phase: the oscillator's WARP.
    static double warpPhase (int warp, float amt, double phase, float other) noexcept
    {
        if (warp == WarpOff || amt <= 0.0001f) return phase;
        switch (warp)
        {
            case WarpSync:
            {
                const double p = phase * (1.0 + amt * 7.0);
                return p - std::floor (p);
            }
            case WarpBendUp:   return std::pow (phase, 1.0 / (1.0 + amt * 3.0));
            case WarpBendDown: return std::pow (phase, 1.0 + amt * 3.0);
            case WarpPwm:
            {
                const double w = 0.5 - 0.48 * amt;
                return phase < w ? phase * 0.5 / w : 0.5 + (phase - w) * 0.5 / (1.0 - w);
            }
            case WarpMirror:
            {
                const double m = phase < 0.5 ? phase * 2.0 : 2.0 - phase * 2.0;
                return juce::jlimit (0.0, 0.99999, phase + (m - phase) * amt);
            }
            case WarpQuantize:
            {
                const double steps = 64.0 - amt * 62.0;
                return std::floor (phase * steps) / steps;
            }
            case WarpFM:
            {
                const double p = phase + amt * 2.0 * other;
                return p - std::floor (p);
            }
            default: return phase;
        }
    }

    float nextNoise() noexcept
    {
        noiseSeed = noiseSeed * 1664525u + 1013904223u;
        return (float) (int32_t) noiseSeed * (1.0f / 2147483648.0f);
    }

    WtBlockParams* params = nullptr;
    double currentSampleRate = 44100.0;
    float  level = 0.0f;     // the note's velocity, 0..1
    int    noteNumber = 60;
    float  randomValue = 0.0f, drift = 0.0f;
    juce::Random random;
    uint32_t noiseSeed = 22222u;

    float currentSemis = 60.0f, targetSemis = 60.0f;
    float bendTarget = 0.0f, bendNow = 0.0f;   // semitones, from the channel's pitch wheel

    double phaseA[MaxUnison] {}, phaseB[MaxUnison] {};
    float  lastA[MaxUnison] {}, lastB[MaxUnison] {};   // for FM
    double subPhase = 0.0;
    double lfoPhase[NumLfos] {};
    juce::int64 noteSamples = 0;

    // Worked out each control tick.
    float src[NumModSources] {}, mods[NumModDests] {};
    double incA = 0.0, incB = 0.0, incSub = 0.0;
    double ratio[MaxUnison] { 1.0 };
    float  gainL[MaxUnison] {}, gainR[MaxUnison] {};
    int    unison = 1, mipA = 0, mipB = 0;
    float  posA = 0.0f, posB = 0.0f, levelA = 0.0f, levelB = 0.0f, warpA = 0.0f, warpB = 0.0f;
    float  subLevel = 0.0f, noiseLevel = 0.0f, driveAmount = 0.0f, voiceGain = 0.15f, panL = 1.0f, panR = 1.0f;
    float  ampValue = 0.0f, env2Value = 0.0f, env3Value = 0.0f;
    int    countdown = 0;

    SynthFilter::Channel filterL, filterR, filter2L, filter2R;
    SynthFilter::Coeffs coeffs, coeffs2;
    float currentCutoff = 2500.0f, currentCutoff2 = 2000.0f;
    juce::ADSR ampAdsr, filterAdsr, env3;
};

//==============================================================================
class WavetableSound : public juce::SynthesiserSound
{
public:
    bool appliesToNote (int) override { return true; }
    bool appliesToChannel (int) override { return true; }
};

//==============================================================================
// The voice allocator: juce::Synthesiser in POLY, and in MONO / LEGATO one
// voice that follows the newest held key (and falls back to the one before
// when it's let go).
class WavetableSynth : public juce::Synthesiser
{
public:
    int voiceMode = WtMod::Poly;

    void noteOn (int channel, int note, float velocity) override
    {
        if (voiceMode == WtMod::Poly) { juce::Synthesiser::noteOn (channel, note, velocity); return; }

        held.erase (std::remove_if (held.begin(), held.end(), [note] (const Held& h) { return h.note == note; }), held.end());
        if (held.size() < 32) held.push_back ({ channel, note, velocity });

        auto* v = monoVoice();
        if (v == nullptr || getNumSounds() == 0) return;
        if (v->isVoiceActive() && v->isKeyDown())
            v->glideTo (note, velocity, voiceMode == WtMod::Mono);
        else
            startVoice (v, getSound (0).get(), channel, note, velocity);
    }

    void noteOff (int channel, int note, float velocity, bool allowTailOff) override
    {
        if (voiceMode == WtMod::Poly) { juce::Synthesiser::noteOff (channel, note, velocity, allowTailOff); return; }

        const bool wasNewest = ! held.empty() && held.back().note == note;
        held.erase (std::remove_if (held.begin(), held.end(), [note] (const Held& h) { return h.note == note; }), held.end());
        auto* v = monoVoice();
        if (v == nullptr || ! wasNewest) return;

        if (! held.empty())
            v->glideTo (held.back().note, held.back().velocity, false);   // back to the key still down
        else if (v->isVoiceActive())
        {
            v->setKeyDown (false);
            v->stopNote (velocity, allowTailOff);
        }
    }

    void allNotesOff (int channel, bool allowTailOff) override
    {
        held.clear();
        juce::Synthesiser::allNotesOff (channel, allowTailOff);
    }

private:
    struct Held { int channel, note; float velocity; };

    WavetableVoice* monoVoice() { return getNumVoices() > 0 ? dynamic_cast<WavetableVoice*> (getVoice (0)) : nullptr; }

    std::vector<Held> held;
};

//==============================================================================
// WavetableEngine — the built-in synth. Each oscillator plays a table: one of
// the single-frame analog waves (Sine / Triangle / Saw / Square), a built-in
// wavetable, or an imported .wav. Pure MIDI API (noteOn / noteOff /
// allNotesOff) plus mod sources (setModWheel / setAccelX/Y); controllers
// reach it through PhoneMidiSink.
//
// Table swaps are lock-free: the audio thread reads a raw pointer per block;
// the UI thread swaps it atomically and keeps the old table alive for a
// while before freeing it, so a render in flight never reads freed memory.
class WavetableEngine : public juce::AudioSource, public SoundSource, public WtMod
{
public:
    enum LFOWaveform { LFOSine = 0, LFOTri = 1, LFOSaw = 2, LFOSquare = 3, NumLFOWaves = 4 };

    // Each track's synth gets its own voice budget: voices only cost CPU
    // while sounding, but the cap bounds the worst case when four tracks
    // all play at once.
    static constexpr int DefaultVoices = 32;

    explicit WavetableEngine (int numVoices = DefaultVoices)
    {
        synth.addSound (new WavetableSound());
        for (int i = 0; i < juce::jmax (1, numVoices); ++i)
        {
            auto* v = new WavetableVoice();
            v->setParams (&block);
            synth.addVoice (v);
        }
        for (auto& m : liveMods) m.store (0.0f);

        resetToDefaults();
    }

    //==========================================================================
    // AudioSource
    void prepareToPlay (int, double sampleRate) override
    {
        currentSampleRate = sampleRate;
        synth.setCurrentPlaybackSampleRate (sampleRate);
        for (int i = 0; i < synth.getNumVoices(); ++i)
            if (auto* v = dynamic_cast<WavetableVoice*> (synth.getVoice (i)))
                v->prepareToPlay (sampleRate);
        fx.prepare (sampleRate);
        audioThreadMidi.ensureSize (2048);
        adsrDirty = true;
    }

    void releaseResources() override {}

    void getNextAudioBlock (const juce::AudioSourceChannelInfo& info) override
    {
        info.clearActiveBufferRegion();

        // juce::Synthesiser reads event positions in whole-buffer terms and
        // skips anything before startSample, so events go at startSample —
        // LocalSequencers renders in sub-blocks that start mid-buffer.
        juce::MidiBuffer midi;
        MidiEvent e;
        while (midiQueue.try_dequeue (e))
            midi.addEvent (e.isNoteOn ? juce::MidiMessage::noteOn (e.channel, e.note, (juce::uint8) e.velocity)
                                      : juce::MidiMessage::noteOff (e.channel, e.note), info.startSample);
        midi.addEvents (audioThreadMidi, 0, -1, info.startSample);
        audioThreadMidi.clear();

        processModulation (info.startSample, info.numSamples);
        synth.renderNextBlock (*info.buffer, midi, info.startSample, info.numSamples);
        publishLiveState();

        auto* buf = info.buffer;
        float* L = buf->getNumChannels() > 0 ? buf->getWritePointer (0, info.startSample) : nullptr;
        float* R = buf->getNumChannels() > 1 ? buf->getWritePointer (1, info.startSample) : nullptr;
        fx.process (L, R, info.numSamples);
    }

    //==========================================================================
    // MIDI API — UI thread, lock-free.
    void noteOn (int midiNote, float velocity, int channel = 1)
    {
        if (midiNote < 0 || midiNote > 127) return;
        lastVelocity.store (juce::jlimit (0.0f, 1.0f, velocity));
        midiQueue.enqueue ({ midiNote, juce::jlimit (1, 127, (int) std::round (velocity * 127.0f)),
                             juce::jlimit (1, 16, channel), true });
    }

    void noteOff (int midiNote, int channel = 1)
    {
        if (midiNote < 0 || midiNote > 127) return;
        midiQueue.enqueue ({ midiNote, 0, juce::jlimit (1, 16, channel), false });
    }

    // Audio-thread twins of noteOn/noteOff, for the local sequencers, which
    // fire from inside the audio callback. The FIFO above has one producer
    // (the UI); these land at the start of the next getNextAudioBlock call.
    void noteOnFromAudioThread (int midiNote, int velocity127, int channel = 1)
    {
        if (midiNote < 0 || midiNote > 127) return;
        const int v = juce::jlimit (1, 127, velocity127);
        lastVelocity.store ((float) v / 127.0f);
        audioThreadMidi.addEvent (juce::MidiMessage::noteOn (juce::jlimit (1, 16, channel), midiNote, (juce::uint8) v), 0);
    }

    void noteOffFromAudioThread (int midiNote, int channel = 1)
    {
        if (midiNote < 0 || midiNote > 127) return;
        audioThreadMidi.addEvent (juce::MidiMessage::noteOff (juce::jlimit (1, 16, channel), midiNote), 0);
    }

    void allNotesOff() override
    {
        // One flag instead of 2048 queued note-offs — the FIFO is only 1024 deep.
        killAll.store (true);
    }

    //==========================================================================
    // SoundSource — the track rack's view of this engine.
    juce::String typeId() const override                  { return "wavetable"; }
    void prepare (double sr, int maxBlockSize) override   { prepareToPlay (maxBlockSize, sr); }
    void release() override                               { releaseResources(); }
    void noteOnAudio (int ch, int note, int velocity127) override { noteOnFromAudioThread (note, velocity127, ch); }
    void noteOffAudio (int ch, int note) override                 { noteOffFromAudioThread (note, ch); }
    void pitchBendAudio (int ch, float semis) override
    {
        const int wheel = juce::jlimit (0, 16383, juce::roundToInt (8192.0f + semis / WavetableVoice::BendRange * 8192.0f));
        audioThreadMidi.addEvent (juce::MidiMessage::pitchWheel (juce::jlimit (1, 16, ch), wheel), 0);
    }
    void setTempo (double bpm) override { tempo.store (juce::jlimit (20.0, 400.0, bpm)); }

    void render (juce::AudioBuffer<float>& buffer, int start, int num) override
    {
        getNextAudioBlock (juce::AudioSourceChannelInfo (&buffer, start, num));
    }

    // CC1 is the mod wheel; nothing else is mapped yet.
    void controller (int cc, int value) override
    {
        if (cc == 1)
            setModWheel ((float) juce::jlimit (0, 127, value) / 127.0f);
    }

    void setModWheel (float v) { modWheel.store (juce::jlimit (0.0f, 1.0f, v)); }
    void setAccelX (float v)   { accelX.store (juce::jlimit (-1.0f, 1.0f, v)); }
    void setAccelY (float v)   { accelY.store (juce::jlimit (-1.0f, 1.0f, v)); }

    //==========================================================================
    // Tables. Index into WavetableLibrary::builtInNames(), or an imported file.
    void setBuiltInTable (int osc, int index)
    {
        index = juce::jlimit (0, WavetableLibrary::builtInNames().size() - 1, index);
        installTable (osc, WavetableLibrary::shared (index));
        oscState (osc).builtInIndex = index;
        oscState (osc).importedFile = juce::File();
    }

    bool importTable (int osc, const juce::File& file, juce::String& error)
    {
        auto table = WavetableLibrary::importWav (file, error);
        if (table == nullptr)
            return false;
        installTable (osc, std::move (table));
        oscState (osc).builtInIndex = -1;
        oscState (osc).importedFile = file;
        return true;
    }

    // Plays a table that isn't saved anywhere yet (the wavetable editor's
    // work in progress). The patch still names the old table until the
    // editor saves and imports the new one.
    void previewTable (int osc, std::shared_ptr<const Wavetable> table) { installTable (osc, std::move (table)); }

    const Wavetable* getTable (int osc) const { return osc == 0 ? tableA.load() : tableB.load(); }
    int getBuiltInIndex (int osc) const       { return oscState (osc).builtInIndex; }
    juce::File getImportedFile (int osc) const { return oscState (osc).importedFile; }

    //==========================================================================
    // Oscillators
    void  setOscLevel (int osc, float v)    { oscState (osc).level = juce::jlimit (0.0f, 1.0f, v); }
    void  setOscPosition (int osc, float v) { oscState (osc).position = juce::jlimit (0.0f, 1.0f, v); }
    void  setOscSemitones (int osc, int s)  { oscState (osc).semis = juce::jlimit (-24, 24, s); }
    void  setOscFineCents (int osc, float c){ oscState (osc).fine = juce::jlimit (-100.0f, 100.0f, c); }
    void  setOscWarp (int osc, int w)       { oscState (osc).warp = juce::jlimit (0, NumWarps - 1, w); }
    void  setOscWarpAmount (int osc, float a){ oscState (osc).warpAmount = juce::jlimit (0.0f, 1.0f, a); }
    float getOscLevel (int osc) const       { return oscState (osc).level; }
    float getOscPosition (int osc) const    { return oscState (osc).position; }
    int   getOscSemitones (int osc) const   { return oscState (osc).semis; }
    float getOscFineCents (int osc) const   { return oscState (osc).fine; }
    int   getOscWarp (int osc) const        { return oscState (osc).warp; }
    float getOscWarpAmount (int osc) const  { return oscState (osc).warpAmount; }

    // Live (modulated) position for the UI's wave display.
    float getEffectivePosition (int osc) const { return (osc == 0 ? effPosA : effPosB).load(); }

    void  setUnison (int n)           { unison = juce::jlimit (1, WavetableVoice::MaxUnison, n); }
    void  setUnisonDetune (float c)   { unisonDetune = juce::jlimit (0.0f, 50.0f, c); }
    void  setSubLevel (float v)       { subLevel = juce::jlimit (0.0f, 1.0f, v); }
    void  setNoiseLevel (float v)     { noiseLevel = juce::jlimit (0.0f, 1.0f, v); }
    void  setDrive (float v)          { drive = juce::jlimit (0.0f, 1.0f, v); }
    void  setGain (float v)           { gain = juce::jlimit (0.01f, 0.4f, v); }
    int   getUnison() const           { return unison; }
    float getUnisonDetune() const     { return unisonDetune; }
    float getSubLevel() const         { return subLevel; }
    float getNoiseLevel() const       { return noiseLevel; }
    float getDrive() const            { return drive; }
    float getGain() const             { return gain; }

    // POLY / MONO / LEGATO, and GLIDE (portamento) time in seconds.
    void  setVoiceMode (int m)
    {
        m = juce::jlimit (0, 2, m);
        if (m != voiceMode) killAll.store (true);   // a mode change drops what's held
        voiceMode = m;
    }
    void  setGlide (float s)          { glide = juce::jlimit (0.0f, 2.0f, s); }
    int   getVoiceMode() const        { return voiceMode; }
    float getGlide() const            { return glide; }

    //==========================================================================
    // Filter + envelopes. ENV is -1..1 (±WavetableVoice::EnvOctaves at the
    // envelope's peak); VEL and KEY are 0..1.
    void  setFilterType (int t)         { filterType = juce::jlimit (0, SynthFilter::NumTypes - 1, t); }
    void  setFilterEnabled (bool on)    { filterOn = on; }
    void  setFilterCutoff (float hz)    { filterCutoff = juce::jlimit (20.0f, 20000.0f, hz); }
    void  setFilterResonance (float q)  { filterReso = juce::jlimit (0.0f, SynthFilter::MaxReso, q); }
    void  setFilterEnvAmount (float a)  { filterEnvAmount = juce::jlimit (-1.0f, 1.0f, a); }
    void  setFilterVelocity (float v)   { filterVel = juce::jlimit (0.0f, 1.0f, v); }
    void  setFilterKeytrack (float k)   { filterKey = juce::jlimit (0.0f, 1.0f, k); }
    void  setEnvToPosition (float a)    { envToPos = juce::jlimit (-1.0f, 1.0f, a); }
    int   getFilterType() const         { return filterType; }
    bool  isFilterEnabled() const       { return filterOn; }
    float getFilterCutoff() const       { return filterCutoff; }
    float getFilterResonance() const    { return filterReso; }
    float getFilterEnvAmount() const    { return filterEnvAmount; }
    float getFilterVelocity() const     { return filterVel; }
    float getFilterKeytrack() const     { return filterKey; }
    float getEnvToPosition() const      { return envToPos; }

    // FILTER 2: its own type, cutoff, resonance and ENV amount (from ENV 2);
    // VEL, KEY and DRIVE are shared with filter 1. ROUTING is serial or
    // parallel when both are on; INPUTS says which sources go through them.
    void  setFilter2Enabled (bool on)   { filter2On = on; }
    void  setFilter2Type (int t)        { filter2Type = juce::jlimit (0, SynthFilter::NumTypes - 1, t); }
    void  setFilter2Cutoff (float hz)   { filter2Cutoff = juce::jlimit (20.0f, 20000.0f, hz); }
    void  setFilter2Resonance (float q) { filter2Reso = juce::jlimit (0.0f, SynthFilter::MaxReso, q); }
    void  setFilter2EnvAmount (float a) { filter2EnvAmount = juce::jlimit (-1.0f, 1.0f, a); }
    void  setFilterRouting (int r)      { filterRouting = juce::jlimit (0, 1, r); }
    void  setFilterInputs (int mask)    { filterInputs = mask & FilterAll; }
    bool  isFilter2Enabled() const      { return filter2On; }
    int   getFilter2Type() const        { return filter2Type; }
    float getFilter2Cutoff() const      { return filter2Cutoff; }
    float getFilter2Resonance() const   { return filter2Reso; }
    float getFilter2EnvAmount() const   { return filter2EnvAmount; }
    int   getFilterRouting() const      { return filterRouting; }
    int   getFilterInputs() const       { return filterInputs; }

    // Either filter by index (0 or 1), for the FILTER tab.
    bool  isFilterOn (int f) const          { return f == 0 ? filterOn : filter2On; }
    int   filterTypeOf (int f) const        { return f == 0 ? filterType : filter2Type; }
    float filterCutoffOf (int f) const      { return f == 0 ? filterCutoff : filter2Cutoff; }
    float filterResoOf (int f) const        { return f == 0 ? filterReso : filter2Reso; }
    float filterEnvOf (int f) const         { return f == 0 ? filterEnvAmount : filter2EnvAmount; }
    void  setFilterOn (int f, bool on)      { if (f == 0) setFilterEnabled (on); else setFilter2Enabled (on); }
    void  setFilterTypeOf (int f, int t)    { if (f == 0) setFilterType (t); else setFilter2Type (t); }
    void  setFilterCutoffOf (int f, float v){ if (f == 0) setFilterCutoff (v); else setFilter2Cutoff (v); }
    void  setFilterResoOf (int f, float v)  { if (f == 0) setFilterResonance (v); else setFilter2Resonance (v); }
    void  setFilterEnvOf (int f, float v)   { if (f == 0) setFilterEnvAmount (v); else setFilter2EnvAmount (v); }

    // Where a filter is right now on the most recent sounding note, for the
    // filter display; negative when nothing is playing.
    float getLiveCutoff (int filter = 0) const { return (filter == 0 ? liveCutoff : liveCutoff2).load(); }

    // ENV 1 is the amp, ENV 2 the filter (and ENV>POS), ENV 3 free.
    void setAmpADSR (float a, float d, float s, float r)    { ampA = a; ampD = d; ampS = s; ampR = r; adsrDirty = true; }
    void setFilterADSR (float a, float d, float s, float r) { filA = a; filD = d; filS = s; filR = r; adsrDirty = true; }
    void setEnv3ADSR (float a, float d, float s, float r)   { e3A = a; e3D = d; e3S = s; e3R = r; adsrDirty = true; }
    void getAmpADSR (float& a, float& d, float& s, float& r) const    { a = ampA; d = ampD; s = ampS; r = ampR; }
    void getFilterADSR (float& a, float& d, float& s, float& r) const { a = filA; d = filD; s = filS; r = filR; }
    void getEnv3ADSR (float& a, float& d, float& s, float& r) const   { a = e3A; d = e3D; s = e3S; r = e3R; }

    // envIndex 0..2 → the three above.
    void setEnvADSR (int envIndex, float a, float d, float s, float r)
    {
        if (envIndex == 0) setAmpADSR (a, d, s, r);
        else if (envIndex == 1) setFilterADSR (a, d, s, r);
        else setEnv3ADSR (a, d, s, r);
    }
    void getEnvADSR (int envIndex, float& a, float& d, float& s, float& r) const
    {
        if (envIndex == 0) getAmpADSR (a, d, s, r);
        else if (envIndex == 1) getFilterADSR (a, d, s, r);
        else getEnv3ADSR (a, d, s, r);
    }

    //==========================================================================
    // LFOs 1–4. The UI edits them in place (SynthMod::Lfo's settings are plain
    // values; its shape swaps atomically).
    SynthMod::Lfo& getLfo (int k) { return lfos[(size_t) juce::jlimit (0, NumLfos - 1, k)]; }
    const SynthMod::Lfo& getLfo (int k) const { return lfos[(size_t) juce::jlimit (0, NumLfos - 1, k)]; }

    // The old single-LFO API, for LFO 1.
    void  setLFORate (float hz)  { lfos[0].rate = juce::jlimit (0.01f, 40.0f, hz); }
    void  setLFODepth (float d)  { lfos[0].depth = juce::jlimit (0.0f, 1.0f, d); }
    void  setLFOWaveform (int w) { lfos[0].setShape (SynthMod::preset (juce::jlimit (0, NumLFOWaves - 1, w))); }
    float getLFORate() const     { return lfos[0].rate; }
    float getLFODepth() const    { return lfos[0].depth; }

    // Where each LFO is now (on the newest note for per-note LFOs), -1..1, for
    // the LFO display.
    float getLiveLfo (int k) const { return liveLfo[(size_t) juce::jlimit (0, NumLfos - 1, k)].load(); }
    float getLiveLfoPhase (int k) const { return liveLfoPhase[(size_t) juce::jlimit (0, NumLfos - 1, k)].load(); }

    // Macros 1–4: 0..1 knobs that are mod sources.
    void  setMacro (int k, float v)  { macros[(size_t) juce::jlimit (0, NumMacros - 1, k)] = juce::jlimit (0.0f, 1.0f, v); }
    float getMacro (int k) const     { return macros[(size_t) juce::jlimit (0, NumMacros - 1, k)]; }
    void  setMacroName (int k, const juce::String& n) { macroNames[(size_t) juce::jlimit (0, NumMacros - 1, k)] = n; }
    juce::String getMacroName (int k) const { return macroNames[(size_t) juce::jlimit (0, NumMacros - 1, k)]; }

    //==========================================================================
    // The mod matrix.
    void setRoutingSource (int slot, int s) { if (validSlot (slot)) routings[slot].source = juce::jlimit (0, NumModSources - 1, s); }
    void setRoutingDest   (int slot, int d) { if (validSlot (slot)) routings[slot].dest   = juce::jlimit (0, NumModDests - 1, d); }
    void setRoutingAmount (int slot, float a) { if (validSlot (slot)) routings[slot].amount = juce::jlimit (-1.0f, 1.0f, a); }
    void setRoutingAux    (int slot, int s) { if (validSlot (slot)) routings[slot].aux    = juce::jlimit (0, NumModSources - 1, s); }
    void setRoutingCurve  (int slot, int c) { if (validSlot (slot)) routings[slot].curve  = juce::jlimit (0, NumCurves - 1, c); }
    void setRoutingEnabled (int slot, bool on) { if (validSlot (slot)) routings[slot].enabled = on; }
    void clearRouting (int slot)            { if (validSlot (slot)) routings[slot] = {}; }
    int   getRoutingSource (int slot) const { return validSlot (slot) ? routings[slot].source : 0; }
    int   getRoutingDest   (int slot) const { return validSlot (slot) ? routings[slot].dest   : 0; }
    float getRoutingAmount (int slot) const { return validSlot (slot) ? routings[slot].amount : 0.0f; }
    int   getRoutingAux    (int slot) const { return validSlot (slot) ? routings[slot].aux    : 0; }
    int   getRoutingCurve  (int slot) const { return validSlot (slot) ? routings[slot].curve  : 0; }
    bool  isRoutingEnabled (int slot) const { return validSlot (slot) && routings[slot].enabled; }
    const Route& getRouting (int slot) const { return routings[(size_t) juce::jlimit (0, NumRoutingSlots - 1, slot)]; }

    // The slot routing `source` to `dest`, or -1.
    int findRouting (int source, int dest) const
    {
        for (int i = 0; i < NumRoutingSlots; ++i)
            if (routings[i].source == source && routings[i].dest == dest)
                return i;
        return -1;
    }

    // An empty slot, or -1 when all 16 are in use.
    int freeRoutingSlot() const
    {
        for (int i = 0; i < NumRoutingSlots; ++i)
            if (routings[i].source == ModSrcOff || routings[i].dest == ModDestOff)
                return i;
        return -1;
    }

    // What the matrix is adding to a destination right now, on the newest
    // note (in matrix units, -1..1-ish), for the knobs' live dots.
    float getLiveMod (int dest) const { return juce::isPositiveAndBelow (dest, (int) NumModDests) ? liveMods[(size_t) dest].load() : 0.0f; }

    //==========================================================================
    FxRack& getFx() noexcept { return fx; }

    void resetToDefaults()
    {
        setBuiltInTable (0, WavetableLibrary::indexForName ("Morph"));
        setBuiltInTable (1, WavetableLibrary::indexForName ("PWM"));
        setOscLevel (0, 1.0f);  setOscPosition (0, 0.6f);
        setOscLevel (1, 0.0f);  setOscPosition (1, 0.0f);
        setOscSemitones (0, 0); setOscFineCents (0, 0.0f);
        setOscSemitones (1, 0); setOscFineCents (1, 7.0f);
        for (int o = 0; o < 2; ++o) { setOscWarp (o, WarpOff); setOscWarpAmount (o, 0.0f); }
        setUnison (3); setUnisonDetune (12.0f);
        setSubLevel (0.0f); setNoiseLevel (0.0f); setDrive (0.0f); setGain (0.15f);
        setVoiceMode (Poly); setGlide (0.0f);

        setFilterType (SynthFilter::Ladder); setFilterEnabled (true);
        setFilterCutoff (1200.0f); setFilterResonance (0.8f); setFilterEnvAmount (0.35f);
        setFilterVelocity (0.5f); setFilterKeytrack (0.0f);
        setFilter2Enabled (false); setFilter2Type (SynthFilter::LP12); setFilter2Cutoff (2000.0f);
        setFilter2Resonance (0.5f); setFilter2EnvAmount (0.0f);
        setFilterRouting (FiltersSerial); setFilterInputs (FilterAll);
        setEnvToPosition (0.0f);
        setAmpADSR (0.01f, 0.4f, 0.7f, 0.6f);
        setFilterADSR (0.01f, 0.5f, 0.3f, 0.5f);
        setEnv3ADSR (0.01f, 0.5f, 0.5f, 0.5f);

        for (auto& l : lfos) l.reset();
        lfos[0].rate = 0.3f;
        for (int k = 0; k < NumMacros; ++k) { macros[(size_t) k] = 0.0f; macroNames[(size_t) k] = "Macro " + juce::String (k + 1); }

        fx.clear();
        fx.add ("delay");
        if (auto* d = fx.get (0)) d->setValue ("mix", 0.15f);
        fx.add ("reverb");

        // Factory routing: tilt scans the table — pick up the phone and the
        // timbre moves. Mod wheel still opens the filter.
        for (int i = 0; i < NumRoutingSlots; ++i)
            routings[i] = {};
        routings[0] = { ModSrcAccelY,   ModDestPosA,   0.5f };
        routings[1] = { ModSrcModWheel, ModDestCutoff, 0.5f };
    }

    //==========================================================================
    // The whole patch as one ValueTree — what presets, projects and autosave
    // all store. Property names are the old "wt_*" settings keys without the
    // prefix, so a patch saved key-by-key converts by just stripping it.
    //
    // Version 2 moved the filter to octaves (ENV is -1..1, not Hz) and the FX
    // into a rack (an FxRack child). Version 1 patches are converted on load.
    // Version 3 added the LFOs, ENV 3, macros, the 16-slot matrix, voice mode,
    // glide, noise and warp; anything of those missing from a patch takes its
    // default, so an older patch loads as it sounded.
    static inline const juce::Identifier stateType { "Wavetable" };
    static constexpr int StateVersion = 3;

    juce::ValueTree getState() const override
    {
        juce::ValueTree t (stateType);
        t.setProperty ("version", StateVersion, nullptr);

        for (int o = 0; o < 2; ++o)
        {
            const juce::String p = "osc" + juce::String (o) + "_";
            const int builtIn = getBuiltInIndex (o);
            t.setProperty (p + "tableName", builtIn >= 0 ? WavetableLibrary::builtInNames()[builtIn] : juce::String(), nullptr);
            t.setProperty (p + "file",  getImportedFile (o).getFullPathName(), nullptr);
            t.setProperty (p + "level", getOscLevel (o), nullptr);
            t.setProperty (p + "pos",   getOscPosition (o), nullptr);
            t.setProperty (p + "semi",  getOscSemitones (o), nullptr);
            t.setProperty (p + "fine",  getOscFineCents (o), nullptr);
            t.setProperty (p + "warp",  getOscWarp (o), nullptr);
            t.setProperty (p + "warpAmt", getOscWarpAmount (o), nullptr);
        }
        t.setProperty ("unison",  unison, nullptr);
        t.setProperty ("detune",  unisonDetune, nullptr);
        t.setProperty ("sub",     subLevel, nullptr);
        t.setProperty ("noise",   noiseLevel, nullptr);
        t.setProperty ("drive",   drive, nullptr);
        t.setProperty ("gain",    gain, nullptr);
        t.setProperty ("voiceMode", voiceMode, nullptr);
        t.setProperty ("glide",   glide, nullptr);
        t.setProperty ("filtType", filterType, nullptr);
        t.setProperty ("filtOn",  filterOn, nullptr);
        t.setProperty ("cutoff",  filterCutoff, nullptr);
        t.setProperty ("reso",    filterReso, nullptr);
        t.setProperty ("filtEnv", filterEnvAmount, nullptr);
        t.setProperty ("filtVel", filterVel, nullptr);
        t.setProperty ("filtKey", filterKey, nullptr);
        t.setProperty ("filt2On",   filter2On, nullptr);
        t.setProperty ("filt2Type", filter2Type, nullptr);
        t.setProperty ("cutoff2",   filter2Cutoff, nullptr);
        t.setProperty ("reso2",     filter2Reso, nullptr);
        t.setProperty ("filt2Env",  filter2EnvAmount, nullptr);
        t.setProperty ("filtRoute", filterRouting, nullptr);
        t.setProperty ("filtIn",    filterInputs, nullptr);
        t.setProperty ("envPos",  envToPos, nullptr);

        auto adsr = [] (float a, float d, float s, float r)
        {
            return juce::String (a) + "," + juce::String (d) + "," + juce::String (s) + "," + juce::String (r);
        };
        t.setProperty ("amp",  adsr (ampA, ampD, ampS, ampR), nullptr);
        t.setProperty ("fenv", adsr (filA, filD, filS, filR), nullptr);
        t.setProperty ("env3", adsr (e3A, e3D, e3S, e3R), nullptr);

        for (int k = 0; k < NumLfos; ++k)
        {
            const auto& l = lfos[(size_t) k];
            const juce::String p = "lfo" + juce::String (k) + "_";
            t.setProperty (p + "shape", SynthMod::toString (l.getShape()), nullptr);
            t.setProperty (p + "rate",  l.rate, nullptr);
            t.setProperty (p + "sync",  l.sync, nullptr);
            t.setProperty (p + "div",   l.division, nullptr);
            t.setProperty (p + "mode",  l.mode, nullptr);
            t.setProperty (p + "rise",  l.rise, nullptr);
            t.setProperty (p + "delay", l.delay, nullptr);
            t.setProperty (p + "phase", l.phase, nullptr);
            t.setProperty (p + "depth", l.depth, nullptr);
        }
        for (int k = 0; k < NumMacros; ++k)
        {
            t.setProperty ("macro" + juce::String (k), macros[(size_t) k], nullptr);
            t.setProperty ("macroName" + juce::String (k), macroNames[(size_t) k], nullptr);
        }

        for (int i = 0; i < NumRoutingSlots; ++i)
        {
            const auto& r = routings[i];
            if (r.source == ModSrcOff && r.dest == ModDestOff) continue;
            const juce::String n (i);
            t.setProperty ("routeSrc_" + n,    r.source, nullptr);
            t.setProperty ("routeDest_" + n,   r.dest, nullptr);
            t.setProperty ("routeAmount_" + n, r.amount, nullptr);
            t.setProperty ("routeAux_" + n,    r.aux, nullptr);
            t.setProperty ("routeCurve_" + n,  r.curve, nullptr);
            t.setProperty ("routeOn_" + n,     r.enabled, nullptr);
        }

        t.appendChild (fx.getState(), nullptr);
        return t;
    }

    // Values may arrive as strings (a converted settings file), so everything
    // goes through var's numeric conversions rather than assuming a type.
    // Properties from before version 3 keep their current value when missing;
    // the version 3 ones (and empty matrix slots) take their defaults.
    void setState (const juce::ValueTree& t) override
    {
        auto f = [&t] (const juce::String& id, float cur) { return (float) (double) t.getProperty (id, cur); };
        auto i = [&t] (const juce::String& id, int cur)   { return (int) t.getProperty (id, cur); };
        auto b = [&t] (const juce::String& id, bool cur)  { return (bool) t.getProperty (id, cur); };
        const int version = (int) t.getProperty ("version", 1);
        const bool legacy = version < 2;

        for (int o = 0; o < 2; ++o)
        {
            const juce::String p = "osc" + juce::String (o) + "_";
            if (t.hasProperty (p + "tableName") || t.hasProperty (p + "file"))
            {
                const int table = WavetableLibrary::indexForName (t[p + "tableName"].toString());
                const juce::File file (t[p + "file"].toString());
                juce::String error;
                if (table >= 0)
                    setBuiltInTable (o, table);
                else if (! file.existsAsFile() || ! importTable (o, file, error))
                    setBuiltInTable (o, WavetableLibrary::indexForName ("Morph")); // imported file has gone
            }
            setOscLevel (o,     f (p + "level", getOscLevel (o)));
            setOscPosition (o,  f (p + "pos",   getOscPosition (o)));
            setOscSemitones (o, i (p + "semi",  getOscSemitones (o)));
            setOscFineCents (o, f (p + "fine",  getOscFineCents (o)));
            setOscWarp (o,       i (p + "warp",    WarpOff));
            setOscWarpAmount (o, f (p + "warpAmt", 0.0f));
        }
        setUnison          (i ("unison",  unison));
        setUnisonDetune    (f ("detune",  unisonDetune));
        setSubLevel        (f ("sub",     subLevel));
        setNoiseLevel      (f ("noise",   0.0f));
        setDrive           (f ("drive",   drive));
        setGain            (f ("gain",    gain));
        setVoiceMode       (i ("voiceMode", Poly));
        setGlide           (f ("glide",   0.0f));
        setFilterCutoff    (f ("cutoff",  filterCutoff));
        setFilterResonance (f ("reso",    filterReso));
        setEnvToPosition   (f ("envPos",  envToPos));

        if (legacy)
        {
            // The ladder, no velocity or keytracking: the filter as it was.
            // ENV was Hz added at the envelope's peak; the octaves that reach
            // the same peak give the same sweep.
            setFilterType (SynthFilter::Ladder);
            setFilterEnabled (true);
            setFilterVelocity (0.0f);
            setFilterKeytrack (0.0f);
            if (t.hasProperty ("filtEnv"))
                setFilterEnvAmount (std::log2 (1.0f + juce::jmax (0.0f, f ("filtEnv", 0.0f)) / filterCutoff)
                                    / WavetableVoice::EnvOctaves);
        }
        else
        {
            setFilterType      (i ("filtType", filterType));
            setFilterEnabled   (b ("filtOn",   filterOn));
            setFilterEnvAmount (f ("filtEnv",  filterEnvAmount));
            setFilterVelocity  (f ("filtVel",  filterVel));
            setFilterKeytrack  (f ("filtKey",  filterKey));
        }

        setFilter2Enabled   (b ("filt2On",   false));
        setFilter2Type      (i ("filt2Type", SynthFilter::LP12));
        setFilter2Cutoff    (f ("cutoff2",   2000.0f));
        setFilter2Resonance (f ("reso2",     0.5f));
        setFilter2EnvAmount (f ("filt2Env",  0.0f));
        setFilterRouting    (i ("filtRoute", FiltersSerial));
        setFilterInputs     (i ("filtIn",    FilterAll));

        auto adsr = [&t] (const char* id, float* out)
        {
            auto parts = juce::StringArray::fromTokens (t[id].toString(), ",", "");
            if (parts.size() != 4) return false;
            for (int k = 0; k < 4; ++k) out[k] = parts[k].getFloatValue();
            return true;
        };
        float v[4];
        if (adsr ("amp", v))  setAmpADSR (v[0], v[1], v[2], v[3]);
        if (adsr ("fenv", v)) setFilterADSR (v[0], v[1], v[2], v[3]);
        if (adsr ("env3", v)) setEnv3ADSR (v[0], v[1], v[2], v[3]);
        else                  setEnv3ADSR (0.01f, 0.5f, 0.5f, 0.5f);

        // LFOs. A patch from before version 3 has one LFO: its old keys.
        for (int k = 0; k < NumLfos; ++k)
        {
            auto& l = lfos[(size_t) k];
            const juce::String p = "lfo" + juce::String (k) + "_";
            l.reset();
            if (t.hasProperty (p + "shape"))
            {
                l.setShape (SynthMod::fromString (t[p + "shape"].toString()));
                l.rate     = juce::jlimit (0.01f, 40.0f, f (p + "rate", 1.0f));
                l.sync     = b (p + "sync", false);
                l.division = juce::jlimit (0, SynthMod::divisionNames().size() - 1, i (p + "div", 8));
                l.mode     = juce::jlimit (0, 2, i (p + "mode", SynthMod::Free));
                l.rise     = juce::jlimit (0.0f, 8.0f, f (p + "rise", 0.0f));
                l.delay    = juce::jlimit (0.0f, 8.0f, f (p + "delay", 0.0f));
                l.phase    = juce::jlimit (0.0f, 1.0f, f (p + "phase", 0.0f));
                l.depth    = juce::jlimit (0.0f, 1.0f, f (p + "depth", 1.0f));
            }
            else if (k == 0 && t.hasProperty ("lfoWave"))
            {
                l.setShape (SynthMod::preset (juce::jlimit (0, NumLFOWaves - 1, i ("lfoWave", 0))));
                l.rate  = juce::jlimit (0.01f, 40.0f, f ("lfoRate", 0.3f));
                l.depth = juce::jlimit (0.0f, 1.0f, f ("lfoDepth", 0.0f));
            }
        }
        for (int k = 0; k < NumMacros; ++k)
        {
            macros[(size_t) k] = juce::jlimit (0.0f, 1.0f, f ("macro" + juce::String (k), 0.0f));
            macroNames[(size_t) k] = t.getProperty ("macroName" + juce::String (k), "Macro " + juce::String (k + 1)).toString();
        }

        for (int s = 0; s < NumRoutingSlots; ++s)
        {
            const juce::String n (s);
            routings[s] = {};
            if (! t.hasProperty ("routeSrc_" + n) && ! t.hasProperty ("routeDest_" + n))
                continue;
            setRoutingSource  (s, i ("routeSrc_" + n,    0));
            setRoutingDest    (s, i ("routeDest_" + n,   0));
            setRoutingAmount  (s, f ("routeAmount_" + n, 0.0f));
            setRoutingAux     (s, i ("routeAux_" + n,    0));
            setRoutingCurve   (s, i ("routeCurve_" + n,  0));
            setRoutingEnabled (s, b ("routeOn_" + n,     true));

            // A route to the cutoff was ±5000 Hz at full depth. Convert it
            // to the octaves that move the cutoff as far for a source at 0.8
            // (velocity 100, a wheel most of the way up).
            if (legacy && routings[s].dest == ModDestCutoff)
            {
                constexpr float typical = 0.8f;
                const float a = routings[s].amount;
                const float octaves = std::log2 (1.0f + std::abs (a) * typical * 5000.0f / filterCutoff);
                setRoutingAmount (s, std::copysign (octaves / (typical * WavetableVoice::MatrixCutoffOctaves), a));
            }
        }

        auto rack = t.getChildWithName (FxRack::stateType);
        if (rack.isValid())
            fx.setState (rack);
        else if (t.hasProperty ("distOn") || t.hasProperty ("delayOn") || t.hasProperty ("revOn"))
            fx.setState (rackFromBus (t));
    }

    // The old fixed bus (distortion → delay → reverb, each switchable) as a
    // rack of the modules that were switched on, set the same.
    static juce::ValueTree rackFromBus (const juce::ValueTree& t)
    {
        auto f = [&t] (const char* id, double def) { return (double) t.getProperty (id, def); };
        juce::ValueTree rack (FxRack::stateType);
        auto add = [&rack] (const char* type, std::initializer_list<std::pair<const char*, double>> values)
        {
            juce::ValueTree u (FxRack::unitType);
            u.setProperty ("type", type, nullptr);
            u.setProperty ("on", true, nullptr);
            u.setProperty ("mode", 0, nullptr);   // SOFT / MONO: the old bus's curve and delay
            for (auto& [id, v] : values)
                u.setProperty (id, v, nullptr);
            rack.appendChild (u, nullptr);
        };

        if ((bool) t.getProperty ("distOn", true))
            add ("distortion", { { "drive", f ("distDrive", 0.0) }, { "tone", 1.0 }, { "mix", 1.0 } });
        if ((bool) t.getProperty ("delayOn", true))
            add ("delay", { { "time", f ("delayTime", 350.0) }, { "fb", f ("delayFb", 0.4) },
                            { "tone", 1.0 }, { "mix", f ("delayMix", 0.18) } });
        if ((bool) t.getProperty ("revOn", true))
            add ("reverb", { { "wet", f ("revMix", 0.2) }, { "size", f ("revSize", 0.5) },
                             { "damp", 0.4 }, { "width", 1.0 } });
        return rack;
    }

private:
    struct OscState
    {
        float level = 1.0f, position = 0.0f, fine = 0.0f;
        int   semis = 0;
        int   warp = WarpOff;
        float warpAmount = 0.0f;
        int   builtInIndex = 0;
        juce::File importedFile;
    };

    OscState&       oscState (int osc)       { return osc == 0 ? oscStateA : oscStateB; }
    const OscState& oscState (int osc) const { return osc == 0 ? oscStateA : oscStateB; }
    static bool validSlot (int s) { return s >= 0 && s < NumRoutingSlots; }

    // Tables are shared between synths (see WavetableLibrary::shared), so a
    // table retired here may still be playing in another engine — that one
    // holds its own reference. The audio thread reads the raw pointer.
    void installTable (int osc, std::shared_ptr<const Wavetable> table)
    {
        const auto now = juce::Time::getMillisecondCounter();

        // Free retired tables nobody can still be rendering from. A block is
        // a few ms; half a second is a very wide margin.
        retired.erase (std::remove_if (retired.begin(), retired.end(),
                                       [now] (const Retired& r) { return now - r.when > 500; }),
                       retired.end());

        auto& slot  = osc == 0 ? ownedA : ownedB;
        auto& atom  = osc == 0 ? tableA : tableB;
        atom.store (table.get());
        if (slot != nullptr)
            retired.push_back ({ std::move (slot), now });
        slot = std::move (table);
    }

    // Once per block, before the voices render: the shared sources, the
    // block's parameters, and the matrix routes to the FX.
    void processModulation (int startSample, int numSamples)
    {
        if (killAll.exchange (false))
            synth.allNotesOff (0, false);
        synth.voiceMode = voiceMode;

        if (adsrDirty.exchange (false))
        {
            juce::ADSR::Parameters amp { ampA, ampD, ampS, ampR };
            juce::ADSR::Parameters fil { filA, filD, filS, filR };
            juce::ADSR::Parameters e3  { e3A, e3D, e3S, e3R };
            for (int i = 0; i < synth.getNumVoices(); ++i)
                if (auto* v = dynamic_cast<WavetableVoice*> (synth.getVoice (i)))
                {
                    v->setAmpADSR (amp);
                    v->setFilterADSR (fil);
                    v->setEnv3ADSR (e3);
                }
        }

        // Shared sources. The per-note ones come from the newest note, for
        // the FX routes below.
        auto& g = block.global;
        for (auto& s : g) s = 0.0f;
        g[ModSrcModWheel] = modWheel.load();
        g[ModSrcAccelX]   = accelX.load();
        g[ModSrcAccelY]   = accelY.load();
        for (int k = 0; k < NumMacros; ++k)
            g[ModSrcMacro1 + k] = macros[(size_t) k];

        float fxSrc[NumModSources];
        const float* newest = newestVoice != nullptr && newestVoice->isVoiceActive() ? newestVoice->getLastSources() : nullptr;
        for (int s = 0; s < NumModSources; ++s)
            fxSrc[s] = isGlobalSource (s) || newest == nullptr ? g[s] : newest[s];
        if (newest == nullptr) fxSrc[ModSrcVelocity] = lastVelocity.load();

        // Routes, packed: only the ones doing something.
        block.numRoutes = 0;
        float fxMod[NumModDests] = { 0 };
        for (const auto& r : routings)
        {
            if (! r.isActive()) continue;
            block.routes[(size_t) block.numRoutes++] = r;
            float v = shapeSource (fxSrc[r.source], r.curve) * r.amount;
            if (r.aux != ModSrcOff) v *= fxSrc[r.aux];
            fxMod[r.dest] += v;
        }

        // LFOs: Free ones run here, shared by every voice; Retrig and Env ones
        // run in each voice from its note-on.
        const double bpm = tempo.load();
        for (int k = 0; k < NumLfos; ++k)
        {
            auto& l = lfos[(size_t) k];
            auto& lb = block.lfo[k];
            lb.lfo = &l;
            lb.hz = l.hz (bpm);
            lb.mode = l.mode; lb.rise = l.rise; lb.delay = l.delay; lb.phase = l.phase; lb.depth = l.depth;
            lb.freePhase = freePhase[k];
            freePhase[k] += lb.hz * std::exp2 (fxMod[ModDestLFO1Rate + k] * 4.0f) * numSamples / currentSampleRate;
            freePhase[k] -= std::floor (freePhase[k]);
        }

        auto fill = [] (WtBlockParams::Osc& o, const Wavetable* table, const OscState& s)
        {
            o.table = table;
            o.level = s.level;
            o.position = s.position;
            o.semis = (float) s.semis + s.fine / 100.0f;
            o.warp = s.warp;
            o.warpAmount = s.warpAmount;
        };
        fill (block.osc[0], tableA.load(), oscStateA);
        fill (block.osc[1], tableB.load(), oscStateB);
        block.sub = subLevel; block.noise = noiseLevel;
        block.unison = unison; block.detune = unisonDetune;
        block.filterType = filterType; block.filterOn = filterOn;
        block.cutoff = filterCutoff; block.reso = filterReso; block.envAmount = filterEnvAmount;
        block.filterVel = filterVel; block.keytrack = filterKey;
        block.filter2On = filter2On; block.filter2Type = filter2Type;
        block.cutoff2 = filter2Cutoff; block.reso2 = filter2Reso; block.envAmount2 = filter2EnvAmount;
        block.filterRouting = filterRouting; block.filterInputs = filterInputs;
        block.envToPos = envToPos; block.drive = drive; block.gain = gain;
        block.glide = glide;
        block.sampleRate = currentSampleRate;
        block.blockStart = startSample;

        // Delay time: ±1000 ms at full depth, same as the classic engine.
        FxUnit::MatrixMods fxMods;
        fxMods.distDrive   = fxMod[ModDestDistDrive];
        fxMods.delayMix    = fxMod[ModDestDelayMix];
        fxMods.delayTimeMs = fxMod[ModDestDelayTime] * 1000.0f;
        fxMods.reverbWet   = fxMod[ModDestReverbMix];
        fx.setModulated (fxMods);
        fx.setTempo (bpm);
    }

    // After the voices have rendered: what the newest sounding note is
    // doing, for the displays and next block's FX routes.
    void publishLiveState()
    {
        for (int i = synth.getNumVoices(); --i >= 0;)
            if (auto* v = dynamic_cast<WavetableVoice*> (synth.getVoice (i)))
                if (v->isVoiceActive()
                    && (newestVoice == nullptr || ! newestVoice->isVoiceActive() || newestVoice->wasStartedBefore (*v)))
                    newestVoice = v;

        const bool playing = newestVoice != nullptr && newestVoice->isVoiceActive();
        liveCutoff.store (playing ? newestVoice->getCurrentCutoff (0) : -1.0f);
        liveCutoff2.store (playing ? newestVoice->getCurrentCutoff (1) : -1.0f);
        effPosA.store (playing ? newestVoice->getPosition (0) : oscStateA.position);
        effPosB.store (playing ? newestVoice->getPosition (1) : oscStateB.position);
        for (int d = 0; d < NumModDests; ++d)
            liveMods[(size_t) d].store (playing ? newestVoice->getLastMods()[d] : 0.0f);

        for (int k = 0; k < NumLfos; ++k)
        {
            const auto& lb = block.lfo[k];
            if (playing)
                liveLfo[(size_t) k].store (newestVoice->getLastSources()[ModSrcLFO + k]);
            else if (lb.lfo != nullptr)
                liveLfo[(size_t) k].store (lb.mode == SynthMod::Free ? lb.lfo->read (freePhase[k]) * lb.depth : 0.0f);
            liveLfoPhase[(size_t) k].store (lb.mode == SynthMod::Free ? (float) freePhase[k] : -1.0f);
        }
    }

    //==========================================================================
    struct MidiEvent { int note, velocity, channel; bool isNoteOn; };

    struct LockFreeFifo
    {
        static constexpr int capacity = 1024;
        MidiEvent buffer[capacity];
        std::atomic<int> readPos { 0 }, writePos { 0 };

        bool try_dequeue (MidiEvent& e)
        {
            int r = readPos.load (std::memory_order_relaxed);
            if (r == writePos.load (std::memory_order_acquire)) return false;
            e = buffer[r];
            readPos.store ((r + 1) % capacity, std::memory_order_release);
            return true;
        }

        void enqueue (MidiEvent e)
        {
            int w = writePos.load (std::memory_order_relaxed);
            buffer[w] = e;
            writePos.store ((w + 1) % capacity, std::memory_order_release);
        }
    };

    struct Retired { std::shared_ptr<const Wavetable> table; juce::uint32 when; };

    LockFreeFifo midiQueue;
    juce::MidiBuffer audioThreadMidi;
    WavetableSynth synth;
    WtBlockParams block;
    FxRack fx;
    double currentSampleRate = 44100.0;
    std::atomic<double> tempo { 120.0 };

    std::shared_ptr<const Wavetable> ownedA, ownedB;
    std::atomic<const Wavetable*> tableA { nullptr }, tableB { nullptr };
    std::vector<Retired> retired;

    OscState oscStateA, oscStateB;
    std::atomic<float> effPosA { 0.0f }, effPosB { 0.0f };
    int   unison = 3;
    float unisonDetune = 12.0f;
    float subLevel = 0.0f, noiseLevel = 0.0f, drive = 0.0f, gain = 0.15f;
    int   voiceMode = Poly;
    float glide = 0.0f;

    int   filterType = SynthFilter::Ladder;
    bool  filterOn = true;
    float filterCutoff = 2500.0f, filterReso = 0.8f, filterEnvAmount = 0.3f, envToPos = 0.0f;
    float filterVel = 0.5f, filterKey = 0.0f;
    bool  filter2On = false;
    int   filter2Type = SynthFilter::LP12;
    float filter2Cutoff = 2000.0f, filter2Reso = 0.5f, filter2EnvAmount = 0.0f;
    int   filterRouting = FiltersSerial, filterInputs = FilterAll;
    std::atomic<float> liveCutoff { -1.0f }, liveCutoff2 { -1.0f };
    WavetableVoice* newestVoice = nullptr;   // audio thread

    float ampA = 0.01f, ampD = 0.4f, ampS = 0.7f, ampR = 0.6f;
    float filA = 0.01f, filD = 0.5f, filS = 0.3f, filR = 0.5f;
    float e3A = 0.01f, e3D = 0.5f, e3S = 0.5f, e3R = 0.5f;
    std::atomic<bool> adsrDirty { true }, killAll { false };

    std::array<SynthMod::Lfo, NumLfos> lfos;
    double freePhase[NumLfos] {};
    std::array<std::atomic<float>, NumLfos> liveLfo {}, liveLfoPhase {};
    std::array<float, NumMacros> macros {};
    std::array<juce::String, NumMacros> macroNames;
    std::array<std::atomic<float>, NumModDests> liveMods;

    // The wheel rests at the bottom, like a real one, so a route from it does
    // nothing until it's moved.
    std::atomic<float> modWheel { 0.0f }, lastVelocity { 0.0f }, accelX { 0.0f }, accelY { 0.0f };
    Route routings[NumRoutingSlots];
};
