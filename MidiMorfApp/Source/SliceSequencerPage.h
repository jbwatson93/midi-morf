#pragma once

#include <JuceHeader.h>
#include "EventRouter.h"
#include "MidiMorfColors.h"
#include "Glassmorphism.h"
#include "StepSequencerPage.h"   // LaneStrip, StepBox, RollerButton, SeqTransportBar
#include <array>

// Slice Sequencer — Creative Sequencer Pack, page 2 of 3. Monome mlr /
// Re:Mix lineage: a 1–2 bar loop sits in Simpler in slice mode, and this page
// re-orders its slices live. A step's column position picks which slice plays
// there — the identity diagonal (step N → slice N) is the untouched loop, and
// remixing means dragging cells off that diagonal.
//
// With DAW sync this is a remote editor: timing lives on the server, driven by
// DAW MIDI clock, and the loop sits in Simpler. With Local sync the same engine
// runs on the phone's clock and cuts up one of the built-in breaks (or an
// imported loop) itself — picked from the transport strip along the bottom.
//
// Layout follows JB's wireframe (IMG_2223, 2026-08-27):
//   • top         — two assignable ribbons side by side (ribbon 1 defaults to
//                   pitch bend, which Simpler honours with no MIDI mapping)
//   • centre      — N columns × N cells, one shaded cell per column
//   • bottom      — step on/off boxes, same drag-sweep as the 909 boxes
//   • right rail  — EDIT (popup: bars/division/root/direction/swing), the
//                   random arm, then the beat-repeat looper 1/4 1/8 1/16 1/32

// Deep ember/rust — warm and tape-machine-like, distinct from the step
// sequencer's indigo-violet.
static const PagePalette sliceSeqPagePalette {
    juce::Colour (0xff2B160E),       // substrate top — deep ember brown
    juce::Colour (0xff0C0503),       // substrate bottom — near-black
    juce::Colour (0xCC5A2E1A),       // acrylic top — translucent rust
    juce::Colour (0xD82B160E),       // acrylic bottom
    juce::Colour (0xCC000000)        // edge
};

static const juce::Colour sliceSeqAccent    { 0xffE8913A };   // amber accent
static const juce::Colour sliceSeqCellFill  { 0xffF0A250 };   // slice marker
static const juce::Colour sliceSeqPlayhead  { 0xff5FD8FF };   // cool cyan against the ember
static const juce::Colour sliceSeqStepLed   { 0xffE8392E };   // 909 red

//==============================================================================
// A horizontal performance ribbon. The left chip shows (and reassigns) what it
// sends; the rest of the strip is the touch surface. Three behaviours:
// pitch bend springs back to centre on release, CC holds its last position,
// and the step repeater is a zoned strip (x1..x8) that springs back to x1.
class RibbonStrip : public juce::Component
{
public:
    enum class Mode { SpringCentre, Hold, SteppedRepeat };

    // What the finger produced: a 14-bit position for SpringCentre/Hold, or
    // the repeat count itself for SteppedRepeat. Fires on release too (the
    // spring back to centre / x1).
    std::function<void (int)> onValue14;
    // Tap on the chip — parent shows the assignment menu.
    std::function<void()> onAssignRequested;

