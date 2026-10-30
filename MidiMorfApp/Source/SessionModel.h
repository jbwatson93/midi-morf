#pragma once

#include <JuceHeader.h>
#include "ClockedSequencer.h"
#include "TrackRack.h"
#include "NoteClip.h"
#include <array>
#include <optional>

//==============================================================================
// A sequencer's pattern: what a clip holds on a Step-Seq, Slice-Seq or Drum
// Grid track. The page's settings (key, scale, rate, the slice grid, swing)
// stay with the track; a clip is the steps.
//
// Step-Seq uses length + every field of the steps. Slice-Seq uses the steps'
// index (the slice) and rest. Drum Grid uses drums.
struct SeqPattern
{
    static constexpr int MaxSteps = ClockedSequencer::MAX_STEPS, DrumLanes = 8, DrumSteps = 16;

    int length = 16;
    int start = 0;   // Step-Seq: the first step played (the range bracket); the engine gets toEngine()
    std::array<ClockedSequencer::Step, MaxSteps> steps {};
    std::array<std::array<uint8_t, DrumSteps>, DrumLanes> drums {};

    bool operator== (const SeqPattern& o) const
    {
        if (length != o.length || start != o.start || drums != o.drums) return false;
        for (int i = 0; i < MaxSteps; ++i)
        {
            const auto& a = steps[(size_t) i];
            const auto& b = o.steps[(size_t) i];
            if (a.index != b.index || a.velocity != b.velocity || a.gate != b.gate || a.mod != b.mod || a.rest != b.rest)
                return false;
        }
        return true;
    }

    // Compact text for the project file: hex bytes.
    juce::String toString() const
    {
        juce::MemoryBlock mb;
        mb.append (&length, sizeof (int));
        for (auto& s : steps)
        {
            const uint8_t b[5] = { s.index, s.velocity, s.gate, s.mod, (uint8_t) (s.rest ? 1 : 0) };
            mb.append (b, 5);
        }
        for (auto& lane : drums)
            mb.append (lane.data(), lane.size());
        mb.append (&start, sizeof (int));
        return mb.toBase64Encoding();
    }

    static std::optional<SeqPattern> fromString (const juce::String& text)
    {
        juce::MemoryBlock mb;
        SeqPattern p;
        // Patterns saved before the range bracket have no start.
        const size_t withoutStart = sizeof (int) + (size_t) MaxSteps * 5 + (size_t) DrumLanes * DrumSteps;
        if (! mb.fromBase64Encoding (text) || (mb.getSize() != withoutStart && mb.getSize() != withoutStart + sizeof (int)))
            return std::nullopt;

        auto* d = static_cast<const uint8_t*> (mb.getData());
        std::memcpy (&p.length, d, sizeof (int));
        p.length = juce::jlimit (1, MaxSteps, p.length);
        d += sizeof (int);
        for (auto& s : p.steps)
        {
            s = { d[0], d[1], d[2], d[3], d[4] != 0 };
            d += 5;
        }
        for (auto& lane : p.drums)
        {
            std::memcpy (lane.data(), d, lane.size());
            d += lane.size();
        }
        if (mb.getSize() > withoutStart)
            std::memcpy (&p.start, d, sizeof (int));
        p.start = juce::jlimit (0, MaxSteps - 1, p.start);
        return p;
    }

    // As the engine plays it: the steps in the range, from 0.
    SeqPattern toEngine() const
    {
        if (start == 0)
            return *this;
        SeqPattern e = *this;
        e.start = 0;
        for (int i = 0; i < MaxSteps; ++i)
            e.steps[(size_t) i] = steps[(size_t) juce::jmin (MaxSteps - 1, start + i)];
        return e;
    }
};

//==============================================================================
// The Session's clips: 4 tracks by Scenes slots. Message thread only.
//
// A track plays at most one clip. `playing` is the clip the track's page is
// showing and editing (edits write back into it); `queued` is the one that
// takes over on the next bar.
class SessionModel
{
public:
    static constexpr int NumTracks = TrackRack::NumTracks, NumScenes = 8;

    // A clip is a sequencer's pattern, or (on a controller track) notes.
    static constexpr int NotesClip = -1;

