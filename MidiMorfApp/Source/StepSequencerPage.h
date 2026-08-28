#pragma once

#include <JuceHeader.h>
#include "EventRouter.h"
#include "MidiMorfColors.h"
#include "Glassmorphism.h"
#include <array>

// Step Sequencer — Creative Sequencer Pack, page 1 of 3.
//
// A remote editor, not an instrument: all timing lives on the server, driven by
// DAW MIDI clock. This page sends state and displays the playhead the server
// pushes back. There is deliberately no Local mode.
//
// Layout follows JB's wireframe (2026-08-25):
//   • top strip   — Key / Scale / Base / Range / Len / Swing / Rate dropdowns,
//                   then the momentary ratchet roller (1/4 1/8 1/16 1/32)
//   • centre      — 16 full-height lanes, mode-switched
//   • right rail  — direction (>> << <> ?) then lane mode (PITCH VEL MOD)
//   • bottom      — 16 step boxes showing the resulting note name, with a
//                   909-style red LED when the step is on. Tap toggles rest.

// Deep indigo-violet — cool and instrument-like, distinct from Morf-Chord's
// emerald and Rose-Chord's mauve.
static const PagePalette stepSeqPagePalette {
    juce::Colour (0xff1B1636),       // substrate top — deep indigo
    juce::Colour (0xff070510),       // substrate bottom — near-black
    juce::Colour (0xCC332A63),       // acrylic top — translucent violet
    juce::Colour (0xD81B1636),       // acrylic bottom
    juce::Colour (0xCC000000)        // edge
};

static const juce::Colour stepSeqAccent    { 0xff7C6BD8 };   // violet accent
static const juce::Colour stepSeqLaneFill  { 0xff8C7BE8 };   // lane value bar
static const juce::Colour stepSeqPlayhead  { 0xffFFC24A };   // amber playhead
static const juce::Colour stepSeqStepLed   { 0xffE8392E };   // 909 red

//==============================================================================
// Button that reports press and release separately, for the momentary roller.
class RollerButton : public juce::TextButton
{
public:
    using juce::TextButton::TextButton;

    std::function<void()> onPress;
    std::function<void()> onRelease;

protected:
    void buttonStateChanged() override
    {
        juce::TextButton::buttonStateChanged();

        const bool nowDown = (getState() == juce::Button::buttonDown);

        if (nowDown == wasDown)
            return;

        wasDown = nowDown;

        if (nowDown) { if (onPress)   onPress(); }
        else         { if (onRelease) onRelease(); }
    }

private:
    bool wasDown = false;
};

//==============================================================================
// The 16 lanes. Drawn as one component rather than 16 sliders so a finger can
// drag across several lanes at once — the gesture that makes hardware step
// sequencers fast to program.
class LaneStrip : public juce::Component
{
public:
    static constexpr int NUM_STEPS = 16;

    // Called with (step, normalised value 0..1) as the finger moves.
    std::function<void (int, float)> onValueChanged;

    void setValue (int step, float normalised)
    {
        if (juce::isPositiveAndBelow (step, NUM_STEPS))
        {
            values[(size_t) step] = juce::jlimit (0.0f, 1.0f, normalised);
            repaint();
        }
    }

    float getValue (int step) const
    {
        return values[(size_t) juce::jlimit (0, NUM_STEPS - 1, step)];
    }

    // Steps beyond the pattern length are dimmed — they exist but never play.
    void setPatternLength (int len)  { patternLength = juce::jlimit (1, NUM_STEPS, len); repaint(); }
    void setPlayhead (int step)      { if (step != playhead) { playhead = step; repaint(); } }
    void setRests (const std::array<bool, NUM_STEPS>& r) { rests = r; repaint(); }

    // Quantised lanes draw tick marks so degree boundaries are visible.
    void setQuantiseSteps (int n) { quantiseSteps = juce::jmax (0, n); repaint(); }

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        const float laneW = b.getWidth() / (float) NUM_STEPS;

