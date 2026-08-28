#pragma once

#include <JuceHeader.h>
#include "VirtualMidiPort.h"
#include "MidiClockReceiver.h"
#include <vector>
#include <array>

// Shared timing core for every sequencer in the Creative Sequencer Pack.
//
// Owns everything that is the same regardless of what the steps *mean*: tick
// counting against DAW MIDI clock, rate division, swing, direction, gate,
// rests, the momentary roller, and the playhead. Subclasses supply only a note
// table — scale degrees for the step sequencer, sample slices for the slicer —
// and the shared machinery does the rest.
//
// Everything is tick-based (24 PPQN), never wall-clock, so tempo changes need
// no special handling: the clock simply arrives faster or slower.
class ClockedSequencer : public MidiClockReceiver::Listener
{
public:
    // 2 bars of 1/16 is the longest grid the slicer offers.
    static constexpr int MAX_STEPS = 32;

    enum class Rate      { Quarter, Eighth, EighthTriplet, Sixteenth, SixteenthTriplet, ThirtySecond };
    enum class Direction { Forward, Reverse, PingPong, Random };

    struct Step
    {
        uint8_t index    = 0;    // into the note table — scale degree or slice
        uint8_t velocity = 100;  // 1..127
        uint8_t gate     = 50;   // % of step length, 1..100
        uint8_t mod      = 64;   // CC1 sent when this step comes round
        bool    rest     = false;
    };

    ClockedSequencer (VirtualMidiPort& port, int channel)
        : midiPort (port), seqChannel (channel) {}

    ~ClockedSequencer() override { allNotesOff(); }

    // ── Config ───────────────────────────────────────────────────────────────

    void setChannel (int ch) { seqChannel = juce::jlimit (1, 16, ch); }
    int  getChannel() const  { return seqChannel; }

    // Gated by the Creative Sequencer Pack entitlement.
    void setEnabled (bool shouldBeEnabled)
    {
        if (enabled == shouldBeEnabled)
            return;

        enabled = shouldBeEnabled;

        if (! enabled)
            allNotesOff();
    }

    bool isEnabled() const { return enabled; }

    void setPatternLength (int steps) { patternLength = juce::jlimit (1, MAX_STEPS, steps); }
    int  getPatternLength() const     { return patternLength; }

    void setRate (Rate r)      { rate = r; }
    Rate getRate() const       { return rate; }

    void setDirection (Direction d) { direction = d; }
    Direction getDirection() const  { return direction; }

    // 0 = straight, 1 = full triplet feel. Delays every second step.
    void setSwing (float amount) { swing = juce::jlimit (0.0f, 1.0f, amount); }
    float getSwing() const       { return swing; }

    void setStep (int i, const Step& s)
    {
        if (! juce::isPositiveAndBelow (i, MAX_STEPS))
            return;

        auto& dest = steps[(size_t) i];
        dest.index    = s.index;
        dest.velocity = (uint8_t) juce::jlimit (1, 127, (int) s.velocity);
        dest.gate     = (uint8_t) juce::jlimit (1, 100, (int) s.gate);
        dest.mod      = (uint8_t) juce::jlimit (0, 127, (int) s.mod);
        dest.rest     = s.rest;
    }

    const Step& getStep (int i) const
    {
        return steps[(size_t) juce::jlimit (0, MAX_STEPS - 1, i)];
    }

    // Momentary note repeat, Beatstep Pro roller style. The playhead keeps
    // advancing underneath, so releasing lands where the sequence would have
    // been rather than however far behind the stutter got.
    void setRatchet (int division, bool active)
    {
        ratchetDivision = juce::jlimit (0, 3, division);
        ratchetActive   = active;
    }

    bool isRatcheting() const { return ratchetActive; }

    int getNoteTableSize() const { return (int) noteTable.size(); }
    int getCurrentStep()  const  { return currentStep.load(); }

    // ── MidiClockReceiver::Listener ──────────────────────────────────────────

    void clockStart() override
    {
        allNotesOff();
        tickCounter = 0;
        currentStep.store (-1);
        onTransportStart();
    }

    void clockContinue() override {}   // resume where we left off

    void clockStop() override
    {
        allNotesOff();
        currentStep.store (-1);
    }

    void clockSongPosition (int sixteenths) override
    {
        allNotesOff();
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

        // Subclasses may warp the playback position (the slicer's beat repeat)
        // while tickCounter itself keeps advancing, so we stay locked to the DAW.
        const int effTick   = transformTick (tickCounter);
        const int tps       = ticksPerStep();
        const int slot      = effTick / tps;
        const int posInStep = effTick % tps;
        const int offset    = ((slot % 2) == 1) ? swingTicks (tps) : 0;

        if (ratchetActive)
        {
            // The roller owns note firing while held; the playhead still moves.
            if (posInStep == offset)
                currentStep.store (patternIndexFor (slot));

            const int rt = ratchetTicks();

            if ((effTick % rt) == 0)
            {
                const int index = currentStep.load();

                if (index >= 0)
                    fireIndex (index, rt);
            }
        }
        else if (posInStep == offset)
        {
            const int index = patternIndexFor (slot);
            currentStep.store (index);
            fireIndex (index, tps);
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

protected:
    // ── Subclass hooks ───────────────────────────────────────────────────────

    // Fill noteTable. Called by subclasses whenever their config changes.
    virtual void buildNoteTable() = 0;

    // Lets a subclass substitute a different step at fire time — the slicer
    // uses this for random-per-repeat while the looper is engaged.
    virtual uint8_t resolveIndex (uint8_t stored) { return stored; }

    // Lets a subclass warp playback position without disturbing tickCounter.
    virtual int transformTick (int t) const { return t; }

    virtual void onTransportStart() {}

    int noteForIndex (uint8_t i) const
    {
        if (noteTable.empty())
            return 60;

        return noteTable[(size_t) juce::jlimit (0, (int) noteTable.size() - 1, (int) i)];
    }

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

    std::vector<int> noteTable;
    std::array<Step, MAX_STEPS> steps;
    juce::Random rng;
    int tickCounter = 0;

private:
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

    // gateBasisTicks is the span the gate percentage measures against — a full
    // step normally, the roller or looper division while one is engaged.
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

        if (activeNote >= 0)
        {
            midiPort.sendNoteOff (seqChannel, activeNote);
            activeNote = -1;
        }

        const int note = noteForIndex (resolveIndex (s.index));

        midiPort.sendNoteOn (seqChannel, note, s.velocity);
        activeNote = note;
        gateTicksRemaining = juce::jmax (1, (gateBasisTicks * (int) s.gate) / 100);
    }

    VirtualMidiPort& midiPort;
    int seqChannel;

    int       patternLength = MAX_STEPS;
    Rate      rate      = Rate::Sixteenth;
    Direction direction = Direction::Forward;
    float     swing     = 0.0f;

    bool enabled = true;   // TODO: default false once the pack entitlement lands

    int ratchetDivision = 2;   // 1/16
    bool ratchetActive  = false;

    int activeNote         = -1;
    int gateTicksRemaining = 0;
    int lastSentMod        = -1;

    std::atomic<int> currentStep { -1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ClockedSequencer)
};
