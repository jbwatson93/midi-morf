#pragma once

#include <JuceHeader.h>
#include "MidiMorfProtocol.h"

struct SlotConfig
{
    juce::String name;
    int ccButton;
    int ccXY_X;
    int ccXY_Y;
    int midiChannel = 1;
    bool noteMode = false;  // true = send Note On/Off, false = send CC
    int midiNote = 36;      // MIDI note number (used when noteMode == true)
};

struct GlobalTiltConfig
{
    int ccTiltX = 72;
    int ccTiltY = 73;
    int midiChannel = 1;
    bool alwaysOn = false;
};

inline std::array<SlotConfig, MidiMorf::NUM_SLOTS> createDefaultSlotConfigs()
{
    return {{
        { "FX 1", 36, 70, 71, 1, false, 36 },
        { "FX 2", 37, 74, 75, 1, false, 37 },
        { "FX 3", 38, 78, 79, 1, false, 38 },
        { "FX 4", 39, 82, 83, 1, false, 39 },
    }};
}