        for (int i = 0; i < NUM_STEPS; ++i)
        {
            auto lane = juce::Rectangle<float> (b.getX() + laneW * i, b.getY(),
                                                laneW, b.getHeight()).reduced (laneW * 0.16f, 0.0f);

            const bool active   = (i < patternLength);
            const bool rested   = rests[(size_t) i];
            const bool isPlaying = (i == playhead);

            // Track
            g.setColour (juce::Colour (0x55000000));
            g.fillRoundedRectangle (lane, 3.0f);

            // Beat shading — every fourth lane slightly lighter, so 4/4 reads
            // at a glance without needing labels.
            if ((i % 4) == 0)
            {
                g.setColour (juce::Colour (0x18FFFFFF));
                g.fillRoundedRectangle (lane, 3.0f);
            }

            // Degree tick marks
            if (quantiseSteps > 1 && quantiseSteps <= 40)
            {
                g.setColour (juce::Colour (0x14FFFFFF));

                for (int t = 1; t < quantiseSteps; ++t)
                {
                    const float y = lane.getBottom() - lane.getHeight() * ((float) t / (float) quantiseSteps);
                    g.fillRect (lane.getX(), y, lane.getWidth(), 1.0f);
                }
            }

            // Value bar, growing from the bottom
            const float v = values[(size_t) i];
            const float h = juce::jmax (3.0f, lane.getHeight() * v);
            auto fill = lane.withTop (lane.getBottom() - h);

            auto colour = stepSeqLaneFill;

            if (! active)      colour = colour.withAlpha (0.20f);
            else if (rested)   colour = colour.withAlpha (0.35f);

            if (isPlaying && active)
                colour = colour.brighter (0.55f);

            g.setColour (colour);
            g.fillRoundedRectangle (fill, 3.0f);

            // Cap — the thing the eye actually tracks while dragging
            g.setColour (colour.brighter (0.5f));
            g.fillRoundedRectangle (fill.withHeight (3.0f), 1.5f);

            if (isPlaying && active)
            {
                g.setColour (stepSeqPlayhead.withAlpha (0.85f));
                g.drawRoundedRectangle (lane.reduced (0.5f), 3.0f, 1.5f);
            }
        }
    }

    void mouseDown (const juce::MouseEvent& e) override { applyDrag (e); }
    void mouseDrag (const juce::MouseEvent& e) override { applyDrag (e); }

private:
    void applyDrag (const juce::MouseEvent& e)
    {
        const auto b = getLocalBounds().toFloat();

        if (b.getWidth() <= 0.0f || b.getHeight() <= 0.0f)
            return;

        const int step = juce::jlimit (0, NUM_STEPS - 1,
                                       (int) ((float) e.position.x / (b.getWidth() / (float) NUM_STEPS)));

        const float v = juce::jlimit (0.0f, 1.0f,
                                      1.0f - ((float) e.position.y / b.getHeight()));

        values[(size_t) step] = v;
        repaint();

        if (onValueChanged)
            onValueChanged (step, v);
    }

    std::array<float, NUM_STEPS> values { };
    std::array<bool, NUM_STEPS> rests { };
    int patternLength = NUM_STEPS;
    int playhead = -1;
    int quantiseSteps = 0;
};

//==============================================================================
// One 909-style step box: note name plus a red LED when the step is on.
class StepBox : public juce::Component
{
public:
    // Reports the finger position in PARENT coordinates so the parent can paint
    // across the whole row. isStart marks the press that decides the direction:
    // a sweep either clears a run or fills it, based on the box it began on.
    std::function<void (juce::Point<int>, bool /*isStart*/)> onPaint;

    void setNoteName (const juce::String& n) { noteName = n; repaint(); }
    void setOn (bool o)        { if (o != isOn) { isOn = o; repaint(); } }
    void setPlaying (bool p)   { if (p != playing) { playing = p; repaint(); } }
    void setActive (bool a)    { if (a != withinLength) { withinLength = a; repaint(); } }

