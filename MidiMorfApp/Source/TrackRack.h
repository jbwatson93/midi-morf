#pragma once

#include <JuceHeader.h>
#include <array>

//==============================================================================
// Anything a track can play: the wavetable synth, the break player, and later
// the sampler and drum synth. A source only makes sound — what plays it (a
// controller page, a sequencer) is the track's input, routed elsewhere.
//
// Threading: render() and the *Audio note calls run on the audio thread.
// allNotesOff() and controller() may be called from any thread. State is
// message thread.
class SoundSource
{
public:
    virtual ~SoundSource() = default;

    virtual juce::String typeId() const = 0;

    virtual void prepare (double sampleRate, int maxBlockSize) = 0;
    virtual void release() {}

    // Writes (does not add) `num` samples into `buffer` from `start`.
    virtual void render (juce::AudioBuffer<float>& buffer, int start, int num) = 0;

    virtual void noteOnAudio (int note, int velocity127) = 0;
    virtual void noteOffAudio (int note) = 0;
    virtual void allNotesOff() = 0;
    virtual void controller (int /*cc*/, int /*value*/) {}

    virtual juce::ValueTree getState() const = 0;
    virtual void setState (const juce::ValueTree&) = 0;
};

//==============================================================================
// The four tracks. Each renders its source into a scratch buffer, which is
// summed into the output — so the output region is written, not added to,
// and per-track level and the master bus can slot in between later.
//
// Sources are assigned before audio starts and outlive the rack.
class TrackRack
{
public:
    static constexpr int NumTracks = 4;

    void setSource (int track, SoundSource* s)
    {
        if (validTrack (track))
            sources[(size_t) track] = s;
    }

    SoundSource* getSource (int track) const { return validTrack (track) ? sources[(size_t) track] : nullptr; }

    void prepare (double sampleRate, int maxBlockSize)
    {
        scratch.setSize (2, juce::jmax (maxBlockSize, 1024));
        for (auto* s : sources)
            if (s != nullptr)
                s->prepare (sampleRate, scratch.getNumSamples());
    }

    void release()
    {
        for (auto* s : sources)
            if (s != nullptr)
                s->release();
    }

    //==========================================================================
    // Audio thread.
    void noteOn (int track, int note, int velocity127)
    {
        if (auto* s = getSource (track))
            s->noteOnAudio (note, velocity127);
    }

    void noteOff (int track, int note)
    {
        if (auto* s = getSource (track))
            s->noteOffAudio (note);
    }

    void render (juce::AudioBuffer<float>& out, int start, int num)
    {
        for (int ch = 0; ch < out.getNumChannels(); ++ch)
            out.clear (ch, start, num);

        while (num > 0)
        {
            const int n = juce::jmin (num, scratch.getNumSamples());

            for (auto* s : sources)
            {
                if (s == nullptr)
                    continue;

                s->render (scratch, 0, n);
                for (int ch = 0; ch < out.getNumChannels(); ++ch)
                    out.addFrom (ch, start, scratch, juce::jmin (ch, scratch.getNumChannels() - 1), 0, n);
            }

            start += n;
            num -= n;
        }
    }

private:
    static bool validTrack (int t) { return t >= 0 && t < NumTracks; }

    std::array<SoundSource*, NumTracks> sources {};
    juce::AudioBuffer<float> scratch;
};
