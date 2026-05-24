#pragma once

#include <JuceHeader.h>
#include <vector>
#include <atomic>

//==============================================================================
// Single synth voice: saw/square osc -> state-variable LPF -> amp envelope
class SynthVoice : public juce::SynthesiserVoice
{
public:
    bool canPlaySound (juce::SynthesiserSound*) override { return true; }

    void startNote (int midiNoteNumber, float velocity,
                    juce::SynthesiserSound*, int /*currentPitchWheelPosition*/) override
    {
        level = velocity;
        frequency = juce::MidiMessage::getMidiNoteInHertz (midiNoteNumber);
        ampAdsr.noteOn();
        filterAdsr.noteOn();
    }

    void stopNote (float /*velocity*/, bool allowTailOff) override
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

    void prepareToPlay (double sampleRate, int /*samplesPerBlock*/)
    {
        ampAdsr.setSampleRate (sampleRate);
        filterAdsr.setSampleRate (sampleRate);
        currentSampleRate = sampleRate;
        phase = 0.0;
        filterZ1 = 0.0f;
        filterZ2 = 0.0f;
        updateAmpADSR();
        updateFilterADSR();
    }

    void setADSR (float a, float d, float s, float r)
    {
        ampAttack = a; ampDecay = d; ampSustain = s; ampRelease = r;
        updateAmpADSR();
    }

    void setFilterADSR (float a, float d, float s, float r)
    {
        filtAttack = a; filtDecay = d; filtSustain = s; filtRelease = r;
        updateFilterADSR();
    }

    void setFilterParams (float cutoff, float reso, float envAmount)
    {
        filterCutoff = cutoff;
        filterReso = reso;
        filterEnvAmount = envAmount;
    }

    void setSawSquareMix (float saw) { sawMix = saw; }
    void setGain (float g) { gain = g; }

    void renderNextBlock (juce::AudioBuffer<float>& outputBuffer,
                          int startSample, int numSamples) override
    {
        if (! isVoiceActive())
            return;

        double phaseIncrement = frequency / currentSampleRate;
        float currentSaw = sawMix;
        float currentGain = gain;

        for (int sample = startSample; sample < startSample + numSamples; ++sample)
        {
            double sawValue = 2.0 * phase - 1.0;
            double squareValue = (phase < 0.5) ? 1.0 : -1.0;
            float osc = (float) (sawValue * currentSaw + squareValue * (1.0f - currentSaw)) * level;

            float filtEnv = filterAdsr.getNextSample();
            float modulatedCutoff = filterCutoff + filterEnvAmount * filtEnv;
            modulatedCutoff = juce::jlimit (20.0f, 20000.0f, modulatedCutoff);

            float w = 2.0f * std::tan (juce::MathConstants<float>::pi * modulatedCutoff / (float) currentSampleRate);
            float q = juce::jlimit (0.5f, 20.0f, filterReso);

            float hp = (osc - (1.0f / q + w) * filterZ1 - filterZ2) / (1.0f + w / q + w * w);
            float bp = w * hp + filterZ1;
            float lp = w * bp + filterZ2;
            filterZ1 = w * hp + bp;
            filterZ2 = w * bp + lp;

            osc = lp;

            float ampEnv = ampAdsr.getNextSample();
            osc *= ampEnv * currentGain;

            for (int channel = 0; channel < outputBuffer.getNumChannels(); ++channel)
                outputBuffer.addSample (channel, sample, osc);

            phase += phaseIncrement;
            if (phase >= 1.0)
                phase -= 1.0;

            if (! ampAdsr.isActive())
            {
                clearCurrentNote();
                break;
            }
        }
    }

private:
    void updateAmpADSR()
    {
        juce::ADSR::Parameters p;
        p.attack = ampAttack; p.decay = ampDecay;
        p.sustain = ampSustain; p.release = ampRelease;
        ampAdsr.setParameters (p);
    }

    void updateFilterADSR()
    {
        juce::ADSR::Parameters p;
        p.attack = filtAttack; p.decay = filtDecay;
        p.sustain = filtSustain; p.release = filtRelease;
        filterAdsr.setParameters (p);
    }

    double phase = 0.0;
    double frequency = 440.0;
    double currentSampleRate = 44100.0;
    float level = 0.0f;
    float sawMix = 0.6f;
    float gain = 0.15f;

