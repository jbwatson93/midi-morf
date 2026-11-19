#pragma once

#include "SampleSource.h"
#include <atomic>

//==============================================================================
// The sampler as a track source. One sample (SampleSource), three ways to
// play it:
//
//   • Slice     — the slice sequencer's breaks: notes from the slice root up
//                 pick slices, one-shots at native speed.
//   • Keys      — the whole sample across the keyboard from a root note, with
//                 an ADSR.
//   • One-shot  — every note plays the whole sample at its own pitch.
//
// Grain used to be a fourth mode; it's its own sound now (GrainSource). The
// playback settings (Param) go straight to the player, Simpler style.
class SamplerSource : public SampleSource
{
public:
    enum Mode { Slice = 0, Keys = 1, OneShot = 2 };

    enum Param { Transpose = 0, Detune, Warp, GrainMs, Speed, Gain, VelSense, Loop,
                 FilterType, Cutoff, Resonance, NumParams };

    struct ParamInfo { const char* key; float min, max, def; };
    static const ParamInfo& paramInfo (Param p)
    {
        static const ParamInfo info[NumParams] = {
            { "transpose", -24.0f, 24.0f, 0.0f },     // semitones
            { "detune",    -50.0f, 50.0f, 0.0f },     // cents
            { "warp",       0.0f,  2.0f,  0.0f },     // BreakPlayer::Warp
            { "grain",      5.0f,  250.0f, 40.0f },   // ms
            { "speed",      0.25f, 4.0f,  1.0f },
            { "gain",     -24.0f, 12.0f,  0.0f },     // dB
            { "vel",        0.0f,  1.0f,  1.0f },
            { "loop",       0.0f,  1.0f,  0.0f },
            { "ftype",      0.0f,  3.0f,  0.0f },     // BreakPlayer::FilterType
            { "cutoff",    20.0f, 20000.0f, 20000.0f },
            { "res",        0.0f,  1.0f,  0.2f },
        };
        return info[p];
    }

    static const juce::StringArray& warpNames()
    {
        static const juce::StringArray n { "Beats", "Stretch", "Repitch" };
        return n;
    }

    SamplerSource()
    {
        for (int p = 0; p < NumParams; ++p)
            params[(size_t) p].store (paramInfo ((Param) p).def);
        applyParams();
    }

    static const juce::StringArray& modeNames()
    {
        static const juce::StringArray n { "Slice", "Keys", "One-shot" };
        return n;
    }

    //==========================================================================
    // Settings — message thread.
    void  setParam (Param p, float v)
    {
        const auto& info = paramInfo (p);
        params[(size_t) p].store (juce::jlimit (info.min, info.max, v));
        applyParams();
    }
    float getParam (Param p) const { return params[(size_t) p].load(); }

    //==========================================================================
    // Playing — any thread.
    void setMode (Mode m)      { mode.store ((int) m); }
    Mode getMode() const       { return (Mode) mode.load(); }

    void setEnvelope (float a, float d, float s, float r)
    {
        attack.store (juce::jlimit (0.0f, 5.0f, a));
        decay.store (juce::jlimit (0.0f, 5.0f, d));
        sustain.store (juce::jlimit (0.0f, 1.0f, s));
        release.store (juce::jlimit (0.0f, 8.0f, r));
    }

    juce::ADSR::Parameters getEnvelope() const { return { attack.load(), decay.load(), sustain.load(), release.load() }; }

    // Audio thread: where slices start and how long they are, from the slice
    // sequencer.
    void setSliceMapping (int rootNote, double stepBeats, double secondsPerBeat)
    {
        sliceRoot = rootNote;
        sliceStepBeats = stepBeats;
        sliceSecondsPerBeat = secondsPerBeat;
    }

    //==========================================================================
    // SoundSource
    juce::String typeId() const override { return "sampler"; }

    void prepare (double sampleRate, int) override
    {
        breaks.prepare (sampleRate);
    }