    void setAssignment (const juce::String& text, Mode m)
    {
        chipText    = text;
        mode        = m;
        value       = (mode == Mode::SpringCentre) ? 0.5f : 0.0f;
        repeatIndex = 0;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced (1.0f);

        Glassmorphism::paintButton (g, b, sliceSeqPagePalette, sliceSeqAccent,
                                    false, false, 6.0f);

        auto chip = b.removeFromLeft (chipW);

        g.setColour (juce::Colour (0x44000000));
        g.fillRoundedRectangle (chip.reduced (3.0f), 4.0f);

        g.setColour (sliceSeqAccent.withAlpha (0.9f));
        g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
        g.drawText (mode == Mode::SteppedRepeat ? "x" + juce::String (kRepeatCounts[repeatIndex])
                                                : chipText,
                    chip, juce::Justification::centred, false);

        auto track = b.reduced (8.0f, 0.0f);

        if (mode == Mode::SteppedRepeat)
        {
            // Zones left-to-right: x1 x2 x3 x4 x6 x8, active one lit.
            const float zoneW = track.getWidth() / (float) kNumRepeats;

            for (int i = 0; i < kNumRepeats; ++i)
            {
                auto zone = juce::Rectangle<float> (track.getX() + zoneW * i, track.getY() + 3.0f,
                                                    zoneW, track.getHeight() - 6.0f).reduced (1.5f, 0.0f);

                g.setColour (i == repeatIndex ? sliceSeqAccent.withAlpha (0.55f)
                                              : juce::Colour (0x33000000));
                g.fillRoundedRectangle (zone, 3.0f);

                g.setColour (i == repeatIndex ? juce::Colours::white
                                              : juce::Colours::white.withAlpha (0.4f));
                g.setFont (juce::Font (juce::FontOptions (11.0f)));
                g.drawText ("x" + juce::String (kRepeatCounts[i]), zone,
                            juce::Justification::centred, false);
            }

            return;
        }

        // Continuous track: centre line for spring ribbons, then the marker.
        if (mode == Mode::SpringCentre)
        {
            g.setColour (juce::Colours::white.withAlpha (0.15f));
            g.fillRect (track.getCentreX() - 0.5f, track.getY() + 4.0f,
                        1.0f, track.getHeight() - 8.0f);
        }

        const float x = track.getX() + track.getWidth() * value;

        g.setColour (sliceSeqAccent.withAlpha (0.35f));
        g.fillRoundedRectangle (x - 6.0f, track.getY() + 3.0f, 12.0f, track.getHeight() - 6.0f, 4.0f);

        g.setColour (sliceSeqAccent);
        g.fillRoundedRectangle (x - 1.5f, track.getY() + 3.0f, 3.0f, track.getHeight() - 6.0f, 1.5f);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.position.x < chipW)
        {
            chipTouched = true;

            if (onAssignRequested)
                onAssignRequested();

            return;
        }

        chipTouched = false;
        applyDrag (e);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! chipTouched)
            applyDrag (e);
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (chipTouched)
            return;

        if (mode == Mode::SpringCentre)
        {
            value = 0.5f;
            repaint();

            if (onValue14)
                onValue14 (8192);
        }
        else if (mode == Mode::SteppedRepeat && repeatIndex != 0)
        {
            repeatIndex = 0;
            repaint();

            if (onValue14)
                onValue14 (1);
        }
    }

private:
    static constexpr float chipW = 52.0f;
    static constexpr int kNumRepeats = 6;
    static constexpr int kRepeatCounts[kNumRepeats] = { 1, 2, 3, 4, 6, 8 };

    void applyDrag (const juce::MouseEvent& e)
    {
        auto track = getLocalBounds().toFloat().reduced (1.0f)
                        .withTrimmedLeft (chipW).reduced (8.0f, 0.0f);

        if (track.getWidth() <= 0.0f)
            return;

        const float pos = juce::jlimit (0.0f, 1.0f, (e.position.x - track.getX()) / track.getWidth());

        if (mode == Mode::SteppedRepeat)
        {
            const int idx = juce::jlimit (0, kNumRepeats - 1, (int) (pos * (float) kNumRepeats));

            if (idx == repeatIndex)
                return;

            repeatIndex = idx;
            repaint();

            if (onValue14)
                onValue14 (kRepeatCounts[idx]);

            return;
        }

        value = pos;
        repaint();

        if (onValue14)
            onValue14 (juce::roundToInt (value * 16383.0f));
    }

    juce::String chipText { "PB" };
    Mode  mode        = Mode::SpringCentre;
    bool  chipTouched = false;
    float value       = 0.5f;
    int   repeatIndex = 0;
};

//==============================================================================
// The beat-repeat looper: one slideable strip rather than four buttons, so a
// held finger can glide between divisions without dropping the repeat.
// Fastest at the top.
class LooperStrip : public juce::Component
{
public:
    // (division 0=1/4..3=1/32, active). Fires on every division change while
    // held, then once with active=false on release.
    std::function<void (int, bool)> onLooper;

