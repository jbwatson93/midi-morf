#pragma once

#include <JuceHeader.h>
#include "MidiMorfColors.h"

// Shared synth-page widgets: audition ribbon, knob, fader, labelled combo,
// section card and FX module card. Styled by SynthLookAndFeel when it's set
// on an ancestor.

//==============================================================================
// AuditionStrip — 2-octave horizontal mono ribbon. Drag X to choose a pitch,
// release to stop. Used as an audition keyboard at the bottom of the synth
// settings page so the user can hear edits while tweaking.
class AuditionStrip : public juce::Component
{
public:
    std::function<void (int midiNote, float velocity)> onNoteOn;
    std::function<void (int midiNote)>                  onNoteOff;

    void setRange (int low, int high)
    {
        lowNote  = low;
        highNote = high;
        repaint();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        int n = noteFromX ((float) e.x);
        currentNote = n;
        active = true;
        if (onNoteOn) onNoteOn (n, 0.85f);
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! active) return;
        int n = noteFromX ((float) e.x);
        if (n != currentNote)
        {
            int prev = currentNote;
            currentNote = n;
            if (onNoteOff) onNoteOff (prev);
            if (onNoteOn)  onNoteOn (n, 0.85f);
            repaint();
        }
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (! active) return;
        int prev = currentNote;
        active = false;
        currentNote = -1;
        if (onNoteOff) onNoteOff (prev);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().reduced (1.5f);

        // Recessed dark slot
        g.setColour (juce::Colour (0xff141417));
        g.fillRoundedRectangle (bounds, 5.0f);

        // Semitone tick marks (subtle, bottom of strip)
        const int range = juce::jmax (1, highNote - lowNote);
        g.setColour (juce::Colour (0xff2A2A30));
        for (int i = 0; i <= range; ++i)
        {
            float t = (float) i / (float) range;
            float x = bounds.getX() + t * bounds.getWidth();
            g.drawVerticalLine ((int) x, bounds.getBottom() - 5.0f, bounds.getBottom());
        }

        // Octave divider lines + C labels
        g.setColour (juce::Colour (0xff3A3A42));
        g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
        for (int n = lowNote; n <= highNote; n += 12)
        {
            float t = (float) (n - lowNote) / (float) range;
            float x = bounds.getX() + t * bounds.getWidth();
            g.setColour (juce::Colour (0xff3A3A42));
            g.drawVerticalLine ((int) x, bounds.getY() + 2.0f, bounds.getBottom() - 6.0f);

            int oct = (n / 12) - 1;  // MIDI convention: MIDI 60 = C4
            juce::String label = "C" + juce::String (oct);
            g.setColour (MidiMorfColors::textLight.withAlpha (0.45f));
            g.drawText (label,
                        juce::Rectangle<float> (x - 14.0f, bounds.getY() + 2.0f, 28.0f, 11.0f),
                        juce::Justification::centred);
        }

        // Active position indicator
        if (active && currentNote >= lowNote && currentNote <= highNote)
        {
            float t = (float) (currentNote - lowNote) / (float) range;
            float x = bounds.getX() + t * bounds.getWidth();

            // Soft glow halo
            g.setColour (MidiMorfColors::accent.withAlpha (0.25f));
            g.fillRect (juce::Rectangle<float> (x - 10.0f, bounds.getY(),
                                                 20.0f, bounds.getHeight()));

            // Hard accent line
            g.setColour (MidiMorfColors::accent);
            g.fillRect (juce::Rectangle<float> (x - 2.0f, bounds.getY(),
                                                 4.0f, bounds.getHeight()));

            // Note name readout (e.g. "C4", "E♭5")
            g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
            g.setColour (MidiMorfColors::textLight);
            g.drawText (noteName (currentNote),
                        juce::Rectangle<float> (x - 22.0f, bounds.getCentreY() - 7.0f,
                                                 44.0f, 14.0f),
                        juce::Justification::centred);
        }

        // Outer border
        g.setColour (juce::Colour (0xff3A3A40));
        g.drawRoundedRectangle (bounds, 5.0f, 1.0f);
    }

private:
    int noteFromX (float x) const
    {
        auto b = getLocalBounds().toFloat().reduced (1.5f);
        float t = juce::jlimit (0.0f, 1.0f, (x - b.getX()) / b.getWidth());
        const int range = juce::jmax (1, highNote - lowNote);
        return lowNote + (int) std::round (t * (float) range);
    }

    static juce::String noteName (int midiNote)
    {
        static const char* names[] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
        int oct = (midiNote / 12) - 1;
        int pc  = midiNote % 12;
        return juce::String (names[pc]) + juce::String (oct);
    }

    int lowNote  = 48;  // C3
    int highNote = 72;  // C5 — 2 octaves
    int currentNote = -1;
    bool active = false;
};

