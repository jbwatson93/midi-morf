#pragma once

// What the shared sequencer engines (ClockedSequencer, StepSequencerEngine,
// SliceSequencerEngine) need from their host. Those three files are identical
// in MidiMorfServer and MidiMorfApp; only this header differs between them.
//
// Server: notes go out of the virtual MIDI port, ticks come from DAW clock.
#include "VirtualMidiPort.h"
#include "MidiClockReceiver.h"

using SeqSink          = VirtualMidiPort;
using SeqClockListener = MidiClockReceiver::Listener;
static constexpr int SeqPPQN = MidiClockReceiver::PPQN;
