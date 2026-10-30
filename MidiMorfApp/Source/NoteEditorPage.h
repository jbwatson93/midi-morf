#pragma once

#include <JuceHeader.h>
#include "Glassmorphism.h"
#include "SynthLookAndFeel.h"
#include "TrackRail.h"
#include "NoteClip.h"
#include "DrumSynth.h"

//==============================================================================
// The editor for a recorded clip: a piano roll, or — when the track plays the
// drum synth — a drum editor with a row per drum.
//
//   • top      — Session (back), the clip's name, its length, the snap grid,
//                QUANTIZE, and zoom.
//   • the roll — tap empty space to add a note; drag a note to move it, drag
//                its right end to resize it; tap a note to delete it. Drag
//                empty space to scroll.
//   • VEL      — the lane underneath: drag the bars to set velocities.
//
// MainComponent hands it a copy of the clip (edit) and takes every change
// back (onChanged); the page never touches the session itself.
class NoteEditorPage : public juce::Component,
                       private juce::Timer
{
public:
    NoteEditorPage()
    {
        setLookAndFeel (&laf);

        backButton.onClick = [this] { if (onBack) onBack(); };
        addAndMakeVisible (backButton);

        for (int b : { 1, 2, 4, 8 })
            lengthCombo.addItem (juce::String (b) + (b == 1 ? " bar" : " bars"), b);
        lengthCombo.onChange = [this]
        {
            clip.lengthBeats = lengthCombo.getSelectedId() * 4.0;
            for (int i = clip.numNotes; --i >= 0;)
                if (clip.notes[(size_t) i].start >= clip.lengthBeats)
                    clip.remove (i);
            changed();
        };
        addAndMakeVisible (lengthCombo);

        const std::pair<const char*, double> grids[] = { { "Snap 1/4", 1.0 }, { "Snap 1/8", 0.5 }, { "Snap 1/16", 0.25 },
                                                        { "Snap 1/32", 0.125 }, { "Snap off", 0.0 } };
        for (int i = 0; i < 5; ++i)
            gridCombo.addItem (grids[i].first, i + 1);
        gridCombo.onChange = [this]
        {
            static const double g[5] = { 1.0, 0.5, 0.25, 0.125, 0.0 };
            snap = g[juce::jlimit (0, 4, gridCombo.getSelectedId() - 1)];
            roll.repaint();
        };
        gridCombo.setSelectedId (3, juce::dontSendNotification);
        addAndMakeVisible (gridCombo);

        quantizeButton.onClick = [this] { clip.quantize (snap > 0.0 ? snap : 0.25); changed(); };
        addAndMakeVisible (quantizeButton);

        zoomOut.onClick = [this] { setZoom (pxPerBeat / 1.5); };
        zoomIn.onClick  = [this] { setZoom (pxPerBeat * 1.5); };
        addAndMakeVisible (zoomOut);
        addAndMakeVisible (zoomIn);

        addAndMakeVisible (roll);
        startTimerHz (30);
    }

    ~NoteEditorPage() override
    {
        stopTimer();
        setLookAndFeel (nullptr);
    }

    std::function<void (const NoteClip&)> onChanged;
    std::function<void()> onBack;
    std::function<double()> getPlayPosition;   // 0..1 through the loop, -1 if not playing

    // drums: a row per drum instead of a keyboard.
    void edit (const NoteClip& c, const juce::String& name, int trackIndex, bool drums)
    {
        clip = c;
        clip.sort();
        clipName = name;
        track = trackIndex;
        drumMode = drums;
        lengthCombo.setSelectedId (juce::jlimit (1, 8, juce::roundToInt (clip.lengthBeats / 4.0)), juce::dontSendNotification);
        scrollX = 0.0;

        // Start the piano roll around the notes (or middle C).
        if (! drumMode)
        {
            int lo = 127, hi = 0;
            for (int i = 0; i < clip.numNotes; ++i) { lo = juce::jmin (lo, (int) clip.notes[(size_t) i].note); hi = juce::jmax (hi, (int) clip.notes[(size_t) i].note); }
            topNote = clip.numNotes > 0 ? juce::jlimit (12, 127, hi + 2) : 72;
        }
        resized();
        repaint();
    }

    int getTrack() const noexcept { return track; }

    void paint (juce::Graphics& g) override
    {
        Glassmorphism::paintPanel (g, getLocalBounds().toFloat(), editorPalette);
        g.setColour (TrackColours::get (track));
        g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        g.drawFittedText (clipName + (drumMode ? juce::String::fromUTF8 ("  \xc2\xb7  drums") : juce::String()), titleArea, juce::Justification::centredLeft, 1);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (8, 6);
        auto top = area.removeFromTop (34);
        backButton.setBounds (top.removeFromLeft (84).reduced (2));
        zoomIn.setBounds (top.removeFromRight (34).reduced (2));
        zoomOut.setBounds (top.removeFromRight (34).reduced (2));
        top.removeFromRight (6);
        quantizeButton.setBounds (top.removeFromRight (92).reduced (2));
        gridCombo.setBounds (top.removeFromRight (104).reduced (2));
        lengthCombo.setBounds (top.removeFromRight (86).reduced (2));
        titleArea = top.reduced (8, 0);
        area.removeFromTop (6);
        roll.setBounds (area);
    }

private:
    static inline const PagePalette editorPalette {
        juce::Colour (0xff1A1C22), juce::Colour (0xff08090C),
        juce::Colour (0xCC2A2D36), juce::Colour (0xD8161820), juce::Colour (0xCC000000)
    };

    void changed()
    {
        clip.sort();
        roll.repaint();
        if (onChanged) onChanged (clip);
    }

    void setZoom (double px)
    {
        pxPerBeat = juce::jlimit (24.0, 400.0, px);
        roll.clampScroll();
        roll.repaint();
    }

    void timerCallback() override
    {
        const double pos = getPlayPosition ? getPlayPosition() : -1.0;
        if (pos != playPos)
        {
            playPos = pos;
            roll.repaint();
        }
    }

    //==========================================================================
    // Rows: in drum mode the 8 drums, top to bottom; otherwise pitches from
    // topNote down.
    int numRows() const      { return drumMode ? Drums::NumDrums : 128; }
    int rowNote (int row) const { return drumMode ? Drums::noteFor (row) : topNote - row; }

    int rowOfNote (int note) const
    {
        if (drumMode)
        {
            float semis = 0.0f;
            return Drums::drumForNote (note, semis);
        }
        return topNote - note;
    }

    //==========================================================================
    struct Roll : public juce::Component
    {
        explicit Roll (NoteEditorPage& o) : owner (o) {}

        static constexpr int KeysW = 56, VelH = 44;

        int rowH() const
        {
            return owner.drumMode ? juce::jmax (18, (getHeight() - VelH) / Drums::NumDrums) : 18;
        }

        juce::Rectangle<int> notesArea() const { return getLocalBounds().withTrimmedLeft (KeysW).withTrimmedBottom (VelH); }
        juce::Rectangle<int> velArea() const   { return getLocalBounds().withTrimmedLeft (KeysW).removeFromBottom (VelH); }

        double xToBeat (int x) const { return owner.scrollX + (x - notesArea().getX()) / owner.pxPerBeat; }
        float beatToX (double b) const { return (float) (notesArea().getX() + (b - owner.scrollX) * owner.pxPerBeat); }
        int yToRow (int y) const     { return (y - notesArea().getY()) / rowH() + (owner.drumMode ? 0 : 0); }
        float rowToY (int row) const { return (float) (notesArea().getY() + row * rowH()); }

        void clampScroll()
        {
            const double visibleBeats = notesArea().getWidth() / owner.pxPerBeat;
            owner.scrollX = juce::jlimit (0.0, juce::jmax (0.0, owner.clip.lengthBeats - visibleBeats), owner.scrollX);
            if (! owner.drumMode)
            {
                const int visibleRows = notesArea().getHeight() / rowH();
                owner.topNote = juce::jlimit (visibleRows - 1, 127, owner.topNote);
            }
        }

        juce::Rectangle<float> noteRect (const NoteEvent& e) const
        {
            const int row = owner.rowOfNote (e.note);
            return { beatToX (e.start), rowToY (row) + 1.0f, (float) (e.length * owner.pxPerBeat), (float) rowH() - 2.0f };
        }

        void paint (juce::Graphics& g) override
        {
            auto area = notesArea();
            const auto colour = TrackColours::get (owner.track);
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.fillRect (getLocalBounds());

            // Rows and their labels.
            const int rows = owner.drumMode ? Drums::NumDrums : area.getHeight() / rowH() + 1;
            g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
            for (int r = 0; r < rows; ++r)
            {
                const int note = owner.rowNote (r);
                if (note < 0) break;
                const float y = rowToY (r);
                const bool black = ! owner.drumMode && juce::MidiMessage::isMidiNoteBlack (note);
                g.setColour (juce::Colours::white.withAlpha (black ? 0.02f : 0.05f));
                g.fillRect ((float) area.getX(), y, (float) area.getWidth(), (float) rowH() - 1.0f);

                auto label = juce::Rectangle<float> (0.0f, y, (float) KeysW - 4.0f, (float) rowH() - 1.0f);
                g.setColour (black ? juce::Colour (0xff202228) : juce::Colour (0xff3A3D46));
                g.fillRoundedRectangle (label, 3.0f);
                g.setColour (MidiMorfColors::textLight.withAlpha (owner.drumMode || note % 12 == 0 ? 0.9f : 0.45f));
                g.drawFittedText (owner.drumMode ? Drums::shortNames()[r] : juce::MidiMessage::getMidiNoteName (note, true, true, 4),
                                  label.toNearestInt().reduced (3, 0), juce::Justification::centredLeft, 1);
            }

            // Beat and bar lines, the snap grid between them.
            g.saveState();
            g.reduceClipRegion (area.withBottom (getHeight()));
            const double step = owner.snap > 0.0 ? owner.snap : 0.25;
            for (double b = 0.0; b <= owner.clip.lengthBeats + 1e-9; b += step)
            {
                const float x = beatToX (b);
                const bool bar = std::fmod (b, 4.0) < 1e-6, beat = std::fmod (b, 1.0) < 1e-6;
                g.setColour (MidiMorfColors::textLight.withAlpha (bar ? 0.4f : beat ? 0.18f : 0.07f));
                g.drawVerticalLine ((int) x, (float) area.getY(), (float) getHeight());
            }
            const float endX = beatToX (owner.clip.lengthBeats);
            g.setColour (juce::Colours::black.withAlpha (0.45f));
            g.fillRect (juce::Rectangle<float> (endX, 0.0f, (float) getWidth() - endX, (float) getHeight()));

            // Notes (kept out of the lane below), then their velocities.
            auto vel = velArea();
            g.saveState();
            g.reduceClipRegion (area);
            for (int i = 0; i < owner.clip.numNotes; ++i)
            {
                const auto& e = owner.clip.notes[(size_t) i];
                const auto r = noteRect (e);
                const float amount = e.velocity / 127.0f;
                g.setColour (colour.withAlpha (0.45f + 0.5f * amount));
                g.fillRoundedRectangle (r, 3.0f);
                g.setColour (colour.brighter (0.5f));
                g.drawRoundedRectangle (r, 3.0f, i == drag.index ? 2.0f : 1.0f);
            }
            g.restoreState();

            g.setColour (juce::Colours::black.withAlpha (0.3f));
            g.fillRect (vel);
            for (int i = 0; i < owner.clip.numNotes; ++i)
            {
                const auto& e = owner.clip.notes[(size_t) i];
                const float amount = e.velocity / 127.0f;
                g.setColour (colour.withAlpha (0.8f));
                g.fillRect (beatToX (e.start), vel.getBottom() - (vel.getHeight() - 4) * amount, 4.0f, (vel.getHeight() - 4) * amount);
            }

            if (owner.playPos >= 0.0)
            {
                g.setColour (juce::Colour (0xffFFC24A));
                g.drawVerticalLine ((int) beatToX (owner.playPos * owner.clip.lengthBeats), 0.0f, (float) getHeight());
            }
            g.restoreState();

            g.setColour (MidiMorfColors::textLight.withAlpha (0.45f));
            g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
            g.drawText ("VEL", juce::Rectangle<int> (0, vel.getY(), KeysW - 4, vel.getHeight()), juce::Justification::centred);
        }

        double snapBeat (double b) const
        {
            return owner.snap > 0.0 ? std::floor (b / owner.snap) * owner.snap : b;
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            drag = {};
            drag.startPos = e.getPosition();
            drag.startScrollX = owner.scrollX;
            drag.startTop = owner.topNote;

            if (velArea().contains (e.getPosition()))
            {
                drag.mode = Drag::Velocity;
                setVelocityAt (e.getPosition());
                return;
            }
            if (! notesArea().contains (e.getPosition()))
                return;

            // Topmost note under the finger (later ones draw on top).
            for (int i = owner.clip.numNotes; --i >= 0;)
            {
                const auto r = noteRect (owner.clip.notes[(size_t) i]);
                if (r.expanded (0.0f, 2.0f).contains (e.position))
                {
                    drag.index = i;
                    drag.original = owner.clip.notes[(size_t) i];
                    drag.mode = e.position.x > r.getRight() - juce::jmax (12.0f, r.getWidth() * 0.25f) ? Drag::Resize : Drag::Move;
                    return;
                }
            }
            drag.mode = Drag::Empty;
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            const auto d = e.getPosition() - drag.startPos;
            if (! drag.moved && d.getDistanceFromOrigin() < 6)
                return;
            drag.moved = true;

            switch (drag.mode)
            {
                case Drag::Empty:
                    owner.scrollX = drag.startScrollX - d.x / owner.pxPerBeat;
                    if (! owner.drumMode)
                        owner.topNote = drag.startTop + d.y / rowH();
                    clampScroll();
                    break;

                case Drag::Move:
                {
                    auto& n = owner.clip.notes[(size_t) drag.index];
                    const double beat = drag.original.start + d.x / owner.pxPerBeat;
                    n.start = juce::jlimit (0.0, owner.clip.lengthBeats - 0.01,
                                            owner.snap > 0.0 ? std::round (beat / owner.snap) * owner.snap : beat);
                    const int row = juce::jlimit (0, owner.numRows() - 1, owner.rowOfNote (drag.original.note) + juce::roundToInt ((float) d.y / (float) rowH()));
                    n.note = (uint8_t) juce::jlimit (0, 127, owner.rowNote (row));
                    break;
                }

                case Drag::Resize:
                {
                    auto& n = owner.clip.notes[(size_t) drag.index];
                    double len = drag.original.length + d.x / owner.pxPerBeat;
                    if (owner.snap > 0.0) len = std::round (len / owner.snap) * owner.snap;
                    n.length = juce::jmax (owner.snap > 0.0 ? owner.snap : 0.03, len);
                    break;
                }

                case Drag::Velocity:
                    setVelocityAt (e.getPosition());
                    break;

                default: break;
            }
            repaint();
        }

        void mouseUp (const juce::MouseEvent& e) override
        {
            switch (drag.mode)
            {
                case Drag::Empty:
                    if (! drag.moved)
                        addNoteAt (e.getPosition());
                    break;

                case Drag::Move:
                case Drag::Resize:
                    if (! drag.moved)
                        owner.clip.remove (drag.index);   // a tap on a note deletes it
                    owner.changed();
                    break;

                case Drag::Velocity:
                    owner.changed();
                    break;

                default: break;
            }
            drag = {};
            repaint();
        }

        void addNoteAt (juce::Point<int> p)
        {
            const double beat = snapBeat (xToBeat (p.x));
            const int row = yToRow (p.y);
            if (beat < 0.0 || beat >= owner.clip.lengthBeats || ! juce::isPositiveAndBelow (row, owner.numRows()))
                return;
            const int note = owner.rowNote (row);
            if (! juce::isPositiveAndBelow (note, 128))
                return;
            owner.clip.add ({ beat, owner.snap > 0.0 ? owner.snap : 0.25, (uint8_t) note, 100 });
            owner.changed();
        }

        // The note nearest the finger along the lane takes its height.
        void setVelocityAt (juce::Point<int> p)
        {
            auto vel = velArea();
            int best = -1;
            float bestDist = 14.0f;
            for (int i = 0; i < owner.clip.numNotes; ++i)
            {
                const float dist = std::abs (beatToX (owner.clip.notes[(size_t) i].start) + 2.0f - (float) p.x);
                if (dist < bestDist) { bestDist = dist; best = i; }
            }
            if (best < 0) return;
            const float amount = juce::jlimit (0.0f, 1.0f, (float) (vel.getBottom() - p.y) / (float) (vel.getHeight() - 4));
            owner.clip.notes[(size_t) best].velocity = (uint8_t) juce::jlimit (1, 127, juce::roundToInt (amount * 127.0f));
            repaint();
        }

        struct Drag
        {
            enum Mode { None, Empty, Move, Resize, Velocity };
            Mode mode = None;
            int index = -1;
            NoteEvent original;
            juce::Point<int> startPos;
            double startScrollX = 0.0;
            int startTop = 72;
            bool moved = false;
        };

        NoteEditorPage& owner;
        Drag drag;
    };

    NoteClip clip;
    juce::String clipName;
    int track = 0, topNote = 72;
    bool drumMode = false;
    double snap = 0.25, pxPerBeat = 80.0, scrollX = 0.0, playPos = -1.0;

    SynthLookAndFeel laf;   // declared before the children
    juce::TextButton backButton { "< Session" }, quantizeButton { "QUANTIZE" }, zoomOut { "-" }, zoomIn { "+" };
    juce::ComboBox lengthCombo, gridCombo;
    juce::Rectangle<int> titleArea;
    Roll roll { *this };
};
