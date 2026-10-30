#pragma once

#include <JuceHeader.h>
#include "Glassmorphism.h"
#include "SynthLookAndFeel.h"
#include "LocalSequencers.h"
#include "BreakLibrary.h"
#include "TrackRail.h"

//==============================================================================
// Drum Grid: eight lanes (the drum synth's drums, on their GM notes) by 16
// steps, each step with its own velocity. It always runs on the phone's clock.
//
//   • top      — PLAY, tempo, swing, GRID / EUCLID, "Load a beat", CLEAR
//   • the grid — tap a lane's name to hear it. Tap a step to switch it on or
//                off; drag up or down on a lit step for its velocity; slide
//                along a row to paint hits.
//   • EUCLID   — each lane gets HITS and ROTATE, spread as evenly as 16 steps
//                allow. They write the lane, so you can still tap after.
//
// The page owns the pattern (and saves it); LocalSequencers plays a copy.
class DrumGridPage : public juce::Component,
                     private juce::Timer
{
public:
    static constexpr int Lanes = LocalSequencers::DrumLanes, Steps = LocalSequencers::DrumSteps;

    explicit DrumGridPage (LocalSequencers& s) : seqs (s)
    {
        setLookAndFeel (&laf);

        playButton.setClickingTogglesState (true);
        playButton.onClick = [this] { seqs.setPlaying (LocalSequencers::Drums, playButton.getToggleState()); refreshPlay(); };
        addAndMakeVisible (playButton);

        bpmDown.onClick = [this] { seqs.setBpm (std::round (seqs.getBpm()) - 1.0); changed(); };
        bpmUp.onClick   = [this] { seqs.setBpm (std::round (seqs.getBpm()) + 1.0); changed(); };
        bpmLabel.setJustificationType (juce::Justification::centred);
        bpmLabel.setColour (juce::Label::textColourId, MidiMorfColors::textLight);
        for (auto* c : std::initializer_list<juce::Component*> { &bpmDown, &bpmUp, &bpmLabel })
            addAndMakeVisible (c);

        swing.setSliderStyle (juce::Slider::LinearHorizontal);
        swing.setTextBoxStyle (juce::Slider::TextBoxRight, false, 44, 20);
        swing.setRange (0.0, 100.0, 1.0);
        swing.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v)) + "%"; };
        swing.onValueChange = [this] { seqs.setDrumSwing ((float) swing.getValue() / 100.0f); };
        swing.onDragEnd = [this] { changed(); };
        addAndMakeVisible (swing);
        swingLabel.setText ("SWING", juce::dontSendNotification);
        swingLabel.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        swingLabel.setColour (juce::Label::textColourId, MidiMorfColors::textLight.withAlpha (0.6f));
        addAndMakeVisible (swingLabel);

        for (int m = 0; m < 2; ++m)
        {
            auto* b = modeButtons.add (new juce::TextButton (m == 0 ? "GRID" : "EUCLID"));
            b->setClickingTogglesState (true);
            b->setRadioGroupId (6161);
            b->onClick = [this, m]
            {
                euclidMode = (m == 1);
                // A lane that already has a beat starts from its own count, so
                // the first + adds a hit rather than starting over.
                if (euclidMode)
                    for (int l = 0; l < Lanes; ++l)
                        if (euclid[(size_t) l].hits == 0)
                            euclid[(size_t) l].hits = (int) std::count_if (pattern[(size_t) l].begin(), pattern[(size_t) l].end(),
                                                                           [] (uint8_t v) { return v > 0; });
                resized();
                repaint();
            };
            addAndMakeVisible (b);
        }
        modeButtons[0]->setToggleState (true, juce::dontSendNotification);

        beatCombo.setTextWhenNothingSelected ("Load a beat");
        int id = 1;
        for (auto& st : BreakLibrary::styles())
            beatCombo.addItem (st.name, id++);
        beatCombo.onChange = [this]
        {
            if (beatCombo.getSelectedId() > 0)
                loadStyle (BreakLibrary::styles()[(size_t) beatCombo.getSelectedId() - 1]);
            beatCombo.setSelectedId (0, juce::dontSendNotification);
        };
        addAndMakeVisible (beatCombo);

        clearButton.onClick = [this]
        {
            for (int l = 0; l < Lanes; ++l)
            {
                euclid[(size_t) l] = {};
                for (int st = 0; st < Steps; ++st)
                    setStep (l, st, 0);
            }
            changed();
            repaint();
        };
        addAndMakeVisible (clearButton);

        // A beat to start from, until the saved one is restored.
        loadStyle (BreakLibrary::styles()[1], false);
        refreshBpm();

        startTimerHz (30);
    }

    ~DrumGridPage() override
    {
        stopTimer();
        setLookAndFeel (nullptr);
    }

    std::function<void (int note, bool down)> onAudition;   // a lane name held
    std::function<void()> onStateChanged;                   // persist

    void setTrack (int t) { track = t; repaint(); }

    //==========================================================================
    void saveState (juce::PropertySet& p) const
    {
        juce::String hex;
        for (auto& lane : pattern)
            for (auto v : lane)
                hex << juce::String::toHexString ((int) v).paddedLeft ('0', 2);
        p.setValue ("drum_pattern", hex);
        p.setValue ("drum_swing", swing.getValue());

        juce::String e;
        for (auto& lane : euclid)
            e << lane.hits << "," << lane.rotate << ";";
        p.setValue ("drum_euclid", e);
    }

    void restoreState (juce::PropertySet& p)
    {
        const auto hex = p.getValue ("drum_pattern");
        if (hex.length() == Lanes * Steps * 2)
            for (int l = 0; l < Lanes; ++l)
                for (int st = 0; st < Steps; ++st)
                    setStep (l, st, hex.substring ((l * Steps + st) * 2, (l * Steps + st) * 2 + 2).getHexValue32());

        if (p.containsKey ("drum_swing"))
            swing.setValue (p.getDoubleValue ("drum_swing"), juce::sendNotificationSync);

        auto lanes = juce::StringArray::fromTokens (p.getValue ("drum_euclid"), ";", "");
        for (int l = 0; l < Lanes && l < lanes.size(); ++l)
        {
            auto v = juce::StringArray::fromTokens (lanes[l], ",", "");
            if (v.size() == 2)
                euclid[(size_t) l] = { juce::jlimit (0, Steps, v[0].getIntValue()), juce::jlimit (0, Steps - 1, v[1].getIntValue()) };
        }
        repaint();
    }

    //==========================================================================
    void paint (juce::Graphics& g) override
    {
        Glassmorphism::paintPanel (g, getLocalBounds().toFloat(), drumGridPagePalette);

        const auto colour = TrackColours::get (track);
        for (int l = 0; l < Lanes; ++l)
        {
            // The lane's name: tap to hear it.
            auto name = laneNameArea (l).toFloat();
            g.setColour (juce::Colours::black.withAlpha (heldLane == l ? 0.15f : 0.35f));
            g.fillRoundedRectangle (name.reduced (1.0f), 5.0f);
            g.setColour (heldLane == l ? colour : MidiMorfColors::textLight.withAlpha (0.85f));
            g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
            g.drawFittedText (::Drums::shortNames()[l], name.toNearestInt().reduced (4, 0), juce::Justification::centred, 1);

            if (euclidMode)
                paintEuclidControls (g, l);

            for (int st = 0; st < Steps; ++st)
            {
                auto cell = cellArea (l, st).toFloat().reduced (1.5f);
                const bool beatStart = (st / 4) % 2 == 0;
                g.setColour (juce::Colours::black.withAlpha (beatStart ? 0.42f : 0.28f));
                g.fillRoundedRectangle (cell, 4.0f);

                if (const int v = pattern[(size_t) l][(size_t) st]; v > 0)
                {
                    const float amount = (float) v / 127.0f;
                    g.setColour (colour.withAlpha (0.35f + 0.25f * amount));
                    g.fillRoundedRectangle (cell, 4.0f);
                    // Velocity as a bar from the bottom.
                    g.setColour (colour.brighter (0.25f));
                    g.fillRoundedRectangle (cell.withTrimmedTop (cell.getHeight() * (1.0f - amount)), 4.0f);
                }

                if (st == playhead)
                {
                    g.setColour (juce::Colour (0xffFFC24A).withAlpha (0.85f));
                    g.drawRoundedRectangle (cell, 4.0f, 1.6f);
                }
            }
        }
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (8, 6);
        auto top = area.removeFromTop (34);

        playButton.setBounds (top.removeFromLeft (66).reduced (2));
        top.removeFromLeft (6);
        bpmDown.setBounds (top.removeFromLeft (30).reduced (2));
        bpmLabel.setBounds (top.removeFromLeft (58));
        bpmUp.setBounds (top.removeFromLeft (30).reduced (2));
        top.removeFromLeft (8);

        clearButton.setBounds (top.removeFromRight (64).reduced (2));
        beatCombo.setBounds (top.removeFromRight (juce::jmin (150, top.getWidth() / 4)).reduced (2));
        for (int m = 1; m >= 0; --m)
            modeButtons[m]->setBounds (top.removeFromRight (m == 1 ? 70 : 54).reduced (2));
        top.removeFromRight (6);
        swingLabel.setBounds (top.removeFromLeft (48));
        swing.setBounds (top.reduced (0, 4));

        area.removeFromTop (6);
        gridArea = area;
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto p = e.getPosition();
        drag = {};

        for (int l = 0; l < Lanes; ++l)
        {
            if (laneNameArea (l).contains (p))
            {
                heldLane = l;
                if (onAudition) onAudition (::Drums::noteFor (l), true);
                repaint();
                return;
            }
            if (euclidMode && euclidClick (l, p))
                return;
        }

        const auto [lane, step] = cellAt (p);
        if (lane < 0)
            return;

        drag.lane = lane;
        drag.startVelocity = pattern[(size_t) lane][(size_t) step];
        drag.startStep = step;
        if (drag.startVelocity == 0)
        {
            drag.painting = true;   // switched on: sliding along the row adds more
            setStep (lane, step, DefaultVelocity);
        }
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (drag.lane < 0)
            return;

        if (drag.painting)
        {
            const int step = stepAtX (e.x);
            if (step >= 0 && pattern[(size_t) drag.lane][(size_t) step] == 0)
            {
                setStep (drag.lane, step, DefaultVelocity);
                repaint();
            }
            return;
        }

        // On a lit step: up and down set its velocity.
        const int dy = e.getDistanceFromDragStartY();
        if (! drag.moved && std::abs (dy) < 6)
            return;
        drag.moved = true;
        setStep (drag.lane, drag.startStep, juce::jlimit (1, 127, drag.startVelocity - dy));
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (heldLane >= 0)
        {
            if (onAudition) onAudition (::Drums::noteFor (heldLane), false);
            heldLane = -1;
            repaint();
            return;
        }

        if (drag.lane >= 0)
        {
            if (! drag.painting && ! drag.moved)
                setStep (drag.lane, drag.startStep, 0);   // a tap on a lit step clears it
            drag = {};
            changed();
            repaint();
        }
    }

