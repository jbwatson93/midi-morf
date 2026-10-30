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
// The sequencers in Local mode: the same engines the server runs, driven by
// the phone's own clock instead of DAW MIDI clock. Each sequencer plays a
// track: by default the step sequencer plays T2 (a synth), the slice
// sequencer T3 (the break player), the drum grid T4 (the drum synth).
//
// The drum grid is the phone's own: eight lanes of 16 steps with a velocity
// each. It fires its steps at the exact sample (swing included) rather than
// on the 24 PPQN ticks the other two share, so a swung hat lands where it
// should, not a whole tick early or late.
//
// This is the AudioSource the device plays. Each block is cut at clock-tick
// boundaries, so notes land on the right sample rather than on whatever block
// edge they fall nearest — at 120 BPM a tick is ~1000 samples, a block 256.
//
// Threading: the engines are only ever ticked on the audio thread. Edits from
// the pages take `lock` briefly; the engines' note output is queued under the
// same lock and drained on the audio thread, so the track sources are only
// ever played from there.
class LocalSequencers : public juce::AudioSource,
                        private LiveEventObserver
{
public:
    enum Which { Step = 0, Slice = 1, Drums = 2 };
    static constexpr int NumSequencers = 3;

    // Tracks are 0-based here; the UI calls them T1-T4.
    static constexpr int DefaultStepTrack = 1, DefaultSliceTrack = 2, DefaultDrumTrack = 3;

    static constexpr int DrumLanes = ::Drums::NumDrums, DrumSteps = 16;

    explicit LocalSequencers (TrackRack& r)
        : rack (r),
          stepSink (*this, Step),
          sliceSink (*this, Slice),
          stepSeq (stepSink, 8),
          sliceSeq (sliceSink, 9)
    {
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
                    if (running[Drums].load())
                    {
                        fireDueDrumSteps (now);
                        const int until = (int) std::lround ((drumStepTick (nextDrumSixteenth) - now) * samplesPerTick());
                        n = juce::jmin (n, juce::jmax (1, until));
                    }

                    // Note clips fire on their exact sample too.
                    for (int t = 0; t < TrackRack::NumTracks; ++t)
                        if (notePlayers[(size_t) t].active)
                        {
                            fireDueNotes (t, now);
                            const int until = (int) std::lround ((nextNoteTick (t) - now) * samplesPerTick());
                            n = juce::jmin (n, juce::jmax (1, until));
                        }
                    segmentTick = lastNow = now;
                }
                else
                {
                    segmentTick = -1.0;
                }

                drainEvents();

                if (std::exchange (panicBreaks, false))
                    if (auto* s = sliceSampler())
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
    // Transport — message thread.

    // Each sequencer has its own PLAY. They share one clock, so starting the
    // second while the first runs drops it in on the next 16th, in phase.
    void setPlaying (Which w, bool shouldPlay) { wantPlaying[w].store (shouldPlay); }
    bool isPlaying (Which w) const             { return wantPlaying[w].load(); }

    void stopAll()
    {
        for (auto& w : wantPlaying)
            w.store (false);
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
        for (auto& w : wantPlaying)
            if (w.load()) return true;
        const juce::SpinLock::ScopedLockType sl (lock);
        for (auto& p : notePlayers)
            if (p.wanted) return true;
        return recording != RecIdle;
    }

    //==========================================================================
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

    // Message thread. The old track gets the sequencer's note-offs (and, if
    // it's the break player, a cut — slices ignore note-offs) before the
    // switch.
    void setTrack (Which w, int track)
    {
        track = juce::jlimit (0, TrackRack::NumTracks - 1, track);
        const juce::SpinLock::ScopedLockType sl (lock);

        if (tracks[w] == track)
            return;

        if (w == Drums) releaseDrumNotes();
        else            sequencer (w).allNotesOff();
        if (auto* s = dynamic_cast<SamplerSource*> (rack.getSource (tracks[w])))
            s->allNotesOff();   // one-shots ignore the note-offs
        tracks[w] = track;
    }

    int getTrack (Which w) const
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        return tracks[w];
    }

    void   setBpm (double b) { bpm.store (juce::jlimit (40.0, 240.0, b)); }
    double getBpm() const    { return bpm.load(); }

    int getPlayhead (Which w) const
    {
        if (! running[w].load())
            return -1;
        if (w == Drums) return drumStep.load();
        return w == Step ? stepSeq.getCurrentStep() : sliceSeq.getCurrentStep();
    }

    //==========================================================================
    // The drum grid — message thread. Velocity 0 is an empty step.
    void setDrumStep (int lane, int step, int velocity)
    {
        if (! juce::isPositiveAndBelow (lane, DrumLanes) || ! juce::isPositiveAndBelow (step, DrumSteps))
            return;
        const juce::SpinLock::ScopedLockType sl (lock);
        drumPattern[(size_t) lane][(size_t) step] = (uint8_t) juce::jlimit (0, 127, velocity);
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

    void queuePattern (Which w, const SeqPattern& p)
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        pending[w] = p;
        hasPending[w] = true;
        pendingStop[w] = false;
        if (! running[w].load() || launchQuantize.load() == Off)
            applyPending (w);
    }

    void queueStop (Which w)
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        hasPending[w] = false;
        if (running[w].load()) pendingStop[w] = true;
        else                   wantPlaying[w].store (false);
    }

    bool isLaunchPending (Which w) const
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        return hasPending[w] || pendingStop[w];
    }

    // Goes up each time a queued pattern takes over, so a page can redraw.
    int getLaunchCount (Which w) const { return launches[w].load(); }

    // The pattern the sequencer is playing now.
    SeqPattern getPattern (Which w) const
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        SeqPattern p;
        if (w == Drums)
        {
            p.drums = drumPattern;
            return p;
        }
        auto& seq = const_cast<LocalSequencers*> (this)->sequencer (w);
        p.length = seq.getPatternLength();
        for (int i = 0; i < SeqPattern::MaxSteps; ++i)
            p.steps[(size_t) i] = seq.getStep (i);
        return p;
    }

    // 0..1: how late the off-beat 16ths come, up to half a 16th.
    void  setDrumSwing (float amount) { drumSwing.store (juce::jlimit (0.0f, 1.0f, amount)); }
    float getDrumSwing() const        { return drumSwing.load(); }

    //==========================================================================
    // Breaks — message thread.
    // A new break also sets the tempo to the loop's own.
    void setBuiltInBreak (int index)
    {
        auto* s = sliceSampler();
        if (s == nullptr)
            return;
        s->setMode (SamplerSource::Slice);   // a break picked for the slicer is to be sliced
        if (auto* b = s->setBuiltInBreak (index))
            setBpm (b->bpm());
    }

    bool importBreak (const juce::File& file, juce::String& error)
    {
        auto* s = sliceSampler();
        if (s == nullptr)
        {
            error = "The slicer's track isn't playing a sampler.";
            return false;
        }
        s->setMode (SamplerSource::Slice);
        auto* b = s->importBreak (file, error);
        if (b == nullptr)
            return false;

        setBpm (b->bpm());
        return true;
    }

    // The slicer's sample: whatever the sampler on its track holds. Nothing
    // if that track plays something else.
    const Break* getBreak() const        { auto* s = sliceSampler(); return s != nullptr ? s->getBreak() : nullptr; }
    int  getBuiltInBreakIndex() const    { auto* s = sliceSampler(); return s != nullptr ? s->getBuiltInBreakIndex() : -1; }
    juce::File getImportedBreak() const  { auto* s = sliceSampler(); return s != nullptr ? s->getImportedBreak() : juce::File(); }

    SamplerSource* sliceSampler() const
    {
        return dynamic_cast<SamplerSource*> (rack.getSource (tracks[Slice]));
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
    // Mirrors of MidiMorfEngine's sequencer handlers — message thread. The
    // pages talk to these through EventRouter exactly as they talk to the
    // server, so one page serves both modes.

    void setActivePage (MidiMorf::ControllerMode m)
    {
        sliceIsActive = (m == MidiMorf::ControllerMode::SliceSequencer);
    }

    void stepSet (uint8_t step, uint8_t degree, uint8_t velocity, uint8_t gate, uint8_t mod, bool rest)
    {
        ClockedSequencer::Step s;
        s.index    = degree;
        s.velocity = velocity;
        s.gate     = gate;
        s.mod      = mod;
        s.rest     = rest;

        const juce::SpinLock::ScopedLockType sl (lock);
        activeSequencer().setStep (step, s);
    }

    void config (uint8_t patternLength, uint8_t rate, uint8_t direction, uint8_t swing,
                 uint8_t scaleIndex, uint8_t rootNote, uint8_t baseOctave, uint8_t octaveRange)
    {
        const juce::SpinLock::ScopedLockType sl (lock);
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
            activeSequencer().allNotesOff();
            if (sliceIsActive)
                panicBreaks = true;
        }
    }

    void ratchet (uint8_t division, bool active)
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        activeSequencer().setRatchet (division, active);
    }

    void sliceConfig (uint8_t bars, uint8_t division, uint8_t sliceCount,
                      uint8_t rootNote, uint8_t direction, uint8_t swing)
    {
        const juce::SpinLock::ScopedLockType sl (lock);

        // Same guard as the server: only a real grid change re-slices.
        if ((int) bars != lastSliceBars || (int) division != lastSliceDivision)
        {
            sliceSeq.setGrid (bars, division);
            lastSliceBars     = bars;
            lastSliceDivision = division;
        }

        sliceSeq.setSlices (sliceCount, rootNote);
        sliceSeq.setDirection (static_cast<ClockedSequencer::Direction> (juce::jlimit (0, 3, (int) direction)));
        sliceSeq.setSwing ((float) juce::jlimit (0, 100, (int) swing) / 100.0f);
    }

    void looper (uint8_t division, bool active, bool randomOn)
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        sliceSeq.setLooperRandom (randomOn);
        sliceSeq.setLooper (division, active);
    }

    // Locally there is no Simpler to receive pitch bend or a CC, so the
    // ribbons play the break player directly: pitch bend repitches the
    // slices ±12 semitones, and any CC sweeps a low-pass over them.
    void ribbon (uint8_t ribbonIndex, int value14bit)
    {
        if (ribbonIndex >= NumRibbons)
            return;

        switch (ribbonTargets[ribbonIndex])
        {
            case MidiMorf::RibbonTarget::PitchBend:
                if (auto* s = sliceSampler())
                    s->player().setPitchSemitones ((float) (value14bit - 8192) / 8192.0f * 12.0f);
                break;

            case MidiMorf::RibbonTarget::CC:
                if (auto* s = sliceSampler())
                    s->player().setTone ((float) value14bit / 16383.0f);
                break;

            case MidiMorf::RibbonTarget::StepRepeat:
            {
                const juce::SpinLock::ScopedLockType sl (lock);
                sliceSeq.setStepRepeat (value14bit);
                break;
            }
        }
    }

    void ribbonAssign (uint8_t ribbonIndex, MidiMorf::RibbonTarget target, uint8_t /*ccNumber*/)
    {
        if (ribbonIndex >= NumRibbons)
            return;

        const auto old = ribbonTargets[ribbonIndex];
        ribbonTargets[ribbonIndex] = target;

        // Reassigning away from a target must not leave it stuck.
        if (old == MidiMorf::RibbonTarget::StepRepeat && target != old)
        {
            const juce::SpinLock::ScopedLockType sl (lock);
            sliceSeq.setStepRepeat (1);
        }
        if (old == MidiMorf::RibbonTarget::PitchBend && target != old)
            if (auto* s = sliceSampler()) s->player().setPitchSemitones (0.0f);
        if (old == MidiMorf::RibbonTarget::CC && target != old)
            if (auto* s = sliceSampler()) s->player().setTone (1.0f);
    }