    bool getOn() const { return isOn; }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced (1.5f);

        Glassmorphism::paintButton (g, b, stepSeqPagePalette, stepSeqAccent,
                                    playing, false, 4.0f);

        g.setColour (juce::Colours::white.withAlpha (withinLength ? (isOn ? 0.92f : 0.35f) : 0.18f));
        g.setFont (juce::Font (juce::FontOptions (juce::jmin (13.0f, b.getHeight() * 0.42f))));
        g.drawText (noteName, b.withTrimmedBottom (b.getHeight() * 0.30f),
                    juce::Justification::centred, false);

        // The LED — lit when the step will sound.
        auto led = juce::Rectangle<float> (b.getCentreX() - b.getWidth() * 0.26f,
                                           b.getBottom() - b.getHeight() * 0.26f,
                                           b.getWidth() * 0.52f,
                                           b.getHeight() * 0.13f);

        if (isOn && withinLength)
        {
            g.setColour (stepSeqStepLed.withAlpha (playing ? 1.0f : 0.85f));
            g.fillRoundedRectangle (led, 2.0f);

            g.setColour (stepSeqStepLed.withAlpha (playing ? 0.55f : 0.25f));
            g.drawRoundedRectangle (led.expanded (1.6f), 3.0f, 1.4f);
        }
        else
        {
            g.setColour (juce::Colour (0x66000000));
            g.fillRoundedRectangle (led, 2.0f);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override { report (e, true); }
    void mouseDrag (const juce::MouseEvent& e) override { report (e, false); }

private:
    void report (const juce::MouseEvent& e, bool isStart)
    {
        if (onPaint == nullptr)
            return;

        if (auto* parent = getParentComponent())
            onPaint (e.getEventRelativeTo (parent).getPosition(), isStart);
    }

    juce::String noteName;
    bool isOn = true;
    bool playing = false;
    bool withinLength = true;
};

//==============================================================================
class StepSequencerContent : public juce::Component
{
public:
    static constexpr int NUM_STEPS = LaneStrip::NUM_STEPS;

    enum class LaneMode { Pitch, Velocity, Mod, Gate };

