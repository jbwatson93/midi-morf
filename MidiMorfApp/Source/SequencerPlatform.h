#pragma once

// What the shared sequencer engines (ClockedSequencer, StepSequencerEngine,
// SliceSequencerEngine) need from their host. Those three files are identical
// in MidiMorfServer and MidiMorfApp; only this header differs between them.
//
// Phone: notes go to an in-process sink (the built-in synth or the break
// player), and ticks come from LocalSequencers' own clock on the audio thread.

// Same three calls as the server's VirtualMidiPort.
class SeqSink
{
public:
    virtual ~SeqSink() = default;

    virtual void sendNoteOn  (int channel, int note, int velocity) = 0;
    virtual void sendNoteOff (int channel, int note) = 0;
    virtual void sendCC      (int channel, int ccNumber, int value) = 0;
};

// Same callbacks as the server's MidiClockReceiver::Listener.
struct SeqClockListener
{
    virtual ~SeqClockListener() = default;

    virtual void clockStart() {}
    virtual void clockContinue() {}
    virtual void clockStop() {}
    virtual void clockTick() {}
    virtual void clockSongPosition (int /*sixteenths*/) {}
};

static constexpr int SeqPPQN = 24;   // MIDI clock resolution, ticks per quarter
