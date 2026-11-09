#pragma once

#include <JuceHeader.h>
#include "Glassmorphism.h"
#include "SynthLookAndFeel.h"
#include "TrackRack.h"
#include "TrackRail.h"
#include "PopupStyle.h"
#include <deque>

//==============================================================================
// Ripple — a pond of notes where distance is time.
//
// The stones are laid out as a Tonnetz: one step right is a fifth, up-right a
// major third, up-left a minor third, so every little triangle of stones is a
// major or minor triad and neighbours always agree. Higher up the pond, the
// stones sit an octave or two higher.
//
// Touch the water and a ring spreads out from your finger in time with the
// tempo, one stone-width per step (SPEED). Each stone plays as the ring
// reaches it, softer the further it travels, until the ring dies at REACH.
// So where you touch is what you play and when: on a stone, that note first
// and then its harmonic neighbours cascading out; in the middle of a
// triangle, that triad as one chord, then the next ring of related notes;
// between two stones, a two-note figure. Keep a finger down and it pulses a
// new ring every PULSE; drag it and the figure morphs as it goes. When two
// rings reach a stone together, it chimes an octave up.
//
// Every note is worked out when its ring starts and handed to the rack with
// its time (TrackRack::postNoteOnAt), so the figures sit on the tempo however
// the screen is doing. STOP (an all-notes-off on the track) clears the pond.
class RipplePage : public juce::Component,
                   private juce::Timer
{
public:
    RipplePage (TrackRack& r, int trackIndex, std::function<double()> bpmSource)
        : rack (r), track (trackIndex), bpm (std::move (bpmSource))
    {
        setLookAndFeel (&laf);

        for (int k = 0; k < 12; ++k)
            keyCombo.addItem (juce::MidiMessage::getMidiNoteName (k, true, false, 4), k + 1);
        keyCombo.onChange = [this] { key = keyCombo.getSelectedId() - 1; layoutStones(); changed(); };

        for (int i = 0; i < (int) scales().size(); ++i)
            scaleCombo.addItem (scales()[(size_t) i].name, i + 1);
        scaleCombo.onChange = [this] { scale = scaleCombo.getSelectedId() - 1; layoutStones(); changed(); };

        static const char* speedNames[] = { "1/32", "1/16", "1/8T", "1/8", "1/4" };
        for (int i = 0; i < 5; ++i) speedCombo.addItem (speedNames[i], i + 1);
        speedCombo.onChange = [this] { speed = speedCombo.getSelectedId() - 1; changed(); };

        static const char* pulseNames[] = { "No pulse", "Pulse 1/4", "Pulse 1/2", "Pulse 1 bar", "Pulse 2 bars" };
        for (int i = 0; i < 5; ++i) pulseCombo.addItem (pulseNames[i], i + 1);
        pulseCombo.onChange = [this] { pulse = pulseCombo.getSelectedId() - 1; changed(); };

        static const char* gateNames[] = { "Short", "Step", "Long", "Ring" };
        for (int i = 0; i < 4; ++i) gateCombo.addItem (gateNames[i], i + 1);
        gateCombo.onChange = [this] { gate = gateCombo.getSelectedId() - 1; changed(); };

        reachDown.onClick = [this] { reach = juce::jmax (1, reach - 1); refresh(); changed(); };
        reachUp.onClick   = [this] { reach = juce::jmin (MaxReach, reach + 1); refresh(); changed(); };
        octDown.onClick   = [this] { octave = juce::jmax (0, octave - 1); layoutStones(); refresh(); changed(); };
        octUp.onClick     = [this] { octave = juce::jmin (3, octave + 1); layoutStones(); refresh(); changed(); };

        for (auto* l : { &reachLabel, &octLabel })
        {
            l->setJustificationType (juce::Justification::centred);
            l->setFont (juce::FontOptions (12.0f, juce::Font::bold));
            l->setColour (juce::Label::textColourId, MidiMorfColors::textLight);
        }

        for (auto* c : std::initializer_list<juce::Component*> { &keyCombo, &scaleCombo, &speedCombo, &pulseCombo, &gateCombo,
                                                                 &reachDown, &reachUp, &reachLabel, &octDown, &octUp, &octLabel })
            addAndMakeVisible (c);

        seenAllOffs = rack.getAllOffCount (track);
        refresh();
    }

    ~RipplePage() override
    {
        stopTimer();
        setLookAndFeel (nullptr);
    }

    std::function<void()> onStateChanged;   // persist

    //==========================================================================
    void saveState (juce::PropertySet& p) const
    {
        p.setValue ("ripple_key", key);
        p.setValue ("ripple_scale", scale);
        p.setValue ("ripple_speed", speed);
        p.setValue ("ripple_pulse", pulse);
        p.setValue ("ripple_gate", gate);
        p.setValue ("ripple_reach", reach);
        p.setValue ("ripple_octave", octave);
    }

    void restoreState (juce::PropertySet& p)
    {
        key    = juce::jlimit (0, 11, p.getIntValue ("ripple_key", 0));
        scale  = juce::jlimit (0, (int) scales().size() - 1, p.getIntValue ("ripple_scale", 1));
        speed  = juce::jlimit (0, 4, p.getIntValue ("ripple_speed", 1));
        pulse  = juce::jlimit (0, 4, p.getIntValue ("ripple_pulse", 2));
        gate   = juce::jlimit (0, 3, p.getIntValue ("ripple_gate", 1));
        reach  = juce::jlimit (1, MaxReach, p.getIntValue ("ripple_reach", 3));
        octave = juce::jlimit (0, 3, p.getIntValue ("ripple_octave", 1));
        layoutStones();
        refresh();
    }

    void visibilityChanged() override
    {
        // The pond keeps playing what it has scheduled when it's not shown;
        // a held finger can't be, so its pulse stops.
        if (! isVisible())
            for (auto& f : fingers) f.down = false;
        updateTimer();
    }

    //==========================================================================
    void paint (juce::Graphics& g) override
    {
        Glassmorphism::paintPanel (g, getLocalBounds().toFloat(), ripplePalette);
        g.setColour (juce::Colours::black.withAlpha (0.25f));
        g.fillRoundedRectangle (pond.toFloat(), 10.0f);

        const double now = juce::Time::getMillisecondCounterHiRes();
        const auto colour = TrackColours::get (track);
        const float s = spacing;

        g.saveState();
        g.reduceClipRegion (pond);

        // Rings.
        for (const auto& r : ripples)
        {
            const float radius = (float) ((now - r.start) / stepMs()) * s;
            if (radius < 0.0f) continue;
            const float life = 1.0f - radius / (reach * s + s * 0.5f);
            if (life <= 0.0f) continue;
            g.setColour (colour.brighter (0.6f).withAlpha (0.55f * life * r.energy));
            g.drawEllipse (r.origin.x - radius, r.origin.y - radius, radius * 2.0f, radius * 2.0f, 1.5f + 2.5f * life);
        }

        // Stones: lit as their notes play.
        g.setFont (juce::FontOptions (juce::jlimit (10.0f, 16.0f, s * 0.22f), juce::Font::bold));
        for (const auto& st : stones)
        {
            const float rad = s * 0.3f;
            auto b = juce::Rectangle<float> (rad * 2.0f, rad * 2.0f).withCentre (st.at);
            float flash = 0.0f;
            if (st.lastHit > 0.0 && now >= st.lastHit)
                flash = (float) juce::jmax (0.0, 1.0 - (now - st.lastHit) / 300.0);

            if (! st.inScale)
            {
                g.setColour (MidiMorfColors::textLight.withAlpha (0.08f));
                g.drawEllipse (b.reduced (rad * 0.35f), 1.0f);
                continue;
            }

            const bool root = st.pc == key;
            g.setColour (colour.withAlpha (0.18f + 0.25f * (root ? 1.0f : 0.0f) + 0.55f * flash));
            g.fillEllipse (b.expanded (flash * rad * 0.35f));
            g.setColour ((st.chimed ? juce::Colours::white : colour.brighter (0.4f)).withAlpha (0.5f + 0.5f * flash));
            g.drawEllipse (b, root ? 2.0f : 1.0f);
            g.setColour (MidiMorfColors::textLight.withAlpha (0.65f + 0.35f * flash));
            g.drawText (juce::MidiMessage::getMidiNoteName (st.note, true, false, 4), b, juce::Justification::centred);
        }

        // Fingers holding the water.
        for (const auto& f : fingers)
            if (f.down)
            {
                g.setColour (juce::Colours::white.withAlpha (0.7f));
                g.drawEllipse (f.at.x - 18.0f, f.at.y - 18.0f, 36.0f, 36.0f, 2.0f);
            }

        g.restoreState();

        if (ripples.empty() && ! anyFingerDown())
        {
            g.setColour (MidiMorfColors::textLight.withAlpha (0.35f));
            g.setFont (juce::FontOptions (12.0f));
            g.drawText ("Touch the water. Distance is time.", pond.withTop (pond.getBottom() - 24).reduced (12, 0), juce::Justification::centredRight);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (10, 8);
        auto bar = area.removeFromTop (34);
        auto place = [&bar] (juce::Component& c, int w) { c.setBounds (bar.removeFromLeft (w).reduced (2)); };
        place (keyCombo, 64);
        place (scaleCombo, 116);
        place (speedCombo, 72);
        place (pulseCombo, 112);
        place (gateCombo, 76);
        bar.removeFromLeft (6);
        place (reachDown, 30); place (reachLabel, 74); place (reachUp, 30);
        bar.removeFromLeft (6);
        place (octDown, 30); place (octLabel, 60); place (octUp, 30);

        area.removeFromTop (6);
        pond = area;
        layoutStones();
    }

    //==========================================================================
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! pond.contains (e.position.toInt()))
            return;
        auto& f = fingers[(size_t) (e.source.getIndex() % (int) fingers.size())];
        f.down = true;
        f.at = e.position;
        const double now = juce::Time::getMillisecondCounterHiRes();
        emit (f.at, now, 0.95f);
        f.nextPulse = now + pulseMs();
        postDue (now);   // the touched stone sounds now, not on the next tick
        updateTimer();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        auto& f = fingers[(size_t) (e.source.getIndex() % (int) fingers.size())];
        if (f.down)
            f.at = pond.toFloat().getConstrainedPoint (e.position);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        fingers[(size_t) (e.source.getIndex() % (int) fingers.size())].down = false;
    }