    StepSequencerContent (EventRouter& router) : eventRouter (router)
    {
        setLookAndFeel (&glassLaf);

        // ── Top strip ────────────────────────────────────────────────────────
        addCombo (keyCombo,   { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" }, 1, "Key");
        addCombo (scaleCombo, { "Major","Minor","Penta","Blues","Chrom" },              1, "Scale");
        addCombo (baseCombo,  { "C0","C1","C2","C3","C4","C5","C6","C7","C8" },         3, "Base");
        addCombo (rangeCombo, { "1 oct","2 oct","3 oct","4 oct","5 oct" },              3, "Range");
        addCombo (lenCombo,   { }, 16, "Len");
        addCombo (swingCombo, { "0%","10%","20%","30%","40%","50%","60%" },             1, "Swing");
        addCombo (rateCombo,  { "1/4","1/8","1/8T","1/16","1/16T","1/32" },             4, "Rate");

        for (int i = 1; i <= NUM_STEPS; ++i)
            lenCombo.addItem (juce::String (i), i);
        lenCombo.setSelectedId (NUM_STEPS, juce::dontSendNotification);

        // ── Ratchet roller — momentary, whole-sequence note repeat ───────────
        static const char* rollerLabels[4] = { "1/4", "1/8", "1/16", "1/32" };

        for (int i = 0; i < 4; ++i)
        {
            auto* b = rollerButtons.add (new RollerButton (rollerLabels[i]));
            b->setLookAndFeel (&glassLaf);
            b->onPress   = [this, i] { eventRouter.sendSeqRatchet (static_cast<MidiMorf::SeqRatchetDiv> (i), true); };
            b->onRelease = [this, i] { eventRouter.sendSeqRatchet (static_cast<MidiMorf::SeqRatchetDiv> (i), false); };
            addAndMakeVisible (b);
        }

        // ── Right rail: direction, then lane mode ────────────────────────────
        static const char* dirLabels[4] = { ">>", "<<", "<>", "?" };

        for (int i = 0; i < 4; ++i)
        {
            auto* b = dirButtons.add (new juce::TextButton (dirLabels[i]));
            b->setLookAndFeel (&glassLaf);
            b->setClickingTogglesState (true);
            b->setRadioGroupId (7101);
            b->onClick = [this, i] { direction = (uint8_t) i; pushConfig(); };
            addAndMakeVisible (b);
        }

        dirButtons[0]->setToggleState (true, juce::dontSendNotification);

        static const char* modeLabels[4] = { "PITCH", "VEL", "MOD", "GATE" };

        for (int i = 0; i < 4; ++i)
        {
            auto* b = modeButtons.add (new juce::TextButton (modeLabels[i]));
            b->setLookAndFeel (&glassLaf);
            b->setClickingTogglesState (true);
            b->setRadioGroupId (7102);
            b->onClick = [this, i] { setLaneMode (static_cast<LaneMode> (i)); };
            addAndMakeVisible (b);
        }

        modeButtons[0]->setToggleState (true, juce::dontSendNotification);

        // ── Lanes ────────────────────────────────────────────────────────────
        addAndMakeVisible (lanes);
        lanes.onValueChanged = [this] (int step, float v) { laneMoved (step, v); };

        // ── Step boxes ───────────────────────────────────────────────────────
        for (int i = 0; i < NUM_STEPS; ++i)
        {
            auto* box = stepBoxes.add (new StepBox());
            box->onPaint = [this] (juce::Point<int> pos, bool isStart) { paintStepAt (pos, isStart); };
            addAndMakeVisible (box);
        }

        addAndMakeVisible (clockLabel);
        clockLabel.setJustificationType (juce::Justification::centredRight);
        clockLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.45f));
        clockLabel.setFont (juce::Font (juce::FontOptions (12.0f)));
        clockLabel.setText ("Waiting for DAW clock", juce::dontSendNotification);

        loadDemoPattern();
        rebuildNoteTable();
        setLaneMode (LaneMode::Pitch);
    }

    ~StepSequencerContent() override
    {
        for (auto* b : rollerButtons) b->setLookAndFeel (nullptr);
        for (auto* b : dirButtons)    b->setLookAndFeel (nullptr);
        for (auto* b : modeButtons)   b->setLookAndFeel (nullptr);
        setLookAndFeel (nullptr);
    }

    // Server pushes the playhead as it advances; -1 when stopped.
    void setPlayhead (int step)
    {
        if (step == playhead)
            return;

        playhead = step;
        lanes.setPlayhead (step);

        for (int i = 0; i < NUM_STEPS; ++i)
            stepBoxes[i]->setPlaying (i == step);

        clockLabel.setText (step < 0 ? "Waiting for DAW clock" : "Running",
                            juce::dontSendNotification);
    }

    // Called when the page becomes visible — the server needs the full state,
    // since it has no idea what the phone was editing while away.
    void pushFullState()
    {
        pushConfig();

        for (int i = 0; i < NUM_STEPS; ++i)
            pushStep (i);
    }