    float ampAttack = 0.02f, ampDecay = 0.3f, ampSustain = 0.6f, ampRelease = 0.4f;
    juce::ADSR ampAdsr;

    float filtAttack = 0.01f, filtDecay = 0.4f, filtSustain = 0.3f, filtRelease = 0.5f;
    juce::ADSR filterAdsr;

    float filterCutoff = 2000.0f;
    float filterReso = 1.0f;
    float filterEnvAmount = 3000.0f;
    float filterZ1 = 0.0f;
    float filterZ2 = 0.0f;
};

//==============================================================================
class SynthSound : public juce::SynthesiserSound
{
public:
    bool appliesToNote (int) override { return true; }
    bool appliesToChannel (int) override { return true; }
};

//==============================================================================
// SynthEngine: 32-voice poly synth with state-variable filter and reverb.
// Pure MIDI-style API — no chord/scale knowledge lives here. Translation from
// semantic gestures (chord degrees, scale notes, etc.) to MIDI notes is the
// caller's job (see *Handler.h files). Includes a single LFO and a 4-slot
// mod-wheel routing matrix that runs per-audio-block.
class SynthEngine : public juce::AudioSource
{
public:
    // Modulation destinations for both the LFO and the mod-wheel routing slots.
    enum ModDest
    {
        ModDestOff       = 0,
        ModDestCutoff    = 1,
        ModDestResonance = 2,
        ModDestEnvAmount = 3,
        ModDestGain      = 4,
        ModDestReverbMix = 5,
        NumModDests      = 6
    };

    enum LFOWaveform { LFOSine = 0, LFOTri = 1, LFOSaw = 2, LFOSquare = 3, NumLFOWaves = 4 };

    static constexpr int NumRoutingSlots = 4;

    SynthEngine()
    {
        synth.addSound (new SynthSound());

        for (int i = 0; i < 32; ++i)
            synth.addVoice (new SynthVoice());

        reverbParams.roomSize = 0.5f;
        reverbParams.damping  = 0.4f;
        reverbParams.wetLevel = 0.2f;
        reverbParams.dryLevel = 0.8f;
        reverbParams.width    = 1.0f;
        reverb.setParameters (reverbParams);

        // Default: mod wheel (Morf-Pad Y axis) drives filter cutoff at +0.5.
        // Other slots start disabled.
        routings[0].dest   = ModDestCutoff;
        routings[0].amount = 0.5f;
    }

    //==========================================================================
    // AudioSource interface
    void prepareToPlay (int samplesPerBlockExpected, double sampleRate) override
    {
        synth.setCurrentPlaybackSampleRate (sampleRate);

        for (int i = 0; i < synth.getNumVoices(); ++i)
        {
            if (auto* voice = dynamic_cast<SynthVoice*> (synth.getVoice (i)))
                voice->prepareToPlay (sampleRate, samplesPerBlockExpected);
        }

        reverb.setSampleRate (sampleRate);
        currentSampleRate = sampleRate;
    }

    void releaseResources() override {}

