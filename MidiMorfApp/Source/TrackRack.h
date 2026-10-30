#pragma once

#include <JuceHeader.h>
#include "MixerFx.h"
#include <array>
#include <vector>
#include <algorithm>

//==============================================================================
// Anything a track can play: the wavetable synth, the break player, and later
// the sampler and drum synth. A source only makes sound — what plays it (a
// controller page, a sequencer) is the track's input, routed elsewhere.
//
// Threading: render() and the *Audio note calls run on the audio thread.
// controller() and allNotesOff() may be called from any thread. State is
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
// Hears the notes played live (the posted ones: controllers, pads), on the
// audio thread as each reaches its track — the clip recorder.
class LiveEventObserver
{
public:
    virtual ~LiveEventObserver() = default;
    virtual void liveEvent (int track, bool on, int channel, int note, int velocity127) = 0;
};

//==============================================================================
// Whatever processes the summed tracks before the limiter — the punch-in FX.
// Audio thread, in place.
class MasterProcessor
{
public:
    virtual ~MasterProcessor() = default;
    virtual void prepare (double sampleRate, int maxBlockSize) = 0;
    virtual void process (juce::AudioBuffer<float>& buffer, int start, int num) = 0;
};

//==============================================================================
// The four tracks. Each renders its source into a scratch buffer, which is
// summed into the output — so the output region is written, not added to —
// then through the master processor (punch-in FX), then a safety limiter
// keeps the sum out of the converter's hard clip.
//
// Notes reach a track two ways:
//   • post*()  — message thread (controller pages). A lock-free FIFO, drained
//                at the start of the next render() call, so a note lands at
//                the start of the next sub-block, as it did on the synth's
//                own queue.
//   • noteOn() / noteOff() — audio thread (the sequencers' clock), immediate.
//
// Sources outlive the rack. A track's source can be swapped while audio runs
// (setSource, message thread): every source that may ever play is added to
// the pool first, so prepare() has already readied it.
class TrackRack
{
public:
    static constexpr int NumTracks = 4;

    void addToPool (SoundSource* s)
    {
        if (s != nullptr && std::find (pool.begin(), pool.end(), s) == pool.end())
            pool.push_back (s);
    }

    // The outgoing source is silenced, so it has no stale voices if it comes
    // back later.
    void setSource (int track, SoundSource* s)
    {
        if (! validTrack (track))
            return;

        addToPool (s);
        if (auto* old = sources[(size_t) track].exchange (s); old != nullptr && old != s)
            old->allNotesOff();
    }

    SoundSource* getSource (int track) const { return validTrack (track) ? sources[(size_t) track].load() : nullptr; }

    // Set once, before audio starts.
    void setMasterProcessor (MasterProcessor* m) { master = m; }
    void setLiveEventObserver (LiveEventObserver* o) { observer = o; }

    // Track volume, 0..MaxVolume linear (1 = unity). Any thread; the audio
    // thread ramps to it over 20 ms so a fader move never clicks.
    static constexpr float MaxVolume = 1.5f;
    void  setVolume (int track, float v) { if (validTrack (track)) volumes[(size_t) track].store (juce::jlimit (0.0f, MaxVolume, v)); }
    float getVolume (int track) const    { return validTrack (track) ? volumes[(size_t) track].load() : 0.0f; }

    // Pan, -1 (left) .. +1 (right). A balance law: the centre is exactly
    // unity on both sides, and panning turns the far side down.
    void  setPan (int track, float p)    { if (validTrack (track)) pans[(size_t) track].store (juce::jlimit (-1.0f, 1.0f, p)); }
    float getPan (int track) const       { return validTrack (track) ? pans[(size_t) track].load() : 0.0f; }

    // Mute keeps the volume setting and ramps like it.
    void setMuted (int track, bool m)    { if (validTrack (track)) muted[(size_t) track].store (m); }
    bool isMuted (int track) const       { return validTrack (track) && muted[(size_t) track].load(); }

    // Solo: while any track is soloed, the others are silent (sends too).
    void setSolo (int track, bool s)     { if (validTrack (track)) solo[(size_t) track].store (s); }
    bool isSolo (int track) const        { return validTrack (track) && solo[(size_t) track].load(); }

