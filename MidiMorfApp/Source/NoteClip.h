#pragma once

#include <JuceHeader.h>
#include <array>
#include <algorithm>

//==============================================================================
// A clip of notes: what recording makes, and what the piano roll and drum
// editor edit. Times are in beats from the clip's start; the clip loops every
// `lengthBeats`.
//
// Fixed capacity and plain data, so the audio thread can hold a copy (taken
// under LocalSequencers' lock) without ever allocating.
struct NoteEvent
{
    double start = 0.0;    // beats
    double length = 0.25;  // beats
    uint8_t note = 60, velocity = 100;
};

struct NoteClip
{
    static constexpr int MaxNotes = 512;

    double lengthBeats = 4.0;
    int numNotes = 0;
    std::array<NoteEvent, MaxNotes> notes {};

    bool add (const NoteEvent& e)
    {
        if (numNotes >= MaxNotes) return false;
        notes[(size_t) numNotes++] = e;
        return true;
    }

    void remove (int index)
    {
        if (! juce::isPositiveAndBelow (index, numNotes)) return;
        std::move (notes.begin() + index + 1, notes.begin() + numNotes, notes.begin() + index);
        --numNotes;
    }

    void clear() { numNotes = 0; }

    // By start, so playback can walk them in order.
    void sort()
    {
        std::stable_sort (notes.begin(), notes.begin() + numNotes,
                          [] (const NoteEvent& a, const NoteEvent& b) { return a.start < b.start; });
    }

    // Snap starts (and lengths, at least one grid step) to a grid in beats,
    // e.g. 0.25 for 16ths. Notes that wrap past the end come back to the start.
    void quantize (double grid)
    {
        if (grid <= 0.0) return;
        for (int i = 0; i < numNotes; ++i)
        {
            auto& n = notes[(size_t) i];
            n.start = std::fmod (std::round (n.start / grid) * grid, lengthBeats);
            n.length = juce::jmax (grid, std::round (n.length / grid) * grid);
        }
        sort();
    }

    bool operator== (const NoteClip& o) const
    {
        if (lengthBeats != o.lengthBeats || numNotes != o.numNotes) return false;
        for (int i = 0; i < numNotes; ++i)
        {
            const auto& a = notes[(size_t) i];
            const auto& b = o.notes[(size_t) i];
            if (a.start != b.start || a.length != b.length || a.note != b.note || a.velocity != b.velocity)
                return false;
        }
        return true;
    }

    // "length;start,len,note,vel;..." — readable, and short for a few hundred notes.
    juce::String toString() const
    {
        juce::String s (lengthBeats, 4);
        for (int i = 0; i < numNotes; ++i)
        {
            const auto& n = notes[(size_t) i];
            s << ";" << juce::String (n.start, 5) << "," << juce::String (n.length, 5) << "," << (int) n.note << "," << (int) n.velocity;
        }
        return s;
    }

    static NoteClip fromString (const juce::String& text)
    {
        NoteClip c;
        auto parts = juce::StringArray::fromTokens (text, ";", "");
        if (parts.isEmpty()) return c;
        c.lengthBeats = juce::jlimit (1.0, 64.0, parts[0].getDoubleValue());
        for (int i = 1; i < parts.size(); ++i)
        {
            auto f = juce::StringArray::fromTokens (parts[i], ",", "");
            if (f.size() != 4) continue;
            c.add ({ juce::jlimit (0.0, c.lengthBeats, f[0].getDoubleValue()), juce::jmax (0.01, f[1].getDoubleValue()),
                     (uint8_t) juce::jlimit (0, 127, f[2].getIntValue()), (uint8_t) juce::jlimit (1, 127, f[3].getIntValue()) });
        }
        c.sort();
        return c;
    }
};
