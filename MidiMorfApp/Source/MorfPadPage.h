#pragma once

#include <JuceHeader.h>
#include "EventRouter.h"
#include "MidiMorfColors.h"
#include "PianoKeySelector.h"
#include "Glassmorphism.h"
#include <vector>
#include <algorithm>

// Deep burgundy palette — warm/expressive identity for the melodic XY pad.
// Complements the teal FX page (cool↔warm) and shares the same black edge.
static const PagePalette morfPadPagePalette {
    juce::Colour (0xff2A0F1A),       // substrate top — deep wine
    juce::Colour (0xff15050D),       // substrate bottom — near-black with red undertone
    juce::Colour (0xCC4A1F30),       // acrylic top — translucent burgundy
    juce::Colour (0xD82A1020),       // acrylic bottom — deeper translucent
    juce::Colour (0xCC000000)        // edge — translucent black
};

//==============================================================================
// Rotated controls strip — scale row at portrait-top (lands at landscape-LEFT
// after CCW rotation), piano keys in the middle, octave controls at
// portrait-bottom (lands at landscape-RIGHT next to the XY pad).
class MorfPadControlsStrip : public juce::Component
{
public:
    // Notifies the host whenever scale/root/range/baseOct changes.
    std::function<void (uint8_t /*scale*/, uint8_t /*root*/, uint8_t /*range*/, uint8_t /*baseOct*/)> onConfigChanged;

    MorfPadControlsStrip()
    {
        // Scale picker — single ComboBox sized to fit alongside the octave
        // controls. Replaces the old 5-button radio row.
        for (int i = 0; i < numScales; ++i)
            scaleCombo.addItem (scaleNames[i], i + 1);
        scaleCombo.setSelectedId (1, juce::dontSendNotification);
        scaleCombo.setColour (juce::ComboBox::backgroundColourId, morfPadPagePalette.substrateTop);
        scaleCombo.setColour (juce::ComboBox::textColourId,       MidiMorfColors::textLight);
        scaleCombo.setColour (juce::ComboBox::outlineColourId,    morfPadPagePalette.edgeGlow);
        scaleCombo.setColour (juce::ComboBox::arrowColourId,      MidiMorfColors::accent);
        scaleCombo.onChange = [this]
        {
            scaleIndex = (uint8_t) juce::jmax (0, scaleCombo.getSelectedId() - 1);
            notifyChange();
        };
        addAndMakeVisible (scaleCombo);

        pianoKeys.onNoteSelected = [this] (int note)
        {
            rootNote = (uint8_t) note;
            notifyChange();
        };
        addAndMakeVisible (pianoKeys);

        octaveDownBtn.setButtonText ("-");
        octaveDownBtn.setColour (juce::TextButton::textColourOffId, MidiMorfColors::textLight);
        octaveDownBtn.setColour (juce::TextButton::textColourOnId,  MidiMorfColors::textLight);
        octaveDownBtn.setLookAndFeel (&glassLnf);
        octaveDownBtn.onClick = [this]
        {
            if (baseOctave > 0) { --baseOctave; updateOctaveLabel(); notifyChange(); }
        };
        addAndMakeVisible (octaveDownBtn);

        octaveLabel.setText ("C2", juce::dontSendNotification);
        octaveLabel.setJustificationType (juce::Justification::centred);
        octaveLabel.setColour (juce::Label::textColourId, MidiMorfColors::textLight);
        addAndMakeVisible (octaveLabel);

        octaveUpBtn.setButtonText ("+");
        octaveUpBtn.setColour (juce::TextButton::textColourOffId, MidiMorfColors::textLight);
        octaveUpBtn.setColour (juce::TextButton::textColourOnId,  MidiMorfColors::textLight);
        octaveUpBtn.setLookAndFeel (&glassLnf);
        octaveUpBtn.onClick = [this]
        {
            if (baseOctave < 8) { ++baseOctave; updateOctaveLabel(); notifyChange(); }
        };
        addAndMakeVisible (octaveUpBtn);

        for (int i = 1; i <= 4; ++i)
        {
            auto* btn = new juce::TextButton (juce::String (i));
            btn->setRadioGroupId (3);
            btn->setClickingTogglesState (true);
            btn->setColour (juce::TextButton::textColourOnId,  MidiMorfColors::textLight);
            btn->setColour (juce::TextButton::textColourOffId, MidiMorfColors::textLight);
            btn->setLookAndFeel (&glassLnf);
            btn->onClick = [this, i] { octaveRange = (uint8_t) i; notifyChange(); };
            if (i == 2) btn->setToggleState (true, juce::dontSendNotification);
            octaveButtons.add (btn);
            addAndMakeVisible (btn);
        }
    }

