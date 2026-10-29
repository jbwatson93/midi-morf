#pragma once

#include <JuceHeader.h>
#include <array>

//==============================================================================
// Anything a track can play: the wavetable synth, the break player, and later
// the sampler and drum synth. A source only makes sound — what plays it (a
// controller page, a sequencer) is the track's input, routed elsewhere.
//
// Threading: render() and the *Audio note calls run on the audio thread.
// controller() may be called from any thread; allNotesOff() is audio thread
// for some sources, so callers off the audio thread go through
// TrackRack::postAllNotesOff. State is message thread.
class SoundSource
{
public:
    virtual ~SoundSource() = default;

    virtual juce::String typeId() const = 0;

    virtual void prepare (double sampleRate, int maxBlockSize) = 0;
    virtual void release() {}

    // Writes (does not add) `num` samples into `buffer` from `start`.
    virtual void render (juce::AudioBuffer<float>& buffer, int start, int num) = 0;

    // The channel lets two inputs on one track hold the same note number
    // without one's note-off cutting the other's note.
    virtual void noteOnAudio (int channel, int note, int velocity127) = 0;
    virtual void noteOffAudio (int channel, int note) = 0;
    virtual void allNotesOff() = 0;
    virtual void controller (int /*cc*/, int /*value*/) {}

    virtual juce::ValueTree getState() const = 0;
    virtual void setState (const juce::ValueTree&) = 0;
};

//==============================================================================
// The four tracks. Each renders its source into a scratch buffer, which is
// summed into the output — so the output region is written, not added to —
// then a safety limiter keeps the sum out of the converter's hard clip.
//
// Notes reach a track two ways:
//   • post*()  — message thread (controller pages). A lock-free FIFO, drained
//                at the start of the next render() call, so a note lands at
//                the start of the next sub-block, as it did on the synth's
//                own queue.
//   • noteOn() / noteOff() — audio thread (the sequencers' clock), immediate.
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

        limiterRelease = (float) std::exp (-1.0 / (0.08 * sampleRate));   // 80 ms
        limiterEnv = 0.0f;
    }

    void release()
    {
        for (auto* s : sources)
            if (s != nullptr)
                s->release();
    }

    //==========================================================================
    // Message thread. A full queue drops the event rather than block; at 1024
    // events between audio callbacks that would take a stuck finger plus a
    // very fast strum, and note-offs are re-sent by allNotesOff anyway.
    void postNoteOn (int track, int channel, int note, int velocity127) { post ({ Event::On, track, channel, note, velocity127 }); }
    void postNoteOff (int track, int channel, int note)                 { post ({ Event::Off, track, channel, note, 0 }); }
    void postAllNotesOff (int track)                                    { post ({ Event::AllOff, track, 0, 0, 0 }); }

    void postAllNotesOffEverywhere()
    {
        for (int t = 0; t < NumTracks; ++t)
            postAllNotesOff (t);
    }

    //==========================================================================
    // Audio thread.
    void noteOn (int track, int channel, int note, int velocity127)
    {
        if (auto* s = getSource (track))
            s->noteOnAudio (channel, note, velocity127);
    }

    void noteOff (int track, int channel, int note)
    {
        if (auto* s = getSource (track))
            s->noteOffAudio (channel, note);
    }

    void render (juce::AudioBuffer<float>& out, int start, int num)
    {
        juce::ScopedNoDenormals noDenormals;
        drainPosted();

        for (int ch = 0; ch < out.getNumChannels(); ++ch)
            out.clear (ch, start, num);

        for (int done = 0; done < num;)
        {
            const int n = juce::jmin (num - done, scratch.getNumSamples());

            for (auto* s : sources)
            {
                if (s == nullptr)
                    continue;

                s->render (scratch, 0, n);
                for (int ch = 0; ch < out.getNumChannels(); ++ch)
                    out.addFrom (ch, start + done, scratch, juce::jmin (ch, scratch.getNumChannels() - 1), 0, n);
            }

            done += n;
        }

        limit (out, start, num);
    }

private:
    struct Event
    {
        enum Type { On, Off, AllOff };
        Type type; int track, channel, note, velocity;
    };

    static constexpr int FifoSize = 1024;
    static bool validTrack (int t) { return t >= 0 && t < NumTracks; }

    void post (const Event& e)
    {
        if (! validTrack (e.track))
            return;

        const auto scope = fifo.write (1);
        if (scope.blockSize1 > 0)       events[(size_t) scope.startIndex1] = e;
        else if (scope.blockSize2 > 0)  events[(size_t) scope.startIndex2] = e;
    }

    void drainPosted()
    {
        const auto scope = fifo.read (fifo.getNumReady());
        auto handle = [this] (int start, int count)
        {
            for (int i = start; i < start + count; ++i)
            {
                const auto& e = events[(size_t) i];
                if (auto* s = getSource (e.track))
                {
                    if (e.type == Event::On)       s->noteOnAudio (e.channel, e.note, e.velocity);
                    else if (e.type == Event::Off) s->noteOffAudio (e.channel, e.note);
                    else                           s->allNotesOff();
                }
            }
        };
        handle (scope.startIndex1, scope.blockSize1);
        handle (scope.startIndex2, scope.blockSize2);
    }

    // Peak limiter at -0.5 dBFS: instant attack, so no sample ever passes
    // the threshold; 80 ms release. Below the threshold the gain is exactly
    // 1, so it costs nothing to normal levels. One gain for both channels
    // keeps the stereo image put.
    void limit (juce::AudioBuffer<float>& out, int start, int num)
    {
        constexpr float threshold = 0.944f;
        const int numCh = juce::jmin (2, out.getNumChannels());
        if (numCh == 0)
            return;

        float* L = out.getWritePointer (0, start);
        float* R = numCh > 1 ? out.getWritePointer (1, start) : nullptr;

        for (int i = 0; i < num; ++i)
        {
            const float peak = juce::jmax (std::abs (L[i]), R != nullptr ? std::abs (R[i]) : 0.0f);
            limiterEnv = juce::jmax (peak, limiterEnv * limiterRelease);

            if (limiterEnv > threshold)
            {
                const float g = threshold / limiterEnv;
                L[i] *= g;
                if (R != nullptr) R[i] *= g;
            }
        }
    }

    std::array<SoundSource*, NumTracks> sources {};
    juce::AudioBuffer<float> scratch;

    juce::AbstractFifo fifo { FifoSize };
    std::array<Event, FifoSize> events {};

    float limiterEnv = 0.0f, limiterRelease = 0.999f;
};
