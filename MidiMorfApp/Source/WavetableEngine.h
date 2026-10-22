#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <memory>
#include <vector>
#include "Wavetable.h"
#include "MasterFx.h"

//==============================================================================
// WavetableVoice: two wavetable oscillators (with stereo unison) + sine sub
// → tanh drive → stereo 4-pole ladder filter → amp ADSR.
//
// The filter envelope doubles as a position envelope: ENV → POS sweeps both
// oscillators through their tables per note, the classic wavetable pluck.
class WavetableVoice : public juce::SynthesiserVoice
{
public:
    static constexpr int MaxUnison = 5;

    struct OscParams
    {
        const Wavetable* table = nullptr;
        float level    = 1.0f;
        float position = 0.0f;  // 0..1, already modulated
        float semis    = 0.0f;  // coarse + fine, in semitones
    };

    bool canPlaySound (juce::SynthesiserSound*) override { return true; }

    void startNote (int midiNoteNumber, float velocity,
                    juce::SynthesiserSound*, int) override
    {
        level = velocity;
        float driftCents = (random.nextFloat() * 2.0f - 1.0f) * 1.5f;
        frequency = juce::MidiMessage::getMidiNoteInHertz (midiNoteNumber)
                  * std::pow (2.0, driftCents / 1200.0);

        // Random start phases keep unison voices from phasing identically on
        // every note; a single voice always starts at 0 so plucks stay punchy.
        for (int u = 0; u < MaxUnison; ++u)
        {
            phaseA[u] = unison > 1 ? random.nextDouble() : 0.0;
            phaseB[u] = unison > 1 ? random.nextDouble() : 0.0;
        }
        subPhase = 0.0;

        for (auto& s : lsL) s = 0.0f;
        for (auto& s : lsR) s = 0.0f;
        ampAdsr.noteOn();
        filterAdsr.noteOn();
    }

    void stopNote (float, bool allowTailOff) override
    {
        if (allowTailOff)
        {
            ampAdsr.noteOff();
            filterAdsr.noteOff();
        }
        else
        {
            clearCurrentNote();
        }
    }

    void pitchWheelMoved (int) override {}
    void controllerMoved (int, int) override {}

    void prepareToPlay (double sampleRate)
    {
        currentSampleRate = sampleRate;
        ampAdsr.setSampleRate (sampleRate);
        filterAdsr.setSampleRate (sampleRate);
    }

    //--------------------------------------------------------------------------
    // Pushed from the engine (audio thread, once per block).
    void setBlockParams (const OscParams& a, const OscParams& b, float sub,
                         int unisonCount, float unisonDetuneCents,
                         float cutoff, float reso, float envAmount, float envToPos,
                         float drv, float gainIn, float pitchModSemis)
    {
        oscA = a; oscB = b;
        subLevel = sub;
        unison = juce::jlimit (1, MaxUnison, unisonCount);
        unisonDetune = unisonDetuneCents;
        filterCutoff = cutoff; filterReso = reso; filterEnvAmount = envAmount;
        envPosAmount = envToPos;
        drive = drv; gain = gainIn; pitchModSemi = pitchModSemis;
    }

    void setAmpADSR (const juce::ADSR::Parameters& p)    { ampAdsr.setParameters (p); }
    void setFilterADSR (const juce::ADSR::Parameters& p) { filterAdsr.setParameters (p); }

