#pragma once

#include <JuceHeader.h>
#include "VirtualMidiPort.h"

class TransportHandler
{
public:
    TransportHandler (VirtualMidiPort& port) : midiPort (port) {}

    void buttonPressed (uint8_t buttonId)
    {
        if (buttonId >= NUM_BUTTONS)
            return;

        int note = mcuNotes[buttonId];

        // Send note-on then note-off (momentary press)
        midiPort.sendNoteOn (MCU_CHANNEL, note, 127);
        midiPort.sendNoteOff (MCU_CHANNEL, note);
    }

    void allNotesOff()
    {
        for (int i = 0; i < NUM_BUTTONS; ++i)
            midiPort.sendNoteOff (MCU_CHANNEL, mcuNotes[i]);
    }

private:
    // MCU protocol: all transport on channel 1
    static constexpr int MCU_CHANNEL = 1;
    static constexpr int NUM_BUTTONS = 11;

    // Button ID to MCU MIDI note mapping
    // 0=Play, 1=Stop, 2=Record, 3=Rewind, 4=FF,
    // 5=Loop/Cycle, 6=(unused), 7=Tap/Click, 8=Auto Write, 9=Auto Read, 10=Metronome
    static constexpr int mcuNotes[NUM_BUTTONS] = {
        94,  // 0: Play      (0x5E)
        93,  // 1: Stop      (0x5D)
        95,  // 2: Record    (0x5F)
        91,  // 3: Rewind    (0x5B)
        92,  // 4: FF        (0x5C)
        86,  // 5: Cycle     (0x56)
        86,  // 6: (unused, alias to cycle)
        89,  // 7: Click     (0x59) — tap tempo
        75,  // 8: Auto Write(0x4B) — automation arm
        74,  // 9: Auto Read (0x4A) — re-enable automation
        89,  // 10: Click    (0x59) — metronome
    };

    VirtualMidiPort& midiPort;
};