    void paint (juce::Graphics& g) override
    {
        static const char* labels[4] = { "1/32", "1/16", "1/8", "1/4" };   // top → bottom

        auto b = getLocalBounds().toFloat();
        const float zoneH = b.getHeight() / 4.0f;

        for (int row = 0; row < 4; ++row)
        {
            auto zone = juce::Rectangle<float> (b.getX(), b.getY() + zoneH * row,
                                                b.getWidth(), zoneH).reduced (0.0f, 2.0f);

            const bool active = held && divisionForRow (row) == currentDiv;

            Glassmorphism::paintButton (g, zone, sliceSeqPagePalette, sliceSeqAccent,
                                        active, false, 4.0f);

            g.setColour (juce::Colours::white.withAlpha (active ? 0.95f : 0.6f));
            g.setFont (juce::Font (juce::FontOptions (12.0f)));
            g.drawText (labels[row], zone, juce::Justification::centred, false);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override { applyDrag (e); }
    void mouseDrag (const juce::MouseEvent& e) override { applyDrag (e); }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (! held)
            return;

        held = false;
        repaint();

        if (onLooper)
            onLooper (currentDiv, false);
    }

private:
    // Row 0 (top) is 1/32 = division 3; row 3 (bottom) is 1/4 = division 0.
    static int divisionForRow (int row) { return 3 - row; }

    void applyDrag (const juce::MouseEvent& e)
    {
        const float h = (float) getHeight();

        if (h <= 0.0f)
            return;

        const int row = juce::jlimit (0, 3, (int) (e.position.y / (h / 4.0f)));
        const int div = divisionForRow (row);

        if (held && div == currentDiv)
            return;

        held       = true;
        currentDiv = div;
        repaint();

        if (onLooper)
            onLooper (div, true);
    }

    int  currentDiv = 2;
    bool held       = false;
};

//==============================================================================
class SliceSequencerContent : public juce::Component
{
public:
    static constexpr int MAX_STEPS = LaneStrip::MAX_STEPS;

    SliceSequencerContent (EventRouter& router) : eventRouter (router)
    {
        setLookAndFeel (&glassLaf);

        // ── Ribbons ──────────────────────────────────────────────────────────
        for (int i = 0; i < 2; ++i)
        {
            auto* r = ribbons.add (new RibbonStrip());
            r->onValue14        = [this, i] (int v) { eventRouter.sendSeqRibbon ((uint8_t) i, v); };
            r->onAssignRequested = [this, i] { showRibbonAssignMenu (i); };
            addAndMakeVisible (r);
        }

        applyRibbonAssignment (0, 1, false);   // pitch bend — Simpler honours it natively
        applyRibbonAssignment (1, 6, false);   // per-step repeater — JB's x8 stutter

        // ── Right rail: edit, reset, random arm, looper ──────────────────────
        editButton.setButtonText ("EDIT");
        editButton.setLookAndFeel (&glassLaf);
        editButton.onClick = [this] { showEditPanel(); };
        addAndMakeVisible (editButton);

        resetButton.setButtonText ("RST");
        resetButton.setLookAndFeel (&glassLaf);
        resetButton.onClick = [this] { applyGrid (true); };   // back to the untouched loop
        addAndMakeVisible (resetButton);

        randomButton.setButtonText ("RND");
        randomButton.setLookAndFeel (&glassLaf);
        randomButton.setClickingTogglesState (true);
        randomButton.onClick = [this] { sendLooperState(); };
        addAndMakeVisible (randomButton);

        looper.onLooper = [this] (int div, bool active)
        {
            looperDiv  = div;
            looperHeld = active;
            sendLooperState();
        };
        addAndMakeVisible (looper);

        // ── Slice grid ───────────────────────────────────────────────────────
        lanes.setRenderMode (LaneStrip::RenderMode::Cell);
        lanes.setFillColour (sliceSeqCellFill);
        lanes.setPlayheadColour (sliceSeqPlayhead);
        lanes.onValueChanged = [this] (int step, float v) { laneMoved (step, v); };
        addAndMakeVisible (lanes);

        // ── Step boxes ───────────────────────────────────────────────────────
        for (int i = 0; i < MAX_STEPS; ++i)
        {
            auto* box = stepBoxes.add (new StepBox());
            box->setStyle (sliceSeqPagePalette, sliceSeqAccent, sliceSeqStepLed);
            box->onPaint = [this] (juce::Point<int> pos, bool isStart) { paintStepAt (pos, isStart); };
            addAndMakeVisible (box);
        }

        transport.onLocalPlayhead = [this] (int step) { showPlayhead (step); };
        transport.onBreakChanged  = [this] { breakChanged(); };
        addAndMakeVisible (transport);

        applyGrid (false);
        breakChanged();   // the strip may have seen the break before this was wired
    }

    ~SliceSequencerContent() override
    {
        editButton.setLookAndFeel (nullptr);
        resetButton.setLookAndFeel (nullptr);
        randomButton.setLookAndFeel (nullptr);
        setLookAndFeel (nullptr);
    }

