#pragma once

#include <JuceHeader.h>
#include "TrackRack.h"
#include "Sampler.h"
#include "StepSequencerEngine.h"
#include "SliceSequencerEngine.h"
#include "MidiMorfProtocol.h"
#include "DrumSynth.h"
#include "SessionModel.h"
#include "NoteClip.h"

//==============================================================================
// The sequencers in Local mode, on the phone's own clock, plus the note-clip
// players and the clip recorder.
//
// Every track has a sequencer slot: nothing, a Step-Seq, a Breaker (the slice
// sequencer), a Drum Grid or a Remix Lane, each with its own engine and
// pattern, so two tracks can both run a Step-Seq.
//
// The Remix Lane plays the track's sample in 16th slices locked to the
// clock, so it loops in time; a tap jumps playback to another place in it
// (from the next 16th, then onward from there), holding two loops between
// them, and SYNC puts it back in line with the bar — monome mlr style. The Step-Seq and Breaker engines are the
// ones the server runs, ticked at 24 PPQN; the Drum Grid is the phone's own
// and fires at the exact sample (swing included).
//
// The pages talk to the engines through EventRouter as they talk to the
// server, without saying which track they are: those messages go to the
// active track (setActiveTrack), which MainComponent keeps on the page being
// shown or touched.
//
// This is the AudioSource the device plays. Each block is cut at clock ticks
// and at every exact-sample event, so notes land on the right sample.
//
// Threading: the engines are only ever ticked on the audio thread. Edits from
// the pages take `lock` briefly; the engines' note output is queued under the
// same lock and drained on the audio thread, so the track sources are only
// ever played from there.
class LocalSequencers : public juce::AudioSource,
                        private LiveEventObserver
{
public:
    // What sequencer a track has. A strong type, so a track number can't be
    // passed where a kind is meant (or the other way round).
    enum class Kind { None = -1, Step = 0, Slice = 1, Drums = 2, Remix = 3 };

    static constexpr int NumTracks = TrackRack::NumTracks;
    static constexpr int DrumLanes = ::Drums::NumDrums, DrumSteps = 16;

    explicit LocalSequencers (TrackRack& r) : rack (r)
    {
        for (int t = 0; t < NumTracks; ++t)
        {
            auto& s = slots[(size_t) t];
            s.sink  = std::make_unique<QueueSink> (*this, t);
            s.step  = std::make_unique<StepSequencerEngine> (*s.sink, 8);
            s.slice = std::make_unique<SliceSequencerEngine> (*s.sink, 9);
        }
        rack.setLiveEventObserver (this);
    }

    ~LocalSequencers() override { rack.setLiveEventObserver (nullptr); }

    //==========================================================================
    // AudioSource
    void prepareToPlay (int blockSize, double sr) override
    {
        sampleRate = sr;
        rack.prepare (sr, blockSize);
    }

    void releaseResources() override { rack.release(); }

    void getNextAudioBlock (const juce::AudioSourceChannelInfo& info) override
    {
        auto* buf = info.buffer;
        int pos = info.startSample, remaining = info.numSamples;

        {
            const juce::SpinLock::ScopedLockType sl (lock);
            updateTransport();
        }

        while (remaining > 0)
        {
            int n = remaining;
            {
                const juce::SpinLock::ScopedLockType sl (lock);

                if (clockRunning)
                {
                    while (samplesToTick <= 0.0)
                    {
                        tick();
                        samplesToTick += samplesPerTick();
                    }
                    n = juce::jmin (remaining, juce::jmax (1, (int) std::ceil (samplesToTick)));

                    // Where we are between ticks, in ticks.
                    const double now = (double) globalTick - samplesToTick / samplesPerTick();

                    // Drum Grids and note clips fire on their exact sample.
                    for (int t = 0; t < NumTracks; ++t)
                    {
                        auto& s = slots[(size_t) t];
                        if (s.kind == Kind::Drums && s.running.load())
                        {
                            fireDueDrumSteps (t, now);
                            const int until = (int) std::lround ((drumStepTick (t, s.nextDrumSixteenth) - now) * samplesPerTick());
                            n = juce::jmin (n, juce::jmax (1, until));
                        }
                        if (s.kind == Kind::Remix && s.running.load())
                        {
                            fireDueRemixSteps (t, now);
                            const int until = (int) std::lround (((double) s.nextDrumSixteenth * (SeqPPQN / 4) - now) * samplesPerTick());
                            n = juce::jmin (n, juce::jmax (1, until));
                        }
                        if (notePlayers[(size_t) t].active)
                        {
                            fireDueNotes (t, now);
                            const int until = (int) std::lround ((nextNoteTick (t) - now) * samplesPerTick());
                            n = juce::jmin (n, juce::jmax (1, until));
                        }
                    }
                    segmentTick = lastNow = now;
                }
                else
                {
                    segmentTick = -1.0;
                }

                drainEvents();

                for (int t = 0; t < NumTracks; ++t)
                    if (std::exchange (slots[(size_t) t].panicBreaks, false))
                        if (auto* s = sliceSampler (t))
                            s->player().releaseAll();
            }

            rack.render (*buf, pos, n);
            addClick (*buf, pos, n);

            if (clockRunning)
                samplesToTick -= n;
            pos += n;
            remaining -= n;
        }
    }

    //==========================================================================
    // Tracks' sequencers — message thread.

    // Gives a track a sequencer (or none). The old one stops and lets go of
    // its notes; its pattern is kept for if it comes back.
    void setKind (int track, Kind k)
    {
        if (! validTrack (track)) return;
        const juce::SpinLock::ScopedLockType sl (lock);
        auto& s = slots[(size_t) track];
        if (s.kind == k) return;
        if (s.running.load())
            stopSequencer (track);
        s.want.store (false);
        s.hasPending = s.pendingStop = false;
        if (auto* smp = dynamic_cast<SamplerSource*> (rack.getSource (track)))
            smp->allNotesOff();   // one-shots ignore the note-offs
        s.kind = k;
    }

    Kind getKind (int track) const { const juce::SpinLock::ScopedLockType sl (lock); return validTrack (track) ? slots[(size_t) track].kind : Kind::None; }

    // Where the pages' messages go (they don't say which track they are).
    void setActiveTrack (int track) { const juce::SpinLock::ScopedLockType sl (lock); if (validTrack (track)) activeTrack = track; }
    int  getActiveTrack() const     { const juce::SpinLock::ScopedLockType sl (lock); return activeTrack; }

    // Each sequencer has its own PLAY. They share one clock, so starting one
    // while another runs drops it in on the next 16th, in phase.
    void setPlaying (int track, bool shouldPlay)
    {
        if (validTrack (track) && (! shouldPlay || getKind (track) != Kind::None))
            slots[(size_t) track].want.store (shouldPlay);
    }
    bool isPlaying (int track) const { return validTrack (track) && slots[(size_t) track].want.load(); }

    void stopAll()
    {
        for (auto& s : slots)
            s.want.store (false);
        const juce::SpinLock::ScopedLockType sl (lock);
        for (auto& p : notePlayers)
        {
            p.wanted = false;
            p.hasPending = false;
        }
        if (recording != RecIdle)
            stopRecordingLocked();
    }

    bool isAnyPlaying() const
    {
        for (auto& s : slots)
            if (s.want.load()) return true;
        const juce::SpinLock::ScopedLockType sl (lock);
        for (auto& p : notePlayers)
            if (p.wanted) return true;
        return recording != RecIdle;
    }

    void   setBpm (double b) { bpm.store (juce::jlimit (40.0, 240.0, b)); }
    double getBpm() const    { return bpm.load(); }

    int getPlayhead (int track) const
    {
        if (! validTrack (track)) return -1;
        const auto& s = slots[(size_t) track];
        if (! s.running.load()) return -1;
        switch (s.kind)
        {
            case Kind::Drums: return s.drumStep.load();
            case Kind::Remix: return s.remixPos.load();
            case Kind::Step:  return s.step->getCurrentStep();
            case Kind::Slice: return s.slice->getCurrentStep();
            default:          return -1;
        }
    }

    // Note clips — message thread. Each track has a player: a launched note
    // clip loops on it, from the launch point (quantized like patterns).
    void queueNoteClip (int track, const NoteClip& c)
    {
        if (! juce::isPositiveAndBelow (track, TrackRack::NumTracks)) return;
        const juce::SpinLock::ScopedLockType sl (lock);
        auto& p = notePlayers[(size_t) track];
        p.pending = c;
        p.hasPending = true;
        p.pendingStop = false;
        p.wanted = true;
    }

    void queueNoteStop (int track)
    {
        if (! juce::isPositiveAndBelow (track, TrackRack::NumTracks)) return;
        const juce::SpinLock::ScopedLockType sl (lock);
        auto& p = notePlayers[(size_t) track];
        p.hasPending = false;
        if (p.active) p.pendingStop = true;
        else          p.wanted = false;
    }

    // Replaces what's playing without moving the loop (an edit, or a
    // recorded pass merged in).
    void updateNoteClip (int track, const NoteClip& c)
    {
        if (! juce::isPositiveAndBelow (track, TrackRack::NumTracks)) return;
        const juce::SpinLock::ScopedLockType sl (lock);
        if (recording != RecIdle && recTrack == track)
            recClip = c;   // the next pass plays this, not the raw takes
        auto& p = notePlayers[(size_t) track];
        if (! p.active) return;
        releaseNotes (track);
        p.clip = c;
        p.clip.sort();
        // Pick up from the next note after where the loop is now.
        p.nextIndex = 0;
        const double pos = (lastNow - p.loopStart) / SeqPPQN;
        while (p.nextIndex < p.clip.numNotes && p.clip.notes[(size_t) p.nextIndex].start < pos)
            ++p.nextIndex;
    }

    bool isNoteClipPlaying (int track) const
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        return juce::isPositiveAndBelow (track, TrackRack::NumTracks) && notePlayers[(size_t) track].active;
    }

    bool isNoteLaunchPending (int track) const
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        return juce::isPositiveAndBelow (track, TrackRack::NumTracks)
               && (notePlayers[(size_t) track].hasPending || notePlayers[(size_t) track].pendingStop);
    }

    // 0..1 through the playing loop, -1 if not playing — for the editors.
    double getNoteClipPosition (int track) const
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        if (! juce::isPositiveAndBelow (track, TrackRack::NumTracks)) return -1.0;
        const auto& p = notePlayers[(size_t) track];
        if (! p.active || p.clip.lengthBeats <= 0.0) return -1.0;
        return juce::jlimit (0.0, 1.0, (lastNow - p.loopStart) / (p.clip.lengthBeats * SeqPPQN));
    }

    //==========================================================================
    // Recording — message thread.
    //
    // startRecording arms a track: on the next bar (after a one-bar count-in
    // if the clock wasn't running) it starts a pass of `lengthBeats`, and
    // keeps going in passes until stopRecording. `existing` is what the clip
    // already holds: it plays underneath, so each pass overdubs. Live notes
    // on that track are captured on the audio thread; finished passes are
    // collected with takePass.
    enum RecState { RecIdle = 0, RecArmed, RecCountIn, RecRecording };

    void startRecording (int track, const NoteClip& existing, bool metronome)
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        if (! juce::isPositiveAndBelow (track, TrackRack::NumTracks)) return;
        if (recording != RecIdle) stopRecordingLocked();
        recTrack = track;
        recClip = existing;
        recCountIn = ! clockRunning;
        recMetronome = metronome;
        captured.clear();
        captured.lengthBeats = existing.lengthBeats;
        openNotes.fill (-1.0);
        recording = RecArmed;
    }

    void stopRecording()
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        stopRecordingLocked();
    }

    int  getRecordingTrack() const { const juce::SpinLock::ScopedLockType sl (lock); return recording != RecIdle ? recTrack : -1; }
    RecState getRecState() const   { const juce::SpinLock::ScopedLockType sl (lock); return recording; }
    int  getPassCount() const      { return passCount.load(); }

    // The notes of the passes finished since the last call (in clip beats).
    NoteClip takePass()
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        auto out = finishedPasses;
        finishedPasses.clear();
        return out;
    }

    void setMetronome (bool on) { const juce::SpinLock::ScopedLockType sl (lock); recMetronome = on; }

    //==========================================================================
    // The Drum Grid — message thread. Velocity 0 is an empty step.
    void setDrumStep (int track, int lane, int step, int velocity)
    {
        if (! validTrack (track) || ! juce::isPositiveAndBelow (lane, DrumLanes) || ! juce::isPositiveAndBelow (step, DrumSteps))
            return;
        const juce::SpinLock::ScopedLockType sl (lock);
        slots[(size_t) track].drumPattern[(size_t) lane][(size_t) step] = (uint8_t) juce::jlimit (0, 127, velocity);
    }

    // 0..1: how late the off-beat 16ths come, up to half a 16th.
    void  setDrumSwing (int track, float amount) { if (validTrack (track)) slots[(size_t) track].drumSwing.store (juce::jlimit (0.0f, 1.0f, amount)); }
    float getDrumSwing (int track) const        { return validTrack (track) ? slots[(size_t) track].drumSwing.load() : 0.0f; }

    //==========================================================================
    // The Remix Lane — message thread. Positions are 16ths into the sample.

    // How long the sample is, in 16ths (it loops after that).
    void setRemixLength (int track, int sixteenths)
    {
        if (validTrack (track)) slots[(size_t) track].remixLength.store (juce::jlimit (1, 128, sixteenths));
    }
    int getRemixLength (int track) const { return validTrack (track) ? slots[(size_t) track].remixLength.load() : 16; }

    // Play on from `position` from the next 16th.
    void remixJump (int track, int position)
    {
        if (! validTrack (track)) return;
        const juce::SpinLock::ScopedLockType sl (lock);
        auto& s = slots[(size_t) track];
        s.remixJumpTo = juce::jlimit (0, s.remixLength.load() - 1, position);
    }

    // Loop the 16ths from a to b (inclusive) from the next 16th; a < 0 ends
    // the loop and plays on from where it is.
    void remixLoop (int track, int a, int b)
    {
        if (! validTrack (track)) return;
        const juce::SpinLock::ScopedLockType sl (lock);
        auto& s = slots[(size_t) track];
        if (a < 0) { s.remixLoopEnd = true; return; }
        const int len = s.remixLength.load();
        s.remixLoopA = juce::jlimit (0, len - 1, juce::jmin (a, b));
        s.remixLoopB = juce::jlimit (0, len - 1, juce::jmax (a, b));
        s.remixLoopStartNext = true;
    }

    // Back in line with the bar: the sample's start on the downbeat.
    void remixSync (int track)
    {
        if (! validTrack (track)) return;
        const juce::SpinLock::ScopedLockType sl (lock);
        slots[(size_t) track].remixSyncNext = true;
    }

    // The loop being played, or {-1, -1}.
    std::pair<int, int> getRemixLoop (int track) const
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        if (! validTrack (track)) return { -1, -1 };
        const auto& s = slots[(size_t) track];
        return s.remixLooping ? std::pair<int, int> { s.remixLoopA, s.remixLoopB } : std::pair<int, int> { -1, -1 };
    }

    //==========================================================================
    // Session launching — message thread.
    //
    // A queued pattern takes over on the next launch point, exactly (the
    // audio thread swaps it in before that point's first step): the next bar,
    // the next beat, or at once (Off). A sequencer that isn't running takes it
    // at once. queueStop stops it on the next launch point.
    enum LaunchQuantize { Bar = 0, Beat = 1, Off = 2 };

    void setLaunchQuantize (int q) { launchQuantize.store (juce::jlimit (0, 2, q)); }
    int  getLaunchQuantize() const { return launchQuantize.load(); }

    void queuePattern (int track, const SeqPattern& p)
    {
        if (! validTrack (track)) return;
        const juce::SpinLock::ScopedLockType sl (lock);
        auto& s = slots[(size_t) track];
        s.pending = p;
        s.hasPending = true;
        s.pendingStop = false;
        if (! s.running.load() || launchQuantize.load() == Off)
            applyPending (track);
    }

    void queueStop (int track)
    {
        if (! validTrack (track)) return;
        const juce::SpinLock::ScopedLockType sl (lock);
        auto& s = slots[(size_t) track];
        s.hasPending = false;
        if (s.running.load()) s.pendingStop = true;
        else                  s.want.store (false);
    }

    bool isLaunchPending (int track) const
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        return validTrack (track) && (slots[(size_t) track].hasPending || slots[(size_t) track].pendingStop);
    }

    // Goes up each time a queued pattern takes over, so a page can redraw.
    int getLaunchCount (int track) const { return validTrack (track) ? slots[(size_t) track].launches.load() : 0; }

    // The pattern the track's sequencer is playing now.
    SeqPattern getPattern (int track) const
    {
        SeqPattern p;
        if (! validTrack (track)) return p;
        const juce::SpinLock::ScopedLockType sl (lock);
        const auto& s = slots[(size_t) track];
        if (s.kind == Kind::Drums)
        {
            p.drums = s.drumPattern;
            return p;
        }
        const ClockedSequencer& seq = s.kind == Kind::Slice ? (const ClockedSequencer&) *s.slice : *s.step;
        p.length = seq.getPatternLength();
        for (int i = 0; i < SeqPattern::MaxSteps; ++i)
            p.steps[(size_t) i] = seq.getStep (i);
        return p;
    }

    //==========================================================================
    // Breaks — message thread. The tempo stays; slices warp the loop to it.
    void setBuiltInBreak (int track, int index)
    {
        auto* s = sliceSampler (track);
        if (s == nullptr)
            return;
        s->setMode (SamplerSource::Slice);   // a break picked for the slicer is to be sliced
        s->setBuiltInBreak (index);
    }

    bool importBreak (int track, const juce::File& file, juce::String& error)
    {
        auto* s = sliceSampler (track);
        if (s == nullptr)
        {
            error = "Breaker's track isn't playing a sampler.";
            return false;
        }
        s->setMode (SamplerSource::Slice);
        return s->importBreak (file, error) != nullptr;
    }

    // A slicer's sample: whatever the sampler on its track holds. Nothing if
    // that track plays something else.
    const Break* getBreak (int track) const        { auto* s = sliceSampler (track); return s != nullptr ? s->getBreak() : nullptr; }
    int  getBuiltInBreakIndex (int track) const    { auto* s = sliceSampler (track); return s != nullptr ? s->getBuiltInBreakIndex() : -1; }
    juce::File getImportedBreak (int track) const  { auto* s = sliceSampler (track); return s != nullptr ? s->getImportedBreak() : juce::File(); }

    SamplerSource* sliceSampler (int track) const
    {
        return validTrack (track) ? dynamic_cast<SamplerSource*> (rack.getSource (track)) : nullptr;
    }

    // Tempo and launch quantize, in the project. The patterns are the
    // Session's clips.
    void saveState (juce::PropertySet& p) const
    {
        p.setValue ("seq_bpm", getBpm());
        p.setValue ("launch_q", getLaunchQuantize());
    }

    void restoreState (juce::PropertySet& p)
    {
        if (p.containsKey ("seq_bpm"))
            setBpm (p.getDoubleValue ("seq_bpm"));
        setLaunchQuantize (p.getIntValue ("launch_q", Bar));
    }

    //==========================================================================
    // Mirrors of MidiMorfEngine's sequencer handlers — message thread, to the
    // active track's sequencer. The pages talk to these through EventRouter
    // exactly as they talk to the server, so one page serves both modes.

    void stepSet (uint8_t step, uint8_t degree, uint8_t velocity, uint8_t gate, uint8_t mod, bool rest)
    {
        ClockedSequencer::Step s;
        s.index    = degree;
        s.velocity = velocity;
        s.gate     = gate;
        s.mod      = mod;
        s.rest     = rest;

        const juce::SpinLock::ScopedLockType sl (lock);
        if (auto* seq = activeSequencer())
            seq->setStep (step, s);
    }

    void config (uint8_t patternLength, uint8_t rate, uint8_t direction, uint8_t swing,
                 uint8_t scaleIndex, uint8_t rootNote, uint8_t baseOctave, uint8_t octaveRange)
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        auto& stepSeq = *slots[(size_t) activeTrack].step;
        stepSeq.setPatternLength (patternLength);
        stepSeq.setRate (static_cast<ClockedSequencer::Rate> (juce::jlimit (0, 5, (int) rate)));
        stepSeq.setDirection (static_cast<ClockedSequencer::Direction> (juce::jlimit (0, 3, (int) direction)));
        stepSeq.setSwing ((float) juce::jlimit (0, 100, (int) swing) / 100.0f);
        stepSeq.setScale (scaleIndex, rootNote, baseOctave, octaveRange);
    }

    void transport (MidiMorf::SeqCommand c)
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        if (c == MidiMorf::SeqCommand::Panic)
        {
            if (auto* seq = activeSequencer())
                seq->allNotesOff();
            if (slots[(size_t) activeTrack].kind == Kind::Slice)
                slots[(size_t) activeTrack].panicBreaks = true;
        }
    }

    void ratchet (uint8_t division, bool active)
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        if (auto* seq = activeSequencer())
            seq->setRatchet (division, active);
    }

    void sliceConfig (uint8_t bars, uint8_t division, uint8_t sliceCount,
                      uint8_t rootNote, uint8_t direction, uint8_t swing)
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        auto& s = slots[(size_t) activeTrack];

        // Same guard as the server: only a real grid change re-slices.
        if ((int) bars != s.lastSliceBars || (int) division != s.lastSliceDivision)
        {
            s.slice->setGrid (bars, division);
            s.lastSliceBars     = bars;
            s.lastSliceDivision = division;
        }

        s.slice->setSlices (sliceCount, rootNote);
        s.slice->setDirection (static_cast<ClockedSequencer::Direction> (juce::jlimit (0, 3, (int) direction)));
        s.slice->setSwing ((float) juce::jlimit (0, 100, (int) swing) / 100.0f);
    }

    void looper (uint8_t division, bool active, bool randomOn)
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        auto& slice = *slots[(size_t) activeTrack].slice;
        slice.setLooperRandom (randomOn);
        slice.setLooper (division, active);
    }

    // Locally there is no Simpler to receive pitch bend or a CC, so the
    // ribbons play the break player directly: pitch bend repitches the
    // slices ±12 semitones, and any CC sweeps a low-pass over them.
    void ribbon (uint8_t ribbonIndex, int value14bit)
    {
        if (ribbonIndex >= NumRibbons)
            return;
        const int t = getActiveTrack();

        switch (slots[(size_t) t].ribbonTargets[ribbonIndex])
        {
            case MidiMorf::RibbonTarget::PitchBend:
                if (auto* s = sliceSampler (t))
                    s->player().setPitchSemitones ((float) (value14bit - 8192) / 8192.0f * 12.0f);
                break;

            case MidiMorf::RibbonTarget::CC:
                if (auto* s = sliceSampler (t))
                    s->player().setTone ((float) value14bit / 16383.0f);
                break;

            case MidiMorf::RibbonTarget::StepRepeat:
            {
                const juce::SpinLock::ScopedLockType sl (lock);
                slots[(size_t) t].slice->setStepRepeat (value14bit);
                break;
            }
        }
    }

    void ribbonAssign (uint8_t ribbonIndex, MidiMorf::RibbonTarget target, uint8_t /*ccNumber*/)
    {
        if (ribbonIndex >= NumRibbons)
            return;
        const int t = getActiveTrack();
        auto& s = slots[(size_t) t];

        const auto old = s.ribbonTargets[ribbonIndex];
        s.ribbonTargets[ribbonIndex] = target;

        // Reassigning away from a target must not leave it stuck.
        if (old == MidiMorf::RibbonTarget::StepRepeat && target != old)
        {
            const juce::SpinLock::ScopedLockType sl (lock);
            s.slice->setStepRepeat (1);
        }
        if (old == MidiMorf::RibbonTarget::PitchBend && target != old)
            if (auto* smp = sliceSampler (t)) smp->player().setPitchSemitones (0.0f);
        if (old == MidiMorf::RibbonTarget::CC && target != old)
            if (auto* smp = sliceSampler (t)) smp->player().setTone (1.0f);
    }