    void paint (juce::Graphics& g) override
    {
        Glassmorphism::paintPanel (g, getLocalBounds().toFloat(), stepSeqPagePalette);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (8);

        // Right rail: 4 direction + 3 mode buttons.
        auto rail = area.removeFromRight (58);
        area.removeFromRight (6);

        const int railGap = 4;
        const int railBtn = juce::jlimit (24, 44,
                                          (rail.getHeight() - railGap * 8 - 10) / 8);

        for (auto* b : dirButtons)
        {
            b->setBounds (rail.removeFromTop (railBtn));
            rail.removeFromTop (railGap);
        }

        rail.removeFromTop (10);   // seam between direction and lane mode

        for (auto* b : modeButtons)
        {
            b->setBounds (rail.removeFromTop (railBtn));
            rail.removeFromTop (railGap);
        }

        // Top strip.
        auto top = area.removeFromTop (34);
        area.removeFromTop (6);

        const int comboGap = 5;
        juce::ComboBox* combos[] = { &keyCombo, &scaleCombo, &baseCombo, &rangeCombo,
                                     &lenCombo, &swingCombo, &rateCombo };

        // The roller takes a fixed slice; the dropdowns share what is left.
        const int rollerW = juce::jlimit (120, 200, top.getWidth() / 4);
        auto rollerArea = top.removeFromRight (rollerW);
        top.removeFromRight (10);

        const int comboW = (top.getWidth() - comboGap * 6) / 7;

        for (auto* c : combos)
        {
            c->setBounds (top.removeFromLeft (comboW));
            top.removeFromLeft (comboGap);
        }

        const int rollerBtnW = rollerArea.getWidth() / 4;

        for (auto* b : rollerButtons)
            b->setBounds (rollerArea.removeFromLeft (rollerBtnW).reduced (1, 0));

        // Bottom: step boxes, then the clock readout.
        clockLabel.setBounds (area.removeFromBottom (16));
        auto boxRow = area.removeFromBottom (juce::jlimit (34, 52, area.getHeight() / 5));
        area.removeFromBottom (6);

        const float boxW = (float) boxRow.getWidth() / (float) NUM_STEPS;

        for (int i = 0; i < NUM_STEPS; ++i)
            stepBoxes[i]->setBounds (juce::Rectangle<float> (boxRow.getX() + boxW * i,
                                                            (float) boxRow.getY(),
                                                            boxW, (float) boxRow.getHeight()).toNearestInt());

        lanes.setBounds (area);
    }

private:
    struct StepState
    {
        int  degree   = 0;
        int  velocity = 100;
        int  mod      = 64;
        int  gate     = 50;   // % of step length
        bool rest     = false;
    };

    void addCombo (juce::ComboBox& c, const juce::StringArray& items, int selectedId,
                   const juce::String& tooltip)
    {
        for (int i = 0; i < items.size(); ++i)
            c.addItem (items[i], i + 1);

        if (items.size() > 0)
            c.setSelectedId (selectedId, juce::dontSendNotification);

        c.setTooltip (tooltip);
        c.onChange = [this] { configChanged(); };
        addAndMakeVisible (c);
    }

    // ── Config ───────────────────────────────────────────────────────────────

    void configChanged()
    {
        rebuildNoteTable();   // also refreshes lane quantisation
        pushConfig();
    }

    void pushConfig()
    {
        eventRouter.sendSeqConfig ((uint8_t) lenCombo.getSelectedId(),
                                   (uint8_t) (rateCombo.getSelectedId() - 1),
                                   direction,
                                   (uint8_t) ((swingCombo.getSelectedId() - 1) * 10),
                                   (uint8_t) (scaleCombo.getSelectedId() - 1),
                                   (uint8_t) (keyCombo.getSelectedId() - 1),
                                   (uint8_t) (baseCombo.getSelectedId() - 1),
                                   (uint8_t) rangeCombo.getSelectedId());

        lanes.setPatternLength (lenCombo.getSelectedId());

        for (int i = 0; i < NUM_STEPS; ++i)
            stepBoxes[i]->setActive (i < lenCombo.getSelectedId());
    }

    void pushStep (int i)
    {
        const auto& s = steps[(size_t) i];
        eventRouter.sendSeqStepSet ((uint8_t) i, (uint8_t) s.degree, (uint8_t) s.velocity,
                                    (uint8_t) s.gate, (uint8_t) s.mod, s.rest);
    }

    // ── Note table — mirrors StepSequencerEngine::buildNoteTable ─────────────

