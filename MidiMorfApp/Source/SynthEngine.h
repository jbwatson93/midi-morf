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

        // Per-voice analog-style pitch drift: ±1.5¢ random offset, frozen for
        // the duration of the note. Keeps voices in a chord just-detuned enough
        // to feel "alive" without sounding wrong.
        float driftCents = (driftRandom.nextFloat() * 2.0f - 1.0f) * 1.5f;
        float driftRatio = std::pow (2.0f, driftCents / 1200.0f);
        frequency = juce::MidiMessage::getMidiNoteInHertz (midiNoteNumber) * driftRatio;

        // Reset ladder filter state
        ls1 = ls2 = ls3 = ls4 = 0.0f;
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
        phase2 = 0.0;
        subPhase = 0.0;
        ls1 = ls2 = ls3 = ls4 = 0.0f;
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

    // OSC 1
    void setMainWaveform (int w) { mainWave = juce::jlimit (0, 3, w); }
    void setOsc1Level (float lvl) { osc1Level = juce::jlimit (0.0f, 1.0f, lvl); }

    // OSC 2
    void setOsc2Waveform (int w)        { osc2Wave = juce::jlimit (0, 3, w); }
    void setOsc2Level (float lvl)       { osc2Level = juce::jlimit (0.0f, 1.0f, lvl); }
    void setOsc2DetuneCents (float c)
    {
        osc2DetuneCents = juce::jlimit (-100.0f, 100.0f, c);
        osc2DetuneRatio = std::pow (2.0, (double) osc2DetuneCents / 1200.0);
    }

    void setSubLevel    (float lvl) { subLevel = juce::jlimit (0.0f, 1.0f, lvl); }
    void setGain (float g) { gain = g; }
    void setDrive (float d) { drive = juce::jlimit (0.0f, 1.0f, d); }
    void setPitchMod (float semitones) { pitchModSemi = semitones; }

    void renderNextBlock (juce::AudioBuffer<float>& outputBuffer,
                          int startSample, int numSamples) override
    {
        if (! isVoiceActive())
            return;

        // Apply per-block pitch modulation (from the routing matrix's Pitch dest).
        double effectiveFreq  = frequency * std::pow (2.0, pitchModSemi / 12.0);
        double phaseIncrement  = effectiveFreq / currentSampleRate;
        double phaseIncrement2 = effectiveFreq * osc2DetuneRatio / currentSampleRate;
        double subIncrement    = phaseIncrement * 0.5;
        float  dt              = (float) phaseIncrement;
        float  dt2             = (float) phaseIncrement2;
        int    currentWave1    = mainWave;
        int    currentWave2    = osc2Wave;
        float  currentL1       = osc1Level;
        float  currentL2       = osc2Level;
        float  currentSub      = subLevel;
        float  currentGain     = gain;
        float  currentDrive    = drive;
        constexpr double twoPi = juce::MathConstants<double>::twoPi;

        auto renderWave = [&] (int wave, double ph, float bdt) -> double
        {
            switch (wave)
            {
                case 0: return std::sin (ph * twoPi);                                                // Sine
                case 1: return (ph < 0.5) ? (4.0 * ph - 1.0) : (3.0 - 4.0 * ph);                     // Triangle
                case 2: return (2.0 * ph - 1.0) - polyBLEP ((float) ph, bdt);                        // Saw
                default:
                {
                    double s = (ph < 0.5) ? 1.0 : -1.0;
                    s += polyBLEP ((float) ph, bdt);
                    float pp = (float) ph + 0.5f;
                    if (pp >= 1.0f) pp -= 1.0f;
                    s -= polyBLEP (pp, bdt);
                    return s;
                }
            }
        };

        for (int sample = startSample; sample < startSample + numSamples; ++sample)
        {
            double main1 = renderWave (currentWave1, phase,  dt);
            double main2 = renderWave (currentWave2, phase2, dt2);

            // Sub oscillator (square at -1 octave, also BLEP'd)
            float subDt = (float) subIncrement;
            double sub = (subPhase < 0.5) ? 1.0 : -1.0;
            sub += polyBLEP ((float) subPhase, subDt);
            float sp2 = (float) subPhase + 0.5f;
            if (sp2 >= 1.0f) sp2 -= 1.0f;
            sub -= polyBLEP (sp2, subDt);

            float osc = (float) ((main1 * currentL1 + main2 * currentL2 + sub * currentSub) * level);

            // Soft tube saturation (tanh waveshaper, drive 0..1 → ~1..7x gain
            // into tanh, output renormalised). Adds harmonic warmth pre-filter.
            if (currentDrive > 0.0001f)
            {
                float k = 1.0f + currentDrive * 6.0f;
                osc = std::tanh (osc * k) * (1.0f / std::tanh (k));
            }

            // Filter envelope adds to the (already-modulated) cutoff supplied
            // by setFilterParams. Velocity is now a routing source rather than
            // a baked-in offset.
            float filtEnv = filterAdsr.getNextSample();
            float modulatedCutoff = filterCutoff + filterEnvAmount * filtEnv;
            modulatedCutoff = juce::jlimit (20.0f, 20000.0f, modulatedCutoff);

            // 4-pole Moog-style ladder filter (Stilson model, linear feedback).
            // g = 1 - exp(-2π·fc/fs). Resonance up to ~4 self-oscillates.
            float g = 1.0f - std::exp (-juce::MathConstants<float>::twoPi *
                                       modulatedCutoff / (float) currentSampleRate);
            float reso = juce::jlimit (0.0f, 4.0f, filterReso);
            float input = osc - reso * ls4;
            ls1 += g * (input - ls1);
            ls2 += g * (ls1   - ls2);
            ls3 += g * (ls2   - ls3);
            ls4 += g * (ls3   - ls4);
            osc = ls4;

            float ampEnv = ampAdsr.getNextSample();
            osc *= ampEnv * currentGain;

            for (int channel = 0; channel < outputBuffer.getNumChannels(); ++channel)
                outputBuffer.addSample (channel, sample, osc);

            phase += phaseIncrement;
            if (phase >= 1.0) phase -= 1.0;
            phase2 += phaseIncrement2;
            if (phase2 >= 1.0) phase2 -= 1.0;
            subPhase += subIncrement;
            if (subPhase >= 1.0) subPhase -= 1.0;

            if (! ampAdsr.isActive())
            {
                clearCurrentNote();
                break;
            }
        }
    }

