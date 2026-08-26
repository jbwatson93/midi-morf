#pragma once

#include <JuceHeader.h>
#include "VirtualMidiPort.h"
#include "MidiClockReceiver.h"
#include <vector>
#include <array>
#include <cmath>

// Step Sequencer — Creative Sequencer Pack, engine 1 of 3.
//
// Moog / Beatstep Pro lineage: 16 steps, pitch quantised to key + scale, with
// velocity, gate and rest per step. The phone is a remote editor only; all
// timing lives here, driven by DAW MIDI clock via MidiClockReceiver.
//
// Pitch is a SCALE DEGREE, not a MIDI note. A slider at 50% is always in key —
// that is the whole appeal, and it means one byte per step over the wire.
//
// Everything is tick-based (24 PPQN), never wall-clock. Tempo changes therefore
// need no special handling: the clock simply arrives faster or slower.
//
// Includes momentary note repeat (the roller), added to v1 at JB's request.
// Still EXCLUDES per-step probability and pattern chaining — those are v1.1.
// See midi-morf-next-plan.md, WS-1.
class StepSequencerEngine : public MidiClockReceiver::Listener
{
public:
    static constexpr int MAX_STEPS = 16;

    enum class Rate     { Quarter, Eighth, EighthTriplet, Sixteenth, SixteenthTriplet, ThirtySecond };
    enum class Direction { Forward, Reverse, PingPong, Random };

    struct Step
    {
        uint8_t degree   = 0;    // index into the scale note table
        uint8_t velocity = 100;  // 1..127
        uint8_t gate     = 50;   // % of step length, 1..100
        uint8_t mod      = 64;   // CC1 value sent when this step comes round
        bool    rest     = false;
    };

    StepSequencerEngine (VirtualMidiPort& port, int channel = 8)
        : midiPort (port), seqChannel (channel)
    {
        buildNoteTable();
        loadDemoPattern();
    }

    ~StepSequencerEngine() override { allNotesOff(); }

    // ── Config ───────────────────────────────────────────────────────────────

    void setChannel (int ch) { seqChannel = juce::jlimit (1, 16, ch); }
    int  getChannel() const  { return seqChannel; }

    // Gated by the Creative Sequencer Pack entitlement. See WS-3.
    void setEnabled (bool shouldBeEnabled)
    {
        if (enabled == shouldBeEnabled)
            return;

        enabled = shouldBeEnabled;

        if (! enabled)
            allNotesOff();
    }

    bool isEnabled() const { return enabled; }

    void setScale (uint8_t scaleIndex, uint8_t rootNote, uint8_t baseOct, uint8_t octRange = 3)
    {
        currentScaleIndex = juce::jlimit ((uint8_t) 0, (uint8_t) 4, scaleIndex);
        currentRoot       = juce::jlimit ((uint8_t) 0, (uint8_t) 11, rootNote);
        baseOctave        = juce::jlimit ((uint8_t) 0, (uint8_t) 8, baseOct);
        octaveRange       = juce::jlimit ((uint8_t) 1, (uint8_t) 5, octRange);
        buildNoteTable();
    }

    // Independent of 16 — this is what gives the polymetric Moog feel.
    void setPatternLength (int steps) { patternLength = juce::jlimit (1, MAX_STEPS, steps); }
    int  getPatternLength() const     { return patternLength; }

    void setRate (Rate r)      { rate = r; }
    Rate getRate() const       { return rate; }

    void setDirection (Direction d) { direction = d; }
    Direction getDirection() const  { return direction; }

    // 0 = straight, 1 = full triplet feel. Delays every second step.
    void setSwing (float amount) { swing = juce::jlimit (0.0f, 1.0f, amount); }
    float getSwing() const       { return swing; }

