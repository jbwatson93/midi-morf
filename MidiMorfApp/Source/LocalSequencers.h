#pragma once

#include <JuceHeader.h>
#include "TrackRack.h"
#include "BreakPlayer.h"
#include "StepSequencerEngine.h"
#include "SliceSequencerEngine.h"
#include "MidiMorfProtocol.h"

//==============================================================================
// The sequencers in Local mode: the same engines the server runs, driven by
// the phone's own clock instead of DAW MIDI clock. The step sequencer plays
// track 1 (the synth); the slice sequencer plays track 3 (the break player).
//
// This is the AudioSource the device plays. Each block is cut at clock-tick
// boundaries, so notes land on the right sample rather than on whatever block
// edge they fall nearest — at 120 BPM a tick is ~1000 samples, a block 256.
//
// Threading: the engines are only ever ticked on the audio thread. Edits from
// the pages take `lock` briefly; the engines' note output is queued under the
// same lock and drained on the audio thread, so the track sources are only
// ever played from there.
class LocalSequencers : public juce::AudioSource
{
public:
    enum Which { Step = 0, Slice = 1 };

    // Tracks are 0-based here; the UI calls them T1-T4.
    static constexpr int StepTrack = 0, SliceTrack = 2;

    LocalSequencers (TrackRack& r, BreakSource& b)
        : rack (r),
          breakSource (b),
          breaks (b.player()),
          stepSink (*this, StepTrack),
          sliceSink (*this, SliceTrack),
          stepSeq (stepSink, 8),
          sliceSeq (sliceSink, 9)
    {
    }

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
                }

                drainEvents();

                if (std::exchange (panicBreaks, false))
                    breaks.releaseAll();
            }

            rack.render (*buf, pos, n);

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
        wantPlaying[Step].store (false);
        wantPlaying[Slice].store (false);
    }

    void   setBpm (double b) { bpm.store (juce::jlimit (40.0, 240.0, b)); }
    double getBpm() const    { return bpm.load(); }

    int getPlayhead (Which w) const
    {
        if (! running[w].load())
            return -1;
        return w == Step ? stepSeq.getCurrentStep() : sliceSeq.getCurrentStep();
    }

    //==========================================================================
    // Breaks — message thread.
    // A new break also sets the tempo to the loop's own.
    void setBuiltInBreak (int index)
    {
        if (auto* b = breakSource.setBuiltInBreak (index))
            setBpm (b->bpm());
    }

    bool importBreak (const juce::File& file, juce::String& error)
    {
        auto* b = breakSource.importBreak (file, error);
        if (b == nullptr)
            return false;

        setBpm (b->bpm());
        return true;
    }

    const Break* getBreak() const        { return breakSource.getBreak(); }
    int  getBuiltInBreakIndex() const    { return breakSource.getBuiltInBreakIndex(); }
    juce::File getImportedBreak() const  { return breakSource.getImportedBreak(); }

    // Tempo and break survive relaunches; patterns don't yet (the pages
    // don't persist them in WiFi mode either).
    void saveState (juce::PropertySet& p) const
    {
        p.setValue ("seq_bpm", getBpm());
        const int breakIndex = getBuiltInBreakIndex();
        p.setValue ("seq_break", breakIndex >= 0 ? BreakLibrary::builtInNames()[breakIndex] : juce::String());
        p.setValue ("seq_breakFile", getImportedBreak().getFullPathName());
    }

    void restoreState (juce::PropertySet& p)
    {
        const auto path = p.getValue ("seq_breakFile");
        juce::String error;
        const bool imported = path.isNotEmpty() && juce::File (path).existsAsFile()
                              && importBreak (juce::File (path), error);

        if (! imported)
            setBuiltInBreak (juce::jmax (0, BreakLibrary::indexForName (p.getValue ("seq_break"))));

        if (p.containsKey ("seq_bpm"))
            setBpm (p.getDoubleValue ("seq_bpm"));
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
                breaks.setPitchSemitones ((float) (value14bit - 8192) / 8192.0f * 12.0f);
                break;

            case MidiMorf::RibbonTarget::CC:
                breaks.setTone ((float) value14bit / 16383.0f);
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
            breaks.setPitchSemitones (0.0f);
        if (old == MidiMorf::RibbonTarget::CC && target != old)
            breaks.setTone (1.0f);
    }

private:
    struct Event { int track; bool on; int note; int velocity; };

    // The engines' output: queued under `lock`, delivered on the audio thread.
    class QueueSink : public SeqSink
    {
    public:
        QueueSink (LocalSequencers& o, int t) : owner (o), track (t) {}

        void sendNoteOn (int, int note, int velocity) override { owner.push ({ track, true, note, velocity }); }
        void sendNoteOff (int, int note) override             { owner.push ({ track, false, note, 0 }); }

        // e.g. the step page's MOD lane rides CC1 into the synth's mod wheel.
        void sendCC (int, int cc, int value) override
        {
            if (auto* s = owner.rack.getSource (track))
                s->controller (cc, value);
        }

    private:
        LocalSequencers& owner;
        int track;
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
            breakSource.setSliceMapping ((int) sliceSeq.getRootNote(), stepBeats());

        for (int i = 0; i < numEvents; ++i)
        {
            const auto& e = events[(size_t) i];
            if (e.on) rack.noteOn (e.track, e.note, e.velocity);
            else      rack.noteOff (e.track, e.note);
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

    // Audio thread, under lock.
    void updateTransport()
    {
        bool anyWanted = false;

        for (int w = 0; w < 2; ++w)
        {
            const bool want = wantPlaying[w].load();
            anyWanted |= want;

            if (! want && running[w].load())
            {
                sequencer (w).clockStop();
                running[w].store (false);

                if (w == Slice)
                    breaks.releaseAll();   // one-shots ignore the note-off
            }
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

    // Audio thread, under lock.
    void tick()
    {
        for (int w = 0; w < 2; ++w)
        {
            if (wantPlaying[w].load() && ! running[w].load() && globalTick % (SeqPPQN / 4) == 0)
            {
                if (globalTick == 0)
                    sequencer (w).clockStart();
                else
                    sequencer (w).clockSongPosition (globalTick / (SeqPPQN / 4));
                running[w].store (true);
            }

            if (running[w].load())
                sequencer (w).clockTick();
        }

        ++globalTick;
    }

    TrackRack& rack;
    BreakSource& breakSource;
    BreakPlayer& breaks;   // the ribbons and STOP play it directly

    // Declared before the engines: their destructors still send note-offs.
    juce::SpinLock lock;
    std::array<Event, MaxEvents> events;
    int numEvents = 0;

    QueueSink stepSink, sliceSink;
    StepSequencerEngine stepSeq;
    SliceSequencerEngine sliceSeq;

    double sampleRate = 48000.0;
    std::atomic<double> bpm { 120.0 };
    std::atomic<bool> wantPlaying[2] { { false }, { false } };
    std::atomic<bool> running[2]     { { false }, { false } };
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
