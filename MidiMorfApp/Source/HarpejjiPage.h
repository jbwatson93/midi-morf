#pragma once

#include <JuceHeader.h>
#include "Glassmorphism.h"
#include "SynthLookAndFeel.h"
#include "TrackRack.h"
#include "TrackRail.h"

//==============================================================================
// Harpejji — a fretboard you tap, like the harpejji: strings side by side,
// each a whole step above the one below, frets a semitone apart, and the
// frets marked like piano keys so every key's shapes can be found. Any finger
// on a fret sounds that note straight away, so two hands play bass and chords
// at once.
//
// Like a real string, each string sounds one note: its highest held fret.
// Tap a higher fret while holding a lower one and it hammers on; let go and
// it pulls off back to the lower one. Dragging along a string slides fret by
// fret; dragging across strings moves to the next. With RING on, a string
// keeps sounding after you let go until you play it again or tap the nut at
// its left end (the mute). TUNING also offers thirds, fourths and fifths.
//
// Each string plays on its own channel, so the same note on two strings
// (fret 2 on one is fret 0 on the next) never cuts the other off.
class HarpejjiPage : public juce::Component,
                     private juce::Timer
{
public:
    HarpejjiPage (TrackRack& r, int trackIndex)
        : rack (r), track (trackIndex)
    {
        setLookAndFeel (&laf);

        for (int n = LowestLow; n <= HighestLow; ++n)
            lowCombo.addItem ("LOW " + juce::MidiMessage::getMidiNoteName (n, true, true, 4), n - LowestLow + 1);
        lowCombo.onChange = [this] { setLayout (lowCombo.getSelectedId() - 1 + LowestLow, tuning); };

        for (int i = 0; i < (int) tunings().size(); ++i)
            tuningCombo.addItem (tunings()[(size_t) i].name, i + 1);
        tuningCombo.onChange = [this] { setLayout (low, tuningCombo.getSelectedId() - 1); };

        static const char* velNames[] = { "Soft", "Medium", "Hard" };
        for (int i = 0; i < 3; ++i) velCombo.addItem (velNames[i], i + 1);
        velCombo.onChange = [this] { vel = velCombo.getSelectedId() - 1; changed(); };

        ringButton.setClickingTogglesState (true);
        ringButton.onClick = [this]
        {
            ring = ringButton.getToggleState();
            if (! ring)
                for (int s = 0; s < MaxStrings; ++s)
                    updateString (s);   // strings nobody's holding stop
            changed();
        };

        for (auto* c : std::initializer_list<juce::Component*> { &lowCombo, &tuningCombo, &velCombo, &ringButton })
            addAndMakeVisible (c);

        playing.fill (-1);
        playingFret.fill (-1);
        heldFret.fill (-1);
        seenAllOffs = rack.getAllOffCount (track);
        refresh();
    }

    ~HarpejjiPage() override
    {
        stopTimer();
        setLookAndFeel (nullptr);
    }

    std::function<void()> onStateChanged;   // persist

    //==========================================================================
    void saveState (juce::PropertySet& p) const
    {
        p.setValue ("harp_low", low);
        p.setValue ("harp_tuning", tuning);
        p.setValue ("harp_vel", vel);
        p.setValue ("harp_ring", ring);
    }

    void restoreState (juce::PropertySet& p)
    {
        allOff();
        low    = juce::jlimit (LowestLow, HighestLow, p.getIntValue ("harp_low", 36));
        tuning = juce::jlimit (0, (int) tunings().size() - 1, p.getIntValue ("harp_tuning", 0));
        vel    = juce::jlimit (0, 2, p.getIntValue ("harp_vel", 1));
        ring   = p.getBoolValue ("harp_ring", false);
        refresh();
        repaint();
    }

    void visibilityChanged() override
    {
        // A finger can't be held on a page that isn't showing.
        if (! isVisible())
        {
            for (auto& t : touches) t = {};
            for (int s = 0; s < MaxStrings; ++s)
                updateString (s);
        }
    }

    //==========================================================================
    void paint (juce::Graphics& g) override
    {
        Glassmorphism::paintPanel (g, getLocalBounds().toFloat(), harpPalette);
        if (board.isEmpty())
            return;

        const auto colour = TrackColours::get (track);
        const double now = juce::Time::getMillisecondCounterHiRes();
        const float rowH = board.getHeight() / (float) numStrings;
        const float fretW = board.getWidth() / (float) numFrets;

        // The board, then the frets, coloured like the keys of a piano.
        g.setColour (juce::Colour (0xff1A120C));
        g.fillRoundedRectangle (nut.getUnion (board).toFloat(), 8.0f);

        const float fontH = juce::jlimit (9.0f, 13.0f, juce::jmin (rowH, fretW) * 0.3f);
        g.setFont (juce::FontOptions (fontH, juce::Font::bold));
        for (int s = 0; s < numStrings; ++s)
            for (int f = 0; f < numFrets; ++f)
            {
                const int note = noteAt (s, f);
                const bool black = juce::MidiMessage::isMidiNoteBlack (note);
                auto cell = cellBounds (s, f).reduced (1.5f);
                const bool held = playingFret[(size_t) s] == f;

                g.setColour (held ? colour.withAlpha (0.85f)
                                  : black ? juce::Colour (0xff2B2420) : juce::Colour (0xffD9CCB4).withAlpha (note % 12 == 0 ? 0.95f : 0.75f));
                g.fillRoundedRectangle (cell, 3.0f);

                // Every C is named, the other notes only where there's room.
                if (note % 12 == 0 || fretW > 54.0f)
                {
                    g.setColour ((black && ! held) ? MidiMorfColors::textLight.withAlpha (0.45f)
                                                   : juce::Colours::black.withAlpha (note % 12 == 0 ? 0.8f : 0.4f));
                    g.drawText (juce::MidiMessage::getMidiNoteName (note, true, note % 12 == 0, 4),
                                cell.withTrimmedTop (cell.getHeight() * 0.5f), juce::Justification::centred);
                }
            }

        // Fret markers between octaves, as on a guitar.
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        for (int f = 12; f < numFrets; f += 12)
            g.fillRect (juce::Rectangle<float> ((float) board.getX() + f * fretW - 1.5f, (float) board.getY(), 3.0f, (float) board.getHeight()));

        // The strings, thicker low down; a sounding one shakes, the most
        // between the fret and the far end, as it would.
        for (int s = 0; s < numStrings; ++s)
        {
            const float y = board.getBottom() - (s + 0.5f) * rowH;
            const float thick = 1.0f + 2.2f * (1.0f - s / (float) juce::jmax (1, numStrings - 1));
            const bool sounding = playing[(size_t) s] >= 0;
            const float x0 = (float) nut.getX() + 4.0f, x1 = (float) board.getRight() - 4.0f;

            if (! sounding)
            {
                g.setColour (juce::Colour (0xffB8B0A0).withAlpha (0.8f));
                g.drawLine (x0, y, x1, y, thick);
                continue;
            }

            const double age = now - startedAt[(size_t) s];
            const float amp = juce::jlimit (0.0f, rowH * 0.22f, rowH * 0.22f * (float) std::exp (-age / 900.0));
            const float from = (float) board.getX() + (playingFret[(size_t) s] + 1) * fretW;
            juce::Path p;
            p.startNewSubPath (x0, y);
            p.lineTo (from, y);
            const float len = juce::jmax (1.0f, x1 - from);
            const float phase = (float) (age * 0.06);
            for (float x = from; x <= x1; x += 3.0f)
            {
                const float u = (x - from) / len;
                p.lineTo (x, y + amp * std::sin (juce::MathConstants<float>::pi * u) * std::sin (phase + u * 9.0f));
            }
            g.setColour (colour.brighter (0.7f));
            g.strokePath (p, juce::PathStrokeType (thick + 0.5f));
        }

        // The nut: tap it to stop a string.
        for (int s = 0; s < numStrings; ++s)
        {
            const float y = board.getBottom() - (s + 0.5f) * rowH;
            const bool live = playing[(size_t) s] >= 0;
            auto mark = juce::Rectangle<float> (12.0f, 12.0f).withCentre ({ (float) nut.getCentreX(), y });
            g.setColour (MidiMorfColors::textLight.withAlpha (live ? 0.9f : 0.25f));
            g.drawLine ({ mark.getTopLeft(), mark.getBottomRight() }, 1.6f);
            g.drawLine ({ mark.getTopRight(), mark.getBottomLeft() }, 1.6f);
        }
        g.setColour (juce::Colour (0xffE8E0D0).withAlpha (0.7f));
        g.fillRect (juce::Rectangle<float> ((float) nut.getRight() - 3.0f, (float) board.getY(), 3.0f, (float) board.getHeight()));
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (10, 8);
        auto bar = area.removeFromTop (34);
        auto place = [&bar] (juce::Component& c, int w) { c.setBounds (bar.removeFromLeft (w).reduced (2)); };
        place (lowCombo, 104);
        place (tuningCombo, 170);
        place (velCombo, 96);
        place (ringButton, 70);

        area.removeFromTop (6);
        nut = area.removeFromLeft (34);
        board = area;

        // About a finger per fret and per string, whatever the screen.
        numStrings = juce::jlimit (6, MaxStrings, board.getHeight() / 34);
        numFrets   = juce::jlimit (8, 24, board.getWidth() / 48);
        allOff();
        repaint();
    }

    //==========================================================================
    void mouseDown (const juce::MouseEvent& e) override
    {
        auto& t = touches[(size_t) (e.source.getIndex() % (int) touches.size())];
        t = hit (e.position);
        if (t.string < 0)
            return;
        if (t.fret == Nut)
        {
            mute (t.string);
            return;
        }
        updateString (t.string);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        auto& t = touches[(size_t) (e.source.getIndex() % (int) touches.size())];
        if (t.string < 0 || t.fret == Nut)
            return;
        const auto now = hit (e.position);
        if (now.string < 0 || now.fret == Nut || (now.string == t.string && now.fret == t.fret))
            return;
        const int was = t.string;
        t = now;
        if (was != t.string)
            updateString (was);
        updateString (t.string);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        auto& t = touches[(size_t) (e.source.getIndex() % (int) touches.size())];
        const int s = t.string;
        t = {};
        if (s >= 0)
            updateString (s);
    }

private:
    static constexpr int MaxStrings = 16;   // one channel each
    static constexpr int Nut = -2;
    static constexpr int LowestLow = 24, HighestLow = 72;

    struct Tuning { const char* name; int step; };   // semitones between strings
    static const std::vector<Tuning>& tunings()
    {
        static const std::vector<Tuning> t {
            { "Whole steps (harpejji)", 2 },
            { "Minor thirds",           3 },
            { "Major thirds",           4 },
            { "Fourths (bass)",         5 },
            { "Fifths (cello)",         7 },
        };
        return t;
    }

    struct Touch { int string = -1, fret = -1; };

    int noteAt (int s, int f) const
    {
        return juce::jlimit (0, 127, low + s * tunings()[(size_t) tuning].step + f);
    }

    juce::Rectangle<float> cellBounds (int s, int f) const
    {
        const float rowH = board.getHeight() / (float) numStrings;
        const float fretW = board.getWidth() / (float) numFrets;
        return { board.getX() + f * fretW, board.getBottom() - (s + 1) * rowH, fretW, rowH };
    }

    Touch hit (juce::Point<float> p) const
    {
        const auto all = nut.getUnion (board).toFloat();
        if (! all.contains (p) || board.isEmpty())
            return {};
        const float rowH = board.getHeight() / (float) numStrings;
        Touch t;
        t.string = juce::jlimit (0, numStrings - 1, (int) ((board.getBottom() - p.y) / rowH));
        t.fret = p.x < board.getX() ? Nut
                                    : juce::jlimit (0, numFrets - 1, (int) ((p.x - board.getX()) / (board.getWidth() / (float) numFrets)));
        return t;
    }

    // Sounds a string's highest held fret, as a real string would: higher
    // than what's sounding hammers on, lower pulls off (a little softer).
    // With nothing held it stops, unless RING.
    void updateString (int s)
    {
        if (! juce::isPositiveAndBelow (s, MaxStrings))
            return;
        int target = -1;
        for (const auto& t : touches)
            if (t.string == s && t.fret >= 0)
                target = juce::jmax (target, t.fret);

        const int was = heldFret[(size_t) s];
        if (target == was && (target >= 0 || ring || playing[(size_t) s] < 0))
            return;   // no change (but a string left ringing stops once RING is off)
        heldFret[(size_t) s] = target;

        if (target < 0)
        {
            if (! ring)
                stop (s);
            return;
        }

        static const int velocities[] = { 64, 96, 122 };
        int velocity = velocities[juce::jlimit (0, 2, vel)];
        if (was > target)
            velocity = velocity * 3 / 4;   // a pull-off

        stop (s);
        heldFret[(size_t) s] = target;
        const int note = noteAt (s, target);
        rack.postNoteOn (track, 1 + s, note, velocity);
        playing[(size_t) s] = note;
        playingFret[(size_t) s] = target;
        startedAt[(size_t) s] = juce::Time::getMillisecondCounterHiRes();
        animate();
    }

    void stop (int s)
    {
        if (playing[(size_t) s] >= 0)
            rack.postNoteOff (track, 1 + s, playing[(size_t) s]);
        playing[(size_t) s] = -1;
        playingFret[(size_t) s] = -1;
        heldFret[(size_t) s] = -1;
        repaint();
    }

    // The nut: stops the string, and fingers still on it no longer count.
    void mute (int s)
    {
        for (auto& t : touches)
            if (t.string == s && t.fret >= 0)
                t = {};
        stop (s);
    }

    void allOff()
    {
        for (auto& t : touches) t = {};
        for (int s = 0; s < MaxStrings; ++s)
            stop (s);
    }

    void setLayout (int newLow, int newTuning)
    {
        allOff();   // the notes under the fingers change
        low = newLow;
        tuning = newTuning;
        refresh();
        repaint();
        changed();
    }

    //==========================================================================
    bool anySounding() const { return std::any_of (playing.begin(), playing.end(), [] (int n) { return n >= 0; }); }

    void animate() { if (! isTimerRunning()) startTimerHz (60); repaint(); }

    void timerCallback() override
    {
        // STOP or panic on this track: the rack has already silenced it.
        if (const int n = rack.getAllOffCount (track); n != seenAllOffs)
        {
            seenAllOffs = n;
            for (auto& t : touches) t = {};
            playing.fill (-1);
            playingFret.fill (-1);
            heldFret.fill (-1);
        }
        if (! anySounding())
            stopTimer();
        if (isShowing())
            repaint();
    }

    void refresh()
    {
        lowCombo.setSelectedId (low - LowestLow + 1, juce::dontSendNotification);
        tuningCombo.setSelectedId (tuning + 1, juce::dontSendNotification);
        velCombo.setSelectedId (vel + 1, juce::dontSendNotification);
        ringButton.setToggleState (ring, juce::dontSendNotification);
    }

    void changed() { if (onStateChanged) onStateChanged(); }

    static inline const PagePalette harpPalette {
        juce::Colour (0xff2A1C12), juce::Colour (0xff0E0906),
        juce::Colour (0xCC2E2016), juce::Colour (0xD8120C08), juce::Colour (0xCC000000)
    };

    TrackRack& rack;
    const int track;

    int low = 36, tuning = 0, vel = 1;
    bool ring = false;
    int numStrings = 8, numFrets = 12;
    juce::Rectangle<int> board, nut;

    std::array<Touch, 10> touches {};
    std::array<int, MaxStrings> playing {}, playingFret {};   // the note and fret each string sounds, or -1
    std::array<int, MaxStrings> heldFret {};                  // its highest fret under a finger, or -1 (RING: may still sound)
    std::array<double, MaxStrings> startedAt {};
    int seenAllOffs = 0;

    SynthLookAndFeel laf;   // declared before the children
    juce::ComboBox lowCombo, tuningCombo, velCombo;
    juce::TextButton ringButton { "RING" };
};