    void setStep (int index, const Step& s)
    {
        if (! juce::isPositiveAndBelow (index, MAX_STEPS))
            return;

        auto& dest = steps[(size_t) index];
        dest.degree   = s.degree;
        dest.velocity = (uint8_t) juce::jlimit (1, 127, (int) s.velocity);
        dest.gate     = (uint8_t) juce::jlimit (1, 100, (int) s.gate);
        dest.mod      = (uint8_t) juce::jlimit (0, 127, (int) s.mod);
        dest.rest     = s.rest;
    }

    // Momentary note repeat, Beatstep Pro roller style. Held from the phone.
    // The playhead keeps advancing underneath, so releasing lands where the
    // sequence would have been rather than however far behind the stutter got.
    void setRatchet (int division, bool active)
    {
        ratchetDivision = juce::jlimit (0, 3, division);
        ratchetActive   = active;
    }

    bool isRatcheting() const { return ratchetActive; }

    const Step& getStep (int index) const
    {
        return steps[(size_t) juce::jlimit (0, MAX_STEPS - 1, index)];
    }

    int getNoteTableSize() const { return (int) noteTable.size(); }

    // ── Playhead (for the phone display) ─────────────────────────────────────

    int getCurrentStep() const { return currentStep.load(); }

    // ── MidiClockReceiver::Listener ──────────────────────────────────────────

    void clockStart() override
    {
        allNotesOff();
        tickCounter = 0;
        currentStep.store (-1);
    }

    void clockContinue() override
    {
        // Resume where we left off — do not reset the tick counter.
    }

    void clockStop() override
    {
        allNotesOff();
        currentStep.store (-1);
    }

    void clockSongPosition (int sixteenths) override
    {
        allNotesOff();
        // SPP counts sixteenths; one sixteenth is 6 ticks at 24 PPQN.
        tickCounter = sixteenths * (MidiClockReceiver::PPQN / 4);
    }

    void clockTick() override
    {
        if (! enabled)
            return;

        // Gate expiry runs BEFORE the next trigger, so a 100% gate releases
        // exactly as the following step fires rather than overlapping it.
        if (activeNote >= 0 && --gateTicksRemaining <= 0)
        {
            midiPort.sendNoteOff (seqChannel, activeNote);
            activeNote = -1;
        }

        const int tps       = ticksPerStep();
        const int slot      = tickCounter / tps;
        const int posInStep = tickCounter % tps;

        // Swing delays every second slot by up to a third of a step.
        const int offset = ((slot % 2) == 1) ? swingTicks (tps) : 0;

        if (ratchetActive)
        {
            // Advance the playhead but suppress the normal trigger — the roller
            // owns note firing while it is held.
            if (posInStep == offset)
                currentStep.store (patternIndexFor (slot));

            const int rt = ratchetTicks();

            if ((tickCounter % rt) == 0)
            {
                const int index = currentStep.load();

                if (index >= 0)
                    fireIndex (index, rt);
            }
        }
        else if (posInStep == offset)
        {
            fireStep (slot, tps);
        }

        ++tickCounter;
    }

    void allNotesOff()
    {
        if (activeNote >= 0)
        {
            midiPort.sendNoteOff (seqChannel, activeNote);
            activeNote = -1;
        }

        gateTicksRemaining = 0;
        lastSentMod = -1;   // force the mod lane to resend on the next step
    }

private:
    // ── Timing ───────────────────────────────────────────────────────────────

    int ticksPerStep() const
    {
        switch (rate)
        {
            case Rate::Quarter:          return 24;
            case Rate::Eighth:           return 12;
            case Rate::EighthTriplet:    return 8;
            case Rate::Sixteenth:        return 6;
            case Rate::SixteenthTriplet: return 4;
            case Rate::ThirtySecond:     return 3;
        }

        return 6;
    }

    int swingTicks (int tps) const
    {
        // A third of a step is the classic triplet-feel maximum.
        return (int) (swing * (float) tps / 3.0f);
    }

    int ratchetTicks() const
    {
        switch (ratchetDivision)
        {
            case 0:  return 24;   // 1/4
            case 1:  return 12;   // 1/8
            case 2:  return 6;    // 1/16
            default: return 3;    // 1/32
        }
    }