//==============================================================================
// What a knob needs from the synth page to show and take modulation, without
// knowing about the engine. Destinations are the engine's ModDest ids.
class ModAssignHost
{
public:
    virtual ~ModAssignHost() = default;

    // amount -1..1. A bipolar source (an LFO, tilt) swings both ways from
    // the knob, so its ring is drawn both sides of the value.
    struct Ring { juce::Colour colour; float amount; bool bipolar = false; };

    // The source being assigned (tapped in the source strip), or -1.
    virtual int  assigningSource() const = 0;
    virtual juce::Colour assigningColour() const = 0;
    // Drag on a knob while assigning: change that source's amount on `dest`.
    virtual void nudgeRoute (int dest, float delta) = 0;
    // Double-tap while assigning: take that source's route off `dest`.
    virtual void removeRoute (int dest) = 0;
    virtual std::vector<Ring> ringsFor (int dest) const = 0;
    // What the matrix adds to `dest` on the newest note right now.
    virtual float liveModFor (int dest) const = 0;
};

//==============================================================================
// Rotary knob with a label below. Wraps a juce::Slider in rotary mode.
//
// Given a mod destination (setModDest), it draws a coloured ring per route
// to it, and a dot where the modulation has it now. While a source is being
// assigned, dragging the knob sets that source's amount instead of turning it.
class Knob : public juce::Component
{
public:
    Knob (const juce::String& name, float minV, float maxV, float val,
          float step = 0.01f, float skewMid = 0.0f)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 16);
        slider.setRange (minV, maxV, step);
        if (skewMid > 0.0f) slider.setSkewFactorFromMidPoint (skewMid);
        slider.setValue (val, juce::dontSendNotification);
        slider.setColour (juce::Slider::rotarySliderFillColourId,    MidiMorfColors::accent);
        slider.setColour (juce::Slider::rotarySliderOutlineColourId, MidiMorfColors::padBorder);
        slider.setColour (juce::Slider::thumbColourId,                MidiMorfColors::textLight);
        slider.setColour (juce::Slider::textBoxTextColourId,          MidiMorfColors::textLight);
        slider.setColour (juce::Slider::textBoxBackgroundColourId,    juce::Colours::transparentBlack);
        slider.setColour (juce::Slider::textBoxOutlineColourId,       juce::Colours::transparentBlack);
        addAndMakeVisible (slider);

        label.setText (name, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setColour (juce::Label::textColourId, MidiMorfColors::textLight);
        label.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        addAndMakeVisible (label);
    }

    void resized() override
    {
        auto a = getLocalBounds();
        label.setBounds (a.removeFromBottom (16));
        slider.setBounds (a);
    }

    juce::Slider& getSlider() noexcept { return slider; }

    //--------------------------------------------------------------------------
    void setModDest (ModAssignHost* h, int dest) { host = h; modDest = dest; refreshModState(); }
    int  getModDest() const noexcept { return host != nullptr ? modDest : -1; }

    // The page calls this when assigning starts or stops, and to animate.
    void refreshModState()
    {
        const bool assigning = host != nullptr && host->assigningSource() >= 0;
        slider.setInterceptsMouseClicks (! assigning, ! assigning);
        repaint();
    }

    bool hasModulation() const { return host != nullptr && ! host->ringsFor (modDest).empty(); }

    void paintOverChildren (juce::Graphics& g) override
    {
        if (host == nullptr) return;

        auto knob = slider.getBounds().withTrimmedBottom (16).toFloat();
        const float radius = juce::jmin (knob.getWidth(), knob.getHeight()) * 0.5f - 1.0f;
        const auto centre = knob.getCentre();
        const auto rp = slider.getRotaryParameters();
        const float span = rp.endAngleRadians - rp.startAngleRadians;
        const float value = (float) slider.valueToProportionOfLength (slider.getValue());
        auto angle = [&] (float proportion) { return rp.startAngleRadians + span * juce::jlimit (0.0f, 1.0f, proportion); };

        if (host->assigningSource() >= 0)
        {
            g.setColour (host->assigningColour().withAlpha (0.55f));
            const float dashes[] = { 3.0f, 3.0f };
            juce::Path outline;
            outline.addEllipse (centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f);
            juce::Path dashed;
            juce::PathStrokeType (1.2f).createDashedStroke (dashed, outline, dashes, 2);
            g.fillPath (dashed);
        }

        auto rings = host->ringsFor (modDest);
        float r = radius + 1.0f;
        for (auto& ring : rings)
        {
            juce::Path arc;
            const float a0 = angle (ring.bipolar ? value - ring.amount : value), a1 = angle (value + ring.amount);
            arc.addCentredArc (centre.x, centre.y, r, r, 0.0f, juce::jmin (a0, a1), juce::jmax (a0, a1), true);
            g.setColour (ring.colour);
            g.strokePath (arc, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            r -= 3.0f;
        }

        if (! rings.empty())
        {
            const float live = host->liveModFor (modDest);
            if (std::abs (live) > 0.0005f)
            {
                const float a = angle (value + live);
                const juce::Point<float> p (centre.x + std::sin (a) * (radius + 1.0f), centre.y - std::cos (a) * (radius + 1.0f));
                g.setColour (juce::Colours::white);
                g.fillEllipse (p.x - 3.0f, p.y - 3.0f, 6.0f, 6.0f);
            }
        }
    }

    void mouseDown (const juce::MouseEvent& e) override { lastDragY = e.position.y; }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (host == nullptr || host->assigningSource() < 0) return;
        const float dy = lastDragY - e.position.y;
        lastDragY = e.position.y;
        host->nudgeRoute (modDest, dy / 150.0f);
        repaint();
    }

    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        if (host == nullptr || host->assigningSource() < 0) return;
        host->removeRoute (modDest);
        repaint();
    }

