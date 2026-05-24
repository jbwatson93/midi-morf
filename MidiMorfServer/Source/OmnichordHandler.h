#pragma once

#include <JuceHeader.h>
#include "VirtualMidiPort.h"
#include <vector>
#include <set>

class OmnichordHandler
{
public:
    OmnichordHandler (VirtualMidiPort& port) : midiPort (port) {}

    void selectChord (uint8_t rootNote, uint8_t quality)
    {
        // Silence everything from previous chord
        silenceAll();

        currentRoot = juce::jlimit ((uint8_t) 0, (uint8_t) 11, rootNote);
        currentQuality = juce::jlimit ((uint8_t) 0, (uint8_t) 3, quality);
        chordHeld = true;

        buildChordNotes();
        buildStrumNotes();

        // Play the sustained chord (lower voicing)
        for (int note : chordNotes)
        {
            midiPort.sendNoteOn (chordChannel, note, 80);
            activeChordNotes.insert (note);
        }
    }

    void releaseChord()
    {
        silenceAll();
        chordHeld = false;
        chordNotes.clear();
        strumNotes.clear();
        lastStrumIndex = -1;
    }

    void strum (float position, float velocity)
    {
        if (! chordHeld || strumNotes.empty())
            return;

        // Velocity 0 = finger lifted, silence strum notes
        if (velocity <= 0.0f)
        {
            silenceStrumNotes();
            lastStrumIndex = -1;
            return;
        }

        int noteIndex = juce::jlimit (0, (int) strumNotes.size() - 1,
                                      (int) (position * strumNotes.size()));
        int vel = juce::jlimit (30, 127, (int) (velocity * 127.0f));

        if (lastStrumIndex < 0)
        {
            // First touch — trigger this note
            triggerStrumNote (noteIndex, vel);
            lastStrumIndex = noteIndex;
            return;
        }

        if (noteIndex == lastStrumIndex)
            return;

        // Cascade: trigger every note between last position and current position
        int step = (noteIndex > lastStrumIndex) ? 1 : -1;
        for (int i = lastStrumIndex + step; i != noteIndex + step; i += step)
        {
            triggerStrumNote (i, vel);
        }

        lastStrumIndex = noteIndex;
    }

    void allNotesOff()
    {
        silenceAll();
        chordHeld = false;
        chordNotes.clear();
        strumNotes.clear();
        lastStrumIndex = -1;
    }

    void setChordChannel (int ch) { chordChannel = juce::jlimit (2, 16, ch); }
    int getChordChannel() const { return chordChannel; }
    void setStrumChannel (int ch) { strumChannel = juce::jlimit (2, 16, ch); }
    int getStrumChannel() const { return strumChannel; }

private:
    void triggerStrumNote (int index, int vel)
    {
        int note = strumNotes[(size_t) index];
        midiPort.sendNoteOn (strumChannel, note, vel);
        activeStrumNotes.insert (note);
    }

    void silenceAll()
    {
        silenceStrumNotes();
        silenceChordNotes();
    }

    void silenceChordNotes()
    {
        for (int note : activeChordNotes)
            midiPort.sendNoteOff (chordChannel, note);

        activeChordNotes.clear();
    }

    void buildChordNotes()
    {
        chordNotes.clear();

        // Chord voicing tables (semitone intervals from root)
        static const int majorIntervals[] = { 0, 4, 7 };        // Major triad
        static const int minorIntervals[] = { 0, 3, 7 };        // Minor triad
        static const int dom7Intervals[]  = { 0, 4, 7, 10 };    // Dominant 7th
        static const int dimIntervals[]   = { 0, 3, 6 };        // Diminished triad

        const int* intervals;
        int numIntervals;

        switch (currentQuality)
        {
            case 0:  intervals = majorIntervals; numIntervals = 3; break;
            case 1:  intervals = minorIntervals; numIntervals = 3; break;
            case 2:  intervals = dom7Intervals;  numIntervals = 4; break;
            case 3:  intervals = dimIntervals;   numIntervals = 3; break;
            default: intervals = majorIntervals; numIntervals = 3; break;
        }

        // Build chord across 2 octaves starting from C3 (MIDI 48) + root
        int baseNote = 48 + currentRoot;

        for (int oct = 0; oct < 2; ++oct)
        {
            for (int i = 0; i < numIntervals; ++i)
            {
                int note = baseNote + oct * 12 + intervals[i];
                if (note <= 127)
                    chordNotes.push_back (note);
            }
        }
    }

    void buildStrumNotes()
    {
        // Strum notes span 3 octaves above the chord base, using chord tones
        strumNotes.clear();

        static const int majorIntervals[] = { 0, 4, 7 };
        static const int minorIntervals[] = { 0, 3, 7 };
        static const int dom7Intervals[]  = { 0, 4, 7, 10 };
        static const int dimIntervals[]   = { 0, 3, 6 };

        const int* intervals;
        int numIntervals;

        switch (currentQuality)
        {
            case 0:  intervals = majorIntervals; numIntervals = 3; break;
            case 1:  intervals = minorIntervals; numIntervals = 3; break;
            case 2:  intervals = dom7Intervals;  numIntervals = 4; break;
            case 3:  intervals = dimIntervals;   numIntervals = 3; break;
            default: intervals = majorIntervals; numIntervals = 3; break;
        }

        // Start one octave above chord base, span 3 octaves
        int baseNote = 60 + currentRoot;  // C4 + root

        for (int oct = 0; oct < 3; ++oct)
        {
            for (int i = 0; i < numIntervals; ++i)
            {
                int note = baseNote + oct * 12 + intervals[i];
                if (note <= 127)
                    strumNotes.push_back (note);
            }
        }
    }

    void silenceStrumNotes()
    {
        for (int note : activeStrumNotes)
            midiPort.sendNoteOff (strumChannel, note);

        activeStrumNotes.clear();
    }

    int chordChannel = 2;
    int strumChannel = 2;

    VirtualMidiPort& midiPort;
    std::vector<int> chordNotes;
    std::vector<int> strumNotes;
    std::set<int> activeStrumNotes;
    std::set<int> activeChordNotes;

    uint8_t currentRoot = 0;
    uint8_t currentQuality = 0;
    bool chordHeld = false;
    int lastStrumIndex = -1;
};