    void rebuildNoteTable()
    {
        static const std::vector<std::vector<int>> scales = {
            { 0, 2, 4, 5, 7, 9, 11 },
            { 0, 2, 3, 5, 7, 8, 10 },
            { 0, 2, 4, 7, 9 },
            { 0, 3, 5, 6, 7, 10 },
            { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 },
        };

        const int scaleIdx   = juce::jlimit (0, 4, scaleCombo.getSelectedId() - 1);
        const int root       = juce::jlimit (0, 11, keyCombo.getSelectedId() - 1);
        const int baseOctave = juce::jlimit (0, 8, baseCombo.getSelectedId() - 1);
        const int octaves    = juce::jlimit (1, 5, rangeCombo.getSelectedId());

        noteTable.clear();
        const auto& intervals = scales[(size_t) scaleIdx];
        const int baseNote = (baseOctave + 1) * 12 + root;

        for (int oct = 0; oct < octaves; ++oct)
            for (int interval : intervals)
            {
                const int n = baseNote + oct * 12 + interval;

                if (n <= 127)
                    noteTable.push_back (n);
            }

        if (noteTable.empty())
            noteTable.push_back (60);

        updateLaneQuantise();

        for (int i = 0; i < NUM_STEPS; ++i)
        {
            steps[(size_t) i].degree = juce::jlimit (0, (int) noteTable.size() - 1,
                                                     steps[(size_t) i].degree);
            refreshStepBox (i);
        }

        if (laneMode == LaneMode::Pitch)
            refreshLanes();
    }

    juce::String noteNameFor (int degree) const
    {
        static const char* names[12] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };

        const int idx  = juce::jlimit (0, (int) noteTable.size() - 1, degree);
        const int note = noteTable[(size_t) idx];

        return juce::String (names[note % 12]) + juce::String (note / 12 - 1);
    }

    // ── Lane mode ────────────────────────────────────────────────────────────

    void setLaneMode (LaneMode m)
    {
        laneMode = m;
        updateLaneQuantise();
        refreshLanes();
    }

    // Ticks per step for the selected rate — mirrors StepSequencerEngine.
    int ticksPerStep() const
    {
        switch (rateCombo.getSelectedId() - 1)
        {
            case 0:  return 24;   // 1/4
            case 1:  return 12;   // 1/8
            case 2:  return 8;    // 1/8T
            case 3:  return 6;    // 1/16
            case 4:  return 4;    // 1/16T
            default: return 3;    // 1/32
        }
    }

    void updateLaneQuantise()
    {
        switch (laneMode)
        {
            // Pitch snaps to scale degrees.
            case LaneMode::Pitch: lanes.setQuantiseSteps ((int) noteTable.size()); break;

            // Gate resolution is bounded by ticks per step, so show where the
            // note length can actually land rather than implying it is smooth.
            case LaneMode::Gate:  lanes.setQuantiseSteps (ticksPerStep()); break;

            default:              lanes.setQuantiseSteps (0); break;
        }
    }

    void refreshLanes()
    {
        const float maxDegree = juce::jmax (1.0f, (float) noteTable.size() - 1.0f);

        for (int i = 0; i < NUM_STEPS; ++i)
        {
            const auto& s = steps[(size_t) i];

            switch (laneMode)
            {
                case LaneMode::Pitch:    lanes.setValue (i, (float) s.degree / maxDegree); break;
                case LaneMode::Velocity: lanes.setValue (i, (float) s.velocity / 127.0f);  break;
                case LaneMode::Mod:      lanes.setValue (i, (float) s.mod / 127.0f);       break;
                case LaneMode::Gate:     lanes.setValue (i, (float) s.gate / 100.0f);      break;
            }
        }

        refreshLaneRests();
    }

    void refreshLaneRests()
    {
        std::array<bool, NUM_STEPS> r { };

        for (int i = 0; i < NUM_STEPS; ++i)
            r[(size_t) i] = steps[(size_t) i].rest;

        lanes.setRests (r);
    }