    // ── Sequencing ───────────────────────────────────────────────────────────

    int patternIndexFor (int slot)
    {
        const int len = patternLength;

        if (len <= 1)
            return 0;

        switch (direction)
        {
            case Direction::Forward:  return slot % len;
            case Direction::Reverse:  return len - 1 - (slot % len);

            case Direction::PingPong:
            {
                const int period = 2 * len - 2;
                const int p = slot % period;
                return p < len ? p : period - p;
            }

            case Direction::Random:   return rng.nextInt (len);
        }

        return slot % len;
    }

    void fireStep (int slot, int tps)
    {
        const int index = patternIndexFor (slot);
        currentStep.store (index);
        fireIndex (index, tps);
    }

    // gateBasisTicks is the span the gate percentage is measured against — a
    // full step normally, the roller division while ratcheting.
    void fireIndex (int index, int gateBasisTicks)
    {
        const auto& s = steps[(size_t) index];

        // The mod lane is a modulation curve, not a note property, so it keeps
        // moving through rests — a sweep continues across the gaps.
        if (s.mod != lastSentMod)
        {
            midiPort.sendCC (seqChannel, 1, s.mod);
            lastSentMod = s.mod;
        }

        if (s.rest)
            return;

        // Retrigger cleanly if the previous note is still sounding.
        if (activeNote >= 0)
        {
            midiPort.sendNoteOff (seqChannel, activeNote);
            activeNote = -1;
        }

        const int note = noteForDegree (s.degree);

        midiPort.sendNoteOn (seqChannel, note, s.velocity);
        activeNote = note;
        gateTicksRemaining = juce::jmax (1, (gateBasisTicks * (int) s.gate) / 100);
    }

    // ── Scale ────────────────────────────────────────────────────────────────

    void buildNoteTable()
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
        {
            for (int interval : intervals)
            {
                const int note = baseNote + oct * 12 + interval;

                if (note <= 127)
                    noteTable.push_back (note);
            }
        }

        if (noteTable.empty())
            noteTable.push_back (60);
    }

    int noteForDegree (uint8_t degree) const
    {
        const int index = juce::jlimit (0, (int) noteTable.size() - 1, (int) degree);
        return noteTable[(size_t) index];
    }

    // A rolling major-scale line so wiring the engine up produces something
    // musical immediately, before any phone is involved. Accents on 1/5/9/13.
    void loadDemoPattern()
    {
        static const uint8_t degrees[MAX_STEPS] =
            { 0, 2, 4, 2, 7, 4, 2, 4, 0, 2, 4, 7, 9, 7, 4, 2 };

        for (int i = 0; i < MAX_STEPS; ++i)
        {
            auto& s = steps[(size_t) i];
            s.degree   = degrees[i];
            s.velocity = (i % 4 == 0) ? 112 : 80;
            s.gate     = 50;
            s.mod      = (uint8_t) (64 + 40 * std::sin (i * 0.4));
            s.rest     = false;
        }
    }

    VirtualMidiPort& midiPort;
    int seqChannel = 8;

    std::array<Step, MAX_STEPS> steps;
    std::vector<int> noteTable;

    uint8_t currentScaleIndex = 0;   // Major
    uint8_t currentRoot       = 0;   // C
    uint8_t baseOctave        = 2;   // C2, matching MorfPadHandler
    uint8_t octaveRange       = 3;

    int       patternLength = MAX_STEPS;
    Rate      rate      = Rate::Sixteenth;
    Direction direction = Direction::Forward;
    float     swing     = 0.0f;

    bool enabled = true;   // TODO: default false once the pack entitlement lands (WS-3)

    int ratchetDivision = 2;       // 1/16
    bool ratchetActive  = false;

    // MIDI-thread state
    int tickCounter        = 0;
    int activeNote         = -1;
    int gateTicksRemaining = 0;
    int lastSentMod        = -1;
    juce::Random rng;

    std::atomic<int> currentStep { -1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StepSequencerEngine)
};