    // Server pushes the playhead as it advances; -1 when stopped. With Local
    // sync the transport bar feeds the local one instead.
    void setPlayhead (int step)
    {
        transport.setRemoteRunning (step >= 0);

        if (! eventRouter.isLocalSync())
            showPlayhead (step);
    }

    SeqTransportBar& getTransport() { return transport; }

    //==========================================================================
    // Session clips: the slices and rests as a pattern. The grid (bars,
    // division) stays with the track.
    SeqPattern getPattern() const
    {
        SeqPattern p;
        p.length = numSteps();
        for (int i = 0; i < MAX_STEPS; ++i)
            p.steps[(size_t) i] = { (uint8_t) steps[(size_t) i].slice, 100, 95, 64, steps[(size_t) i].rest };
        return p;
    }

    void showPattern (const SeqPattern& p)
    {
        for (int i = 0; i < MAX_STEPS; ++i)
        {
            steps[(size_t) i].slice = juce::jlimit (0, MAX_STEPS - 1, (int) p.steps[(size_t) i].index);
            steps[(size_t) i].rest  = p.steps[(size_t) i].rest;
        }
        refreshAllCells();
    }

    void loadPattern (const SeqPattern& p)
    {
        showPattern (p);
        pushAllSteps();
    }

    // Called when the page becomes visible — the server keeps no per-page
    // memory, so config, pattern, and ribbon routing all go up together.
    void pushFullState()
    {
        pushConfig();
        pushAllSteps();
        pushRibbonAssignments();
    }

    void paint (juce::Graphics& g) override
    {
        Glassmorphism::paintPanel (g, getLocalBounds().toFloat(), sliceSeqPagePalette);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (8);

        // Right rail: EDIT, RST, RND, then the looper strip.
        auto rail = area.removeFromRight (58);
        area.removeFromRight (6);

        const int railGap = 4;
        const int railBtn = juce::jlimit (24, 40,
                                          (rail.getHeight() - railGap * 2 - 10) / 7);

        editButton.setBounds (rail.removeFromTop (railBtn));
        rail.removeFromTop (railGap);
        resetButton.setBounds (rail.removeFromTop (railBtn));
        rail.removeFromTop (railGap);
        randomButton.setBounds (rail.removeFromTop (railBtn));
        rail.removeFromTop (10);   // seam between the buttons and the looper

        looper.setBounds (rail);

        // Top: the two ribbons, side by side.
        auto top = area.removeFromTop (48);
        area.removeFromTop (6);

        const int ribbonW = (top.getWidth() - 8) / 2;
        ribbons[0]->setBounds (top.removeFromLeft (ribbonW));
        top.removeFromLeft (8);
        ribbons[1]->setBounds (top);

        // Bottom: step boxes, then the transport strip.
        transport.setBounds (area.removeFromBottom (30));
        area.removeFromBottom (6);
        auto boxRow = area.removeFromBottom (juce::jlimit (30, 46, area.getHeight() / 5));
        area.removeFromBottom (6);

        const float boxW = (float) boxRow.getWidth() / (float) numSteps();

        for (int i = 0; i < MAX_STEPS; ++i)
        {
            stepBoxes[i]->setVisible (i < numSteps());

            if (i < numSteps())
                stepBoxes[i]->setBounds (juce::Rectangle<float> (boxRow.getX() + boxW * i,
                                                                 (float) boxRow.getY(),
                                                                 boxW, (float) boxRow.getHeight()).toNearestInt());
        }

        lanes.setBounds (area);
    }

private:
    struct SliceStep
    {
        int  slice = 0;
        bool rest  = false;
    };

    void showPlayhead (int step)
    {
        if (step == playhead)
            return;

        playhead = step;
        lanes.setPlayhead (step);

        for (int i = 0; i < MAX_STEPS; ++i)
            stepBoxes[i]->setPlaying (i == step);
    }

    // A loop of a different length re-grids to match it, so the untouched
    // pattern plays the whole loop — or the bars the break asks to open on
    // (the Amen: 16 steps of its first bar). Same length keeps the current
    // remix — swapping breaks under a pattern is half the fun.
    void breakChanged()
    {
        if (const auto* b = eventRouter.getSequencers().getBreak())
        {
            const int loopBars = juce::jlimit (1, 2, b->gridBars > 0 ? b->gridBars : b->bars());
            if (loopBars != bars)
            {
                bars = loopBars;
                applyGrid (true);
            }
        }
    }