    void render (juce::AudioBuffer<float>& buffer, int start, int num) override
    {
        if (releasePending.exchange (false))
            breaks.releaseAll();

        float* L = buffer.getNumChannels() > 0 ? buffer.getWritePointer (0, start) : nullptr;
        float* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1, start) : nullptr;
        if (L != nullptr) juce::FloatVectorOperations::clear (L, num);
        if (R != nullptr) juce::FloatVectorOperations::clear (R, num);
        breaks.render (L, R, num);
    }

    void noteOnAudio (int, int note, int velocity127) override
    {
        const float vel = (float) velocity127 / 127.0f;
        switch (getMode())
        {
            case Slice:   breaks.trigger (note - sliceRoot, sliceStepBeats, sliceSecondsPerBeat, vel); break;
            case Keys:    breaks.startKey (note, vel, std::pow (2.0, (note - getRootNote()) / 12.0), false, getEnvelope()); break;
            case OneShot: breaks.startKey (note, vel, 1.0, true, {}); break;
        }
    }

    // Slices and one-shots ignore note-offs; STOP and panic cut them with
    // allNotesOff.
    void noteOffAudio (int, int note) override
    {
        if (getMode() == Keys)
            breaks.stopKey (note);
    }

    // Any thread: the voices aren't atomic, so this flags the audio thread.
    void allNotesOff() override { releasePending.store (true); }

    //==========================================================================
    static inline const juce::Identifier stateType { "Sampler" };

    juce::ValueTree getState() const override
    {
        juce::ValueTree t (stateType);
        t.setProperty ("mode", mode.load(), nullptr);
        writeSample (t);
        t.setProperty ("env", juce::String (attack.load()) + "," + juce::String (decay.load()) + ","
                              + juce::String (sustain.load()) + "," + juce::String (release.load()), nullptr);
        for (int p = 0; p < NumParams; ++p)
            t.setProperty (paramInfo ((Param) p).key, params[(size_t) p].load(), nullptr);
        return t;
    }

    // Mode 3 was Grain, which is its own sound now: what was loaded for it
    // plays as Keys here (the project loader moves the clouds to the track's
    // GrainSource).
    void setState (const juce::ValueTree& t) override
    {
        const int m = t.getProperty ("mode", (int) Slice);
        setMode (m == 3 ? Keys : (Mode) juce::jlimit (0, 2, m));
        for (int p = 0; p < NumParams; ++p)
            setParam ((Param) p, (float) t.getProperty (paramInfo ((Param) p).key, paramInfo ((Param) p).def));

        auto env = juce::StringArray::fromTokens (t["env"].toString(), ",", "");
        if (env.size() == 4)
            setEnvelope (env[0].getFloatValue(), env[1].getFloatValue(), env[2].getFloatValue(), env[3].getFloatValue());

        readSample (t);
    }

private:
    void applyParams()
    {
        breaks.setTune (getParam (Transpose) + getParam (Detune) * 0.01f);
        breaks.setWarp ((BreakPlayer::Warp) juce::roundToInt (getParam (Warp)));
        breaks.setGrainMs (getParam (GrainMs));
        breaks.setSpeed (getParam (Speed));
        breaks.setGain (juce::Decibels::decibelsToGain (getParam (Gain)));
        breaks.setVelocitySense (getParam (VelSense));
        breaks.setLoop (getParam (Loop) >= 0.5f);
        breaks.setFilter ((BreakPlayer::FilterType) juce::roundToInt (getParam (FilterType)), getParam (Cutoff), getParam (Resonance));
    }

    std::array<std::atomic<float>, NumParams> params;

    std::atomic<int> mode { Slice };
    std::atomic<float> attack { 0.002f }, decay { 0.3f }, sustain { 1.0f }, release { 0.3f };
    std::atomic<bool> releasePending { false };

    int sliceRoot = 36;          // audio thread
    double sliceStepBeats = 0.25;
    double sliceSecondsPerBeat = 0.0;   // the project's tempo, to warp slices to
};
