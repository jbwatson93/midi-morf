#pragma once

#include "ClockedSequencer.h"

// Step Sequencer — Creative Sequencer Pack, engine 1.
//
// Moog / Beatstep Pro lineage. All the timing lives in ClockedSequencer; this
// class supplies only the note table, which maps step indices to scale degrees
// so a slider at 50% is always in key. That is the whole appeal, and it means
// one byte per step over the wire.
//
// Excludes per-step probability and pattern chaining — those are v1.1.
class StepSequencerEngine : public ClockedSequencer
{
public:
    StepSequencerEngine (VirtualMidiPort& port, int channel = 8)
        : ClockedSequencer (port, channel)
    {
        buildNoteTable();
        loadDemoPattern();
    }

    void setScale (uint8_t scaleIndex, uint8_t rootNote, uint8_t baseOct, uint8_t octRange = 3)
    {
        currentScaleIndex = juce::jlimit ((uint8_t) 0, (uint8_t) 4, scaleIndex);
        currentRoot       = juce::jlimit ((uint8_t) 0, (uint8_t) 11, rootNote);
        baseOctave        = juce::jlimit ((uint8_t) 0, (uint8_t) 8, baseOct);
        octaveRange       = juce::jlimit ((uint8_t) 1, (uint8_t) 5, octRange);
        buildNoteTable();
    }

protected:
    void buildNoteTable() override
    {
        // Same five scales as MorfPadHandler — keep the two consistent.
        static const std::vector<std::vector<int>> scales = {
            { 0, 2, 4, 5, 7, 9, 11 },                    // Major
            { 0, 2, 3, 5, 7, 8, 10 },                    // Minor
            { 0, 2, 4, 7, 9 },                           // Pentatonic
            { 0, 3, 5, 6, 7, 10 },                       // Blues
            { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 },    // Chromatic
        };

        noteTable.clear();
        const auto& intervals = scales[currentScaleIndex];
        const int baseNote = (baseOctave + 1) * 12 + currentRoot;

        for (int oct = 0; oct < octaveRange; ++oct)
            for (int interval : intervals)
            {
                const int note = baseNote + oct * 12 + interval;

                if (note <= 127)
                    noteTable.push_back (note);
            }

        if (noteTable.empty())
            noteTable.push_back (60);
    }

private:
    // A rolling major-scale line so the engine produces something musical
    // immediately, before any phone is involved. Accents on 1/5/9/13.
    void loadDemoPattern()
    {
        static const uint8_t degrees[16] =
            { 0, 2, 4, 2, 7, 4, 2, 4, 0, 2, 4, 7, 9, 7, 4, 2 };

        for (int i = 0; i < MAX_STEPS; ++i)
        {
            Step s;
            s.index    = degrees[i % 16];
            s.velocity = (i % 4 == 0) ? 112 : 80;
            s.gate     = 50;
            s.mod      = (uint8_t) (64 + 40 * std::sin (i * 0.4));
            s.rest     = false;
            setStep (i, s);
        }

        setPatternLength (16);
    }

    uint8_t currentScaleIndex = 0;   // Major
    uint8_t currentRoot       = 0;   // C
    uint8_t baseOctave        = 2;   // C2, matching MorfPadHandler
    uint8_t octaveRange       = 3;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StepSequencerEngine)
};