private:
    struct Event { int track; bool on; int channel, note, velocity; };

    // The engines' output: queued under `lock`, delivered on the audio thread.
    class QueueSink : public SeqSink
    {
    public:
        // The engines only send from inside `lock`, so reading the track is safe.
        QueueSink (LocalSequencers& o, Which w) : owner (o), which (w) {}

        void sendNoteOn (int ch, int note, int velocity) override { owner.push ({ owner.tracks[which], true, ch, note, velocity }); }
        void sendNoteOff (int ch, int note) override             { owner.push ({ owner.tracks[which], false, ch, note, 0 }); }

        // e.g. the step page's MOD lane rides CC1 into the synth's mod wheel.
        void sendCC (int, int cc, int value) override
        {
            if (auto* s = owner.rack.getSource (owner.tracks[which]))
                s->controller (cc, value);
        }

    private:
        LocalSequencers& owner;
        Which which;
    };

    static constexpr int NumRibbons = 2;
    static constexpr int MaxEvents  = 256;

    void push (const Event& e)
    {
        if (numEvents < MaxEvents)
            events[(size_t) numEvents++] = e;
    }

    // Audio thread, under lock.
    void drainEvents()
    {
        if (numEvents > 0)
            if (auto* s = sliceSampler())
                s->setSliceMapping ((int) sliceSeq.getRootNote(), stepBeats());

        for (int i = 0; i < numEvents; ++i)
        {
            const auto& e = events[(size_t) i];
            if (e.on) rack.noteOn (e.track, e.channel, e.note, e.velocity);
            else      rack.noteOff (e.track, e.channel, e.note);
        }
        numEvents = 0;
    }

    double stepBeats() const
    {
        return lastSliceDivision == 0 ? 1.0 : lastSliceDivision == 1 ? 0.5 : 0.25;
    }

    double samplesPerTick() const
    {
        return sampleRate * 60.0 / (bpm.load() * SeqPPQN);
    }

    ClockedSequencer& sequencer (int w) { return w == Step ? (ClockedSequencer&) stepSeq : sliceSeq; }

    ClockedSequencer& activeSequencer() { return sliceIsActive ? (ClockedSequencer&) sliceSeq : stepSeq; }

    //==========================================================================
    // The drum grid's clock — audio thread, under lock.

    // The tick a 16th falls on, swing included. Sixteenths count from the
    // clock's start, so the grid stays in phase with the other sequencers.
    double drumStepTick (int sixteenth) const
    {
        constexpr int tps = SeqPPQN / 4;
        return sixteenth * tps + ((sixteenth % 2) == 1 ? drumSwing.load() * tps * 0.5 : 0.0);
    }

    // Steps fire on the nearest sample.
    void fireDueDrumSteps (double now)
    {
        while ((drumStepTick (nextDrumSixteenth) - now) * samplesPerTick() < 0.5)
        {
            if (nextDrumSixteenth % launchSixteenths() == 0 && atBarLine (Drums))
                return;   // stopped on the launch point

            // Last step's notes end as the next step starts (a sampler in
            // Keys mode needs its note-offs; the drum synth ignores them).
            releaseDrumNotes();

            const int step = nextDrumSixteenth % DrumSteps;
            for (int lane = 0; lane < DrumLanes; ++lane)
                if (const int vel = drumPattern[(size_t) lane][(size_t) step]; vel > 0)
                {
                    push ({ tracks[Drums], true, DrumChannel, ::Drums::noteFor (lane), vel });
                    drumSounding[(size_t) lane] = true;
                }

            drumStep.store (step);
            ++nextDrumSixteenth;
        }
    }

    void releaseDrumNotes()
    {
        for (int lane = 0; lane < DrumLanes; ++lane)
            if (std::exchange (drumSounding[(size_t) lane], false))
                push ({ tracks[Drums], false, DrumChannel, ::Drums::noteFor (lane), 0 });
    }

    // Under lock. The queued pattern becomes the playing one.
    void applyPending (int w)
    {
        if (! hasPending[w])
            return;
        hasPending[w] = false;

        const auto& p = pending[w];
        if (w == Drums)
        {
            drumPattern = p.drums;
        }
        else
        {
            auto& seq = sequencer (w);
            for (int i = 0; i < SeqPattern::MaxSteps; ++i)
                seq.setStep (i, p.steps[(size_t) i]);
            if (w == Step)
                seq.setPatternLength (p.length);   // the slicer's length is its grid's
        }
        launches[w].fetch_add (1);
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
    bool atBarLine (int w)
    {
        if (std::exchange (pendingStop[w], false))
        {
            wantPlaying[w].store (false);
            stopSequencer (w);
            return true;
        }
        applyPending (w);
        return false;
    }

    void stopSequencer (int w)
    {
        if (w == Drums)
        {
            releaseDrumNotes();
            drumStep.store (-1);
        }
        else
        {
            sequencer (w).clockStop();
            if (w == Slice)
                if (auto* s = sliceSampler())
                    s->player().releaseAll();   // one-shots ignore the note-off
        }
        running[w].store (false);
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

        for (int w = 0; w < NumSequencers; ++w)
        {
            const bool want = wantPlaying[w].load();
            anyWanted |= want;

            if (! want && running[w].load())
                stopSequencer (w);
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

    //==========================================================================
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

        // Launch points for the tick-driven two; the drum grid has its own.
        if (globalTick % (launchSixteenths() * (SeqPPQN / 4)) == 0)
            for (int w : { Step, Slice })
                if (running[w].load())
                    atBarLine (w);

        for (int w = 0; w < NumSequencers; ++w)
        {
            if (wantPlaying[w].load() && ! running[w].load() && globalTick % (SeqPPQN / 4) == 0)
            {
                if (w == Drums)
                {
                    nextDrumSixteenth = globalTick / (SeqPPQN / 4);   // fires right after this tick
                    running[w].store (true);
                    continue;
                }

                if (globalTick == 0)
                    sequencer (w).clockStart();
                else
                    sequencer (w).clockSongPosition (globalTick / (SeqPPQN / 4));
                running[w].store (true);
            }

            if (running[w].load() && w != Drums)
                sequencer (w).clockTick();
        }

        ++globalTick;
    }

    TrackRack& rack;

    // Declared before the engines: their destructors still send note-offs.
    mutable juce::SpinLock lock;
    int tracks[NumSequencers] = { DefaultStepTrack, DefaultSliceTrack, DefaultDrumTrack };   // under lock
    std::array<Event, MaxEvents> events;
    int numEvents = 0;

    QueueSink stepSink, sliceSink;
    StepSequencerEngine stepSeq;
    SliceSequencerEngine sliceSeq;

    double sampleRate = 48000.0;
    std::atomic<double> bpm { 120.0 };
    std::atomic<bool> wantPlaying[NumSequencers] { { false }, { false }, { false } };
    std::atomic<bool> running[NumSequencers]     { { false }, { false }, { false } };

    // The drum grid.
    static constexpr int DrumChannel = 10;
    std::array<std::array<uint8_t, DrumSteps>, DrumLanes> drumPattern {};   // under lock
    std::array<bool, DrumLanes> drumSounding {};                            // under lock
    int nextDrumSixteenth = 0;                                              // under lock
    std::atomic<float> drumSwing { 0.0f };

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

    // Session launching, under lock.
    std::array<SeqPattern, NumSequencers> pending {};
    bool hasPending[NumSequencers] {}, pendingStop[NumSequencers] {};
    std::atomic<int> launches[NumSequencers] { { 0 }, { 0 }, { 0 } };
    std::atomic<int> launchQuantize { Bar };
    std::atomic<int> drumStep { -1 };
    bool   clockRunning  = false;   // audio thread
    int    globalTick    = 0;
    double samplesToTick = 0.0;

    bool sliceIsActive = false;
    bool panicBreaks   = false;   // under lock
    int  lastSliceBars = -1, lastSliceDivision = 2;
    MidiMorf::RibbonTarget ribbonTargets[NumRibbons] = { MidiMorf::RibbonTarget::PitchBend,
                                                         MidiMorf::RibbonTarget::StepRepeat };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LocalSequencers)
};
