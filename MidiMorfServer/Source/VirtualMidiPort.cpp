#include "VirtualMidiPort.h"

VirtualMidiPort::VirtualMidiPort() {}

VirtualMidiPort::~VirtualMidiPort()
{
    close();
}

bool VirtualMidiPort::open()
{
    if (midiOutput != nullptr)
        return true;

    midiOutput = juce::MidiOutput::createNewDevice ("Midi-Morf");

    if (midiOutput != nullptr)
    {
        DBG ("VirtualMidiPort: opened virtual MIDI device 'Midi-Morf'");
        return true;
    }

    DBG ("VirtualMidiPort: failed to create virtual MIDI device");
    return false;
}

void VirtualMidiPort::close()
{
    if (midiOutput != nullptr)
    {
        // Send all CCs off on all channels before closing
        for (int ch = 1; ch <= 16; ++ch)
            midiOutput->sendMessageNow (juce::MidiMessage::allControllersOff (ch));

        midiOutput.reset();
        DBG ("VirtualMidiPort: closed");
    }
}

bool VirtualMidiPort::isOpen() const
{
    return midiOutput != nullptr;
}

void VirtualMidiPort::sendCC (int channel, int ccNumber, int value)
{
    if (midiOutput != nullptr)
        midiOutput->sendMessageNow (juce::MidiMessage::controllerEvent (channel, ccNumber, value));
}

void VirtualMidiPort::sendNoteOn (int channel, int note, int velocity)
{
    if (midiOutput != nullptr)
        midiOutput->sendMessageNow (juce::MidiMessage::noteOn (channel, note, (juce::uint8) velocity));
}

void VirtualMidiPort::sendNoteOff (int channel, int note)
{
    if (midiOutput != nullptr)
        midiOutput->sendMessageNow (juce::MidiMessage::noteOff (channel, note));
}