    int numSteps() const
    {
        const int perBar = (division == 0) ? 4 : (division == 1) ? 8 : 16;
        return bars * perBar;
    }

    // ── Grid ─────────────────────────────────────────────────────────────────

    // Grid geometry changed (or first build): back to the identity pattern,
    // matching what the engine's setGrid does on its side.
    void applyGrid (bool push)
    {
        const int n = numSteps();

        for (int i = 0; i < MAX_STEPS; ++i)
        {
            steps[(size_t) i].slice = i % n;
            steps[(size_t) i].rest  = false;
        }

        lanes.setNumSteps (n);
        lanes.setQuantiseSteps (n);
        lanes.setPatternLength (n);

        refreshAllCells();
        resized();

        if (push)
        {
            pushConfig();
            pushAllSteps();
        }
    }

    void refreshAllCells()
    {
        const int n = numSteps();
        const float maxSlice = juce::jmax (1.0f, (float) n - 1.0f);
        std::array<bool, MAX_STEPS> r { };

        for (int i = 0; i < MAX_STEPS; ++i)
        {
            const auto& s = steps[(size_t) i];
            lanes.setValue (i, (float) s.slice / maxSlice);
            stepBoxes[i]->setNoteName (juce::String (s.slice + 1));
            stepBoxes[i]->setOn (! s.rest);
            stepBoxes[i]->setActive (true);
            r[(size_t) i] = s.rest;
        }

        lanes.setRests (r);
    }

    // ── Sends ────────────────────────────────────────────────────────────────

    void pushConfig()
    {
        // v1 keeps slices == steps: the grid the loop was sliced at is the
        // grid it is remixed on.
        eventRouter.sendSeqSliceConfig ((uint8_t) bars, (uint8_t) division,
                                        (uint8_t) numSteps(), (uint8_t) rootNote,
                                        (uint8_t) direction, (uint8_t) swing);
    }

    void pushStep (int i)
    {
        // Fixed velocity, near-full gate — slices want to run into each other.
        const auto& s = steps[(size_t) i];
        eventRouter.sendSeqStepSet ((uint8_t) i, (uint8_t) s.slice,
                                    100, 95, 64, s.rest);
    }

    void pushAllSteps()
    {
        for (int i = 0; i < numSteps(); ++i)
            pushStep (i);
    }

    void pushRibbonAssignments()
    {
        for (int i = 0; i < 2; ++i)
            eventRouter.sendSeqRibbonAssign ((uint8_t) i, ribbonTarget[i], ribbonCC[i]);
    }

    void sendLooperState()
    {
        eventRouter.sendSeqLooper ((uint8_t) looperDiv, looperHeld,
                                   randomButton.getToggleState());
    }

    // ── Editing ──────────────────────────────────────────────────────────────

    void laneMoved (int step, float v)
    {
        if (step >= numSteps())
            return;

        const int maxSlice = juce::jmax (0, numSteps() - 1);
        auto& s = steps[(size_t) step];

        s.slice = juce::jlimit (0, maxSlice, juce::roundToInt (v * (float) maxSlice));

        // Snap the marker onto the cell, not between two.
        lanes.setValue (step, maxSlice > 0 ? (float) s.slice / (float) maxSlice : 0.0f);
        stepBoxes[step]->setNoteName (juce::String (s.slice + 1));
        pushStep (step);
    }

    // Same sweep behaviour as the step page: the box under the initial press
    // decides whether the drag knocks steps out or brings them back in.
    void paintStepAt (juce::Point<int> pos, bool isStart)
    {
        for (int i = 0; i < numSteps(); ++i)
        {
            if (! stepBoxes[i]->getBounds().contains (pos))
                continue;

            if (isStart)
                paintRestState = ! steps[(size_t) i].rest;

            if (steps[(size_t) i].rest == paintRestState)
                return;

            steps[(size_t) i].rest = paintRestState;
            stepBoxes[i]->setOn (! paintRestState);
            refreshAllCells();
            pushStep (i);
            return;
        }
    }

    // ── Ribbon assignment ────────────────────────────────────────────────────