    // Sends, 0..1, post-fader: 0 the delay bus, 1 the reverb bus.
    enum Bus { DelayBus = 0, ReverbBus = 1 };
    void  setSend (int track, int bus, float v) { if (validTrack (track) && (bus == 0 || bus == 1)) sends[(size_t) track][(size_t) bus].store (juce::jlimit (0.0f, 1.0f, v)); }
    float getSend (int track, int bus) const    { return validTrack (track) && (bus == 0 || bus == 1) ? sends[(size_t) track][(size_t) bus].load() : 0.0f; }

    TrackInsert& getInsert (int track) { return inserts[(size_t) juce::jlimit (0, NumTracks - 1, track)]; }
    SendBuses& getBuses() noexcept     { return buses; }

    // Master level, 0..MaxVolume, before the limiter. Ramps like the tracks.
    void  setMasterVolume (float v) { masterVolume.store (juce::jlimit (0.0f, MaxVolume, v)); }
    float getMasterVolume() const   { return masterVolume.load(); }

    // The delay bus follows this tempo (set once, before audio starts).
    void setTempoSource (std::function<double()> f) { tempoSource = std::move (f); }

    void prepare (double sampleRate, int maxBlockSize)
    {
        scratch.setSize (2, juce::jmax (maxBlockSize, 1024));
        for (auto* s : pool)
            s->prepare (sampleRate, scratch.getNumSamples());

        if (master != nullptr)
            master->prepare (sampleRate, scratch.getNumSamples());

        limiterRelease = (float) std::exp (-1.0 / (0.08 * sampleRate));   // 80 ms
        limiterEnv = 0.0f;

        for (auto& ins : inserts) ins.prepare (sampleRate);
        buses.prepare (sampleRate, scratch.getNumSamples());
        masterGain.reset (sampleRate, 0.02);
        masterGain.setCurrentAndTargetValue (masterVolume.load());
        for (auto& t : lastSend) t.fill (0.0f);

        for (int t = 0; t < NumTracks; ++t)
            for (int side = 0; side < 2; ++side)
            {
                auto& g = gains[(size_t) t][(size_t) side];
                g.reset (sampleRate, 0.02);
                g.setCurrentAndTargetValue (targetGain (t, side));
            }
    }

    void release()
    {
        for (auto* s : pool)
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

            buses.clearInputs (n);
            anySolo = std::any_of (solo.begin(), solo.end(), [] (const auto& s) { return s.load(); });

            for (int t = 0; t < NumTracks; ++t)
            {
                auto* s = sources[(size_t) t].load();
                if (s == nullptr)
                    continue;

                s->render (scratch, 0, n);
                if (! inserts[(size_t) t].isNeutral())
                    inserts[(size_t) t].process (scratch.getWritePointer (0), scratch.getNumChannels() > 1 ? scratch.getWritePointer (1) : nullptr, n);
                mixTrack (t, out, start + done, n);
                feedSends (t, n);
            }

            if (! buses.isIdle())
                buses.process (out.getWritePointer (0, start + done), out.getNumChannels() > 1 ? out.getWritePointer (1, start + done) : nullptr,
                               n, tempoSource ? tempoSource() : 120.0);

            done += n;
        }

        if (master != nullptr)
            master->process (out, start, num);

