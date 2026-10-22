#pragma once

#include <JuceHeader.h>
#include <atomic>
#include "SynthEngine.h"
#include "WavetableEngine.h"

//==============================================================================
// ActiveSynth — the one thing the controllers play. Forwards the shared MIDI /
// mod-source API to whichever built-in engine is selected and renders it.
//
// On a switch the old engine gets all-notes-off and keeps rendering for a few
// seconds so its release, delay and reverb tails ring out instead of cutting.
class ActiveSynth : public juce::AudioSource
{
public:
    enum Engine { Classic = 0, Wavetable = 1 };

    ActiveSynth (SynthEngine& c, WavetableEngine& w) : classic (c), wavetable (w) {}

    void setEngine (int e)
    {
        e = juce::jlimit (0, 1, e);
        if (engine.load() == e)
            return;

        if (e == Classic) wavetable.allNotesOff();
        else              classic.allNotesOff();

        engine.store (e);
        tailRequested.store (true);
    }

    int getEngine() const { return engine.load(); }

    //==========================================================================
    void noteOn (int note, float velocity, int channel = 1)
    {
        if (engine.load() == Classic) classic.noteOn (note, velocity, channel);
        else                          wavetable.noteOn (note, velocity, channel);
    }

    void noteOff (int note, int channel = 1)
    {
        if (engine.load() == Classic) classic.noteOff (note, channel);
        else                          wavetable.noteOff (note, channel);
    }

    void allNotesOff()
    {
        classic.allNotesOff();
        wavetable.allNotesOff();
    }

    // Mod sources are cheap atomics — keep both engines current so a switch
    // never jumps.
    void setModWheel (float v) { classic.setModWheel (v); wavetable.setModWheel (v); }
    void setAccelX (float v)   { classic.setAccelX (v);   wavetable.setAccelX (v); }
    void setAccelY (float v)   { classic.setAccelY (v);   wavetable.setAccelY (v); }

    //==========================================================================
    void prepareToPlay (int samplesPerBlockExpected, double sampleRate) override
    {
        classic.prepareToPlay (samplesPerBlockExpected, sampleRate);
        wavetable.prepareToPlay (samplesPerBlockExpected, sampleRate);
        tailBuffer.setSize (2, juce::jmax (samplesPerBlockExpected, 4096));
        tailLength = (int) (sampleRate * 4.0);
    }

    void releaseResources() override
    {
        classic.releaseResources();
        wavetable.releaseResources();
    }

    void getNextAudioBlock (const juce::AudioSourceChannelInfo& info) override
    {
        const int active = engine.load();
        juce::AudioSource& now  = active == Classic ? (juce::AudioSource&) classic : wavetable;
        juce::AudioSource& prev = active == Classic ? (juce::AudioSource&) wavetable : classic;

        if (tailRequested.exchange (false))
            tailRemaining = tailLength;

        now.getNextAudioBlock (info);

        if (tailRemaining > 0 && info.numSamples <= tailBuffer.getNumSamples())
        {
            juce::AudioSourceChannelInfo tail (&tailBuffer, 0, info.numSamples);
            prev.getNextAudioBlock (tail);
            for (int ch = 0; ch < info.buffer->getNumChannels(); ++ch)
                info.buffer->addFrom (ch, info.startSample, tailBuffer,
                                      juce::jmin (ch, tailBuffer.getNumChannels() - 1), 0, info.numSamples);
            tailRemaining -= info.numSamples;
        }
    }

private:
    SynthEngine& classic;
    WavetableEngine& wavetable;
    std::atomic<int>  engine { Classic };
    std::atomic<bool> tailRequested { false };
    juce::AudioBuffer<float> tailBuffer;
    int tailLength = 0;
    int tailRemaining = 0;
};