    void showRibbonAssignMenu (int ribbonIndex)
    {
        const auto target = ribbonTarget[ribbonIndex];
        const auto cc     = ribbonCC[ribbonIndex];
        const bool isCC   = (target == MidiMorf::RibbonTarget::CC);

        juce::PopupMenu menu;
        menu.addItem (1, "Pitch Bend",     true, target == MidiMorf::RibbonTarget::PitchBend);
        menu.addItem (2, "CC 1 (Mod)",     true, isCC && cc == 1);
        menu.addItem (3, "CC 2 (Breath)",  true, isCC && cc == 2);
        menu.addItem (4, "CC 11 (Expr)",   true, isCC && cc == 11);
        menu.addItem (5, "CC 74 (Cutoff)", true, isCC && cc == 74);
        menu.addItem (6, "Step Repeat",    true, target == MidiMorf::RibbonTarget::StepRepeat);

        menu.showMenuAsync (juce::PopupMenu::Options()
                                .withTargetComponent (ribbons[ribbonIndex])
                                .withStandardItemHeight (32),
            [this, ribbonIndex] (int result)
            {
                if (result > 0)
                    applyRibbonAssignment (ribbonIndex, result, true);
            });
    }

    void applyRibbonAssignment (int ribbonIndex, int menuId, bool push)
    {
        static const uint8_t ccForId[]  = { 0, 0, 1, 2, 11, 74, 0 };
        static const char* textForId[]  = { "", "PB", "CC1", "CC2", "CC11", "CC74", "x1" };

        ribbonTarget[ribbonIndex] = (menuId == 1) ? MidiMorf::RibbonTarget::PitchBend
                                  : (menuId == 6) ? MidiMorf::RibbonTarget::StepRepeat
                                                  : MidiMorf::RibbonTarget::CC;
        ribbonCC[ribbonIndex] = ccForId[menuId];

        const auto mode = (menuId == 1) ? RibbonStrip::Mode::SpringCentre
                        : (menuId == 6) ? RibbonStrip::Mode::SteppedRepeat
                                        : RibbonStrip::Mode::Hold;

        ribbons[ribbonIndex]->setAssignment (textForId[menuId], mode);

        if (push)
            eventRouter.sendSeqRibbonAssign ((uint8_t) ribbonIndex,
                                             ribbonTarget[ribbonIndex], ribbonCC[ribbonIndex]);
    }

    // ── Edit popup ───────────────────────────────────────────────────────────

    // Small callout with the settings that don't earn permanent screen space:
    // bars, division, slice root, direction, swing.
    class EditPanel : public juce::Component
    {
    public:
        EditPanel (SliceSequencerContent& c) : owner (c)
        {
            setLookAndFeel (&owner.glassLaf);

            auto addRow = [this] (juce::Label& label, const juce::String& text, juce::ComboBox& combo)
            {
                label.setText (text, juce::dontSendNotification);
                label.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.7f));
                label.setFont (juce::Font (juce::FontOptions (12.0f)));
                addAndMakeVisible (label);
                addAndMakeVisible (combo);
            };

            addRow (barsLabel,  "BARS",  barsCombo);
            barsCombo.addItem ("1", 1);
            barsCombo.addItem ("2", 2);
            barsCombo.setSelectedId (owner.bars, juce::dontSendNotification);

            addRow (divLabel,   "DIV",   divCombo);
            divCombo.addItem ("1/4", 1);
            divCombo.addItem ("1/8", 2);
            divCombo.addItem ("1/16", 3);
            divCombo.setSelectedId (owner.division + 1, juce::dontSendNotification);

            addRow (rootLabel,  "ROOT",  rootCombo);
            {
                static const char* names[12] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };

                // Octave numbers use Live's naming (middle C = C3) so the
                // root the user reads off Simpler is the root they pick here:
                // its default slice root C1 = MIDI 36. C0..B3 covers
                // everywhere that root plausibly sits.
                for (int n = 24; n < 72; ++n)
                    rootCombo.addItem (juce::String (names[n % 12]) + juce::String (n / 12 - 2), n);

                rootCombo.setSelectedId (owner.rootNote, juce::dontSendNotification);
            }

            addRow (dirLabel,   "DIR",   dirCombo);
            dirCombo.addItem (">>", 1);
            dirCombo.addItem ("<<", 2);
            dirCombo.addItem ("<>", 3);
            dirCombo.addItem ("?",  4);
            dirCombo.setSelectedId (owner.direction + 1, juce::dontSendNotification);