    struct Clip
    {
        juce::String name;
        SeqPattern pattern;
        int sequencer = 0;   // LocalSequencers::Which it was made on, or NotesClip; it only plays there
        NoteClip notes;

        bool isNotes() const { return sequencer == NotesClip; }
    };

    const std::optional<Clip>& clip (int track, int scene) const { return clips[(size_t) track][(size_t) scene]; }
    std::optional<Clip>&       clip (int track, int scene)       { return clips[(size_t) track][(size_t) scene]; }

    bool hasClip (int track, int scene) const { return valid (track, scene) && clip (track, scene).has_value(); }

    void setClip (int track, int scene, Clip c)   { if (valid (track, scene)) clip (track, scene) = std::move (c); }

    void removeClip (int track, int scene)
    {
        if (! valid (track, scene)) return;
        clip (track, scene).reset();
        if (playing[(size_t) track] == scene) playing[(size_t) track] = -1;
        if (queued[(size_t) track] == scene)  queued[(size_t) track] = -1;
    }

    // A name nobody on this track uses yet: "Pattern 1", "Pattern 2"...
    // ("Take 1"... for recordings).
    juce::String freshName (int track, const juce::String& stem = "Pattern") const
    {
        for (int n = 1;; ++n)
        {
            const auto name = stem + " " + juce::String (n);
            bool used = false;
            for (int s = 0; s < NumScenes; ++s)
                used |= hasClip (track, s) && clip (track, s)->name == name;
            if (! used) return name;
        }
    }

    int  getPlaying (int track) const { return playing[(size_t) track]; }
    int  getQueued (int track) const  { return queued[(size_t) track]; }
    void setPlaying (int track, int scene) { playing[(size_t) track] = scene; if (queued[(size_t) track] == scene) queued[(size_t) track] = -1; }
    void setQueued (int track, int scene)  { queued[(size_t) track] = scene; }
    void clearAll() { for (auto& t : clips) for (auto& c : t) c.reset(); playing.fill (-1); queued.fill (-1); }

    //==========================================================================
    juce::ValueTree toValueTree() const
    {
        juce::ValueTree t ("Session");
        for (int tr = 0; tr < NumTracks; ++tr)
        {
            t.setProperty ("playing" + juce::String (tr), playing[(size_t) tr], nullptr);
            for (int s = 0; s < NumScenes; ++s)
                if (const auto& c = clip (tr, s))
                {
                    juce::ValueTree ct ("Clip");
                    ct.setProperty ("track", tr, nullptr);
                    ct.setProperty ("scene", s, nullptr);
                    ct.setProperty ("name", c->name, nullptr);
                    ct.setProperty ("sequencer", c->sequencer, nullptr);
                    if (c->isNotes()) ct.setProperty ("notes", c->notes.toString(), nullptr);
                    else              ct.setProperty ("pattern", c->pattern.toString(), nullptr);
                    t.appendChild (ct, nullptr);
                }
        }
        return t;
    }

    void fromValueTree (const juce::ValueTree& t)
    {
        clearAll();
        for (const auto& ct : t)
        {
            const int tr = ct["track"], s = ct["scene"];
            const int seq = ct.getProperty ("sequencer", 0);
            if (! valid (tr, s))
                continue;
            if (seq == NotesClip)
                setClip (tr, s, { ct["name"].toString(), {}, NotesClip, NoteClip::fromString (ct["notes"].toString()) });
            else if (auto p = SeqPattern::fromString (ct["pattern"].toString()))
                setClip (tr, s, { ct["name"].toString(), *p, seq });
        }
        for (int tr = 0; tr < NumTracks; ++tr)
        {
            const int s = t.getProperty ("playing" + juce::String (tr), -1);
            playing[(size_t) tr] = hasClip (tr, s) ? s : -1;
        }
    }

private:
    static bool valid (int track, int scene)
    {
        return juce::isPositiveAndBelow (track, NumTracks) && juce::isPositiveAndBelow (scene, NumScenes);
    }

    std::array<std::array<std::optional<Clip>, NumScenes>, NumTracks> clips;
    std::array<int, NumTracks> playing { -1, -1, -1, -1 }, queued { -1, -1, -1, -1 };
};
