#pragma once

#include "ClockedSequencer.h"

// Slice Sequencer — Creative Sequencer Pack, engine 2. Monome mlr / Re:Mix
// lineage: drop a 1–2 bar loop into Simpler or Sampler in slice mode, and this
// re-orders the slices live.
//
// Slices map chromatically upward from a root note, which is exactly how
// Simpler's slice mode lays them out. So the note table is simply N chromatic
// notes, and a step's index IS its slice number. Identity — step N plays slice
// N — reproduces the original loop; pulling steps off that diagonal is the
// remix.
//
// Adds a beat-repeat looper on top of the shared roller: hold a division and
// playback loops that window, snapped to the grid. With random armed, each
// repeat fires a different slice instead.
class SliceSequencerEngine : public ClockedSequencer
{
public:
    SliceSequencerEngine (VirtualMidiPort& port, int channel = 9)
        : ClockedSequencer (port, channel)
    {
        buildNoteTable();
        loadIdentityPattern();
    }

    // Simpler lays slices out chromatically from its root note (C1 by default).
    void setSlices (int count, uint8_t rootNote)
    {
        sliceCount = juce::jlimit (1, MAX_STEPS, count);
        root       = juce::jlimit ((uint8_t) 0, (uint8_t) 127, rootNote);
        buildNoteTable();
    }

    int     getSliceCount() const { return sliceCount; }
    uint8_t getRootNote()   const { return root; }

    // Grid size follows bars x division, so 1 bar of 1/16 is 16 steps and
    // 2 bars is 32. Re-slices to match, then rebuilds the identity pattern.
    void setGrid (int bars, int division)
    {
        const int perBar = (division == 0) ? 4 : (division == 1) ? 8 : 16;
        const int steps  = juce::jlimit (1, MAX_STEPS, juce::jlimit (1, 2, bars) * perBar);

        setPatternLength (steps);
        setRate (division == 0 ? Rate::Quarter
               : division == 1 ? Rate::Eighth
                               : Rate::Sixteenth);

        sliceCount = steps;
        buildNoteTable();
        loadIdentityPattern();
    }

    // Beat repeat. Held from the phone; snaps its loop start to the grid so a
    // stutter lands musically rather than wherever the finger happened to fall.
    void setLooper (int division, bool active)
    {
        looperDivision = juce::jlimit (0, 3, division);

        if (active && ! looperActive)
        {
            const int d = looperTicks();
            loopStartTick = (tickCounter / d) * d;   // snap down to the grid
        }

        looperActive = active;

        // A repeat window shorter than one step never crosses a step
        // boundary, so the window transform alone would fall silent. Those
        // divisions ride the roller instead: it retriggers the held step at
        // the division rate, which is the stutter the gesture means anyway.
        setRatchet (looperDivision, active && looperTicks() < ticksPerStep());
    }

    void setLooperRandom (bool shouldRandomise) { looperRandom = shouldRandomise; }

    bool isLooping() const { return looperActive; }

protected:
    void buildNoteTable() override
    {
        noteTable.clear();

        for (int i = 0; i < sliceCount; ++i)
        {
            const int note = (int) root + i;

            if (note <= 127)
                noteTable.push_back (note);
        }

        if (noteTable.empty())
            noteTable.push_back ((int) root);
    }

    // One random slice per repeat, but only while the looper is actually held.
    uint8_t resolveIndex (uint8_t stored) override
    {
        if (looperActive && looperRandom && sliceCount > 1)
            return (uint8_t) rng.nextInt (sliceCount);

        return stored;
    }

    // Warp the playback position without touching tickCounter, so releasing
    // the looper drops us back exactly where the DAW expects us to be.
    int transformTick (int t) const override
    {
        if (! looperActive)
            return t;

        const int d = looperTicks();
        return loopStartTick + ((t - loopStartTick) % d);
    }

    void onTransportStart() override { loopStartTick = 0; }

private:
    int looperTicks() const
    {
        switch (looperDivision)
        {
            case 0:  return 24;   // 1/4
            case 1:  return 12;   // 1/8
            case 2:  return 6;    // 1/16
            default: return 3;    // 1/32
        }
    }

    // Step N plays slice N — the untouched loop, and the baseline the remix
    // departs from.
    void loadIdentityPattern()
    {
        for (int i = 0; i < MAX_STEPS; ++i)
        {
            Step s;
            s.index    = (uint8_t) (i % juce::jmax (1, sliceCount));
            s.velocity = 100;
            s.gate     = 95;      // slices want to run into each other
            s.mod      = 64;
            s.rest     = false;
            setStep (i, s);
        }
    }

    int     sliceCount = 16;
    uint8_t root       = 36;   // C1 — Simpler's default slice root

    int  looperDivision = 2;   // 1/16
    bool looperActive   = false;
    bool looperRandom   = false;
    int  loopStartTick  = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SliceSequencerEngine)
};
