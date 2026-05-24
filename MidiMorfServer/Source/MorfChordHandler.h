#pragma once

#include <JuceHeader.h>
#include "VirtualMidiPort.h"
#include <vector>
#include <set>

class MorfChordHandler
{
public:
    MorfChordHandler (VirtualMidiPort& port) : midiPort (port) {}

    void setKey (uint8_t root, uint8_t mode)
    {
        rootNote = juce::jlimit ((uint8_t) 0, (uint8_t) 11, root);
        scaleMode = juce::jlimit ((uint8_t) 0, (uint8_t) 1, mode);
    }

    void selectDegree (uint8_t degreeIndex, float x, float y)
    {
        if (degreeIndex > 6)
            return;

        silenceAll();

        currentDegree = degreeIndex;
        currentX = x;
        currentY = y;
        chordHeld = true;

        buildChordNotes();
        buildStrumNotes();

        for (int note : chordNotes)
        {
            midiPort.sendNoteOn (chordChannel, note, 80);
            activeChordNotes.insert (note);
        }
    }

    void moveDegree (uint8_t degreeIndex, float x, float y)
    {
        if (! chordHeld || degreeIndex != currentDegree)
            return;

        VoicingZone oldZone = getZone();

        currentX = x;
        currentY = y;

        VoicingZone newZone = getZone();

        // Only retrigger if the voicing zone actually changed
        if (newZone == oldZone)
            return;

        silenceChordNotes();
        buildChordNotes();

        for (int note : chordNotes)
        {
            midiPort.sendNoteOn (chordChannel, note, 80);
            activeChordNotes.insert (note);
        }

        buildStrumNotes();
    }