private:
    struct Event { int track; bool on; int channel, note, velocity; };

    static bool validTrack (int t) { return juce::isPositiveAndBelow (t, NumTracks); }

    // The engines' output: queued under `lock`, delivered on the audio thread.
    class QueueSink : public SeqSink
    {
    public:
        QueueSink (LocalSequencers& o, int t) : owner (o), track (t) {}

        void sendNoteOn (int ch, int note, int velocity) override { owner.push ({ track, true, ch, note, velocity }); }
        void sendNoteOff (int ch, int note) override             { owner.push ({ track, false, ch, note, 0 }); }

        // e.g. the step page's MOD lane rides CC1 into the synth's mod wheel.
        void sendCC (int, int cc, int value) override
        {
            if (auto* s = owner.rack.getSource (track))
                s->controller (cc, value);
        }

    private:
        LocalSequencers& owner;
        const int track;
    };

    static constexpr int NumRibbons  = 2;
    static constexpr int MaxEvents   = 512;
    static constexpr int DrumChannel = 10, RemixChannel = 9, RemixRoot = 36;

    struct Slot
    {
        Kind kind = Kind::None;
        std::unique_ptr<QueueSink> sink;
        std::unique_ptr<StepSequencerEngine> step;
        std::unique_ptr<SliceSequencerEngine> slice;
        std::atomic<bool> want { false }, running { false };

        // Launching, under lock.
        SeqPattern pending {};
        bool hasPending = false, pendingStop = false;
        std::atomic<int> launches { 0 };

        // The Drum Grid.
        std::array<std::array<uint8_t, DrumSteps>, DrumLanes> drumPattern {};   // under lock
        std::array<bool, DrumLanes> drumSounding {};                            // under lock
        int nextDrumSixteenth = 0;                                              // under lock
        std::atomic<float> drumSwing { 0.0f };
        std::atomic<int> drumStep { -1 };

        // The Remix Lane (its 16ths count in nextDrumSixteenth).
        std::atomic<int> remixLength { 32 }, remixPos { -1 };
        int remixOffset = 0, remixJumpTo = -1, remixLoopA = 0, remixLoopB = 0, remixLoopAnchor = 0;   // under lock
        bool remixLooping = false, remixLoopStartNext = false, remixLoopEnd = false, remixSyncNext = false;

        // Breaker.
        int lastSliceBars = -1, lastSliceDivision = 2;   // under lock
        bool panicBreaks = false;                        // under lock
        MidiMorf::RibbonTarget ribbonTargets[NumRibbons] = { MidiMorf::RibbonTarget::PitchBend,
                                                             MidiMorf::RibbonTarget::StepRepeat };
    };

    void push (const Event& e)
    {
        if (numEvents < MaxEvents)
            events[(size_t) numEvents++] = e;
    }

    // Audio thread, under lock.
    void drainEvents()
    {
        if (numEvents > 0)
            for (int t = 0; t < NumTracks; ++t)
            {
                if (slots[(size_t) t].kind == Kind::Slice)
                    if (auto* s = sliceSampler (t))
                        s->setSliceMapping ((int) slots[(size_t) t].slice->getRootNote(), stepBeats (t), 60.0 / bpm.load());
                if (slots[(size_t) t].kind == Kind::Remix)
                    if (auto* s = sliceSampler (t))
                        s->setSliceMapping (RemixRoot, 0.25, 60.0 / bpm.load());
            }

        for (int i = 0; i < numEvents; ++i)
        {
            const auto& e = events[(size_t) i];
            if (e.on) rack.noteOn (e.track, e.channel, e.note, e.velocity);
            else      rack.noteOff (e.track, e.channel, e.note);
        }
        numEvents = 0;
    }

    double stepBeats (int t) const
    {
        const int d = slots[(size_t) t].lastSliceDivision;
        return d == 0 ? 1.0 : d == 1 ? 0.5 : 0.25;
    }

    double samplesPerTick() const
    {
        return sampleRate * 60.0 / (bpm.load() * SeqPPQN);
    }

    // The tick-driven engine for a track (Step-Seq or Breaker).
    ClockedSequencer& sequencer (int t)
    {
        auto& s = slots[(size_t) t];
        return s.kind == Kind::Slice ? (ClockedSequencer&) *s.slice : *s.step;
    }

    ClockedSequencer* activeSequencer()
    {
        const auto k = slots[(size_t) activeTrack].kind;
        return k == Kind::Step || k == Kind::Slice ? &sequencer (activeTrack) : nullptr;
    }

    //==========================================================================
    // The Drum Grid's clock — audio thread, under lock.

    // The tick a 16th falls on, swing included. Sixteenths count from the
    // clock's start, so the grids stay in phase with the other sequencers.
    double drumStepTick (int t, int sixteenth) const
    {
        constexpr int tps = SeqPPQN / 4;
        return sixteenth * tps + ((sixteenth % 2) == 1 ? slots[(size_t) t].drumSwing.load() * tps * 0.5 : 0.0);
    }

    // Steps fire on the nearest sample.
    void fireDueDrumSteps (int t, double now)
    {
        auto& s = slots[(size_t) t];
        while ((drumStepTick (t, s.nextDrumSixteenth) - now) * samplesPerTick() < 0.5)
        {
            if (s.nextDrumSixteenth % launchSixteenths() == 0 && atLaunchPoint (t))
                return;   // stopped on the launch point

            // Last step's notes end as the next step starts (a sampler in
            // Keys mode needs its note-offs; the drum synth ignores them).
            releaseDrumNotes (t);

            const int step = s.nextDrumSixteenth % DrumSteps;
            for (int lane = 0; lane < DrumLanes; ++lane)
                if (const int vel = s.drumPattern[(size_t) lane][(size_t) step]; vel > 0)
                {
                    push ({ t, true, DrumChannel, ::Drums::noteFor (lane), vel });
                    s.drumSounding[(size_t) lane] = true;
                }

            s.drumStep.store (step);
            ++s.nextDrumSixteenth;
        }
    }

    // The Remix Lane's 16ths: jumps, loops and SYNC take effect here, then
    // the slice for this 16th plays. Slices are one-shots, so no note-offs.
    void fireDueRemixSteps (int t, double now)
    {
        auto& s = slots[(size_t) t];
        constexpr int tps = SeqPPQN / 4;
        auto mod = [] (int a, int m) { return ((a % m) + m) % m; };

        while (((double) s.nextDrumSixteenth * tps - now) * samplesPerTick() < 0.5)
        {
            if (s.nextDrumSixteenth % launchSixteenths() == 0 && std::exchange (s.pendingStop, false))
            {
                s.want.store (false);
                stopSequencer (t);
                return;
            }

            const int len = s.remixLength.load();
            const int six = s.nextDrumSixteenth;

            if (std::exchange (s.remixSyncNext, false))   { s.remixOffset = 0; s.remixLooping = false; }
            if (s.remixJumpTo >= 0)                         { s.remixOffset = mod (s.remixJumpTo - six, len); s.remixJumpTo = -1; s.remixLooping = false; }
            if (std::exchange (s.remixLoopStartNext, false)) { s.remixLooping = true; s.remixLoopAnchor = six; }
            if (std::exchange (s.remixLoopEnd, false) && s.remixLooping)
            {
                s.remixLooping = false;
                s.remixOffset = mod (s.remixPos.load() + 1 - six, len);   // on from where it is
            }

            const int pos = s.remixLooping ? s.remixLoopA + mod (six - s.remixLoopAnchor, s.remixLoopB - s.remixLoopA + 1)
                                           : mod (six + s.remixOffset, len);
            push ({ t, true, RemixChannel, RemixRoot + pos, 100 });
            s.remixPos.store (pos);
            ++s.nextDrumSixteenth;
        }
    }

    void releaseDrumNotes (int t)
    {
        auto& s = slots[(size_t) t];
        for (int lane = 0; lane < DrumLanes; ++lane)
            if (std::exchange (s.drumSounding[(size_t) lane], false))
                push ({ t, false, DrumChannel, ::Drums::noteFor (lane), 0 });
    }

    // Under lock. The queued pattern becomes the playing one.
    void applyPending (int t)
    {
        auto& s = slots[(size_t) t];
        if (! s.hasPending)
            return;
        s.hasPending = false;

        const auto& p = s.pending;
        if (s.kind == Kind::Drums)
        {
            s.drumPattern = p.drums;
        }
        else if (s.kind == Kind::Step || s.kind == Kind::Slice)
        {
            auto& seq = sequencer (t);
            for (int i = 0; i < SeqPattern::MaxSteps; ++i)
                seq.setStep (i, p.steps[(size_t) i]);
            if (s.kind == Kind::Step)
                seq.setPatternLength (p.length);   // the slicer's length is its grid's
        }
        s.launches.fetch_add (1);
    }

    // How many 16ths apart the launch points are.
    int launchSixteenths() const
    {
        switch (launchQuantize.load())
        {
            case Bar:  return 16;
            case Beat: return 4;
            default:   return 1;
        }
    }

    // Audio thread, under lock, on a launch point: a queued stop or pattern.
    // True if the sequencer stopped.
    bool atLaunchPoint (int t)
    {
        auto& s = slots[(size_t) t];
        if (std::exchange (s.pendingStop, false))
        {
            s.want.store (false);
            stopSequencer (t);
            return true;
        }
        applyPending (t);
        return false;
    }

    void stopSequencer (int t)
    {
        auto& s = slots[(size_t) t];
        if (s.kind == Kind::Drums)
        {
            releaseDrumNotes (t);
            s.drumStep.store (-1);
        }
        else if (s.kind == Kind::Remix)
        {
            if (auto* smp = sliceSampler (t))
                smp->player().releaseAll();
            s.remixPos.store (-1);
        }
        else if (s.kind != Kind::None)
        {
            sequencer (t).clockStop();
            if (s.kind == Kind::Slice)
                if (auto* smp = sliceSampler (t))
                    smp->player().releaseAll();   // one-shots ignore the note-off
        }
        s.running.store (false);
    }

    // Audio thread, under lock.
    void updateTransport()
    {
        bool anyWanted = recording != RecIdle;
        for (auto& p : notePlayers)
        {
            anyWanted |= p.wanted;
            if (! p.wanted && p.active)
                stopNotePlayer ((int) (&p - notePlayers.data()));
        }

        for (int t = 0; t < NumTracks; ++t)
        {
            auto& s = slots[(size_t) t];
            const bool want = s.want.load() && s.kind != Kind::None;
            anyWanted |= want;
            if (! want && s.running.load())
                stopSequencer (t);
        }

        if (anyWanted && ! clockRunning)
        {
            clockRunning  = true;
            globalTick    = 0;
            samplesToTick = 0.0;
        }
        else if (! anyWanted && clockRunning)
        {
            clockRunning = false;
        }
    }

    // Note clips and recording — audio thread, under lock.
    struct NotePlayer
    {
        NoteClip clip, pending;
        bool hasPending = false, pendingStop = false, wanted = false, active = false;
        double loopStart = 0.0;   // global tick of the current loop's beat 0
        int nextIndex = 0;
        std::array<double, 128> heldUntil {};   // tick each held note ends, or 0
    };

    static constexpr int NoteChannel = 15;   // clear of the controllers' channels

    double tickOf (const NotePlayer& p, int index) const { return p.loopStart + p.clip.notes[(size_t) index].start * SeqPPQN; }

    // The next on or off due on this track, in ticks.
    double nextNoteTick (int t) const
    {
        const auto& p = notePlayers[(size_t) t];
        double next = p.loopStart + p.clip.lengthBeats * SeqPPQN;   // the loop point
        if (p.nextIndex < p.clip.numNotes)
            next = juce::jmin (next, tickOf (p, p.nextIndex));
        for (double until : p.heldUntil)
            if (until > 0.0)
                next = juce::jmin (next, until);
        return next;
    }

    void fireDueNotes (int t, double now)
    {
        auto& p = notePlayers[(size_t) t];
        const double due = now + 0.5 / samplesPerTick();   // within half a sample
        lastNow = now;

        for (int guard = 0; guard < NoteClip::MaxNotes * 2; ++guard)
        {
            // Offs first, so a note retriggered on the same sample restarts.
            for (int n = 0; n < 128; ++n)
                if (p.heldUntil[(size_t) n] > 0.0 && p.heldUntil[(size_t) n] <= due)
                {
                    p.heldUntil[(size_t) n] = 0.0;
                    push ({ t, false, NoteChannel, n, 0 });
                }

            if (p.nextIndex < p.clip.numNotes && tickOf (p, p.nextIndex) <= due)
            {
                const auto& e = p.clip.notes[(size_t) p.nextIndex++];
                if (p.heldUntil[e.note] > 0.0)
                    push ({ t, false, NoteChannel, e.note, 0 });
                push ({ t, true, NoteChannel, e.note, e.velocity });
                p.heldUntil[e.note] = juce::jmax (now + 0.001, tickOf (p, p.nextIndex - 1) + e.length * SeqPPQN);
                continue;
            }

            const double loopEnd = p.loopStart + p.clip.lengthBeats * SeqPPQN;
            if (loopEnd <= due && p.clip.lengthBeats > 0.0)
            {
                p.loopStart = loopEnd;
                p.nextIndex = 0;
                continue;
            }
            break;
        }
    }

    void releaseNotes (int t)
    {
        auto& p = notePlayers[(size_t) t];
        for (int n = 0; n < 128; ++n)
            if (std::exchange (p.heldUntil[(size_t) n], 0.0) > 0.0)
                push ({ t, false, NoteChannel, n, 0 });
    }

    void stopNotePlayer (int t)
    {
        releaseNotes (t);
        notePlayers[(size_t) t].active = false;
    }

    // On a launch point: queued note clips start, queued stops stop.
    void notesAtLaunchPoint()
    {
        for (int t = 0; t < TrackRack::NumTracks; ++t)
        {
            auto& p = notePlayers[(size_t) t];
            if (std::exchange (p.pendingStop, false))
            {
                p.wanted = false;
                stopNotePlayer (t);
            }
            else if (std::exchange (p.hasPending, false))
            {
                releaseNotes (t);
                p.clip = p.pending;
                p.clip.sort();
                p.loopStart = (double) globalTick;
                p.nextIndex = 0;
                p.active = true;
            }
        }
    }

    // Each bar while recording: the count-in ends, a pass ends and the next
    // begins.
    void recordingAtBar()
    {
        if (recording == RecArmed)
        {
            if (recCountIn)
            {
                recording = RecCountIn;
                recPassStart = (double) globalTick + 4 * SeqPPQN;
                return;
            }
            beginPass();
        }
        else if (recording == RecCountIn && (double) globalTick >= recPassStart)
        {
            beginPass();
        }
        else if (recording == RecRecording && (double) globalTick >= recPassStart + captured.lengthBeats * SeqPPQN)
        {
            endPass ((double) globalTick);
            beginPass();
        }
    }

    void beginPass()
    {
        recording = RecRecording;
        recPassStart = (double) globalTick;

        // The clip plays underneath from the pass start, so what you hear
        // lines up with what you record.
        auto& p = notePlayers[(size_t) recTrack];
        releaseNotes (recTrack);
        p.clip = recClip;
        p.clip.sort();
        p.loopStart = recPassStart;
        p.nextIndex = 0;
        p.active = p.wanted = true;
        p.hasPending = p.pendingStop = false;
    }

    // Notes still held at the end of a pass end there.
    void endPass (double atTick)
    {
        const double end = (atTick - recPassStart) / SeqPPQN;
        for (int n = 0; n < 128; ++n)
            if (openNotes[(size_t) n] >= 0.0)
            {
                captured.add ({ openNotes[(size_t) n], juce::jmax (0.05, end - openNotes[(size_t) n]), (uint8_t) n, openVelocity[(size_t) n] });
                openNotes[(size_t) n] = -1.0;
            }

        for (int i = 0; i < captured.numNotes; ++i)
        {
            finishedPasses.lengthBeats = captured.lengthBeats;
            finishedPasses.add (captured.notes[(size_t) i]);
            recClip.add (captured.notes[(size_t) i]);   // the next pass hears this one
        }
        captured.clear();
        passCount.fetch_add (1);
    }

    void stopRecordingLocked()
    {
        if (recording == RecRecording && lastNow >= 0.0)
            endPass (lastNow);
        recording = RecIdle;
    }

    // LiveEventObserver — audio thread, during rack.render (not under lock).
    void liveEvent (int track, bool on, int, int note, int velocity) override
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        if (recording != RecRecording || track != recTrack || segmentTick < 0.0 || ! juce::isPositiveAndBelow (note, 128))
            return;

        const double beat = juce::jlimit (0.0, captured.lengthBeats, (segmentTick - recPassStart) / SeqPPQN);
        if (on)
        {
            openNotes[(size_t) note] = beat;
            openVelocity[(size_t) note] = (uint8_t) juce::jlimit (1, 127, velocity);
        }
        else if (openNotes[(size_t) note] >= 0.0)
        {
            captured.add ({ openNotes[(size_t) note], juce::jmax (0.05, beat - openNotes[(size_t) note]), (uint8_t) note, openVelocity[(size_t) note] });
            openNotes[(size_t) note] = -1.0;
        }
    }

    // The click for the count-in and while recording: higher on the bar.
    void addClick (juce::AudioBuffer<float>& buf, int start, int n)
    {
        if (clickLeft <= 0) return;
        const int count = juce::jmin (n, clickLeft);
        for (int i = 0; i < count; ++i)
        {
            const double t = (double) clickPos++ / sampleRate;
            const float v = 0.3f * (float) (std::sin (juce::MathConstants<double>::twoPi * clickHz * t) * std::exp (-t / 0.012));
            for (int ch = 0; ch < buf.getNumChannels(); ++ch)
                buf.addSample (ch, start + i, v);
        }
        clickLeft -= count;
    }

    // Audio thread, under lock.
    void tick()
    {
        if (globalTick % (4 * SeqPPQN) == 0 && recording != RecIdle)
            recordingAtBar();

        if (globalTick % (launchSixteenths() * (SeqPPQN / 4)) == 0)
            notesAtLaunchPoint();

        if (globalTick % SeqPPQN == 0 && recMetronome && (recording == RecCountIn || recording == RecRecording))
        {
            clickHz = globalTick % (4 * SeqPPQN) == 0 ? 1500.0 : 1000.0;
            clickPos = 0;
            clickLeft = (int) (0.06 * sampleRate);
        }

        const bool launchPoint = globalTick % (launchSixteenths() * (SeqPPQN / 4)) == 0;

        for (int t = 0; t < NumTracks; ++t)
        {
            auto& s = slots[(size_t) t];
            if (s.kind == Kind::None)
                continue;

            const bool sampleExact = s.kind == Kind::Drums || s.kind == Kind::Remix;

            // Launch points for the tick-driven two; the others have their own.
            if (launchPoint && ! sampleExact && s.running.load())
                atLaunchPoint (t);

            if (s.want.load() && ! s.running.load() && globalTick % (SeqPPQN / 4) == 0)
            {
                if (sampleExact)
                {
                    s.nextDrumSixteenth = globalTick / (SeqPPQN / 4);   // fires right after this tick
                    s.running.store (true);
                    continue;
                }

                if (globalTick == 0)
                    sequencer (t).clockStart();
                else
                    sequencer (t).clockSongPosition (globalTick / (SeqPPQN / 4));
                s.running.store (true);
            }

            if (s.running.load() && ! sampleExact)
                sequencer (t).clockTick();
        }

        ++globalTick;
    }

    TrackRack& rack;

    // Declared before the slots: the engines' destructors still send note-offs.
    mutable juce::SpinLock lock;
    std::array<Event, MaxEvents> events;
    int numEvents = 0;
    int activeTrack = 0;   // under lock

    std::array<Slot, NumTracks> slots;

    double sampleRate = 48000.0;
    std::atomic<double> bpm { 120.0 };
    std::atomic<int> launchQuantize { Bar };
    bool   clockRunning  = false;   // audio thread
    int    globalTick    = 0;
    double samplesToTick = 0.0;

    // Note clips and recording, under lock.
    std::array<NotePlayer, TrackRack::NumTracks> notePlayers {};
    double segmentTick = -1.0, lastNow = -1.0;
    RecState recording = RecIdle;
    int recTrack = 0;
    bool recCountIn = false, recMetronome = true;
    double recPassStart = 0.0;
    NoteClip recClip, captured, finishedPasses;
    std::array<double, 128> openNotes {};
    std::array<uint8_t, 128> openVelocity {};
    std::atomic<int> passCount { 0 };
    double clickHz = 1000.0;
    int clickPos = 0, clickLeft = 0;   // audio thread

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LocalSequencers)
};
