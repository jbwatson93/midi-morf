#pragma once

#include <JuceHeader.h>
#include "VirtualMidiPort.h"
#include <vector>
#include <set>
#include <algorithm>

// Rose-Chord: Orchid-inspired chord engine.
//   • typeBits / modBits are bitmasks of currently-held chord type / modifier
//     buttons (multiple can be held at once).
//   • If both are 0 when noteOn fires, only the single root note plays (no
//     chord). This matches the Orchid's "hold chord button + tap key = chord,
//     no button + tap key = single note" model.
//
// MIRRORS MidiMorfApp/Source/RoseChordHandler.h — keep in sync. Only the
// MIDI-sink type differs (this side uses VirtualMidiPort, phone uses PhoneMidiSink).
class RoseChordHandler
{
public:
    enum TypeBit : uint8_t { TypeDim = 1 << 0, TypeMin = 1 << 1, TypeMaj = 1 << 2, TypeSus = 1 << 3 };
    enum ModBit  : uint8_t { ModSixth = 1 << 0, ModMin7 = 1 << 1, ModMaj7 = 1 << 2, ModNinth = 1 << 3 };

    RoseChordHandler (VirtualMidiPort& port, int channel = 7) : midiPort (port), chordChannel (channel) {}

    void setChannel (int ch) { chordChannel = juce::jlimit (2, 16, ch); }
    int getChannel() const { return chordChannel; }

    void setVoicing (int8_t offset)
    {
        int clamped = juce::jlimit (-7, 7, (int) offset);
        if (clamped == voicingOffset) return;
        voicingOffset = (int8_t) clamped;
        retriggerIfHeld();
    }

    void setConfig (uint8_t typeBits, uint8_t modBits, int8_t voicing, int8_t octave)
    {
        int8_t clampedOctave = (int8_t) juce::jlimit (-2, 2, (int) octave);
        bool changed = (typeBits != currentTypes) || (modBits != currentMods)
                    || (voicing != voicingOffset) || (clampedOctave != octaveOffset);
        currentTypes  = typeBits;
        currentMods   = modBits;
        voicingOffset = (int8_t) juce::jlimit (-7, 7, (int) voicing);
        octaveOffset  = clampedOctave;
        if (changed) retriggerIfHeld();
    }

    void noteOn (uint8_t rootIdx)
    {
        silenceActive();
        currentRoot = juce::jlimit ((uint8_t) 0, (uint8_t) 11, rootIdx);
        chordHeld = true;
        playChord();
    }

    void noteOff()
    {
        silenceActive();
        chordHeld = false;
    }

    void allNotesOff()
    {
        silenceActive();
        chordHeld = false;
    }

private:
    void retriggerIfHeld()
    {
        if (! chordHeld) return;
        silenceActive();
        playChord();
    }

    void playChord()
    {
        auto notes = buildChordNotes();
        for (int n : notes)
        {
            midiPort.sendNoteOn (chordChannel, n, 90);
            activeNotes.insert (n);
        }
    }

    void silenceActive()
    {
        for (int n : activeNotes)
            midiPort.sendNoteOff (chordChannel, n);
        activeNotes.clear();
    }

    std::vector<int> buildChordNotes() const
    {
        const int baseNote = 48 + currentRoot + (int) octaveOffset * 12;
        std::vector<int> notes;

        if (currentTypes == 0 && currentMods == 0)
        {
            notes.push_back (baseNote);
            applyVoicing (notes);
            return notes;
        }

        static const int typeIntervals[4][3] = {
            { 0, 3, 6 },   // Dim
            { 0, 3, 7 },   // Min
            { 0, 4, 7 },   // Maj
            { 0, 5, 7 },   // Sus4
        };

        std::set<int> intervalSet;
        intervalSet.insert (0);

        for (int t = 0; t < 4; ++t)
        {
            if (currentTypes & (1 << t))
                for (int i = 0; i < 3; ++i)
                    intervalSet.insert (typeIntervals[t][i]);
        }

        if (currentMods & ModSixth) intervalSet.insert (9);
        if (currentMods & ModMin7)  intervalSet.insert (10);
        if (currentMods & ModMaj7)  intervalSet.insert (11);
        if (currentMods & ModNinth) intervalSet.insert (14);

        notes.reserve (intervalSet.size());
        for (int iv : intervalSet)
            notes.push_back (baseNote + iv);

        applyVoicing (notes);
        return notes;
    }

    void applyVoicing (std::vector<int>& notes) const
    {
        std::sort (notes.begin(), notes.end());
        const int count = (int) notes.size();
        if (count > 0)
        {
            int shifts = std::abs ((int) voicingOffset);
            if (voicingOffset > 0)
                for (int i = 0; i < shifts; ++i)
                    notes[i % count] += 12;
            else if (voicingOffset < 0)
                for (int i = 0; i < shifts; ++i)
                    notes[count - 1 - (i % count)] -= 12;
        }

        notes.erase (std::remove_if (notes.begin(), notes.end(),
                                     [] (int n) { return n < 0 || n > 127; }),
                     notes.end());
        std::sort (notes.begin(), notes.end());
    }

    VirtualMidiPort& midiPort;
    int chordChannel = 7;

    uint8_t currentRoot = 0;
    uint8_t currentTypes = 0;  // bitmask of TypeBit
    uint8_t currentMods = 0;   // bitmask of ModBit
    int8_t  voicingOffset = 0;
    int8_t  octaveOffset = 0;  // -2..+2

    bool chordHeld = false;
    std::set<int> activeNotes;
};