private:
    juce::Slider slider;
    juce::Label  label;
    ModAssignHost* host = nullptr;
    int   modDest = -1;
    float lastDragY = 0.0f;
};

//==============================================================================
// Juno-style vertical fader (label below). Used for ADSR envelopes.
class Fader : public juce::Component
{
public:
    Fader (const juce::String& name, float minV, float maxV, float val,
           float step = 0.01f, float skewMid = 0.0f)
    {
        slider.setSliderStyle (juce::Slider::LinearVertical);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider.setRange (minV, maxV, step);
        if (skewMid > 0.0f) slider.setSkewFactorFromMidPoint (skewMid);
        slider.setValue (val, juce::dontSendNotification);
        addAndMakeVisible (slider);

        label.setText (name, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setColour (juce::Label::textColourId, MidiMorfColors::textLight);
        label.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        addAndMakeVisible (label);
    }

    void resized() override
    {
        auto a = getLocalBounds();
        label.setBounds (a.removeFromBottom (16));
        slider.setBounds (a);
    }

    juce::Slider& getSlider() noexcept { return slider; }

private:
    juce::Slider slider;
    juce::Label  label;
};

//==============================================================================
// A simple labelled combo box (used for LFO waveform/target + routing dests).
class LabeledCombo : public juce::Component
{
public:
    LabeledCombo (const juce::String& name)
    {
        combo.setColour (juce::ComboBox::backgroundColourId,    MidiMorfColors::padBackground);
        combo.setColour (juce::ComboBox::textColourId,          MidiMorfColors::textLight);
        combo.setColour (juce::ComboBox::outlineColourId,       MidiMorfColors::padBorder);
        combo.setColour (juce::ComboBox::arrowColourId,         MidiMorfColors::textLight);
        combo.setColour (juce::ComboBox::buttonColourId,        MidiMorfColors::buttonFace);
        addAndMakeVisible (combo);

        label.setText (name, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setColour (juce::Label::textColourId, MidiMorfColors::textLight);
        label.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        addAndMakeVisible (label);
    }

    void resized() override
    {
        auto a = getLocalBounds();
        label.setBounds (a.removeFromBottom (16));
        a.reduce (4, 4);
        combo.setBounds (a.withSizeKeepingCentre (a.getWidth(), 28));
    }

    juce::ComboBox& getCombo() noexcept { return combo; }

private:
    juce::ComboBox combo;
    juce::Label    label;
};

//==============================================================================
// SectionCard — labelled module card with accent stripe down the left, no
// on/off button. Used to group related controls on the OSC tab (OSC 1, OSC 2,
// AMP) so the page reads as distinct modules rather than a flat knob row.
class SectionCard : public juce::Component
{
public:
    SectionCard (const juce::String& name) : moduleName (name) {}