    void renderNextBlock (juce::AudioBuffer<float>& outputBuffer,
                          int startSample, int numSamples) override
    {
        if (! isVoiceActive())
            return;

        const double base = frequency * std::pow (2.0, pitchModSemi / 12.0) / currentSampleRate;
        const double incA = base * std::pow (2.0, oscA.semis / 12.0);
        const double incB = base * std::pow (2.0, oscB.semis / 12.0);
        const double incSub = base * 0.5;

        // Per-unison-voice detune ratio and pan gains. Spread is symmetric
        // around the centre pitch; outer voices go hardest left/right.
        double ratio[MaxUnison];
        float  gainL[MaxUnison], gainR[MaxUnison];
        const float unisonNorm = 1.0f / std::sqrt ((float) unison);
        for (int u = 0; u < unison; ++u)
        {
            float spread = unison > 1 ? ((float) u / (float) (unison - 1)) * 2.0f - 1.0f : 0.0f;
            ratio[u] = std::pow (2.0, (double) (spread * unisonDetune) / 1200.0);
            float pan = (spread + 1.0f) * 0.25f * juce::MathConstants<float>::pi; // 0..π/2
            gainL[u] = std::cos (pan) * juce::MathConstants<float>::sqrt2 * unisonNorm;
            gainR[u] = std::sin (pan) * juce::MathConstants<float>::sqrt2 * unisonNorm;
        }

        // One mip level per oscillator per block, chosen for its widest-
        // detuned unison voice so nothing in the stack aliases.
        const double maxRatio = ratio[unison - 1] > ratio[0] ? ratio[unison - 1] : ratio[0];
        const int levelA = Wavetable::levelForIncrement (incA * maxRatio);
        const int levelB = Wavetable::levelForIncrement (incB * maxRatio);

        const bool useA = oscA.table != nullptr && oscA.level > 0.0001f;
        const bool useB = oscB.table != nullptr && oscB.level > 0.0001f;

        float* outL = outputBuffer.getWritePointer (0);
        float* outR = outputBuffer.getNumChannels() > 1 ? outputBuffer.getWritePointer (1) : nullptr;

        const float twoPiOverSr = juce::MathConstants<float>::twoPi / (float) currentSampleRate;
        const float reso = juce::jlimit (0.0f, 4.0f, filterReso);

        for (int sample = startSample; sample < startSample + numSamples; ++sample)
        {
            const float filtEnv = filterAdsr.getNextSample();
            const float posA = juce::jlimit (0.0f, 1.0f, oscA.position + envPosAmount * filtEnv);
            const float posB = juce::jlimit (0.0f, 1.0f, oscB.position + envPosAmount * filtEnv);

            float l = 0.0f, r = 0.0f;
            for (int u = 0; u < unison; ++u)
            {
                float s = 0.0f;
                if (useA)
                {
                    s += oscA.table->read (levelA, phaseA[u], posA) * oscA.level;
                    phaseA[u] += incA * ratio[u];
                    if (phaseA[u] >= 1.0) phaseA[u] -= 1.0;
                }
                if (useB)
                {
                    s += oscB.table->read (levelB, phaseB[u], posB) * oscB.level;
                    phaseB[u] += incB * ratio[u];
                    if (phaseB[u] >= 1.0) phaseB[u] -= 1.0;
                }
                l += s * gainL[u];
                r += s * gainR[u];
            }

            if (subLevel > 0.0001f)
            {
                float sub = std::sin ((float) subPhase * juce::MathConstants<float>::twoPi) * subLevel;
                l += sub; r += sub;
                subPhase += incSub;
                if (subPhase >= 1.0) subPhase -= 1.0;
            }

            l *= level; r *= level;

            if (drive > 0.0001f)
            {
                float k = 1.0f + drive * 6.0f;
                float norm = 1.0f / std::tanh (k);
                l = std::tanh (l * k) * norm;
                r = std::tanh (r * k) * norm;
            }

            float fc = juce::jlimit (20.0f, 20000.0f, filterCutoff + filterEnvAmount * filtEnv);
            float g  = 1.0f - std::exp (-twoPiOverSr * fc);
            l = ladder (lsL, l, g, reso);
            r = ladder (lsR, r, g, reso);

            const float amp = ampAdsr.getNextSample() * gain;
            if (outR != nullptr)
            {
                outL[sample] += l * amp;
                outR[sample] += r * amp;
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
    static float ladder (float* s, float in, float g, float reso)
    {
        float x = in - reso * s[3];
        s[0] += g * (x    - s[0]);
        s[1] += g * (s[0] - s[1]);
        s[2] += g * (s[1] - s[2]);
        s[3] += g * (s[2] - s[3]);
        return s[3];
    }

    double currentSampleRate = 44100.0;
    double frequency = 440.0;
    float  level = 0.0f;
    juce::Random random;

    double phaseA[MaxUnison] {}, phaseB[MaxUnison] {};
    double subPhase = 0.0;

    OscParams oscA, oscB;
    float subLevel = 0.0f;
    int   unison = 1;
    float unisonDetune = 0.0f;
    float filterCutoff = 2000.0f, filterReso = 1.0f, filterEnvAmount = 0.0f;
    float envPosAmount = 0.0f;
    float drive = 0.0f, gain = 0.15f, pitchModSemi = 0.0f;

    float lsL[4] {}, lsR[4] {};
    juce::ADSR ampAdsr, filterAdsr;
};

//==============================================================================
class WavetableSound : public juce::SynthesiserSound
{
public:
    bool appliesToNote (int) override { return true; }
    bool appliesToChannel (int) override { return true; }
};

//==============================================================================
// WavetableEngine — second built-in synth. Same MIDI API and mod sources as
// SynthEngine (noteOn / noteOff / allNotesOff / setModWheel / setAccelX/Y)
// so the controllers drive either one through the same sink.
//
// Table swaps are lock-free: the audio thread reads a raw pointer per block;
// the UI thread swaps it atomically and keeps the old table alive for a
// while before freeing it, so a render in flight never reads freed memory.
class WavetableEngine : public juce::AudioSource
{
public:
    enum ModSource
    {
        ModSrcOff      = 0,
        ModSrcModWheel = 1,
        ModSrcVelocity = 2,
        ModSrcAccelX   = 3,
        ModSrcAccelY   = 4,
        ModSrcLFO      = 5,
        NumModSources  = 6
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
        NumModDests      = 10
    };

    enum LFOWaveform { LFOSine = 0, LFOTri = 1, LFOSaw = 2, LFOSquare = 3, NumLFOWaves = 4 };

    static constexpr int NumRoutingSlots = 4;
    static constexpr int NumVoices = 32;

    WavetableEngine()
    {
        synth.addSound (new WavetableSound());
        for (int i = 0; i < NumVoices; ++i)
            synth.addVoice (new WavetableVoice());

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
        adsrDirty = true;
    }

    void releaseResources() override {}

    void getNextAudioBlock (const juce::AudioSourceChannelInfo& info) override
    {
        info.clearActiveBufferRegion();

        juce::MidiBuffer midi;
        MidiEvent e;
        while (midiQueue.try_dequeue (e))
            midi.addEvent (e.isNoteOn ? juce::MidiMessage::noteOn (e.channel, e.note, (juce::uint8) e.velocity)
                                      : juce::MidiMessage::noteOff (e.channel, e.note), 0);

        processModulation (info.numSamples);
        synth.renderNextBlock (*info.buffer, midi, info.startSample, info.numSamples);

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

    void allNotesOff()
    {
        // One flag instead of 2048 queued note-offs — the FIFO is only 1024 deep.
        killAll.store (true);
    }

    void setModWheel (float v) { modWheel.store (juce::jlimit (0.0f, 1.0f, v)); }
    void setAccelX (float v)   { accelX.store (juce::jlimit (-1.0f, 1.0f, v)); }
    void setAccelY (float v)   { accelY.store (juce::jlimit (-1.0f, 1.0f, v)); }

    //==========================================================================
    // Tables. Index into WavetableLibrary::builtInNames(), or an imported file.
    void setBuiltInTable (int osc, int index)
    {
        index = juce::jlimit (0, WavetableLibrary::builtInNames().size() - 1, index);
        installTable (osc, WavetableLibrary::build (index));
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

    const Wavetable* getTable (int osc) const { return osc == 0 ? tableA.load() : tableB.load(); }
    int getBuiltInIndex (int osc) const       { return oscState (osc).builtInIndex; }
    juce::File getImportedFile (int osc) const { return oscState (osc).importedFile; }

    //==========================================================================
    // Oscillators
    void  setOscLevel (int osc, float v)    { oscState (osc).level = juce::jlimit (0.0f, 1.0f, v); }
    void  setOscPosition (int osc, float v) { oscState (osc).position = juce::jlimit (0.0f, 1.0f, v); }
    void  setOscSemitones (int osc, int s)  { oscState (osc).semis = juce::jlimit (-24, 24, s); }
    void  setOscFineCents (int osc, float c){ oscState (osc).fine = juce::jlimit (-100.0f, 100.0f, c); }
    float getOscLevel (int osc) const       { return oscState (osc).level; }
    float getOscPosition (int osc) const    { return oscState (osc).position; }
    int   getOscSemitones (int osc) const   { return oscState (osc).semis; }
    float getOscFineCents (int osc) const   { return oscState (osc).fine; }

    // Live (modulated) position for the UI's wave display.
    float getEffectivePosition (int osc) const { return (osc == 0 ? effPosA : effPosB).load(); }

    void  setUnison (int n)           { unison = juce::jlimit (1, WavetableVoice::MaxUnison, n); }
    void  setUnisonDetune (float c)   { unisonDetune = juce::jlimit (0.0f, 50.0f, c); }
    void  setSubLevel (float v)       { subLevel = juce::jlimit (0.0f, 1.0f, v); }
    void  setDrive (float v)          { drive = juce::jlimit (0.0f, 1.0f, v); }
    void  setGain (float v)           { gain = juce::jlimit (0.01f, 0.4f, v); }
    int   getUnison() const           { return unison; }
    float getUnisonDetune() const     { return unisonDetune; }
    float getSubLevel() const         { return subLevel; }
    float getDrive() const            { return drive; }
    float getGain() const             { return gain; }

    //==========================================================================
    // Filter + envelopes
    void  setFilterCutoff (float hz)    { filterCutoff = juce::jlimit (20.0f, 20000.0f, hz); }
    void  setFilterResonance (float q)  { filterReso = juce::jlimit (0.0f, 4.0f, q); }
    void  setFilterEnvAmount (float hz) { filterEnvAmount = juce::jlimit (0.0f, 10000.0f, hz); }
    void  setEnvToPosition (float a)    { envToPos = juce::jlimit (-1.0f, 1.0f, a); }
    float getFilterCutoff() const       { return filterCutoff; }
    float getFilterResonance() const    { return filterReso; }
    float getFilterEnvAmount() const    { return filterEnvAmount; }
    float getEnvToPosition() const      { return envToPos; }

    void setAmpADSR (float a, float d, float s, float r)    { ampA = a; ampD = d; ampS = s; ampR = r; adsrDirty = true; }
    void setFilterADSR (float a, float d, float s, float r) { filA = a; filD = d; filS = s; filR = r; adsrDirty = true; }
    void getAmpADSR (float& a, float& d, float& s, float& r) const    { a = ampA; d = ampD; s = ampS; r = ampR; }
    void getFilterADSR (float& a, float& d, float& s, float& r) const { a = filA; d = filD; s = filS; r = filR; }

    //==========================================================================
    // LFO + routing (same model as SynthEngine)
    void  setLFORate (float hz)  { lfoRate = juce::jlimit (0.05f, 20.0f, hz); }
    void  setLFODepth (float d)  { lfoDepth = juce::jlimit (0.0f, 1.0f, d); }
    void  setLFOWaveform (int w) { lfoWaveform = juce::jlimit (0, NumLFOWaves - 1, w); }
    float getLFORate() const     { return lfoRate; }
    float getLFODepth() const    { return lfoDepth; }
    int   getLFOWaveform() const { return lfoWaveform; }

    void setRoutingSource (int slot, int s) { if (validSlot (slot)) routings[slot].source = juce::jlimit (0, NumModSources - 1, s); }
    void setRoutingDest   (int slot, int d) { if (validSlot (slot)) routings[slot].dest   = juce::jlimit (0, NumModDests - 1, d); }
    void setRoutingAmount (int slot, float a) { if (validSlot (slot)) routings[slot].amount = juce::jlimit (-1.0f, 1.0f, a); }
    int   getRoutingSource (int slot) const { return validSlot (slot) ? routings[slot].source : 0; }
    int   getRoutingDest   (int slot) const { return validSlot (slot) ? routings[slot].dest   : 0; }
    float getRoutingAmount (int slot) const { return validSlot (slot) ? routings[slot].amount : 0.0f; }

    //==========================================================================
    MasterFx& getFx() noexcept { return fx; }

    void resetToDefaults()
    {
        setBuiltInTable (0, 0);   // Analog
        setBuiltInTable (1, 1);   // PWM
        setOscLevel (0, 1.0f);  setOscPosition (0, 0.6f);
        setOscLevel (1, 0.0f);  setOscPosition (1, 0.0f);
        setOscSemitones (0, 0); setOscFineCents (0, 0.0f);
        setOscSemitones (1, 0); setOscFineCents (1, 7.0f);
        setUnison (3); setUnisonDetune (12.0f);
        setSubLevel (0.0f); setDrive (0.0f); setGain (0.15f);

        setFilterCutoff (2500.0f); setFilterResonance (0.8f); setFilterEnvAmount (2000.0f);
        setEnvToPosition (0.0f);
        setAmpADSR (0.01f, 0.4f, 0.7f, 0.6f);
        setFilterADSR (0.01f, 0.5f, 0.3f, 0.5f);

        setLFORate (0.3f); setLFODepth (0.0f); setLFOWaveform (LFOSine);

        fx.distDrive = 0.0f;  fx.distEnabled = true;
        fx.delayTimeMs = 350.0f; fx.delayFeedback = 0.4f; fx.delayMix = 0.15f; fx.delayEnabled = true;
        fx.reverbWet = 0.25f; fx.reverbSize = 0.6f; fx.reverbEnabled = true;

        // Factory routing: tilt scans the table — pick up the phone and the
        // timbre moves. Mod wheel still opens the filter.
        for (int i = 0; i < NumRoutingSlots; ++i)
            routings[i] = {};
        routings[0] = { ModSrcAccelY,   ModDestPosA,   0.5f };
        routings[1] = { ModSrcModWheel, ModDestCutoff, 0.5f };
    }

private:
    struct OscState
    {
        float level = 1.0f, position = 0.0f, fine = 0.0f;
        int   semis = 0;
        int   builtInIndex = 0;
        juce::File importedFile;
    };

    OscState&       oscState (int osc)       { return osc == 0 ? oscStateA : oscStateB; }
    const OscState& oscState (int osc) const { return osc == 0 ? oscStateA : oscStateB; }
    static bool validSlot (int s) { return s >= 0 && s < NumRoutingSlots; }

    void installTable (int osc, std::unique_ptr<Wavetable> table)
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

    void processModulation (int numSamples)
    {
        if (killAll.exchange (false))
            synth.allNotesOff (0, false);

        if (adsrDirty.exchange (false))
        {
            juce::ADSR::Parameters amp { ampA, ampD, ampS, ampR };
            juce::ADSR::Parameters fil { filA, filD, filS, filR };
            for (int i = 0; i < synth.getNumVoices(); ++i)
                if (auto* v = dynamic_cast<WavetableVoice*> (synth.getVoice (i)))
                {
                    v->setAmpADSR (amp);
                    v->setFilterADSR (fil);
                }
        }

        lfoPhase += (double) lfoRate * numSamples / currentSampleRate;
        lfoPhase -= std::floor (lfoPhase);
        float lfoRaw = 0.0f;
        switch (lfoWaveform)
        {
            case LFOSine:   lfoRaw = (float) std::sin (lfoPhase * juce::MathConstants<double>::twoPi); break;
            case LFOTri:    lfoRaw = (float) (std::abs (2.0 * (lfoPhase - std::floor (lfoPhase + 0.5))) * 2.0 - 1.0); break;
            case LFOSaw:    lfoRaw = (float) (2.0 * lfoPhase - 1.0); break;
            case LFOSquare: lfoRaw = lfoPhase < 0.5 ? 1.0f : -1.0f; break;
        }

        float src[NumModSources];
        src[ModSrcOff]      = 0.0f;
        src[ModSrcModWheel] = modWheel.load();
        src[ModSrcVelocity] = lastVelocity.load();
        src[ModSrcAccelX]   = accelX.load();
        src[ModSrcAccelY]   = accelY.load();
        src[ModSrcLFO]      = lfoRaw * lfoDepth;

        float mod[NumModDests] = { 0 };
        for (const auto& r : routings)
            mod[r.dest] += src[r.source] * r.amount;

        WavetableVoice::OscParams a, b;
        a.table    = tableA.load();
        a.level    = oscStateA.level;
        a.position = juce::jlimit (0.0f, 1.0f, oscStateA.position + mod[ModDestPosA]);
        a.semis    = (float) oscStateA.semis + oscStateA.fine / 100.0f;
        b.table    = tableB.load();
        b.level    = oscStateB.level;
        b.position = juce::jlimit (0.0f, 1.0f, oscStateB.position + mod[ModDestPosB]);
        b.semis    = (float) oscStateB.semis + oscStateB.fine / 100.0f;
        effPosA.store (a.position);
        effPosB.store (b.position);

        const float cutoff = juce::jlimit (20.0f, 20000.0f, filterCutoff + mod[ModDestCutoff] * 5000.0f);
        const float reso   = juce::jlimit (0.0f, 4.0f, filterReso + mod[ModDestResonance] * 4.0f);
        const float g      = juce::jlimit (0.0f, 0.6f, gain + mod[ModDestGain] * 0.2f);
        const float pitch  = mod[ModDestPitch] * 7.0f;

        for (int i = 0; i < synth.getNumVoices(); ++i)
            if (auto* v = dynamic_cast<WavetableVoice*> (synth.getVoice (i)))
                v->setBlockParams (a, b, subLevel, unison, unisonDetune,
                                   cutoff, reso, filterEnvAmount, envToPos, drive, g, pitch);

        fx.setModulated (mod[ModDestDistDrive], mod[ModDestDelayMix], 0.0f, mod[ModDestReverbMix]);
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

    struct Routing { int source = ModSrcOff; int dest = ModDestOff; float amount = 0.0f; };
    struct Retired { std::unique_ptr<Wavetable> table; juce::uint32 when; };

    LockFreeFifo midiQueue;
    juce::Synthesiser synth;
    MasterFx fx;
    double currentSampleRate = 44100.0;

    std::unique_ptr<Wavetable> ownedA, ownedB;
    std::atomic<const Wavetable*> tableA { nullptr }, tableB { nullptr };
    std::vector<Retired> retired;

    OscState oscStateA, oscStateB;
    std::atomic<float> effPosA { 0.0f }, effPosB { 0.0f };
    int   unison = 3;
    float unisonDetune = 12.0f;
    float subLevel = 0.0f, drive = 0.0f, gain = 0.15f;

    float filterCutoff = 2500.0f, filterReso = 0.8f, filterEnvAmount = 2000.0f, envToPos = 0.0f;
    float ampA = 0.01f, ampD = 0.4f, ampS = 0.7f, ampR = 0.6f;
    float filA = 0.01f, filD = 0.5f, filS = 0.3f, filR = 0.5f;
    std::atomic<bool> adsrDirty { true }, killAll { false };

    float  lfoRate = 0.3f, lfoDepth = 0.0f;
    int    lfoWaveform = LFOSine;
    double lfoPhase = 0.0;

    std::atomic<float> modWheel { 0.5f }, lastVelocity { 0.0f }, accelX { 0.0f }, accelY { 0.0f };
    Routing routings[NumRoutingSlots];
};