    void getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill) override
    {
        bufferToFill.clearActiveBufferRegion();

        juce::MidiBuffer incomingMidi;

        MidiEvent event;
        while (midiQueue.try_dequeue (event))
        {
            if (event.isNoteOn)
                incomingMidi.addEvent (
                    juce::MidiMessage::noteOn (event.channel, event.note, (juce::uint8) event.velocity),
                    0);
            else
                incomingMidi.addEvent (
                    juce::MidiMessage::noteOff (event.channel, event.note),
                    0);
        }

        // Apply per-block modulation (LFO + mod wheel routings) to params.
        processModulation (bufferToFill.numSamples);

        synth.renderNextBlock (*bufferToFill.buffer,
                               incomingMidi,
                               bufferToFill.startSample,
                               bufferToFill.numSamples);

        if (bufferToFill.buffer->getNumChannels() >= 2)
        {
            reverb.processStereo (
                bufferToFill.buffer->getWritePointer (0, bufferToFill.startSample),
                bufferToFill.buffer->getWritePointer (1, bufferToFill.startSample),
                bufferToFill.numSamples);
        }
        else if (bufferToFill.buffer->getNumChannels() == 1)
        {
            reverb.processMono (
                bufferToFill.buffer->getWritePointer (0, bufferToFill.startSample),
                bufferToFill.numSamples);
        }
    }

    //==========================================================================
    // MIDI API — call from UI thread, lock-free, audio-thread safe.

    void noteOn (int midiNote, float velocity, int channel = 1)
    {
        if (midiNote < 0 || midiNote > 127) return;
        int vel = juce::jlimit (1, 127, (int) std::round (velocity * 127.0f));
        midiQueue.enqueue ({ midiNote, vel, juce::jlimit (1, 16, channel), true });
    }

    void noteOff (int midiNote, int channel = 1)
    {
        if (midiNote < 0 || midiNote > 127) return;
        midiQueue.enqueue ({ midiNote, 0, juce::jlimit (1, 16, channel), false });
    }

    void allNotesOff()
    {
        for (int ch = 1; ch <= 16; ++ch)
            for (int note = 0; note < 128; ++note)
                midiQueue.enqueue ({ note, 0, ch, false });
    }

    //==========================================================================
    // Synth parameter API (UI thread)

    void setAttack (float v)  { adsrAttack  = v; applyADSRToVoices(); }
    void setDecay (float v)   { adsrDecay   = v; applyADSRToVoices(); }
    void setSustain (float v) { adsrSustain = v; applyADSRToVoices(); }
    void setRelease (float v) { adsrRelease = v; applyADSRToVoices(); }

    float getAttack()  const { return adsrAttack; }
    float getDecay()   const { return adsrDecay; }
    float getSustain() const { return adsrSustain; }
    float getRelease() const { return adsrRelease; }

    void setWaveformMix (float sawAmount)
    {
        sawMix = juce::jlimit (0.0f, 1.0f, sawAmount);
        for (int i = 0; i < synth.getNumVoices(); ++i)
            if (auto* v = dynamic_cast<SynthVoice*> (synth.getVoice (i)))
                v->setSawSquareMix (sawMix);
    }
    float getWaveformMix() const { return sawMix; }

    void setGain (float g)
    {
        masterGain = juce::jlimit (0.01f, 0.4f, g);
        for (int i = 0; i < synth.getNumVoices(); ++i)
            if (auto* v = dynamic_cast<SynthVoice*> (synth.getVoice (i)))
                v->setGain (masterGain);
    }
    float getGain() const { return masterGain; }

    void setReverbMix (float wet)
    {
        reverbWet = juce::jlimit (0.0f, 1.0f, wet);
        reverbParams.wetLevel = reverbWet;
        reverbParams.dryLevel = 1.0f - reverbWet;
        reverb.setParameters (reverbParams);
    }
    float getReverbMix() const { return reverbWet; }

    void setReverbSize (float size)
    {
        reverbSize = juce::jlimit (0.0f, 1.0f, size);
        reverbParams.roomSize = reverbSize;
        reverb.setParameters (reverbParams);
    }
    float getReverbSize() const { return reverbSize; }

    void setFilterCutoff (float hz)
    {
        filterCutoff = juce::jlimit (20.0f, 20000.0f, hz);
        applyFilterToVoices();
    }
    float getFilterCutoff() const { return filterCutoff; }

    void setFilterResonance (float q)
    {
        filterReso = juce::jlimit (0.5f, 20.0f, q);
        applyFilterToVoices();
    }
    float getFilterResonance() const { return filterReso; }

    void setFilterEnvAmount (float hz)
    {
        filterEnvAmount = juce::jlimit (0.0f, 10000.0f, hz);
        applyFilterToVoices();
    }
    float getFilterEnvAmount() const { return filterEnvAmount; }

    void setFilterAttack (float v)  { filtAttack  = v; applyFilterADSRToVoices(); }
    void setFilterDecay (float v)   { filtDecay   = v; applyFilterADSRToVoices(); }
    void setFilterSustain (float v) { filtSustain = v; applyFilterADSRToVoices(); }
    void setFilterRelease (float v) { filtRelease = v; applyFilterADSRToVoices(); }

    float getFilterAttack()  const { return filtAttack; }
    float getFilterDecay()   const { return filtDecay; }
    float getFilterSustain() const { return filtSustain; }
    float getFilterRelease() const { return filtRelease; }

    //==========================================================================
    // LFO parameters (single LFO, per-block update, applied via lfoTarget)

    void setLFORate     (float hz)  { lfoRate     = juce::jlimit (0.05f, 20.0f, hz); }
    void setLFODepth    (float d)   { lfoDepth    = juce::jlimit (0.0f, 1.0f, d); }
    void setLFOWaveform (int w)     { lfoWaveform = juce::jlimit (0, NumLFOWaves - 1, w); }
    void setLFOTarget   (int dest)  { lfoTarget   = juce::jlimit (0, NumModDests - 1, dest); }

    float getLFORate()     const { return lfoRate; }
    float getLFODepth()    const { return lfoDepth; }
    int   getLFOWaveform() const { return lfoWaveform; }
    int   getLFOTarget()   const { return lfoTarget; }

    //==========================================================================
    // Mod wheel (CC1) — updated by PhoneMidiSink whenever a controller sends
    // CC1 (e.g. Morf-Pad's Y axis). Atomic so it's safe across threads.

    void setModWheel (float value01) { modWheel.store (juce::jlimit (0.0f, 1.0f, value01)); }
    float getModWheel() const { return modWheel.load(); }

    //==========================================================================
    // Mod wheel routing matrix (4 slots, each: destination + amount).

    void setRoutingDest   (int slot, int dest)
    {
        if (slot < 0 || slot >= NumRoutingSlots) return;
        routings[slot].dest = juce::jlimit (0, NumModDests - 1, dest);
    }
    void setRoutingAmount (int slot, float amount)
    {
        if (slot < 0 || slot >= NumRoutingSlots) return;
        routings[slot].amount = juce::jlimit (-1.0f, 1.0f, amount);
    }
    int   getRoutingDest   (int slot) const
    {
        return (slot >= 0 && slot < NumRoutingSlots) ? routings[slot].dest : 0;
    }
    float getRoutingAmount (int slot) const
    {
        return (slot >= 0 && slot < NumRoutingSlots) ? routings[slot].amount : 0.0f;
    }