private:
    // PolyBLEP correction: subtract from saw at each phase wrap; add at square
    // rising edge / subtract at falling edge. Eliminates high-frequency aliasing
    // for cheap (1 polynomial evaluation per wrap per sample).
    static float polyBLEP (float t, float dt)
    {
        if (t < dt)
        {
            t /= dt;
            return t + t - t * t - 1.0f;
        }
        if (t > 1.0f - dt)
        {
            t = (t - 1.0f) / dt;
            return t * t + t + t + 1.0f;
        }
        return 0.0f;
    }

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
    double phase2 = 0.0;
    double subPhase = 0.0;
    double frequency = 440.0;
    double osc2DetuneRatio = 1.0;
    double currentSampleRate = 44100.0;
    float level = 0.0f;
    int   mainWave = 2;     // 0=Sine, 1=Triangle, 2=Saw, 3=Square — default Saw
    int   osc2Wave = 0;     // default Sine
    float osc1Level = 1.0f;
    float osc2Level = 0.0f;
    float osc2DetuneCents = 0.0f;
    float subLevel = 0.3f;  // 0..1 — sub adds low-end warmth to the default patch
    float gain = 0.15f;
    float drive = 0.0f;     // 0..1 — soft saturation (post-osc)
    float pitchModSemi = 0.0f; // pitch modulation in semitones (set per block)
    juce::Random driftRandom;

    float ampAttack = 0.02f, ampDecay = 0.3f, ampSustain = 0.6f, ampRelease = 0.7f;
    juce::ADSR ampAdsr;

    float filtAttack = 0.01f, filtDecay = 0.4f, filtSustain = 0.3f, filtRelease = 0.5f;
    juce::ADSR filterAdsr;

    float filterCutoff = 1400.0f;   // slightly darker start so mod wheel/velocity/tilt sweep is audibly obvious
    float filterReso = 1.0f;
    float filterEnvAmount = 3000.0f;

    // 4-pole ladder filter state
    float ls1 = 0.0f, ls2 = 0.0f, ls3 = 0.0f, ls4 = 0.0f;
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
    // Modulation sources — any can be picked per routing slot.
    enum ModSource
    {
        ModSrcOff      = 0,
        ModSrcModWheel = 1,   // 0..1 (CC1, raw)
        ModSrcVelocity = 2,   // 0..1 (last note velocity)
        ModSrcAccelX   = 3,   // -1..+1 (device tilt X)
        ModSrcAccelY   = 4,   // -1..+1 (device tilt Y)
        ModSrcLFO      = 5,   // -depth..+depth
        NumModSources  = 6
    };

    // Modulation destinations.
    enum ModDest
    {
        ModDestOff       = 0,
        ModDestCutoff    = 1,
        ModDestResonance = 2,
        ModDestEnvAmount = 3,
        ModDestGain      = 4,
        ModDestReverbMix = 5,
        ModDestPitch     = 6,
        ModDestDistDrive = 7,
        ModDestDelayMix  = 8,
        ModDestDelayTime = 9,
        NumModDests      = 10
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

        // Default routings: mod wheel + device tilt both open the filter.
        // The tilt routing is what makes the very first launch feel alive —
        // pick up the phone, tap a chord, tilt it, hear the filter sweep.
        // Both slots are reassignable on the Routing tab.
        routings[0].source = ModSrcModWheel;
        routings[0].dest   = ModDestCutoff;
        routings[0].amount = 0.5f;

        routings[1].source = ModSrcAccelY;
        routings[1].dest   = ModDestCutoff;
        routings[1].amount = 0.5f;
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

        // Delay buffer: 2-second max @ current sample rate (mono).
        delayBuf.assign ((size_t) (sampleRate * 2.0), 0.0f);
        delayWritePos = 0;
    }

    void releaseResources() override {}

    void getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill) override
    {
        bufferToFill.clearActiveBufferRegion();

        if (killAll.exchange (false))
            synth.allNotesOff (0, false);

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

        const int numCh = bufferToFill.buffer->getNumChannels();
        const int start = bufferToFill.startSample;
        const int n     = bufferToFill.numSamples;
        float* L = numCh > 0 ? bufferToFill.buffer->getWritePointer (0, start) : nullptr;
        float* R = numCh > 1 ? bufferToFill.buffer->getWritePointer (1, start) : L;

        // Master distortion (post-mix, pre-delay).
        if (distEnabled && effDistDrive > 0.001f && L != nullptr)
        {
            float k    = 1.0f + effDistDrive * 8.0f;
            float norm = 1.0f / std::tanh (k);
            for (int i = 0; i < n; ++i)
            {
                L[i] = std::tanh (L[i] * k) * norm;
                if (R != L) R[i] = std::tanh (R[i] * k) * norm;
            }
        }

        // Mono-feed stereo delay.
        if (delayEnabled && effDelayMix > 0.001f && ! delayBuf.empty() && L != nullptr)
        {
            const int bufSize = (int) delayBuf.size();
            // Use the post-modulation delay time so routings like LFO → Delay
            // Time produce moving echoes / dub sweeps.
            float delayTimeSec = juce::jlimit (0.005f, 1.99f, effDelayTimeMs * 0.001f);
            int   delaySamples = juce::jlimit (1, bufSize - 1,
                                                (int) (delayTimeSec * (float) currentSampleRate));

            for (int i = 0; i < n; ++i)
            {
                int readPos = delayWritePos - delaySamples;
                if (readPos < 0) readPos += bufSize;

                float delayed = delayBuf[(size_t) readPos];
                float inMix   = (R != L) ? (L[i] + R[i]) * 0.5f : L[i];

                delayBuf[(size_t) delayWritePos] = inMix + delayed * delayFeedback;
                delayWritePos = (delayWritePos + 1) % bufSize;

                float dry = 1.0f - effDelayMix;
                L[i] = L[i] * dry + delayed * effDelayMix;
                if (R != L) R[i] = R[i] * dry + delayed * effDelayMix;
            }
        }

        if (reverbEnabled)
        {
            if (numCh >= 2)
                reverb.processStereo (L, R, n);
            else if (numCh == 1 && L != nullptr)
                reverb.processMono (L, n);
        }
    }

    //==========================================================================
    // MIDI API — call from UI thread, lock-free, audio-thread safe.

    void noteOn (int midiNote, float velocity, int channel = 1)
    {
        if (midiNote < 0 || midiNote > 127) return;
        int vel = juce::jlimit (1, 127, (int) std::round (velocity * 127.0f));
        // Track most-recent velocity for the modulation matrix (Velocity source).
        lastVelocity.store (juce::jlimit (0.0f, 1.0f, velocity));
        midiQueue.enqueue ({ midiNote, vel, juce::jlimit (1, 16, channel), true });
    }

    void noteOff (int midiNote, int channel = 1)
    {
        if (midiNote < 0 || midiNote > 127) return;
        midiQueue.enqueue ({ midiNote, 0, juce::jlimit (1, 16, channel), false });
    }

    void allNotesOff()
    {
        // A flag, not queued note-offs: 16 x 128 events into the 1024-slot
        // FIFO wrapped the write index back onto the read index, so the queue
        // looked empty and every note-off was dropped.
        killAll.store (true);
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

    // Main oscillator waveform: 0=Sine, 1=Triangle, 2=Saw, 3=Square.
    void setMainWaveform (int wave)
    {
        mainWaveform = juce::jlimit (0, 3, wave);
        for (int i = 0; i < synth.getNumVoices(); ++i)
            if (auto* v = dynamic_cast<SynthVoice*> (synth.getVoice (i)))
                v->setMainWaveform (mainWaveform);
    }
    int getMainWaveform() const { return mainWaveform; }

    // Sub oscillator level (square wave, one octave below the main pitch).
    void setSubLevel (float lvl)
    {
        subLevel = juce::jlimit (0.0f, 1.0f, lvl);
        for (int i = 0; i < synth.getNumVoices(); ++i)
            if (auto* v = dynamic_cast<SynthVoice*> (synth.getVoice (i)))
                v->setSubLevel (subLevel);
    }
    float getSubLevel() const { return subLevel; }

    // OSC 1 level
    void setOsc1Level (float lvl)
    {
        osc1Level = juce::jlimit (0.0f, 1.0f, lvl);
        for (int i = 0; i < synth.getNumVoices(); ++i)
            if (auto* v = dynamic_cast<SynthVoice*> (synth.getVoice (i)))
                v->setOsc1Level (osc1Level);
    }
    float getOsc1Level() const { return osc1Level; }

    // OSC 2 wave + level + detune
    void setOsc2Waveform (int w)
    {
        osc2Waveform = juce::jlimit (0, 3, w);
        for (int i = 0; i < synth.getNumVoices(); ++i)
            if (auto* v = dynamic_cast<SynthVoice*> (synth.getVoice (i)))
                v->setOsc2Waveform (osc2Waveform);
    }
    int getOsc2Waveform() const { return osc2Waveform; }

    void setOsc2Level (float lvl)
    {
        osc2Level = juce::jlimit (0.0f, 1.0f, lvl);
        for (int i = 0; i < synth.getNumVoices(); ++i)
            if (auto* v = dynamic_cast<SynthVoice*> (synth.getVoice (i)))
                v->setOsc2Level (osc2Level);
    }
    float getOsc2Level() const { return osc2Level; }

    void setOsc2DetuneCents (float cents)
    {
        osc2DetuneCents = juce::jlimit (-100.0f, 100.0f, cents);
        for (int i = 0; i < synth.getNumVoices(); ++i)
            if (auto* v = dynamic_cast<SynthVoice*> (synth.getVoice (i)))
                v->setOsc2DetuneCents (osc2DetuneCents);
    }
    float getOsc2DetuneCents() const { return osc2DetuneCents; }

    // Drive: soft tanh saturation post-oscillator, pre-filter.
    void setDrive (float d)
    {
        drive = juce::jlimit (0.0f, 1.0f, d);
        for (int i = 0; i < synth.getNumVoices(); ++i)
            if (auto* v = dynamic_cast<SynthVoice*> (synth.getVoice (i)))
                v->setDrive (drive);
    }
    float getDrive() const { return drive; }

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

    // Master distortion (post-mix tanh waveshaper).
    void setDistDrive (float d) { distDrive = juce::jlimit (0.0f, 1.0f, d); }
    float getDistDrive() const { return distDrive; }
    void setDistEnabled (bool on) { distEnabled = on; }
    bool getDistEnabled() const { return distEnabled; }

    // Delay
    void setDelayTimeMs  (float ms)   { delayTimeMs  = juce::jlimit (5.0f, 2000.0f, ms); }
    void setDelayFeedback (float fb)  { delayFeedback = juce::jlimit (0.0f, 0.95f, fb); }
    void setDelayMix     (float mix)  { delayMix     = juce::jlimit (0.0f, 1.0f, mix); }
    float getDelayTimeMs() const { return delayTimeMs; }
    float getDelayFeedback() const { return delayFeedback; }
    float getDelayMix() const { return delayMix; }
    void setDelayEnabled (bool on) { delayEnabled = on; }
    bool getDelayEnabled() const { return delayEnabled; }

    // Reverb (bypassable like the others)
    void setReverbEnabled (bool on) { reverbEnabled = on; }
    bool getReverbEnabled() const { return reverbEnabled; }

    void setFilterCutoff (float hz)
    {
        filterCutoff = juce::jlimit (20.0f, 20000.0f, hz);
        applyFilterToVoices();
    }
    float getFilterCutoff() const { return filterCutoff; }

    // Ladder filter resonance: 0..4. Self-oscillates near 4.
    void setFilterResonance (float q)
    {
        filterReso = juce::jlimit (0.0f, 4.0f, q);
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

    //==========================================================================
    // Reset everything to the construction defaults — used by the "CLEAR"
    // button on the synth page to wipe the patch back to a sane init state.
    void resetToDefaults()
    {
        // Amp env — slightly longer release so chord tails ring out
        setAttack  (0.02f); setDecay   (0.3f);
        setSustain (0.6f);  setRelease (0.7f);

        // Filter — slightly darker start so modulation sweeps read as musical
        setFilterCutoff    (1400.0f);
        setFilterResonance (1.0f);
        setFilterEnvAmount (3000.0f);
        setFilterAttack  (0.01f); setFilterDecay (0.4f);
        setFilterSustain (0.3f);  setFilterRelease (0.5f);

        // OSC — sub at 0.3 for low-end warmth on the default patch
        setMainWaveform     (2);   // Saw
        setOsc1Level        (1.0f);
        setOsc2Waveform     (0);   // Sine
        setOsc2Level        (0.0f);
        setOsc2DetuneCents  (0.0f);
        setSubLevel         (0.3f);
        setDrive            (0.0f);
        setGain             (0.15f);

        // LFO
        setLFORate     (1.0f);
        setLFODepth    (0.0f);
        setLFOWaveform (LFOSine);

        // FX — subtle default delay so taps feel spacious
        setReverbMix     (0.2f); setReverbSize (0.5f);
        setDistDrive     (0.0f);
        setDelayTimeMs   (350.0f);
        setDelayFeedback (0.4f);
        setDelayMix      (0.18f);
        setDistEnabled   (true);
        setDelayEnabled  (true);
        setReverbEnabled (true);

        // Routing — factory defaults: mod wheel + accel Y both wired to filter
        // cutoff. Slots 2 and 3 stay free for the user's own routing.
        for (int i = 0; i < NumRoutingSlots; ++i)
        {
            setRoutingSource (i, ModSrcOff);
            setRoutingDest   (i, ModDestOff);
            setRoutingAmount (i, 0.0f);
        }
        setRoutingSource (0, ModSrcModWheel);
        setRoutingDest   (0, ModDestCutoff);
        setRoutingAmount (0, 0.5f);
        setRoutingSource (1, ModSrcAccelY);
        setRoutingDest   (1, ModDestCutoff);
        setRoutingAmount (1, 0.5f);
    }

    float getFilterAttack()  const { return filtAttack; }
    float getFilterDecay()   const { return filtDecay; }
    float getFilterSustain() const { return filtSustain; }
    float getFilterRelease() const { return filtRelease; }

    //==========================================================================
    // LFO parameters. LFO no longer has its own target picker — it's a routing
    // source like Mod Wheel and Accelerometer.

    void setLFORate     (float hz)  { lfoRate     = juce::jlimit (0.05f, 20.0f, hz); }
    void setLFODepth    (float d)   { lfoDepth    = juce::jlimit (0.0f, 1.0f, d); }
    void setLFOWaveform (int w)     { lfoWaveform = juce::jlimit (0, NumLFOWaves - 1, w); }

    float getLFORate()     const { return lfoRate; }
    float getLFODepth()    const { return lfoDepth; }
    int   getLFOWaveform() const { return lfoWaveform; }

    //==========================================================================
    // Modulation sources updated from external inputs.

    void setModWheel (float value01) { modWheel.store (juce::jlimit (0.0f, 1.0f, value01)); }
    float getModWheel() const { return modWheel.load(); }

    void setAccelX (float xMinus1to1) { accelX.store (juce::jlimit (-1.0f, 1.0f, xMinus1to1)); }
    void setAccelY (float yMinus1to1) { accelY.store (juce::jlimit (-1.0f, 1.0f, yMinus1to1)); }
    float getAccelX() const { return accelX.load(); }
    float getAccelY() const { return accelY.load(); }

    //==========================================================================
    // Routing matrix (4 slots, each: source + destination + depth).

    void setRoutingSource (int slot, int source)
    {
        if (slot < 0 || slot >= NumRoutingSlots) return;
        routings[slot].source = juce::jlimit (0, NumModSources - 1, source);
    }
    void setRoutingDest (int slot, int dest)
    {
        if (slot < 0 || slot >= NumRoutingSlots) return;
        routings[slot].dest = juce::jlimit (0, NumModDests - 1, dest);
    }
    void setRoutingAmount (int slot, float amount)
    {
        if (slot < 0 || slot >= NumRoutingSlots) return;
        routings[slot].amount = juce::jlimit (-1.0f, 1.0f, amount);
    }

    int   getRoutingSource (int slot) const { return (slot >= 0 && slot < NumRoutingSlots) ? routings[slot].source : 0; }
    int   getRoutingDest   (int slot) const { return (slot >= 0 && slot < NumRoutingSlots) ? routings[slot].dest   : 0; }
    float getRoutingAmount (int slot) const { return (slot >= 0 && slot < NumRoutingSlots) ? routings[slot].amount : 0.0f; }

private:
    //==========================================================================
    // Per-block modulation update. Computes LFO value, sums contributions from
    // mod-wheel routings and the LFO target, then pushes effective param values
    // into the voices / reverb. Linear modulation with per-destination scale.

    void processModulation (int numSamples)
    {
        if (currentSampleRate <= 0.0) return;

        // Advance LFO phase + compute LFO output (-1..+1 scaled by depth).
        lfoPhase += (double) lfoRate * (double) numSamples / currentSampleRate;
        lfoPhase -= std::floor (lfoPhase);

        float lfoRaw = 0.0f;
        switch (lfoWaveform)
        {
            case LFOSine:   lfoRaw = std::sin (lfoPhase * juce::MathConstants<double>::twoPi); break;
            case LFOTri:    lfoRaw = (float) (std::abs (2.0 * (lfoPhase - std::floor (lfoPhase + 0.5))) * 2.0 - 1.0); break;
            case LFOSaw:    lfoRaw = (float) (2.0 * lfoPhase - 1.0); break;
            case LFOSquare: lfoRaw = (lfoPhase < 0.5) ? 1.0f : -1.0f; break;
        }
        float lfoOut = lfoRaw * lfoDepth;

        // Pre-fetch all source values (cheap atomic loads).
        float srcVals[NumModSources];
        srcVals[ModSrcOff]      = 0.0f;
        srcVals[ModSrcModWheel] = modWheel.load();        // 0..1
        srcVals[ModSrcVelocity] = lastVelocity.load();    // 0..1
        srcVals[ModSrcAccelX]   = accelX.load();          // -1..+1
        srcVals[ModSrcAccelY]   = accelY.load();          // -1..+1
        srcVals[ModSrcLFO]      = lfoOut;                  // -depth..+depth

        // Sum modulation per destination.
        float modPerDest[NumModDests] = { 0 };
        for (int i = 0; i < NumRoutingSlots; ++i)
        {
            const auto& r = routings[i];
            modPerDest[r.dest] += srcVals[r.source] * r.amount;
        }

        // Apply with per-destination scaling.
        float effCutoff = filterCutoff    + modPerDest[ModDestCutoff]    * 5000.0f;
        float effReso   = filterReso      + modPerDest[ModDestResonance] * 4.0f;
        float effEnvAmt = filterEnvAmount + modPerDest[ModDestEnvAmount] * 5000.0f;
        float effGain   = masterGain      + modPerDest[ModDestGain]      * 0.2f;
        float effRevWet = reverbWet       + modPerDest[ModDestReverbMix] * 1.0f;
        float pitchSemi = modPerDest[ModDestPitch] * 7.0f; // ±7 semitones at full mod

        effDistDrive    = juce::jlimit (0.0f, 1.0f, distDrive + modPerDest[ModDestDistDrive] * 1.0f);
        effDelayMix     = juce::jlimit (0.0f, 1.0f, delayMix  + modPerDest[ModDestDelayMix]  * 1.0f);
        // ±1000 ms at full mod — wide enough for dub-style sweeps without
        // wrapping past the buffer's 2 s capacity.
        effDelayTimeMs  = juce::jlimit (5.0f, 2000.0f, delayTimeMs + modPerDest[ModDestDelayTime] * 1000.0f);

        effCutoff = juce::jlimit (20.0f,  20000.0f, effCutoff);
        effReso   = juce::jlimit (0.0f,    4.0f,    effReso);
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
                v->setPitchMod (pitchSemi);
            }
        }

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
    std::atomic<bool> killAll { false };

    juce::Synthesiser synth;
    juce::Reverb reverb;
    juce::Reverb::Parameters reverbParams;
    double currentSampleRate = 44100.0;

    float adsrAttack  = 0.02f;
    float adsrDecay   = 0.3f;
    float adsrSustain = 0.6f;
    float adsrRelease = 0.4f;
    int   mainWaveform = 2;    // 0=Sine, 1=Triangle, 2=Saw, 3=Square
    float osc1Level    = 1.0f; // 0..1
    int   osc2Waveform = 0;    // default Sine
    float osc2Level    = 0.0f; // 0..1 — off by default
    float osc2DetuneCents = 0.0f; // -100..+100
    float subLevel    = 0.0f;
    float drive       = 0.0f;  // per-voice osc drive (OSC tab)
    float masterGain  = 0.15f;
    float reverbWet   = 0.2f;
    float reverbSize  = 0.5f;

    // FX bus: master distortion + delay (apply post-mix in getNextAudioBlock).
    float distDrive    = 0.0f;
    bool  distEnabled  = true;

    float delayTimeMs  = 350.0f;
    float delayFeedback = 0.4f;
    float delayMix     = 0.18f;      // subtle default delay so single-note taps feel spacious
    bool  delayEnabled = true;
    std::vector<float> delayBuf;
    int   delayWritePos = 0;

    bool  reverbEnabled = true;

    // Effective (modulated) FX values pushed by processModulation each block.
    float effDistDrive = 0.0f;
    float effDelayMix  = 0.0f;
    float effDelayTimeMs = 350.0f;

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
    int   lfoWaveform = LFOSine;
    double lfoPhase = 0.0;

    // Modulation sources updated externally — atomic for cross-thread reads.
    std::atomic<float> modWheel     { 0.5f };  // 0..1
    std::atomic<float> lastVelocity { 0.0f };  // 0..1
    std::atomic<float> accelX       { 0.0f };  // -1..+1
    std::atomic<float> accelY       { 0.0f };  // -1..+1

    // Routing matrix: each slot picks any source → any destination, scaled
    // by depth (-1..+1).
    struct Routing
    {
        int   source = ModSrcOff;
        int   dest   = ModDestOff;
        float amount = 0.0f;
    };
    Routing routings[NumRoutingSlots];

    // Track last reverb wet value applied so we only push to reverb on change
    float lastEffectiveReverbWet = 0.2f;
};
