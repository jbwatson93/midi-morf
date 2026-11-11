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
// at once. STRINGS sets how many (4 to 16).
//
// Like a real string, each string sounds one note: its highest held fret.
// Tap a higher fret while holding a lower one and it hammers on; let go and
// it pulls off back to the lower one.
//
// A finger that's down slides and bends without playing a new note: along
// the string it slides, fret by fret (FRETTED) or gliding (SMOOTH); across
// the string, either way, it bends the note up, by BEND when it has moved one
// string's width. With RING on, a string keeps sounding after you let go
// until you play it again or tap the nut at its left end (the mute). TUNING
// also offers thirds, fourths and fifths.
//
// Each string plays on its own channel, which is how its slides and bends
// reach only its own note (TrackRack::postPitchBend; the synth and Halo
// follow them), and why the same note on two strings never cuts the other
// off.
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
        lowCombo.onChange = [this] { setLayout (lowCombo.getSelectedId() - 1 + LowestLow, tuning, strings); };

        for (int i = 0; i < (int) tunings().size(); ++i)
            tuningCombo.addItem (tunings()[(size_t) i].name, i + 1);
        tuningCombo.onChange = [this] { setLayout (low, tuningCombo.getSelectedId() - 1, strings); };

        stringsDown.onClick = [this] { setLayout (low, tuning, strings - 1); };
        stringsUp.onClick   = [this] { setLayout (low, tuning, strings + 1); };
        stringsLabel.setJustificationType (juce::Justification::centred);
        stringsLabel.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        stringsLabel.setColour (juce::Label::textColourId, MidiMorfColors::textLight);

        slideCombo.addItem ("Fretted", 1);
        slideCombo.addItem ("Smooth", 2);
        slideCombo.onChange = [this] { smooth = slideCombo.getSelectedId() == 2; changed(); };

        for (int i = 0; i < (int) bendNames().size(); ++i)
            bendCombo.addItem (bendNames()[i], i + 1);
        bendCombo.onChange = [this] { bend = bendCombo.getSelectedId() - 1; changed(); };

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

        for (auto* c : std::initializer_list<juce::Component*> { &lowCombo, &tuningCombo, &stringsDown, &stringsLabel, &stringsUp,
                                                                 &slideCombo, &bendCombo, &velCombo, &ringButton })
            addAndMakeVisible (c);

        playing.fill (-1);
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
        p.setValue ("harp_strings", strings);
        p.setValue ("harp_smooth", smooth);
        p.setValue ("harp_bend", bend);
        p.setValue ("harp_vel", vel);
        p.setValue ("harp_ring", ring);
    }

    void restoreState (juce::PropertySet& p)
    {
        allOff();
        low     = juce::jlimit (LowestLow, HighestLow, p.getIntValue ("harp_low", 36));
        tuning  = juce::jlimit (0, (int) tunings().size() - 1, p.getIntValue ("harp_tuning", 0));
        strings = juce::jlimit (MinStrings, MaxStrings, p.getIntValue ("harp_strings", 8));
        smooth  = p.getBoolValue ("harp_smooth", false);
        bend    = juce::jlimit (0, (int) bendNames().size() - 1, p.getIntValue ("harp_bend", 2));
        vel     = juce::jlimit (0, 2, p.getIntValue ("harp_vel", 1));
        ring    = p.getBoolValue ("harp_ring", false);
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
        const float rowH = rowHeight(), fretW = fretWidth();

        // The board, then the frets, coloured like the keys of a piano.
        g.setColour (juce::Colour (0xff1A120C));
        g.fillRoundedRectangle (nut.getUnion (board).toFloat(), 8.0f);

        const float fontH = juce::jlimit (8.0f, 13.0f, juce::jmin (rowH, fretW) * 0.3f);
        g.setFont (juce::FontOptions (fontH, juce::Font::bold));
        for (int s = 0; s < strings; ++s)
        {
            const int lit = litFret (s);
            for (int f = 0; f < numFrets; ++f)
            {
                const int note = noteAt (s, f);
                const bool black = juce::MidiMessage::isMidiNoteBlack (note);
                auto cell = cellBounds (s, f).reduced (rowH > 24.0f ? 1.5f : 1.0f);
                const bool held = lit == f;

                g.setColour (held ? colour.withAlpha (0.85f)
                                  : black ? juce::Colour (0xff2B2420) : juce::Colour (0xffD9CCB4).withAlpha (note % 12 == 0 ? 0.95f : 0.75f));
                g.fillRoundedRectangle (cell, 3.0f);

                // Every C is named, the other notes only where there's room.
                if ((note % 12 == 0 || fretW > 54.0f) && rowH > 18.0f)
                {
                    g.setColour ((black && ! held) ? MidiMorfColors::textLight.withAlpha (0.45f)
                                                   : juce::Colours::black.withAlpha (note % 12 == 0 ? 0.8f : 0.4f));
                    g.drawText (juce::MidiMessage::getMidiNoteName (note, true, note % 12 == 0, 4),
                                cell.withTrimmedTop (cell.getHeight() * 0.5f), juce::Justification::centred);
                }
            }
        }

        // Fret markers between octaves, as on a guitar.
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        for (int f = 12; f < numFrets; f += 12)
            g.fillRect (juce::Rectangle<float> ((float) board.getX() + f * fretW - 1.5f, (float) board.getY(), 3.0f, (float) board.getHeight()));

        // The strings, thicker low down. A sounding one is pulled to the
        // finger playing it (that's the bend) and shakes beyond it.
        for (int s = 0; s < strings; ++s)
        {
            const float y = stringY (s);
            const float thick = 1.0f + 2.2f * (1.0f - s / (float) juce::jmax (1, strings - 1)) * juce::jmin (1.0f, rowH / 34.0f);
            const float x0 = (float) nut.getX() + 4.0f, x1 = (float) board.getRight() - 4.0f;

            if (playing[(size_t) s] < 0)
            {
                g.setColour (juce::Colour (0xffB8B0A0).withAlpha (0.8f));
                g.drawLine (x0, y, x1, y, thick);
                continue;
            }

            float fx = (float) board.getX() + (litFret (s) + 1) * fretW, pull = 0.0f;
            if (const auto* t = driver (s))
            {
                fx = (float) board.getX() + t->pos * fretW;
                pull = juce::jlimit (-rowH, rowH, t->y - t->startY);
            }

            const double age = now - startedAt[(size_t) s];
            const float amp = rowH * 0.22f * (float) std::exp (-age / 900.0);
            juce::Path p;
            p.startNewSubPath (x0, y);
            p.lineTo (fx, y + pull);
            const float len = juce::jmax (1.0f, x1 - fx);
            const float phase = (float) (age * 0.06);
            for (float x = fx; x <= x1; x += 3.0f)
            {
                const float u = (x - fx) / len;
                p.lineTo (x, y + pull * (1.0f - u) + amp * std::sin (juce::MathConstants<float>::pi * u) * std::sin (phase + u * 9.0f));
            }
            p.lineTo (x1, y);
            g.setColour (colour.brighter (0.7f));
            g.strokePath (p, juce::PathStrokeType (thick + 0.5f));
        }

        // The nut: tap it to stop a string.
        for (int s = 0; s < strings; ++s)
        {
            const bool live = playing[(size_t) s] >= 0;
            const float m = juce::jmin (12.0f, rowH * 0.45f);
            auto mark = juce::Rectangle<float> (m, m).withCentre ({ (float) nut.getCentreX(), stringY (s) });
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
        place (lowCombo, 100);
        place (tuningCombo, 112);
        place (stringsDown, 30); place (stringsLabel, 70); place (stringsUp, 30);
        bar.removeFromLeft (4);
        place (slideCombo, 88);
        place (bendCombo, 96);
        place (velCombo, 84);
        place (ringButton, 64);

        area.removeFromTop (6);
        nut = area.removeFromLeft (34);
        board = area;

        // About a finger per fret, whatever the screen.
        numFrets = juce::jlimit (8, 24, board.getWidth() / 48);
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

    // The finger stays on its string: along it slides, across it bends.
    void mouseDrag (const juce::MouseEvent& e) override
    {
        auto& t = touches[(size_t) (e.source.getIndex() % (int) touches.size())];
        if (t.string < 0 || t.fret == Nut)
            return;
        t.pos = fretPosition (e.position.x);
        t.y = e.position.y;
        followFinger (t);
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
    static constexpr int MinStrings = 4, MaxStrings = 16;   // one channel each
    static constexpr int Nut = -2;
    static constexpr int LowestLow = 24, HighestLow = 72;

    struct Tuning { const char* name; int step; };   // semitones between strings
    static const std::vector<Tuning>& tunings()
    {
        static const std::vector<Tuning> t {
            { "Whole steps", 2 },
            { "Minor 3rds",  3 },
            { "Major 3rds",  4 },
            { "4ths (bass)", 5 },
            { "5ths (cello)", 7 },
        };
        return t;
    }

    // How far a full bend (one string's width across) takes the note.
    static juce::StringArray bendNames() { return { "No bend", "Bend 1/2", "Bend 1", "Bend 1 1/2", "Bend 2" }; }
    static float bendSemis (int i) { static const float s[] = { 0.0f, 1.0f, 2.0f, 3.0f, 4.0f }; return s[juce::jlimit (0, 4, i)]; }

    // A finger: the fret it came down on (the note it plays), where it is now
    // along the string (in frets, 0 = the nut end of the first fret) and how
    // far it has moved across it.
    struct Touch
    {
        int string = -1, fret = -1;
        float startPos = 0.0f, pos = 0.0f, startY = 0.0f, y = 0.0f;
    };

    int noteAt (int s, int f) const
    {
        return juce::jlimit (0, 127, low + s * tunings()[(size_t) tuning].step + f);
    }

    float rowHeight() const { return board.getHeight() / (float) strings; }
    float fretWidth() const { return board.getWidth() / (float) numFrets; }
    float stringY (int s) const { return board.getBottom() - (s + 0.5f) * rowHeight(); }
    float fretPosition (float x) const { return juce::jlimit (0.0f, numFrets - 0.001f, (x - board.getX()) / fretWidth()); }

    juce::Rectangle<float> cellBounds (int s, int f) const
    {
        const float rowH = rowHeight(), fretW = fretWidth();
        return { board.getX() + f * fretW, board.getBottom() - (s + 1) * rowH, fretW, rowH };
    }

    Touch hit (juce::Point<float> p) const
    {
        const auto all = nut.getUnion (board).toFloat();
        if (! all.contains (p) || board.isEmpty())
            return {};
        Touch t;
        t.string = juce::jlimit (0, strings - 1, (int) ((board.getBottom() - p.y) / rowHeight()));
        t.startPos = t.pos = fretPosition (p.x);
        t.fret = p.x < board.getX() ? Nut : (int) t.pos;
        t.startY = t.y = p.y;
        return t;
    }

    // The finger that plays string s: its highest fret down.
    const Touch* driver (int s) const
    {
        const Touch* d = nullptr;
        for (const auto& t : touches)
            if (t.string == s && t.fret >= 0 && (d == nullptr || t.fret > d->fret))
                d = &t;
        return d;
    }

    // How far the finger has taken its note, in semitones: the slide along
    // the string plus the bend across it.
    float offset (const Touch& t) const
    {
        const float slide = smooth ? t.pos - t.startPos : (float) ((int) t.pos - t.fret);
        const float across = std::abs (t.y - t.startY) / juce::jmax (36.0f, rowHeight());
        return slide + bendSemis (bend) * juce::jmin (1.0f, across);
    }

    // The fret lit on string s: under the finger playing it, or where a
    // ringing string was let go.
    int litFret (int s) const
    {
        if (const auto* t = driver (s))
            return (int) t->pos;
        return playing[(size_t) s] >= 0 ? shownFret[(size_t) s] : -1;
    }

    void sendBend (int s, float semis)
    {
        if (std::abs (semis - lastBend[(size_t) s]) < 0.004f)
            return;
        rack.postPitchBend (track, 1 + s, semis);
        lastBend[(size_t) s] = semis;
    }

    void followFinger (const Touch& t)
    {
        if (driver (t.string) != &t || playing[(size_t) t.string] < 0)
            return;   // a lower finger under a hammer-on just waits
        sendBend (t.string, offset (t));
        shownFret[(size_t) t.string] = (int) t.pos;
        animate();
    }

    // Sounds a string's highest held fret, as a real string would: higher
    // than what's sounding hammers on, lower pulls off (a little softer).
    // With nothing held it stops, unless RING.
    void updateString (int s)
    {
        if (! juce::isPositiveAndBelow (s, MaxStrings))
            return;
        const auto* d = driver (s);
        const int target = d != nullptr ? d->fret : -1;

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
        sendBend (s, offset (*d));   // before the note, which starts at the channel's bend
        const int note = noteAt (s, target);
        rack.postNoteOn (track, 1 + s, note, velocity);
        playing[(size_t) s] = note;
        shownFret[(size_t) s] = (int) d->pos;
        startedAt[(size_t) s] = juce::Time::getMillisecondCounterHiRes();
        animate();
    }

    void stop (int s)
    {
        if (playing[(size_t) s] >= 0)
            rack.postNoteOff (track, 1 + s, playing[(size_t) s]);
        sendBend (s, 0.0f);   // anything else on the channel plays in tune
        playing[(size_t) s] = -1;
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

    void setLayout (int newLow, int newTuning, int newStrings)
    {
        allOff();   // the notes under the fingers change
        low = newLow;
        tuning = newTuning;
        strings = juce::jlimit (MinStrings, MaxStrings, newStrings);
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
        stringsLabel.setText (juce::String (strings) + " STRINGS", juce::dontSendNotification);
        stringsDown.setEnabled (strings > MinStrings);
        stringsUp.setEnabled (strings < MaxStrings);
        slideCombo.setSelectedId (smooth ? 2 : 1, juce::dontSendNotification);
        bendCombo.setSelectedId (bend + 1, juce::dontSendNotification);
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

    int low = 36, tuning = 0, strings = 8, bend = 2, vel = 1;
    bool smooth = false, ring = false;
    int numFrets = 12;
    juce::Rectangle<int> board, nut;

    std::array<Touch, 10> touches {};
    std::array<int, MaxStrings> playing {};     // the note each string sounds, or -1
    std::array<int, MaxStrings> heldFret {};    // the fret its note came from while held, or -1 (RING: may still sound)
    std::array<int, MaxStrings> shownFret {};   // where it's sounding now, for the board
    std::array<float, MaxStrings> lastBend {};  // what each string's channel was last bent to
    std::array<double, MaxStrings> startedAt {};
    int seenAllOffs = 0;

    SynthLookAndFeel laf;   // declared before the children
    juce::ComboBox lowCombo, tuningCombo, slideCombo, bendCombo, velCombo;
    juce::TextButton stringsDown { "-" }, stringsUp { "+" };
    juce::Label stringsLabel;
    juce::TextButton ringButton { "RING" };
};