private:
    //==========================================================================
    // Per-block modulation update. Computes LFO value, sums contributions from
    // mod-wheel routings and the LFO target, then pushes effective param values
    // into the voices / reverb. Linear modulation with per-destination scale.

    void processModulation (int numSamples)
    {
        if (currentSampleRate <= 0.0) return;

        // Advance LFO phase
        lfoPhase += (double) lfoRate * (double) numSamples / currentSampleRate;
        lfoPhase -= std::floor (lfoPhase);  // wrap to [0..1)

        // Compute LFO output in [-1, +1]
        float lfoRaw = 0.0f;
        switch (lfoWaveform)
        {
            case LFOSine:   lfoRaw = std::sin (lfoPhase * juce::MathConstants<double>::twoPi); break;
            case LFOTri:    lfoRaw = (float) (std::abs (2.0 * (lfoPhase - std::floor (lfoPhase + 0.5))) * 2.0 - 1.0); break;
            case LFOSaw:    lfoRaw = (float) (2.0 * lfoPhase - 1.0); break;
            case LFOSquare: lfoRaw = (lfoPhase < 0.5) ? 1.0f : -1.0f; break;
        }
        float lfoOut = lfoRaw * lfoDepth;

        // mod-wheel source in [-1, +1] (centred at 0.5 input → 0 mod, full up → +1)
        float modW = (modWheel.load() * 2.0f - 1.0f);

        // Sum modulation per destination
        float modPerDest[NumModDests] = { 0 };

        for (int i = 0; i < NumRoutingSlots; ++i)
            modPerDest[routings[i].dest] += modW * routings[i].amount;

        modPerDest[lfoTarget] += lfoOut;

        // Apply to params (linear add with per-dest scale)
        float effCutoff = filterCutoff   + modPerDest[ModDestCutoff]    * 5000.0f;
        float effReso   = filterReso     + modPerDest[ModDestResonance] * 5.0f;
        float effEnvAmt = filterEnvAmount + modPerDest[ModDestEnvAmount] * 5000.0f;
        float effGain   = masterGain     + modPerDest[ModDestGain]      * 0.2f;
        float effRevWet = reverbWet      + modPerDest[ModDestReverbMix] * 1.0f;

        effCutoff = juce::jlimit (20.0f,  20000.0f, effCutoff);
        effReso   = juce::jlimit (0.5f,    20.0f,   effReso);
        effEnvAmt = juce::jlimit (0.0f,   10000.0f, effEnvAmt);
        effGain   = juce::jlimit (0.0f,    0.6f,    effGain);
        effRevWet = juce::jlimit (0.0f,    1.0f,    effRevWet);

        // Push to voices
        for (int i = 0; i < synth.getNumVoices(); ++i)
        {
            if (auto* v = dynamic_cast<SynthVoice*> (synth.getVoice (i)))
            {
                v->setFilterParams (effCutoff, effReso, effEnvAmt);
                v->setGain (effGain);
            }
        }

        // Push to reverb if changed meaningfully
        if (std::abs (effRevWet - lastEffectiveReverbWet) > 0.001f)
        {
            reverbParams.wetLevel = effRevWet;
            reverbParams.dryLevel = 1.0f - effRevWet;
            reverb.setParameters (reverbParams);
            lastEffectiveReverbWet = effRevWet;
        }
    }

    void applyADSRToVoices()
    {
        for (int i = 0; i < synth.getNumVoices(); ++i)
            if (auto* v = dynamic_cast<SynthVoice*> (synth.getVoice (i)))
                v->setADSR (adsrAttack, adsrDecay, adsrSustain, adsrRelease);
    }

    void applyFilterToVoices()
    {
        for (int i = 0; i < synth.getNumVoices(); ++i)
            if (auto* v = dynamic_cast<SynthVoice*> (synth.getVoice (i)))
                v->setFilterParams (filterCutoff, filterReso, filterEnvAmount);
    }

    void applyFilterADSRToVoices()
    {
        for (int i = 0; i < synth.getNumVoices(); ++i)
            if (auto* v = dynamic_cast<SynthVoice*> (synth.getVoice (i)))
                v->setFilterADSR (filtAttack, filtDecay, filtSustain, filtRelease);
    }

    //==========================================================================
    // Lock-free MIDI event FIFO for UI → audio thread.
    struct MidiEvent
    {
        int note;
        int velocity;
        int channel;
        bool isNoteOn;
    };

    struct LockFreeFifo
    {
        static constexpr int capacity = 1024;
        MidiEvent buffer[capacity];
        std::atomic<int> readPos { 0 };
        std::atomic<int> writePos { 0 };

        bool try_dequeue (MidiEvent& event)
        {
            int r = readPos.load (std::memory_order_relaxed);
            int w = writePos.load (std::memory_order_acquire);
            if (r == w)
                return false;
            event = buffer[r];
            readPos.store ((r + 1) % capacity, std::memory_order_release);
            return true;
        }

        void enqueue (MidiEvent event)
        {
            int w = writePos.load (std::memory_order_relaxed);
            buffer[w] = event;
            writePos.store ((w + 1) % capacity, std::memory_order_release);
        }
    };

    LockFreeFifo midiQueue;

    juce::Synthesiser synth;
    juce::Reverb reverb;
    juce::Reverb::Parameters reverbParams;
    double currentSampleRate = 44100.0;

    float adsrAttack  = 0.02f;
    float adsrDecay   = 0.3f;
    float adsrSustain = 0.6f;
    float adsrRelease = 0.4f;
    float sawMix      = 0.6f;
    float masterGain  = 0.15f;
    float reverbWet   = 0.2f;
    float reverbSize  = 0.5f;

    float filterCutoff    = 2000.0f;
    float filterReso      = 1.0f;
    float filterEnvAmount = 3000.0f;
    float filtAttack  = 0.01f;
    float filtDecay   = 0.4f;
    float filtSustain = 0.3f;
    float filtRelease = 0.5f;

    // LFO state
    float lfoRate = 1.0f;        // Hz
    float lfoDepth = 0.0f;       // 0..1
    int   lfoWaveform = LFOSine; // index into NumLFOWaves
    int   lfoTarget = ModDestOff;
    double lfoPhase = 0.0;

    // Mod wheel (CC1) — updated by PhoneMidiSink, read on audio thread
    std::atomic<float> modWheel { 0.5f };

    // Routing matrix
    struct Routing { int dest = ModDestOff; float amount = 0.0f; };
    Routing routings[NumRoutingSlots];

    // Track last reverb wet value applied so we only push to reverb on change
    float lastEffectiveReverbWet = 0.2f;
};