    ~MorfPadControlsStrip() override
    {
        // glassLnf is declared before the buttons so it outlives them, but
        // nulling explicitly silences JUCE's LookAndFeel assertion.
        octaveDownBtn.setLookAndFeel (nullptr);
        octaveUpBtn.setLookAndFeel   (nullptr);
        for (auto* b : octaveButtons) b->setLookAndFeel (nullptr);
    }

    void resized() override
    {
        // Native landscape layout (no parent rotation any more). Text reads
        // upright on every label, button, and combo. Piano keys are the only
        // thing that needs to be rotated — they handle that themselves.
        auto area = getLocalBounds();

        constexpr int rowH = 32;
        constexpr int gap  = 4;

        // 1) Scale combo
        auto comboRow = area.removeFromTop (rowH);
        scaleCombo.setBounds (comboRow.reduced (2));
        area.removeFromTop (gap);

        // 2) Octave -/+ around a base-note label
        auto octRow = area.removeFromTop (rowH);
        const int btnW   = 30;
        const int labelW = octRow.getWidth() - btnW * 2;
        octaveDownBtn.setBounds (octRow.getX(),                  octRow.getY(), btnW,   octRow.getHeight());
        octaveLabel  .setBounds (octRow.getX() + btnW,           octRow.getY(), labelW, octRow.getHeight());
        octaveUpBtn  .setBounds (octRow.getX() + btnW + labelW,  octRow.getY(), btnW,   octRow.getHeight());
        area.removeFromTop (gap);

        // 3) Range 1..4
        auto rangeRow = area.removeFromTop (rowH);
        const int rangeColW = rangeRow.getWidth() / 4;
        for (int i = 0; i < octaveButtons.size(); ++i)
            octaveButtons[i]->setBounds (rangeRow.getX() + i * rangeColW,
                                          rangeRow.getY(),
                                          rangeColW - 2,
                                          rangeRow.getHeight());
        area.removeFromTop (gap);

        // 4) Piano keys fill the rest. PianoKeySelector draws horizontally;
        // we rotate it -π/2 so it runs vertical inside the strip.
        auto keysArea = area;
        pianoKeys.setSize (keysArea.getHeight(), keysArea.getWidth());
        pianoKeys.setTransform (
            juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi)
                .translated (0.0f, (float) keysArea.getHeight())
                .translated ((float) keysArea.getX(), (float) keysArea.getY()));
    }

    void setState (uint8_t scale, uint8_t root, uint8_t range, uint8_t baseOct)
    {
        scaleIndex  = juce::jlimit ((uint8_t) 0, (uint8_t) (numScales - 1), scale);
        rootNote    = juce::jlimit ((uint8_t) 0, (uint8_t) 11, root);
        octaveRange = juce::jlimit ((uint8_t) 1, (uint8_t) 4, range);
        baseOctave  = juce::jlimit ((uint8_t) 0, (uint8_t) 8, baseOct);

        scaleCombo.setSelectedId ((int) scaleIndex + 1, juce::dontSendNotification);
        pianoKeys.setSelectedNote (rootNote);
        for (int i = 0; i < octaveButtons.size(); ++i)
            octaveButtons[i]->setToggleState (i + 1 == octaveRange, juce::dontSendNotification);
        updateOctaveLabel();
    }

    uint8_t getScaleIndex()  const { return scaleIndex; }
    uint8_t getRootNote()    const { return rootNote; }
    uint8_t getOctaveRange() const { return octaveRange; }
    uint8_t getBaseOctave()  const { return baseOctave; }

