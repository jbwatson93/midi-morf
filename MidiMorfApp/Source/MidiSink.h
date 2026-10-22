#pragma once

#include "ActiveSynth.h"

// PhoneMidiSink — adapter around the active built-in synth that matches the server's
// VirtualMidiPort API. Per-controller handlers (MorfPadHandler etc.) target
// this so their internal note-mapping logic stays identical to the server.
// MIDI channel is recorded but doesn't affect the synth (single global patch).
// CC messages are ignored locally for v2 (no synth-side CC routing yet).
class PhoneMidiSink
{
public:
    PhoneMidiSink (ActiveSynth& s) : synth (s) {}

    void sendNoteOn (int channel, int note, int velocity)
    {
        synth.noteOn (note, juce::jlimit (0.0f, 1.0f, (float) velocity / 127.0f), channel);
    }

    void sendNoteOff (int channel, int note)
    {
        synth.noteOff (note, channel);
    }

    void sendCC (int /*channel*/, int cc, int value)
    {
        // CC1 (mod wheel) drives the synth's modulation routings. Other CCs
        // currently no-op locally; they only affect WiFi-mode output.
        if (cc == 1)
            synth.setModWheel ((float) juce::jlimit (0, 127, value) / 127.0f);
    }

private:
    ActiveSynth& synth;
};
