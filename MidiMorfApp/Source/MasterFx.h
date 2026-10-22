#pragma once

#include <JuceHeader.h>
#include <vector>

//==============================================================================
// MasterFx — the post-voice bus: tanh distortion → mono-fed delay → reverb,
// each bypassable. Same DSP as the FX section inside SynthEngine; this copy
// is shared so new engines don't each grow their own. SynthEngine can move
// onto it later — it's left alone for now because it has already shipped.
//
// Base params are set from the UI thread. Each block the engine passes the
// modulated values in via setModulated() before calling process().
class MasterFx
{
public:
    MasterFx()
    {
        reverbParams.roomSize = reverbSize;
        reverbParams.damping  = 0.4f;
        reverbParams.wetLevel = reverbWet;
        reverbParams.dryLevel = 1.0f - reverbWet;
        reverbParams.width    = 1.0f;
        reverb.setParameters (reverbParams);
    }

    void prepare (double sampleRate)
    {
        currentSampleRate = sampleRate;
        reverb.setSampleRate (sampleRate);
        delayBuf.assign ((size_t) (sampleRate * 2.0), 0.0f);
        delayWritePos = 0;
    }

    // Called on the audio thread once per block, before process().
    void setModulated (float distDriveMod, float delayMixMod, float delayTimeMsMod, float reverbWetMod)
    {
        effDistDrive   = juce::jlimit (0.0f, 1.0f, distDrive + distDriveMod);
        effDelayMix    = juce::jlimit (0.0f, 1.0f, delayMix + delayMixMod);
        effDelayTimeMs = juce::jlimit (5.0f, 2000.0f, delayTimeMs + delayTimeMsMod);

        float wet = juce::jlimit (0.0f, 1.0f, reverbWet + reverbWetMod);
        if (std::abs (wet - lastReverbWet) > 0.001f || std::abs (reverbSize - lastReverbSize) > 0.001f)
        {
            reverbParams.wetLevel = wet;
            reverbParams.dryLevel = 1.0f - wet;
            reverbParams.roomSize = reverbSize;
            reverb.setParameters (reverbParams);
            lastReverbWet  = wet;
            lastReverbSize = reverbSize;
        }
    }

    void process (float* L, float* R, int n)
    {
        if (L == nullptr)
            return;
        if (R == nullptr)
            R = L;

        if (distEnabled && effDistDrive > 0.001f)
        {
            float k    = 1.0f + effDistDrive * 8.0f;
            float norm = 1.0f / std::tanh (k);
            for (int i = 0; i < n; ++i)
            {
                L[i] = std::tanh (L[i] * k) * norm;
                if (R != L) R[i] = std::tanh (R[i] * k) * norm;
            }
        }

        if (delayEnabled && effDelayMix > 0.001f && ! delayBuf.empty())
        {
            const int bufSize = (int) delayBuf.size();
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
            if (R != L) reverb.processStereo (L, R, n);
            else        reverb.processMono (L, n);
        }
    }

    // Base parameters (UI thread)
    float distDrive     = 0.0f;
    bool  distEnabled   = true;
    float delayTimeMs   = 350.0f;
    float delayFeedback = 0.4f;
    float delayMix      = 0.18f;
    bool  delayEnabled  = true;
    float reverbWet     = 0.2f;
    float reverbSize    = 0.5f;
    bool  reverbEnabled = true;

private:
    double currentSampleRate = 44100.0;
    juce::Reverb reverb;
    juce::Reverb::Parameters reverbParams;
    std::vector<float> delayBuf;
    int   delayWritePos = 0;

    float effDistDrive   = 0.0f;
    float effDelayMix    = 0.18f;
    float effDelayTimeMs = 350.0f;
    float lastReverbWet  = 0.2f;
    float lastReverbSize = 0.5f;
};
