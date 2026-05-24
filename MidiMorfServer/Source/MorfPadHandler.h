#pragma once

#include <JuceHeader.h>
#include "VirtualMidiPort.h"
#include <vector>

class MorfPadHandler
{
public:
    MorfPadHandler (VirtualMidiPort& port, int channel = 3) : midiPort (port), morfPadChannel (channel) {}

    void setChannel (int ch) { morfPadChannel = juce::jlimit (2, 16, ch); }
    int getChannel() const { return morfPadChannel; }

    void setScale (uint8_t scaleIndex, uint8_t rootNote, uint8_t octRange, uint8_t baseOct = 2)
    {
        currentScaleIndex = juce::jlimit ((uint8_t) 0, (uint8_t) 4, scaleIndex);
        currentRoot = juce::jlimit ((uint8_t) 0, (uint8_t) 11, rootNote);
        octaveRange = juce::jlimit ((uint8_t) 1, (uint8_t) 4, octRange);
        baseOctave = juce::jlimit ((uint8_t) 0, (uint8_t) 8, baseOct);
        buildNoteTable();
    }

    void touchDown (float x, float y)
    {
        if (noteTable.empty())
            buildNoteTable();

        int note = xToNote (x);
        int modVal = juce::jlimit (0, 127, (int) (y * 127.0f));

        currentNote = note;
        touching = true;

        midiPort.sendNoteOn (morfPadChannel, note, 100);
        midiPort.sendCC (morfPadChannel, 1, modVal);  // CC 1 = mod wheel
    }

    void touchMove (float x, float y)
    {
        if (! touching)
            return;

        int note = xToNote (x);
        int modVal = juce::jlimit (0, 127, (int) (y * 127.0f));

        // Legato: if note changed, send note-off for old, note-on for new
        if (note != currentNote)
        {
            midiPort.sendNoteOff (morfPadChannel, currentNote);
            midiPort.sendNoteOn (morfPadChannel, note, 100);
            currentNote = note;
        }

        midiPort.sendCC (morfPadChannel, 1, modVal);
    }

    void touchUp()
    {
        if (touching)
        {
            midiPort.sendNoteOff (morfPadChannel, currentNote);
            midiPort.sendCC (morfPadChannel, 1, 0);
            touching = false;
            currentNote = -1;
        }
    }

    void allNotesOff()
    {
        if (touching)
        {
            midiPort.sendNoteOff (morfPadChannel, currentNote);
            midiPort.sendCC (morfPadChannel, 1, 0);
            touching = false;
            currentNote = -1;
        }
    }

private:
    void buildNoteTable()
    {
        // Scale intervals (semitones from root)
        static const std::vector<std::vector<int>> scales = {
            { 0, 2, 4, 5, 7, 9, 11 },                     // Major
            { 0, 2, 3, 5, 7, 8, 10 },                     // Minor
            { 0, 2, 4, 7, 9 },                             // Pentatonic
            { 0, 3, 5, 6, 7, 10 },                         // Blues
            { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 },  // Chromatic
        };

        noteTable.clear();
        const auto& intervals = scales[currentScaleIndex];

        // Build notes across octave range, starting from base octave + root offset
        int baseNote = (baseOctave + 1) * 12 + currentRoot;

        for (int oct = 0; oct < octaveRange; ++oct)
        {
            for (int interval : intervals)
            {
                int note = baseNote + oct * 12 + interval;
                if (note <= 127)
                    noteTable.push_back (note);
            }
        }
    }

    int xToNote (float x) const
    {
        if (noteTable.empty())
            return 60;  // Middle C fallback

        int index = juce::jlimit (0, (int) noteTable.size() - 1,
                                  (int) (x * noteTable.size()));
        return noteTable[(size_t) index];
    }

    VirtualMidiPort& midiPort;
    int morfPadChannel = 3;
    std::vector<int> noteTable;

    uint8_t currentScaleIndex = 0;
    uint8_t currentRoot = 0;
    uint8_t octaveRange = 2;
    uint8_t baseOctave = 2;  // C2 default (MIDI note = (baseOctave+1)*12)

    bool touching = false;
    int currentNote = -1;
};