    // Children should be positioned inside getContentArea().
    juce::Rectangle<int> getContentArea() const
    {
        return getLocalBounds().withTrimmedTop (26).reduced (8, 6);
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().reduced (1.5f);

        g.setColour (juce::Colour (0xff232328));
        g.fillRoundedRectangle (bounds, 6.0f);

        g.setColour (MidiMorfColors::accent);
        g.fillRect (bounds.withWidth (3.0f));

        auto header = bounds.withHeight (22.0f);
        g.setColour (juce::Colour (0xff2A2A30));
        g.fillRoundedRectangle (header.withTrimmedLeft (3.0f), 6.0f);

        g.setColour (MidiMorfColors::textLight);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (moduleName, header.reduced (10.0f, 0.0f),
                    juce::Justification::centredLeft);

        g.setColour (juce::Colour (0xff3A3A40));
        g.drawRoundedRectangle (bounds.reduced (0.5f), 6.0f, 1.0f);
    }

private:
    juce::String moduleName;
};

//==============================================================================
// FxModule — Serum-style FX card. Header bar with the FX name + a power
// toggle on the right; knob area below. Bypassed modules dim their content
// and switch their accent stripe off.
class FxModule : public juce::Component
{
public:
    FxModule (const juce::String& name,
              std::function<bool()> getEnabled,
              std::function<void(bool)> setEnabled)
        : moduleName (name), enabledGetter (std::move (getEnabled)), enabledSetter (std::move (setEnabled))
    {
        powerBtn.setClickingTogglesState (true);
        powerBtn.setToggleState (enabledGetter(), juce::dontSendNotification);
        powerBtn.setColour (juce::TextButton::buttonColourId,   juce::Colour (0xff1F1F23));
        powerBtn.setColour (juce::TextButton::buttonOnColourId, MidiMorfColors::accent);
        powerBtn.setColour (juce::TextButton::textColourOffId,  juce::Colour (0xff666666));
        powerBtn.setColour (juce::TextButton::textColourOnId,   juce::Colours::white);
        powerBtn.onClick = [this]
        {
            bool on = powerBtn.getToggleState();
            enabledSetter (on);
            repaint();
            if (onEnabledChanged) onEnabledChanged();
        };
        refreshPowerLabel();
        addAndMakeVisible (powerBtn);
    }

    std::function<void()> onEnabledChanged;

    void refreshFromModel()
    {
        powerBtn.setToggleState (enabledGetter(), juce::dontSendNotification);
        refreshPowerLabel();
        repaint();
    }

    // Caller positions knob children inside getContentArea() after resized().
    juce::Rectangle<int> getContentArea() const
    {
        return getLocalBounds().withTrimmedTop (28).reduced (8, 6);
    }

    void paint (juce::Graphics& g) override
    {
        bool on = enabledGetter();
        auto bounds = getLocalBounds().toFloat().reduced (1.5f);

        // Card background
        g.setColour (juce::Colour (0xff232328));
        g.fillRoundedRectangle (bounds, 6.0f);

        // Left accent stripe when enabled
        if (on)
        {
            g.setColour (MidiMorfColors::accent);
            g.fillRect (bounds.withWidth (3.0f));
        }

        // Header bar
        auto header = bounds.withHeight (24.0f);
        g.setColour (juce::Colour (0xff2A2A30));
        g.fillRoundedRectangle (header.withTrimmedLeft (on ? 3.0f : 0.0f), 6.0f);

        g.setColour (on ? MidiMorfColors::textLight : juce::Colour (0xff666666));
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (moduleName, header.reduced (10.0f, 0.0f), juce::Justification::centredLeft);

        // Outer border
        g.setColour (juce::Colour (0xff3A3A40));
        g.drawRoundedRectangle (bounds.reduced (0.5f), 6.0f, 1.0f);

        // Dim the content area when bypassed
        if (! on)
        {
            g.setColour (juce::Colour (0x80000000));
            g.fillRoundedRectangle (bounds.withTrimmedTop (24.0f).reduced (4.0f), 4.0f);
        }
    }

    void resized() override
    {
        powerBtn.setBounds (getWidth() - 44, 4, 36, 18);
    }

private:
    void refreshPowerLabel()
    {
        powerBtn.setButtonText (powerBtn.getToggleState() ? "ON" : "OFF");
    }

    juce::String moduleName;
    std::function<bool()> enabledGetter;
    std::function<void(bool)> enabledSetter;
    juce::TextButton powerBtn;
};