private:
    static constexpr int DefaultVelocity = 100;

    static inline const PagePalette drumGridPagePalette {
        juce::Colour (0xff2B1A12), juce::Colour (0xff0E0805),
        juce::Colour (0xCC3D2418), juce::Colour (0xD8201309), juce::Colour (0xCC000000)
    };

    struct Euclid { int hits = 0, rotate = 0; };
    struct Drag { int lane = -1, startStep = 0, startVelocity = 0; bool painting = false, moved = false; };

    void changed() { refreshBpm(); if (onStateChanged) onStateChanged(); }

    void setStep (int lane, int step, int velocity)
    {
        pattern[(size_t) lane][(size_t) step] = (uint8_t) velocity;
        seqs.setDrumStep (lane, step, velocity);
    }

    // One of the built-in breaks' patterns, with its tempo and swing.
    void loadStyle (const BreakLibrary::Style& st, bool notify = true)
    {
        // Break lanes -> drum lanes. The ride, where there is one, plays the
        // closed-hat lane.
        const char* lanes[Lanes] = { st.kick, st.snare, st.clap, st.hat, st.openHat, "", "", "" };
        if ((st.hat == nullptr || *st.hat == 0) && st.ride != nullptr)
            lanes[::Drums::ClosedHat] = st.ride;

        for (int l = 0; l < Lanes; ++l)
        {
            euclid[(size_t) l] = {};
            for (int s = 0; s < Steps; ++s)
            {
                const char c = (lanes[l] != nullptr && (int) std::strlen (lanes[l]) > s) ? lanes[l][s] : '.';
                setStep (l, s, juce::roundToInt (BreakLibrary::detail::velocityFor (c) * 127.0f));
            }
        }

        swing.setValue (juce::roundToInt (st.swing * 100.0f), juce::sendNotificationSync);
        if (notify)
        {
            seqs.setBpm (st.bpm);
            changed();
        }
        repaint();
    }

    //==========================================================================
    // Layout of the grid, worked out on demand from gridArea.
    int nameWidth() const    { return juce::jmin (64, gridArea.getWidth() / 10); }
    int euclidWidth() const  { return euclidMode ? juce::jmin (150, gridArea.getWidth() / 5) : 0; }
    int rowHeight() const    { return gridArea.getHeight() / Lanes; }

    juce::Rectangle<int> rowArea (int l) const
    {
        return { gridArea.getX(), gridArea.getY() + l * rowHeight(), gridArea.getWidth(), rowHeight() };
    }

    juce::Rectangle<int> laneNameArea (int l) const { return rowArea (l).withWidth (nameWidth()).reduced (0, 1); }

    juce::Rectangle<int> euclidArea (int l) const
    {
        return rowArea (l).withTrimmedLeft (nameWidth() + 4).withWidth (euclidWidth());
    }

    juce::Rectangle<int> cellsArea() const
    {
        return gridArea.withTrimmedLeft (nameWidth() + 4 + (euclidMode ? euclidWidth() + 4 : 0));
    }

    juce::Rectangle<int> cellArea (int l, int st) const
    {
        const auto c = cellsArea();
        const int x0 = c.getX() + c.getWidth() * st / Steps, x1 = c.getX() + c.getWidth() * (st + 1) / Steps;
        const auto r = rowArea (l);
        return { x0, r.getY(), x1 - x0, r.getHeight() };
    }

    int stepAtX (int x) const
    {
        const auto c = cellsArea();
        if (x < c.getX() || x >= c.getRight()) return -1;
        return juce::jlimit (0, Steps - 1, (x - c.getX()) * Steps / c.getWidth());
    }

    std::pair<int, int> cellAt (juce::Point<int> p) const
    {
        if (! cellsArea().contains (p)) return { -1, -1 };
        return { juce::jlimit (0, Lanes - 1, (p.y - gridArea.getY()) / juce::jmax (1, rowHeight())), stepAtX (p.x) };
    }

    //==========================================================================
    // Euclid: [-] hits [+]  [<] [>]
    std::array<juce::Rectangle<int>, 4> euclidButtons (int l) const
    {
        auto r = euclidArea (l).reduced (0, 2);
        const int w = r.getWidth() / 6;
        return { r.withWidth (w), r.withX (r.getX() + 3 * w).withWidth (w),
                 r.withX (r.getX() + 4 * w).withWidth (w), r.withX (r.getX() + 5 * w).withWidth (w) };
    }

    void paintEuclidControls (juce::Graphics& g, int l)
    {
        const auto b = euclidButtons (l);
        const char* labels[4] = { "-", "+", "<", ">" };
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        for (int i = 0; i < 4; ++i)
        {
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.fillRoundedRectangle (b[(size_t) i].toFloat().reduced (1.5f), 4.0f);
            g.setColour (MidiMorfColors::textLight.withAlpha (0.85f));
            g.drawText (labels[i], b[(size_t) i], juce::Justification::centred);
        }
        const auto hits = juce::Rectangle<int>::leftTopRightBottom (b[0].getRight(), b[0].getY(), b[1].getX(), b[0].getBottom());
        g.setColour (TrackColours::get (track));
        g.drawText (juce::String (euclid[(size_t) l].hits), hits, juce::Justification::centred);
    }

    bool euclidClick (int l, juce::Point<int> p)
    {
        const auto b = euclidButtons (l);
        auto& e = euclid[(size_t) l];
        if      (b[0].contains (p)) e.hits = juce::jmax (0, e.hits - 1);
        else if (b[1].contains (p)) e.hits = juce::jmin (Steps, e.hits + 1);
        else if (b[2].contains (p)) e.rotate = (e.rotate + Steps - 1) % Steps;
        else if (b[3].contains (p)) e.rotate = (e.rotate + 1) % Steps;
        else return false;

        // Hits spread as evenly as the steps allow, the first on the downbeat,
        // then turned by ROTATE.
        for (int st = 0; st < Steps; ++st)
        {
            const int i = (st - e.rotate + Steps) % Steps;
            const bool on = e.hits > 0 && (i * e.hits) % Steps < e.hits;
            setStep (l, st, on ? (pattern[(size_t) l][(size_t) st] > 0 ? pattern[(size_t) l][(size_t) st] : DefaultVelocity) : 0);
        }
        changed();
        repaint();
        return true;
    }

    //==========================================================================
    void timerCallback() override
    {
        const int step = seqs.getPlayhead (LocalSequencers::Drums);
        if (step != playhead)
        {
            playhead = step;
            repaint (cellsArea());
        }
        if (playButton.getToggleState() != seqs.isPlaying (LocalSequencers::Drums))
        {
            playButton.setToggleState (seqs.isPlaying (LocalSequencers::Drums), juce::dontSendNotification);
            refreshPlay();
        }
        if (juce::roundToInt (seqs.getBpm()) != shownBpm)
            refreshBpm();
    }

    void refreshPlay() { playButton.setButtonText (playButton.getToggleState() ? "STOP" : "PLAY"); }

    void refreshBpm()
    {
        shownBpm = juce::roundToInt (seqs.getBpm());
        bpmLabel.setText (juce::String (shownBpm) + " BPM", juce::dontSendNotification);
    }

    LocalSequencers& seqs;
    int track = 3;

    std::array<std::array<uint8_t, Steps>, Lanes> pattern {};
    std::array<Euclid, Lanes> euclid {};
    bool euclidMode = false;
    int playhead = -1, heldLane = -1, shownBpm = -1;
    Drag drag;
    juce::Rectangle<int> gridArea;

    SynthLookAndFeel laf;   // declared before the children
    juce::TextButton playButton { "PLAY" }, bpmDown { "-" }, bpmUp { "+" }, clearButton { "CLEAR" };
    juce::Label bpmLabel, swingLabel;
    juce::Slider swing;
    juce::OwnedArray<juce::TextButton> modeButtons;
    juce::ComboBox beatCombo;
};