        applyMasterVolume (out, start, num);
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
                if (observer != nullptr && e.type != Event::AllOff)
                    observer->liveEvent (e.track, e.type == Event::On, e.channel, e.note, e.velocity);
            }
        };
        handle (scope.startIndex1, scope.blockSize1);
        handle (scope.startIndex2, scope.blockSize2);
    }

    // Volume x balance for one side (0 left, 1 right).
    float targetGain (int t, int side) const
    {
        if (muted[(size_t) t].load() || (anySolo && ! solo[(size_t) t].load()))
            return 0.0f;
        const float pan = pans[(size_t) t].load();
        const float balance = side == 0 ? juce::jmin (1.0f, 1.0f - pan) : juce::jmin (1.0f, 1.0f + pan);
        return volumes[(size_t) t].load() * balance;
    }

    // Adds the track's scratch render into `out` at its volume and pan.
    // Steady gains are a plain (gained) add — unity is exactly the unscaled
    // signal; while a change is ramping, per sample.
    void mixTrack (int t, juce::AudioBuffer<float>& out, int start, int n)
    {
        auto& g = gains[(size_t) t];
        for (int side = 0; side < 2; ++side)
            g[(size_t) side].setTargetValue (targetGain (t, side));
        const int numOut = out.getNumChannels(), numIn = scratch.getNumChannels();

        if (! g[0].isSmoothing() && ! g[1].isSmoothing())
        {
            for (int ch = 0; ch < numOut; ++ch)
            {
                const float gain = g[(size_t) juce::jmin (ch, 1)].getCurrentValue();
                if (gain > 0.0f)
                    out.addFrom (ch, start, scratch, juce::jmin (ch, numIn - 1), 0, n, gain);
            }
            return;
        }

        float* dst[2] = { out.getWritePointer (0, start), numOut > 1 ? out.getWritePointer (1, start) : nullptr };
        const float* src[2] = { scratch.getReadPointer (0), scratch.getReadPointer (juce::jmin (1, numIn - 1)) };
        for (int i = 0; i < n; ++i)
        {
            const float gl = g[0].getNextValue(), gr = g[1].getNextValue();
            dst[0][i] += gl * src[0][i];
            if (dst[1] != nullptr) dst[1][i] += gr * src[1][i];
        }
    }

    // Post-fader sends: the track as it was just mixed (gain and pan), times
    // its send, ramped across the block when the send moves.
    void feedSends (int t, int n)
    {
        const float gl = gains[(size_t) t][0].getCurrentValue(), gr = gains[(size_t) t][1].getCurrentValue();
        const int numIn = scratch.getNumChannels();
        const float* src[2] = { scratch.getReadPointer (0), scratch.getReadPointer (juce::jmin (1, numIn - 1)) };

        for (int bus = 0; bus < 2; ++bus)
        {
            const float target = sends[(size_t) t][(size_t) bus].load();
            float& last = lastSend[(size_t) t][(size_t) bus];
            if (target <= 0.0f && last <= 0.0f)
                continue;

            buses.touched();
            float* dst[2] = { bus == DelayBus ? buses.delayIn (0) : buses.reverbIn (0),
                              bus == DelayBus ? buses.delayIn (1) : buses.reverbIn (1) };
            const float step = (target - last) / (float) n;
            for (int i = 0; i < n; ++i)
            {
                const float amount = last + step * (float) i;
                dst[0][i] += src[0][i] * gl * amount;
                dst[1][i] += src[1][i] * gr * amount;
            }
            last = target;
        }
    }

    void applyMasterVolume (juce::AudioBuffer<float>& out, int start, int num)
    {
        masterGain.setTargetValue (masterVolume.load());
        if (! masterGain.isSmoothing())
        {
            const float g = masterGain.getCurrentValue();
            if (g != 1.0f)
                for (int ch = 0; ch < out.getNumChannels(); ++ch)
                    out.applyGain (ch, start, num, g);
            return;
        }
        for (int i = 0; i < num; ++i)
        {
            const float g = masterGain.getNextValue();
            for (int ch = 0; ch < out.getNumChannels(); ++ch)
                out.getWritePointer (ch, start)[i] *= g;
        }
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

    std::array<std::atomic<SoundSource*>, NumTracks> sources {};
    std::vector<SoundSource*> pool;   // message thread
    MasterProcessor* master = nullptr;
    LiveEventObserver* observer = nullptr;
    std::array<std::atomic<float>, NumTracks> volumes { 1.0f, 1.0f, 1.0f, 1.0f };
    std::array<std::atomic<float>, NumTracks> pans {};
    std::array<std::atomic<bool>, NumTracks> muted {}, solo {};
    std::array<std::array<std::atomic<float>, 2>, NumTracks> sends {};
    std::array<std::array<float, 2>, NumTracks> lastSend {};   // audio thread
    std::array<TrackInsert, NumTracks> inserts;
    SendBuses buses;
    std::atomic<float> masterVolume { 1.0f };
    juce::SmoothedValue<float> masterGain;   // audio thread
    std::function<double()> tempoSource;
    bool anySolo = false;                    // audio thread
    std::array<std::array<juce::SmoothedValue<float>, 2>, NumTracks> gains;   // audio thread, per side
    juce::AudioBuffer<float> scratch;

    juce::AbstractFifo fifo { FifoSize };
    std::array<Event, FifoSize> events {};

    float limiterEnv = 0.0f, limiterRelease = 0.999f;
};
