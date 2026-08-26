#pragma once

#include <JuceHeader.h>
#include "EventRouter.h"
#include "MidiMorfColors.h"
#include "PianoKeySelector.h"
#include "Glassmorphism.h"
#include <vector>
#include <algorithm>

// Deep plum — related to Morf-Pad's burgundy but a touch cooler/violet so
// the two pad pages read as siblings. Same translucent-black edge as the
// other pages.
static const PagePalette dualMPadPagePalette {
    juce::Colour (0xff1F0D24),       // substrate top — deep plum
    juce::Colour (0xff0F0512),       // substrate bottom — near-black
    juce::Colour (0xCC401A40),       // acrylic top — translucent plum
    juce::Colour (0xD8200D24),       // acrylic bottom — deeper translucent
    juce::Colour (0xCC000000)        // edge — translucent black
};

//==============================================================================
// A single morfPad pad with its own config strip — used twice in DualMPadPage
class DualMPadPanel : public juce::Component
{
public:
    DualMPadPanel (EventRouter& client, uint8_t padIdx)
        : networkClient (client), padIndex (padIdx)
    {
        // Scale picker — single ComboBox replaces the old 5-button row.
        for (int i = 0; i < numScales; ++i)
            scaleCombo.addItem (scaleNames[i], i + 1);
        scaleCombo.setSelectedId (1, juce::dontSendNotification);
        scaleCombo.setColour (juce::ComboBox::backgroundColourId, dualMPadPagePalette.substrateTop);
        scaleCombo.setColour (juce::ComboBox::textColourId,       MidiMorfColors::textLight);
        scaleCombo.setColour (juce::ComboBox::outlineColourId,    dualMPadPagePalette.edgeGlow);
        scaleCombo.setColour (juce::ComboBox::arrowColourId,      MidiMorfColors::accent);
        scaleCombo.onChange = [this]
        {
            scaleIndex = (uint8_t) juce::jmax (0, scaleCombo.getSelectedId() - 1);
            sendScaleConfig();
        };
        addAndMakeVisible (scaleCombo);

        // Piano key selector for root note
        pianoKeys.onNoteSelected = [this] (int note) { rootNote = (uint8_t) note; sendScaleConfig(); };
        addAndMakeVisible (pianoKeys);

        // Base octave +/- buttons
        octaveDownBtn.setButtonText ("-");
        octaveDownBtn.setColour (juce::TextButton::textColourOffId, MidiMorfColors::textLight);
        octaveDownBtn.setColour (juce::TextButton::textColourOnId,  MidiMorfColors::textLight);
        octaveDownBtn.setLookAndFeel (&glassLnf);
        octaveDownBtn.onClick = [this] {
            if (baseOctave > 0) { --baseOctave; updateOctaveLabel(); sendScaleConfig(); }
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
        octaveUpBtn.onClick = [this] {
            if (baseOctave < 8) { ++baseOctave; updateOctaveLabel(); sendScaleConfig(); }
        };
        addAndMakeVisible (octaveUpBtn);

        // Octave range selector
        for (int i = 1; i <= 4; ++i)
        {
            auto* btn = new juce::TextButton (juce::String (i));
            btn->setRadioGroupId (100 + padIdx * 10 + 3);
            btn->setClickingTogglesState (true);
            btn->setColour (juce::TextButton::textColourOnId,  MidiMorfColors::textLight);
            btn->setColour (juce::TextButton::textColourOffId, MidiMorfColors::textLight);
            btn->setLookAndFeel (&glassLnf);
            btn->onClick = [this, i] { octaveRange = (uint8_t) i; sendScaleConfig(); };
            if (i == 2) btn->setToggleState (true, juce::dontSendNotification);
            octaveButtons.add (btn);
            addAndMakeVisible (btn);
        }
    }

    ~DualMPadPanel() override
    {
        octaveDownBtn.setLookAndFeel (nullptr);
        octaveUpBtn.setLookAndFeel   (nullptr);
        for (auto* b : octaveButtons) b->setLookAndFeel (nullptr);
    }

    void paint (juce::Graphics& g) override
    {
        // Pad area
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
            g.drawVerticalLine ((int) px, (float) xyArea.getY(), (float) xyArea.getBottom());

            float dotSize = 24.0f;
            g.setColour (MidiMorfColors::dot.withAlpha (0.8f));
            g.fillEllipse (px - dotSize * 0.5f, py - dotSize * 0.5f, dotSize, dotSize);
        }
        else
        {
            g.setColour (MidiMorfColors::textLight.withAlpha (0.3f));
            g.setFont (juce::FontOptions (11.0f));
            g.drawText (padIndex == 0 ? "PAD L" : "PAD R", xyArea.toFloat().reduced (8.0f),
                        juce::Justification::topLeft);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds();

        // Single-row config strip — dropped the old top scale-button row so
        // the XY pad gets more vertical room. Layout: combo on left, piano
        // keys in the middle, octave -/+ on the right top half, range 1..4
        // on the right bottom half.
        auto configStrip = area.removeFromTop (52);

        // Combo on the far left
        auto comboArea = configStrip.removeFromLeft (90);
        scaleCombo.setBounds (comboArea.reduced (2));

        configStrip.removeFromLeft (4);

        // Octave + range stacked on the right (existing 2-row layout preserved)
        auto octArea = configStrip.removeFromRight (96);
        auto octTop = octArea.removeFromTop (octArea.getHeight() / 2);
        auto octBot = octArea;

        int btnW = 30;
        int labelW = octTop.getWidth() - btnW * 2;
        octaveDownBtn.setBounds (octTop.getX(), octTop.getY(), btnW, octTop.getHeight());
        octaveLabel  .setBounds (octTop.getX() + btnW, octTop.getY(), labelW, octTop.getHeight());
        octaveUpBtn  .setBounds (octTop.getX() + btnW + labelW, octTop.getY(), btnW, octTop.getHeight());

        int octBtnW = octBot.getWidth() / 4;
        for (int i = 0; i < octaveButtons.size(); ++i)
            octaveButtons[i]->setBounds (octBot.getX() + i * octBtnW, octBot.getY(),
                                         octBtnW - 1, octBot.getHeight());

        configStrip.removeFromRight (4);

        // Piano keys fill the middle
        pianoKeys.setBounds (configStrip);

        area.removeFromTop (2);

        // XY pad fills the rest
        xyArea = area;
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! xyArea.toFloat().contains (e.position))
            return;

        int srcIdx = e.source.getIndex();

        TouchInfo ti;
        ti.sourceIndex = srcIdx;
        posFromEvent (e.position, ti.x, ti.y);
        activeTouches.push_back (ti);

        if (activeTouches.size() > 1)
            networkClient.sendDualMPadTouchUp (padIndex);

        touchX = ti.x;
        touchY = ti.y;
        touching = true;
        networkClient.sendDualMPadTouchDown (padIndex, touchX, touchY);
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
            networkClient.sendDualMPadTouchMove (padIndex, touchX, touchY);
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
            networkClient.sendDualMPadTouchUp (padIndex);

            if (! activeTouches.empty())
            {
                auto& prev = activeTouches.back();
                touchX = prev.x;
                touchY = prev.y;
                networkClient.sendDualMPadTouchDown (padIndex, touchX, touchY);
            }
            else
            {
                touching = false;
            }
            repaint();
        }
    }

    void saveState (juce::PropertiesFile& props)
    {
        auto prefix = "dualMPad_" + juce::String (padIndex) + "_";
        props.setValue (prefix + "scaleIndex", (int) scaleIndex);
        props.setValue (prefix + "rootNote", (int) rootNote);
        props.setValue (prefix + "octaveRange", (int) octaveRange);
        props.setValue (prefix + "baseOctave", (int) baseOctave);
    }

    void restoreState (const juce::PropertiesFile& props)
    {
        auto prefix = "dualMPad_" + juce::String (padIndex) + "_";
        scaleIndex = (uint8_t) props.getIntValue (prefix + "scaleIndex", 0);
        rootNote = (uint8_t) props.getIntValue (prefix + "rootNote", 0);
        octaveRange = (uint8_t) props.getIntValue (prefix + "octaveRange", 2);
        baseOctave = (uint8_t) props.getIntValue (prefix + "baseOctave", 2);

        scaleCombo.setSelectedId ((int) scaleIndex + 1, juce::dontSendNotification);
        pianoKeys.setSelectedNote (rootNote);
        for (int i = 0; i < octaveButtons.size(); ++i)
            octaveButtons[i]->setToggleState (i + 1 == octaveRange, juce::dontSendNotification);
        updateOctaveLabel();
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

    void updateTouch (juce::Point<float> pos)
    {
        touchX = juce::jlimit (0.0f, 1.0f, (pos.x - xyArea.getX()) / (float) xyArea.getWidth());
        touchY = juce::jlimit (0.0f, 1.0f, 1.0f - (pos.y - xyArea.getY()) / (float) xyArea.getHeight());
        repaint();
    }

    void sendScaleConfig()
    {
        networkClient.sendDualMPadScaleSelect (padIndex, scaleIndex, rootNote, octaveRange, baseOctave);
    }

    void updateOctaveLabel()
    {
        octaveLabel.setText ("C" + juce::String ((int) baseOctave), juce::dontSendNotification);
    }

    EventRouter& networkClient;
    uint8_t padIndex;

    static constexpr int numScales = 5;
    const juce::String scaleNames[5] = { "Major", "Minor", "Penta", "Blues", "Chrom" };

    // Must outlive the buttons it skins — declared first so it's destroyed last.
    GlassButtonLookAndFeel glassLnf { dualMPadPagePalette, MidiMorfColors::accent, 6.0f };

    juce::ComboBox scaleCombo;
    PianoKeySelector pianoKeys;
    juce::TextButton octaveDownBtn, octaveUpBtn;
    juce::Label octaveLabel;
    juce::OwnedArray<juce::TextButton> octaveButtons;

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

//==============================================================================
// Inner landscape container — holds two pads side by side
class DualMPadLandscapeContent : public juce::Component
{
public:
    DualMPadLandscapeContent (EventRouter& client)
        : leftPad (client, 0),
          rightPad (client, 1)
    {
        addAndMakeVisible (leftPad);
        addAndMakeVisible (rightPad);
    }

    void resized() override
    {
        // This component is sized to landscape dimensions (parent height x parent width)
        auto area = getLocalBounds();
        int halfW = area.getWidth() / 2;
        int gap = 6;

        leftPad.setBounds (area.getX(), area.getY(), halfW - gap / 2, area.getHeight());
        rightPad.setBounds (area.getX() + halfW + gap / 2, area.getY(), halfW - gap / 2, area.getHeight());
    }

    void saveState (juce::PropertiesFile& props) { leftPad.saveState (props); rightPad.saveState (props); }
    void restoreState (const juce::PropertiesFile& props) { leftPad.restoreState (props); rightPad.restoreState (props); }

private:
    DualMPadPanel leftPad;
    DualMPadPanel rightPad;
};

//==============================================================================
// Outer page — fits in portrait PageContainer, rotates content for landscape use
class DualMPadPage : public juce::Component
{
public:
    DualMPadPage (EventRouter& client)
        : content (client)
    {
        addAndMakeVisible (content);
    }

    void paint (juce::Graphics& g) override
    {
        Glassmorphism::paintPanel (g, getLocalBounds().toFloat(), dualMPadPagePalette);
    }

    void saveState (juce::PropertiesFile& props) { content.saveState (props); }
    void restoreState (const juce::PropertiesFile& props) { content.restoreState (props); }

    void resized() override
    {
        // App is now native landscape — no rotation needed.
        content.setBounds (getLocalBounds().reduced (12));
    }

private:
    DualMPadLandscapeContent content;
};
