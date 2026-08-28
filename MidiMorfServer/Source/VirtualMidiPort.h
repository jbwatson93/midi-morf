#pragma once

#include <JuceHeader.h>

class VirtualMidiPort
{
public:
    VirtualMidiPort();
    ~VirtualMidiPort();

    bool open();
    void close();
    bool isOpen() const;

    void sendCC (int channel, int ccNumber, int value);
    void sendNoteOn (int channel, int note, int velocity);
    void sendNoteOff (int channel, int note);

    // 0..16383, centre 8192. Native in Simpler/Sampler — no MIDI mapping needed,
    // which is why the slicer's first ribbon defaults to it.
    void sendPitchBend (int channel, int value);

private:
    std::unique_ptr<juce::MidiOutput> midiOutput;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VirtualMidiPort)
};
