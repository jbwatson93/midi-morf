#pragma once

#include "SampleSource.h"
#include "GrainEngine.h"
#include <atomic>

//==============================================================================
// The Grain sound: one sample (SampleSource) played as clouds of grains — one
// per finger on the waveform, one per held note (GrainEngine). The grains mix
// into the player's voice buffers, so TONE, the filter and GAIN apply to them
// as they do in the Sampler.
//
// Until 3.1 this was the Sampler's fourth mode. Its param keys are the ones
// the Sampler saved them under, so an old Sampler state loads straight in.
class GrainSource : public SampleSource,
                    private BreakPlayer::ExtraVoices
{
public:
    enum Param { Transpose = 0, Detune, Gain, VelSense, FilterType, Cutoff, Resonance,
                 Density, Size, Spray, Scan, Loop, Jitter, Spread, Window, Reverse,
                 Release, YAxis, Position, Hold, Freeze, NumParams };

    struct ParamInfo { const char* key; float min, max, def; };
    static const ParamInfo& paramInfo (Param p)
    {
        static const ParamInfo info[NumParams] = {
            { "transpose", -24.0f, 24.0f, 0.0f },     // semitones
            { "detune",    -50.0f, 50.0f, 0.0f },     // cents
            { "gain",     -24.0f, 12.0f,  0.0f },     // dB
            { "vel",        0.0f,  1.0f,  1.0f },
            { "ftype",      0.0f,  3.0f,  0.0f },     // BreakPlayer::FilterType
            { "cutoff",    20.0f, 20000.0f, 20000.0f },
            { "res",        0.0f,  1.0f,  0.2f },
            { "gDensity",   1.0f,  100.0f, 30.0f },   // grains per second
            { "gSize",      5.0f,  500.0f, 80.0f },   // ms
            { "gSpray",     0.0f,  1.0f,  0.1f },
            { "gScan",     -2.0f,  2.0f,  1.0f },     // x the sample's own speed
            { "gLoop",     10.0f,  2000.0f, 300.0f }, // ms
            { "gJitter",    0.0f,  12.0f, 0.0f },     // semitones
            { "gSpread",    0.0f,  1.0f,  0.5f },
            { "gWindow",    0.0f,  1.0f,  0.0f },
            { "gReverse",   0.0f,  1.0f,  0.0f },
            { "gRelease",   5.0f,  4000.0f, 300.0f }, // ms
            { "gYAxis",     0.0f,  3.0f,  0.0f },     // GrainEngine::YAxis
            { "gPos",       0.0f,  1.0f,  0.25f },
            { "gHold",      0.0f,  1.0f,  0.0f },
            { "gFreeze",    0.0f,  1.0f,  0.0f },
        };
        return info[p];
    }

    static const juce::StringArray& yAxisNames()
    {
        static const juce::StringArray n { "Size", "Pitch", "Spray", "Density" };
        return n;
    }

    // What a new Grain track starts with: a pad holds still under a finger.
    static constexpr const char* defaultPreset = "Warm Pad";

    GrainSource()
    {
        for (int p = 0; p < NumParams; ++p)
            params[(size_t) p].store (paramInfo ((Param) p).def);
        applyParams();
        breaks.setExtraVoices (this);
    }

    GrainEngine& grains() noexcept { return grain; }

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
    // SoundSource
    juce::String typeId() const override { return "grain"; }

    void prepare (double sampleRate, int) override
    {
        breaks.prepare (sampleRate);
        grain.prepare (sampleRate);
    }

    void render (juce::AudioBuffer<float>& buffer, int start, int num) override
    {
        if (releasePending.exchange (false))
            for (int i = 0; i < 128; ++i)
                grain.noteOff (i);

        float* L = buffer.getNumChannels() > 0 ? buffer.getWritePointer (0, start) : nullptr;
        float* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1, start) : nullptr;
        if (L != nullptr) juce::FloatVectorOperations::clear (L, num);
        if (R != nullptr) juce::FloatVectorOperations::clear (R, num);
        breaks.render (L, R, num);
    }

    void noteOnAudio (int, int note, int velocity127) override
    {
        grain.noteOn (note, getRootNote(), (float) velocity127 / 127.0f);
    }

    void noteOffAudio (int, int note) override { grain.noteOff (note); }

    // Any thread: the note clouds aren't atomic, so this flags the audio
    // thread. Held finger clouds let go too, so a track that switches away
    // and back doesn't come back droning.
    void allNotesOff() override
    {
        releasePending.store (true);
        grain.releaseAllTouches();
    }

    //==========================================================================
    static inline const juce::Identifier stateType { "Grain" };

    juce::ValueTree getState() const override
    {
        juce::ValueTree t (stateType);
        writeSample (t);
        for (int p = 0; p < NumParams; ++p)
            t.setProperty (paramInfo ((Param) p).key, params[(size_t) p].load(), nullptr);
        return t;
    }

    // Also takes a Sampler state from before Grain was its own sound.
    void setState (const juce::ValueTree& t) override
    {
        for (int p = 0; p < NumParams; ++p)
            setParam ((Param) p, (float) t.getProperty (paramInfo ((Param) p).key, paramInfo ((Param) p).def));
        readSample (t);
    }

private:
    void applyParams()
    {
        breaks.setTune (getParam (Transpose) + getParam (Detune) * 0.01f);
        breaks.setGain (juce::Decibels::decibelsToGain (getParam (Gain)));
        breaks.setVelocitySense (getParam (VelSense));
        breaks.setFilter ((BreakPlayer::FilterType) juce::roundToInt (getParam (FilterType)), getParam (Cutoff), getParam (Resonance));

        auto& g = grain.settings;
        g.density   = getParam (Density);
        g.sizeMs    = getParam (Size);
        g.spray     = getParam (Spray);
        g.scan      = getParam (Scan);
        g.loopMs    = getParam (Loop);
        g.jitter    = getParam (Jitter);
        g.spread    = getParam (Spread);
        g.window    = getParam (Window);
        g.reverse   = getParam (Reverse);
        g.releaseMs = getParam (Release);
        g.yAxis     = juce::roundToInt (getParam (YAxis));
        g.position  = getParam (Position);
        g.freeze    = getParam (Freeze) >= 0.5f;
        g.tuneSemis = getParam (Transpose) + getParam (Detune) * 0.01f;
        g.velSense  = getParam (VelSense);
    }

    // BreakPlayer::ExtraVoices: the clouds.
    bool renderVoices (const Break& b, float* L, float* R, int n) override
    {
        return grain.render (b, L, R, n);
    }

    GrainEngine grain;
    std::array<std::atomic<float>, NumParams> params;
    std::atomic<bool> releasePending { false };
};