    void laneMoved (int step, float v)
    {
        auto& s = steps[(size_t) step];

        switch (laneMode)
        {
            case LaneMode::Pitch:
            {
                const int maxDegree = juce::jmax (0, (int) noteTable.size() - 1);
                s.degree = juce::jlimit (0, maxDegree, juce::roundToInt (v * (float) maxDegree));
                refreshStepBox (step);
                // Snap the lane so it sits on the degree, not between two.
                lanes.setValue (step, maxDegree > 0 ? (float) s.degree / (float) maxDegree : 0.0f);
                break;
            }

            case LaneMode::Velocity: s.velocity = juce::jlimit (1, 127, juce::roundToInt (v * 127.0f)); break;
            case LaneMode::Mod:      s.mod      = juce::jlimit (0, 127, juce::roundToInt (v * 127.0f)); break;
            case LaneMode::Gate:     s.gate     = juce::jlimit (1, 100, juce::roundToInt (v * 100.0f)); break;
        }

        pushStep (step);
    }

    // Drag across the row to knock out a run of steps or bring one back in.
    // The box under the initial press decides the direction for the whole
    // sweep, so a drag never leaves a checkerboard behind.
    void paintStepAt (juce::Point<int> pos, bool isStart)
    {
        for (int i = 0; i < NUM_STEPS; ++i)
        {
            if (! stepBoxes[i]->getBounds().contains (pos))
                continue;

            if (isStart)
                paintRestState = ! steps[(size_t) i].rest;

            if (steps[(size_t) i].rest == paintRestState)
                return;   // already in the target state — nothing to send

            steps[(size_t) i].rest = paintRestState;
            refreshStepBox (i);
            refreshLaneRests();
            pushStep (i);
            return;
        }
    }

    void refreshStepBox (int i)
    {
        stepBoxes[i]->setNoteName (noteNameFor (steps[(size_t) i].degree));
        stepBoxes[i]->setOn (! steps[(size_t) i].rest);
    }

    void loadDemoPattern()
    {
        static const int degrees[NUM_STEPS] = { 0, 2, 4, 2, 7, 4, 2, 4, 0, 2, 4, 7, 9, 7, 4, 2 };

        for (int i = 0; i < NUM_STEPS; ++i)
        {
            steps[(size_t) i].degree   = degrees[i];
            steps[(size_t) i].velocity = (i % 4 == 0) ? 112 : 80;
            steps[(size_t) i].mod      = 64;
            steps[(size_t) i].gate     = 50;
            steps[(size_t) i].rest     = false;
        }
    }

    EventRouter& eventRouter;

    GlassButtonLookAndFeel glassLaf { stepSeqPagePalette, stepSeqAccent };

    juce::ComboBox keyCombo, scaleCombo, baseCombo, rangeCombo, lenCombo, swingCombo, rateCombo;
    juce::OwnedArray<RollerButton> rollerButtons;
    juce::OwnedArray<juce::TextButton> dirButtons, modeButtons;
    juce::OwnedArray<StepBox> stepBoxes;
    LaneStrip lanes;
    juce::Label clockLabel;

    std::array<StepState, NUM_STEPS> steps;
    std::vector<int> noteTable;

    LaneMode laneMode = LaneMode::Pitch;
    bool paintRestState = false;   // direction of the in-progress box sweep
    uint8_t direction = 0;
    int playhead = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StepSequencerContent)
};

//==============================================================================
class StepSequencerPage : public juce::Component
{
public:
    StepSequencerPage (EventRouter& router) : content (router)
    {
        addAndMakeVisible (content);
    }

    void setPlayhead (int step) { content.setPlayhead (step); }
    void pushFullState()        { content.pushFullState(); }

    void resized() override { content.setBounds (getLocalBounds()); }

private:
    StepSequencerContent content;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StepSequencerPage)
};