private:
    static constexpr int MaxReach = 6;
    static constexpr int Channel = 2;
    static constexpr double LookAheadMs = 60.0;   // notes go to the rack this far ahead

    struct Scale { const char* name; int mask; };   // bit n: n semitones above the key
    static const std::vector<Scale>& scales()
    {
        static const std::vector<Scale> s {
            { "Chromatic",  0xFFF },
            { "Major",      0b101010110101 },
            { "Minor",      0b010110101101 },
            { "Dorian",     0b011010101101 },
            { "Pentatonic", 0b001010010101 },
            { "Minor pent", 0b010010101001 },
            { "Lydian",     0b101011010101 },
        };
        return s;
    }

    struct Stone
    {
        juce::Point<float> at;
        int pc = 0, note = 60;
        bool inScale = true, chimed = false;
        double lastHit = 0.0;   // when its latest note plays (may be ahead)
        int lastRipple = 0;
    };

    struct Ripple
    {
        juce::Point<float> origin;
        double start = 0.0;
        float energy = 1.0f;
        int id = 0;
    };

    // A note-off carries when its note started, so a later note on the same
    // pitch knows whether they overlap.
    struct Pending { double due; bool on; int note, velocity; double onAt = 0.0; };

    struct Finger
    {
        bool down = false;
        juce::Point<float> at;
        double nextPulse = 0.0;
    };

    //==========================================================================
    // Timing, in ms at the current tempo.
    double beatMs() const   { return 60000.0 / juce::jlimit (20.0, 300.0, bpm ? bpm() : 120.0); }
    double stepMs() const
    {
        static const double beats[] = { 0.125, 0.25, 1.0 / 6.0, 0.5, 1.0 };
        return beatMs() * beats[juce::jlimit (0, 4, speed)];
    }
    double pulseMs() const
    {
        static const double beats[] = { 0.0, 1.0, 2.0, 4.0, 8.0 };
        return beatMs() * beats[juce::jlimit (0, 4, pulse)];
    }
    double gateMs() const
    {
        static const double steps[] = { 0.4, 0.9, 2.0, 6.0 };
        return stepMs() * steps[juce::jlimit (0, 3, gate)];
    }

    // The Tonnetz across the pond: a row of fifths, each row up a major third
    // and half a stone to the right. Each stone takes the octave of its pitch
    // class nearest to where it sits, from low at the bottom to high at the
    // top.
    void layoutStones()
    {
        stones.clear();
        if (pond.isEmpty())
            return;

        spacing = juce::jlimit (48.0f, 96.0f, pond.getWidth() / 9.5f);
        const float rowH = spacing * 0.8660254f;
        const int rows = juce::jmax (2, (int) ((pond.getHeight() - spacing * 0.6f) / rowH) + 1);
        const float y0 = pond.getBottom() - (pond.getHeight() - (rows - 1) * rowH) * 0.5f;
        const int low = 36 + 12 * octave, span = 26;
        const int mask = scales()[(size_t) juce::jlimit (0, (int) scales().size() - 1, scale)].mask;

        // Odd rows sit half a stone to the right: a brick pattern keeps the
        // pond square while every stone keeps six neighbours one stone away.
        const int cols = juce::jmax (2, (int) ((pond.getWidth() - spacing * 1.1f) / spacing) + 1);
        const float x0 = pond.getX() + (pond.getWidth() - (cols - 1) * spacing - spacing * 0.5f) * 0.5f;
        for (int j = 0; j < rows; ++j)
        {
            const float y = y0 - j * rowH;
            const float shift = (float) (j % 2) * 0.5f * spacing;
            for (int c = 0; c < cols; ++c)
            {
                // Axial coordinates: i along the fifths, j up the thirds.
                const int i = c - j / 2;
                Stone st;
                st.at = { x0 + shift + c * spacing, y };
                st.pc = ((key + 7 * i + 4 * j) % 12 + 12) % 12;
                st.inScale = (mask >> ((st.pc - key + 12) % 12)) & 1;
                const float target = low + span * (1.0f - (y - pond.getY()) / (float) pond.getHeight());
                st.note = st.pc + 12 * juce::roundToInt ((target - st.pc) / 12.0f);
                st.note = juce::jlimit (0, 127, st.note);
                stones.push_back (st);
            }
        }
        repaint();
    }

    // A ring from `at`, starting at `start`: every stone it will reach is
    // scheduled now.
    void emit (juce::Point<float> at, double start, float energy)
    {
        const int id = ++rippleIds;
        ripples.push_back ({ at, start, energy, id });
        while (ripples.size() > 24)
            ripples.erase (ripples.begin());

        const double step = stepMs();
        for (auto& st : stones)
        {
            if (! st.inScale)
                continue;
            const float d = st.at.getDistanceFrom (at) / spacing;
            if (d > (float) reach)
                continue;

            const double due = start + d * step;
            const float level = energy * (1.0f - 0.7f * d / (float) reach);
            int note = st.note;
            int velocity = juce::jlimit (12, 127, juce::roundToInt (level * 127.0f));

            // Another ring got here at the same moment: chime an octave up
            // instead of playing the note twice.
            const bool together = st.lastHit > 0.0 && std::abs (st.lastHit - due) < 35.0 && st.lastRipple != id;
            st.chimed = together;
            if (together)
            {
                note = juce::jmin (127, note + 12);
                velocity = juce::jmin (127, velocity + 30);
            }
            st.lastHit = due;
            st.lastRipple = id;
            schedule (due, note, velocity);
        }
        repaint();
    }

    // A note is kept here until it's due within the look-ahead, so a note
    // that comes round again before its last one ended can cut it short.
    void schedule (double due, int note, int velocity)
    {
        // One pitch never overlaps itself: a note already sounding ends just
        // before this one starts; one starting later cuts this one short.
        if (postedOffAt[(size_t) note] > due - 1.0)
            due = postedOffAt[(size_t) note] + 1.0;   // its end is already with the rack
        double off = due + gateMs();

        for (auto& p : pending)
        {
            if (p.on || p.note != note || p.due <= due - 1.0 || p.onAt >= off)
                continue;   // a different note, or no overlap
            if (p.onAt <= due) p.due = juce::jmax (p.onAt + 1.0, due - 1.0);
            else               off = juce::jmin (off, p.onAt - 1.0);
        }
        if (off < due + 5.0)
            return;   // squeezed out between two others

        insert ({ due, true, note, velocity });
        insert ({ off, false, note, 0, due });
    }

    void insert (const Pending& p)
    {
        auto it = std::upper_bound (pending.begin(), pending.end(), p, [] (const Pending& a, const Pending& b) { return a.due < b.due; });
        pending.insert (it, p);
    }

    //==========================================================================
    void timerCallback() override
    {
        const double now = juce::Time::getMillisecondCounterHiRes();

        // STOP or panic on this track: the pond goes still.
        if (const int n = rack.getAllOffCount (track); n != seenAllOffs)
        {
            seenAllOffs = n;
            pending.clear();
            ripples.clear();
            postedOffAt.fill (0.0);
            for (auto& st : stones) st.lastHit = 0.0;
            for (auto& f : fingers) f.down = false;
        }

        // Held fingers pulse, a look-ahead early so the ring starts on time.
        if (pulse > 0)
            for (auto& f : fingers)
                if (f.down && now >= f.nextPulse - LookAheadMs)
                {
                    emit (f.at, f.nextPulse, 0.7f);
                    f.nextPulse += pulseMs();
                }

        postDue (now);

        // Rings that have died.
        const double life = (reach + 0.5) * stepMs();
        while (! ripples.empty() && now - ripples.front().start > life)
            ripples.erase (ripples.begin());

        if (++frame % 2 == 0 && isShowing())
            repaint();
        updateTimer();
    }

    // Hands the rack what's due soon, re-sorted since cut-short note-offs may
    // have moved.
    void postDue (double now)
    {
        std::stable_sort (pending.begin(), pending.end(), [] (const Pending& a, const Pending& b) { return a.due < b.due; });
        while (! pending.empty() && pending.front().due <= now + LookAheadMs)
        {
            const auto p = pending.front();
            pending.pop_front();
            if (p.on) rack.postNoteOnAt (p.due, track, Channel, p.note, p.velocity);
            else
            {
                rack.postNoteOffAt (p.due, track, Channel, p.note);
                postedOffAt[(size_t) p.note] = p.due;
            }
        }
    }

    bool anyFingerDown() const { return std::any_of (fingers.begin(), fingers.end(), [] (const Finger& f) { return f.down; }); }

    // Runs while there's anything to play or show.
    void updateTimer()
    {
        const bool busy = ! pending.empty() || ! ripples.empty() || anyFingerDown();
        if (busy && ! isTimerRunning())      startTimerHz (120);
        else if (! busy && isTimerRunning()) { stopTimer(); repaint(); }
    }

    void refresh()
    {
        keyCombo.setSelectedId (key + 1, juce::dontSendNotification);
        scaleCombo.setSelectedId (scale + 1, juce::dontSendNotification);
        speedCombo.setSelectedId (speed + 1, juce::dontSendNotification);
        pulseCombo.setSelectedId (pulse + 1, juce::dontSendNotification);
        gateCombo.setSelectedId (gate + 1, juce::dontSendNotification);
        reachLabel.setText ("REACH " + juce::String (reach), juce::dontSendNotification);
        octLabel.setText ("OCT " + juce::String (octave + 2), juce::dontSendNotification);
    }

    void changed() { if (onStateChanged) onStateChanged(); }

    static inline const PagePalette ripplePalette {
        juce::Colour (0xff0E2226), juce::Colour (0xff040C0E),
        juce::Colour (0xCC12303A), juce::Colour (0xD8081A1F), juce::Colour (0xCC000000)
    };

    TrackRack& rack;
    const int track;
    std::function<double()> bpm;

    int key = 0, scale = 1, speed = 1, pulse = 2, gate = 1, reach = 3, octave = 1;

    std::vector<Stone> stones;
    std::vector<Ripple> ripples;
    std::deque<Pending> pending;
    std::array<double, 128> postedOffAt {};
    std::array<Finger, 10> fingers {};
    float spacing = 64.0f;
    int rippleIds = 0, seenAllOffs = 0, frame = 0;
    juce::Rectangle<int> pond;

    SynthLookAndFeel laf;   // declared before the children
    juce::ComboBox keyCombo, scaleCombo, speedCombo, pulseCombo, gateCombo;
    juce::TextButton reachDown { "-" }, reachUp { "+" }, octDown { "-" }, octUp { "+" };
    juce::Label reachLabel, octLabel;
};