            addRow (swingLabel, "SWING", swingCombo);
            for (int i = 0; i <= 6; ++i)
                swingCombo.addItem (juce::String (i * 10) + "%", i + 1);
            swingCombo.setSelectedId (owner.swing / 10 + 1, juce::dontSendNotification);

            auto apply = [this] { applyToOwner(); };
            barsCombo.onChange  = apply;
            divCombo.onChange   = apply;
            rootCombo.onChange  = apply;
            dirCombo.onChange   = apply;
            swingCombo.onChange = apply;

            setSize (210, 5 * 34 + 12);
        }

        ~EditPanel() override { setLookAndFeel (nullptr); }

        void resized() override
        {
            auto area = getLocalBounds().reduced (6);

            juce::Label*    labels[] = { &barsLabel, &divLabel, &rootLabel, &dirLabel, &swingLabel };
            juce::ComboBox* combos[] = { &barsCombo, &divCombo, &rootCombo, &dirCombo, &swingCombo };

            for (int i = 0; i < 5; ++i)
            {
                auto row = area.removeFromTop (30);
                area.removeFromTop (4);
                labels[i]->setBounds (row.removeFromLeft (54));
                combos[i]->setBounds (row);
            }
        }

    private:
        void applyToOwner()
        {
            const int newBars = barsCombo.getSelectedId();
            const int newDiv  = divCombo.getSelectedId() - 1;
            const bool gridChanged = (newBars != owner.bars || newDiv != owner.division);

            owner.bars      = newBars;
            owner.division  = newDiv;
            owner.rootNote  = rootCombo.getSelectedId();
            owner.direction = dirCombo.getSelectedId() - 1;
            owner.swing     = (swingCombo.getSelectedId() - 1) * 10;

            // A grid change re-slices and resets to identity (server does the
            // same); anything else just updates config in place.
            if (gridChanged)
                owner.applyGrid (true);
            else
            {
                owner.pushConfig();
                owner.pushAllSteps();
            }
        }

        SliceSequencerContent& owner;
        juce::Label barsLabel, divLabel, rootLabel, dirLabel, swingLabel;
        juce::ComboBox barsCombo, divCombo, rootCombo, dirCombo, swingCombo;
    };

    void showEditPanel()
    {
        juce::CallOutBox::launchAsynchronously (std::make_unique<EditPanel> (*this),
                                                editButton.getScreenBounds(),
                                                nullptr);
    }

    EventRouter& eventRouter;

    GlassButtonLookAndFeel glassLaf { sliceSeqPagePalette, sliceSeqAccent };

    juce::OwnedArray<RibbonStrip> ribbons;
    juce::TextButton editButton, resetButton, randomButton;
    LooperStrip looper;
    juce::OwnedArray<StepBox> stepBoxes;
    LaneStrip lanes;
    SeqTransportBar transport { eventRouter, LocalSequencers::Slice, true };

    std::array<SliceStep, MAX_STEPS> steps;

    int  bars      = 1;      // 1..2
    int  division  = 2;      // 0=1/4, 1=1/8, 2=1/16
    int  rootNote  = 36;     // C1 — Simpler's default slice root
    int  direction = 0;      // matches ClockedSequencer::Direction
    int  swing     = 0;      // 0..60, percent

    MidiMorf::RibbonTarget ribbonTarget[2] = { MidiMorf::RibbonTarget::PitchBend,
                                               MidiMorf::RibbonTarget::StepRepeat };
    uint8_t ribbonCC[2] = { 0, 0 };

    int  looperDiv  = 2;     // 0=1/4 .. 3=1/32
    bool looperHeld = false;

    bool paintRestState = false;   // direction of the in-progress box sweep
    int  playhead = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SliceSequencerContent)
};

//==============================================================================
class SliceSequencerPage : public juce::Component
{
public:
    SliceSequencerPage (EventRouter& router) : content (router)
    {
        addAndMakeVisible (content);
    }

    void setPlayhead (int step) { content.setPlayhead (step); }
    void pushFullState()        { content.pushFullState(); }
    SeqPattern getPattern() const               { return content.getPattern(); }
    void showPattern (const SeqPattern& p)      { content.showPattern (p); }
    void loadPattern (const SeqPattern& p)      { content.loadPattern (p); }
    void setOnStateChanged (std::function<void()> f) { content.getTransport().onStateChanged = std::move (f); }

    void resized() override { content.setBounds (getLocalBounds()); }

private:
    SliceSequencerContent content;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SliceSequencerPage)
};