private:
    void notifyChange()
    {
        if (onConfigChanged) onConfigChanged (scaleIndex, rootNote, octaveRange, baseOctave);
    }

    void updateOctaveLabel()
    {
        octaveLabel.setText ("C" + juce::String ((int) baseOctave), juce::dontSendNotification);
    }

    static constexpr int numScales = 5;
    const juce::String scaleNames[5] = { "Major", "Minor", "Pentatonic", "Blues", "Chromatic" };

    // Must outlive every button it skins — declared before them so it's
    // destroyed last.
    GlassButtonLookAndFeel glassLnf { morfPadPagePalette, MidiMorfColors::accent, 6.0f };

    juce::ComboBox scaleCombo;
    PianoKeySelector pianoKeys;
    juce::TextButton octaveDownBtn, octaveUpBtn;
    juce::Label octaveLabel;
    juce::OwnedArray<juce::TextButton> octaveButtons;

    uint8_t scaleIndex = 0;
    uint8_t rootNote = 0;
    uint8_t octaveRange = 2;
    uint8_t baseOctave = 2;
};

//==============================================================================
// Morf-Pad page: rotated controls strip on the left (CCW 90°), XY pad on the
// right rendering natively in landscape.
class MorfPadPage : public juce::Component
{
public:
    MorfPadPage (EventRouter& client) : networkClient (client)
    {
        addAndMakeVisible (controlsStrip);
        controlsStrip.onConfigChanged =
            [this] (uint8_t s, uint8_t r, uint8_t rg, uint8_t b)
        {
            scaleIndex = s; rootNote = r; octaveRange = rg; baseOctave = b;
            sendScaleConfig();
        };
    }