    void releaseDegree()
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
            triggerStrumNote (noteIndex, vel);
            lastStrumIndex = noteIndex;
            return;
        }

        if (noteIndex == lastStrumIndex)
            return;

        int step = (noteIndex > lastStrumIndex) ? 1 : -1;
        for (int i = lastStrumIndex + step; i != noteIndex + step; i += step)
            triggerStrumNote (i, vel);

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
    // XY zone detection - 3x3 grid
    // Returns: 0=center, 1=up, 2=down, 3=left, 4=right,
    //          5=top-left, 6=top-right, 7=bottom-left, 8=bottom-right
    enum VoicingZone
    {
        Center    = 0,  // base diatonic triad
        Up        = 1,  // major/minor toggle
        Down      = 2,  // sus4
        Left      = 3,  // dim
        Right     = 4,  // add 7th (Maj7 or min7)
        TopLeft   = 5,  // aug
        TopRight  = 6,  // dom7
        BotLeft   = 7,  // 6th / sus2
        BotRight  = 8,  // 9th
    };

    VoicingZone getZone() const
    {
        bool left  = currentX < 0.33f;
        bool right = currentX > 0.67f;
        bool up    = currentY > 0.67f;
        bool down  = currentY < 0.33f;

        if (up && left)   return TopLeft;
        if (up && right)  return TopRight;
        if (down && left) return BotLeft;
        if (down && right) return BotRight;
        if (up)    return Up;
        if (down)  return Down;
        if (left)  return Left;
        if (right) return Right;
        return Center;
    }

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

    void silenceStrumNotes()
    {
        for (int note : activeStrumNotes)
            midiPort.sendNoteOff (strumChannel, note);
        activeStrumNotes.clear();
    }

    // Get the intervals for the current chord voicing based on XY zone
    void getVoicingIntervals (int* intervals, int& numIntervals) const
    {
        static const int majorScale[7] = { 0, 2, 4, 5, 7, 9, 11 };
        static const int minorScale[7] = { 0, 2, 3, 5, 7, 8, 10 };

        const int* scale = (scaleMode == 0) ? majorScale : minorScale;

        int degreeRoot = scale[currentDegree];

        // Base triad intervals (from scale)
        int thirdDeg = (currentDegree + 2) % 7;
        int fifthDeg = (currentDegree + 4) % 7;
        int seventhDeg = (currentDegree + 6) % 7;

        int third = scale[thirdDeg] - degreeRoot;
        if (third < 0) third += 12;

        int fifth = scale[fifthDeg] - degreeRoot;
        if (fifth < 0) fifth += 12;

        int seventh = scale[seventhDeg] - degreeRoot;
        if (seventh < 0) seventh += 12;

        VoicingZone zone = getZone();

        switch (zone)
        {
            case Center:  // base diatonic triad
                intervals[0] = 0; intervals[1] = third; intervals[2] = fifth;
                numIntervals = 3;
                break;

            case Up:  // major/minor toggle (flip the third)
                intervals[0] = 0;
                intervals[1] = (third == 3) ? 4 : (third == 4) ? 3 : third;
                intervals[2] = fifth;
                numIntervals = 3;
                break;

            case Down:  // sus4
                intervals[0] = 0; intervals[1] = 5; intervals[2] = fifth;
                numIntervals = 3;
                break;

            case Left:  // dim (minor third + diminished fifth)
                intervals[0] = 0; intervals[1] = 3; intervals[2] = 6;
                numIntervals = 3;
                break;

            case Right:  // add 7th (diatonic seventh)
                intervals[0] = 0; intervals[1] = third; intervals[2] = fifth; intervals[3] = seventh;
                numIntervals = 4;
                break;

            case TopLeft:  // aug (major third + augmented fifth)
                intervals[0] = 0; intervals[1] = 4; intervals[2] = 8;
                numIntervals = 3;
                break;

            case TopRight:  // dom7 (major third + perfect fifth + minor seventh)
                intervals[0] = 0; intervals[1] = 4; intervals[2] = 7; intervals[3] = 10;
                numIntervals = 4;
                break;

            case BotLeft:  // sus2
                intervals[0] = 0; intervals[1] = 2; intervals[2] = fifth;
                numIntervals = 3;
                break;

            case BotRight:  // add 9th
                intervals[0] = 0; intervals[1] = third; intervals[2] = fifth; intervals[3] = 14;
                numIntervals = 4;
                break;
        }
    }

    void buildChordNotes()
    {
        chordNotes.clear();

        static const int majorScale[7] = { 0, 2, 4, 5, 7, 9, 11 };
        static const int minorScale[7] = { 0, 2, 3, 5, 7, 8, 10 };
        const int* scale = (scaleMode == 0) ? majorScale : minorScale;

        int degreeRoot = scale[currentDegree];
        int baseNote = 48 + rootNote + degreeRoot;  // C3 + key root + degree

        int intervals[5];
        int numIntervals = 0;
        getVoicingIntervals (intervals, numIntervals);

        // Build chord across 2 octaves
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
        strumNotes.clear();

        static const int majorScale[7] = { 0, 2, 4, 5, 7, 9, 11 };
        static const int minorScale[7] = { 0, 2, 3, 5, 7, 8, 10 };
        const int* scale = (scaleMode == 0) ? majorScale : minorScale;

        int degreeRoot = scale[currentDegree];
        int baseNote = 60 + rootNote + degreeRoot;  // C4 + key root + degree

        int intervals[5];
        int numIntervals = 0;
        getVoicingIntervals (intervals, numIntervals);

        // Span 3 octaves above chord base
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

    VirtualMidiPort& midiPort;
    int chordChannel = 5;
    int strumChannel = 6;

    uint8_t rootNote = 0;
    uint8_t scaleMode = 0;
    uint8_t currentDegree = 0;
    float currentX = 0.5f;
    float currentY = 0.5f;

    std::vector<int> chordNotes;
    std::vector<int> strumNotes;
    std::set<int> activeChordNotes;
    std::set<int> activeStrumNotes;

    bool chordHeld = false;
    int lastStrumIndex = -1;
};