    void paint (juce::Graphics& g) override
    {
        // Page background — deep burgundy glass panel
        Glassmorphism::paintPanel (g, getLocalBounds().toFloat(), morfPadPagePalette);

        // Recessed XY pad surface sits on top of the panel
        g.setColour (MidiMorfColors::padBackground);
        g.fillRoundedRectangle (xyArea.toFloat(), 8.0f);

        g.setColour (MidiMorfColors::padBorder);
        g.drawRoundedRectangle (xyArea.toFloat().reduced (1.0f), 8.0f, 1.5f);

        if (touching)
        {
            float px = xyArea.getX() + touchX * xyArea.getWidth();
            float py = xyArea.getY() + (1.0f - touchY) * xyArea.getHeight();

            g.setColour (MidiMorfColors::crosshair);
            g.drawHorizontalLine ((int) py, (float) xyArea.getX(), (float) xyArea.getRight());
            g.drawVerticalLine   ((int) px, (float) xyArea.getY(), (float) xyArea.getBottom());

            float dotSize = 30.0f;
            g.setColour (MidiMorfColors::dot.withAlpha (0.8f));
            g.fillEllipse (px - dotSize * 0.5f, py - dotSize * 0.5f, dotSize, dotSize);
        }
        else
        {
            g.setColour (MidiMorfColors::textLight.withAlpha (0.3f));
            g.setFont (juce::FontOptions (12.0f));
            g.drawText ("MORF-PAD", xyArea.toFloat().reduced (10.0f), juce::Justification::topLeft);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (12);
        if (area.getWidth() <= 0 || area.getHeight() <= 0) return;

        // Left strip in landscape: 110 wide, full height. Native landscape
        // layout now — the strip's children handle their own rotation if any
        // (piano keys do this internally; everything else stays upright so
        // labels and the scale combo read naturally).
        const int stripW = 110;
        auto stripArea = area.removeFromLeft (stripW);
        controlsStrip.setBounds (stripArea);

        area.removeFromLeft (8);

        // XY pad fills the rest natively in landscape.
        xyArea = area;
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! xyArea.toFloat().contains (e.position)) return;

        int srcIdx = e.source.getIndex();

        TouchInfo ti;
        ti.sourceIndex = srcIdx;
        posFromEvent (e.position, ti.x, ti.y);
        activeTouches.push_back (ti);

        if (activeTouches.size() > 1)
            networkClient.sendMorfPadTouchUp();

        touchX = ti.x;
        touchY = ti.y;
        touching = true;
        networkClient.sendMorfPadTouchDown (touchX, touchY);
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        int srcIdx = e.source.getIndex();

        for (auto& t : activeTouches)
        {
            if (t.sourceIndex == srcIdx)
            {
                posFromEvent (e.position, t.x, t.y);
                break;
            }
        }

        if (! activeTouches.empty() && activeTouches.back().sourceIndex == srcIdx)
        {
            touchX = activeTouches.back().x;
            touchY = activeTouches.back().y;
            networkClient.sendMorfPadTouchMove (touchX, touchY);
            repaint();
        }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        int srcIdx = e.source.getIndex();
        bool wasActive = (! activeTouches.empty() && activeTouches.back().sourceIndex == srcIdx);

        activeTouches.erase (
            std::remove_if (activeTouches.begin(), activeTouches.end(),
                [srcIdx] (const TouchInfo& t) { return t.sourceIndex == srcIdx; }),
            activeTouches.end());

        if (wasActive)
        {
            networkClient.sendMorfPadTouchUp();

            if (! activeTouches.empty())
            {
                auto& prev = activeTouches.back();
                touchX = prev.x;
                touchY = prev.y;
                networkClient.sendMorfPadTouchDown (touchX, touchY);
            }
            else
            {
                touching = false;
            }
            repaint();
        }
    }

    void saveState (juce::PropertySet& props)
    {
        props.setValue ("morfPad_scaleIndex", (int) scaleIndex);
        props.setValue ("morfPad_rootNote",   (int) rootNote);
        props.setValue ("morfPad_octaveRange", (int) octaveRange);
        props.setValue ("morfPad_baseOctave",  (int) baseOctave);
    }

    void restoreState (const juce::PropertySet& props)
    {
        scaleIndex  = (uint8_t) props.getIntValue ("morfPad_scaleIndex", 0);
        rootNote    = (uint8_t) props.getIntValue ("morfPad_rootNote", 0);
        octaveRange = (uint8_t) props.getIntValue ("morfPad_octaveRange", 2);
        baseOctave  = (uint8_t) props.getIntValue ("morfPad_baseOctave", 2);

        controlsStrip.setState (scaleIndex, rootNote, octaveRange, baseOctave);
        sendScaleConfig();
    }

private:
    struct TouchInfo
    {
        int sourceIndex;
        float x, y;
    };

    void posFromEvent (juce::Point<float> pos, float& outX, float& outY)
    {
        outX = juce::jlimit (0.0f, 1.0f, (pos.x - xyArea.getX()) / (float) xyArea.getWidth());
        outY = juce::jlimit (0.0f, 1.0f, 1.0f - (pos.y - xyArea.getY()) / (float) xyArea.getHeight());
    }

    void sendScaleConfig()
    {
        networkClient.sendMorfPadScaleSelect (scaleIndex, rootNote, octaveRange, baseOctave);
    }

    EventRouter& networkClient;
    MorfPadControlsStrip controlsStrip;

    uint8_t scaleIndex = 0;
    uint8_t rootNote = 0;
    uint8_t octaveRange = 2;
    uint8_t baseOctave = 2;

    juce::Rectangle<int> xyArea;
    bool touching = false;
    float touchX = 0.5f;
    float touchY = 0.5f;
    std::vector<TouchInfo> activeTouches;
};
