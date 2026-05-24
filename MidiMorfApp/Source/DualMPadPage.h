#pragma once

#include <JuceHeader.h>
#include "EventRouter.h"
#include "MidiMorfColors.h"
#include "PianoKeySelector.h"
#include "MorfPadPage.h"  // for scale name array used by panel
#include <vector>
#include <algorithm>

//==============================================================================
// A single morfPad pad with its own config strip — used twice in DualMPadPage
class DualMPadPanel : public juce::Component
{
public:
    DualMPadPanel (EventRouter& client, uint8_t padIdx)
        : networkClient (client), padIndex (padIdx)
    {
        // Scale selector
        for (int i = 0; i < numScales; ++i)
        {
            auto* btn = new juce::TextButton (scaleNames[i]);
            btn->setRadioGroupId (100 + padIdx * 10 + 1);
            btn->setClickingTogglesState (true);
            btn->setColour (juce::TextButton::buttonColourId, MidiMorfColors::buttonFace);
            btn->setColour (juce::TextButton::buttonOnColourId, MidiMorfColors::accent);
            btn->setColour (juce::TextButton::textColourOnId, MidiMorfColors::textLight);
            btn->setColour (juce::TextButton::textColourOffId, MidiMorfColors::textDark);
            btn->onClick = [this, i] { scaleIndex = (uint8_t) i; sendScaleConfig(); };
            if (i == 0) btn->setToggleState (true, juce::dontSendNotification);
            scaleButtons.add (btn);
            addAndMakeVisible (btn);
        }

        // Piano key selector for root note
        pianoKeys.onNoteSelected = [this] (int note) { rootNote = (uint8_t) note; sendScaleConfig(); };
        addAndMakeVisible (pianoKeys);

        // Base octave +/- buttons
        octaveDownBtn.setButtonText ("-");
        octaveDownBtn.setColour (juce::TextButton::buttonColourId, MidiMorfColors::buttonFace);
        octaveDownBtn.setColour (juce::TextButton::textColourOffId, MidiMorfColors::textDark);
        octaveDownBtn.onClick = [this] {
            if (baseOctave > 0) { --baseOctave; updateOctaveLabel(); sendScaleConfig(); }
        };
        addAndMakeVisible (octaveDownBtn);

        octaveLabel.setText ("C2", juce::dontSendNotification);
        octaveLabel.setJustificationType (juce::Justification::centred);
        octaveLabel.setColour (juce::Label::textColourId, MidiMorfColors::textDark);
        addAndMakeVisible (octaveLabel);

        octaveUpBtn.setButtonText ("+");
        octaveUpBtn.setColour (juce::TextButton::buttonColourId, MidiMorfColors::buttonFace);
        octaveUpBtn.setColour (juce::TextButton::textColourOffId, MidiMorfColors::textDark);
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
            btn->setColour (juce::TextButton::buttonColourId, MidiMorfColors::buttonFace);
            btn->setColour (juce::TextButton::buttonOnColourId, MidiMorfColors::accent);
            btn->setColour (juce::TextButton::textColourOnId, MidiMorfColors::textLight);
            btn->setColour (juce::TextButton::textColourOffId, MidiMorfColors::textDark);
            btn->onClick = [this, i] { octaveRange = (uint8_t) i; sendScaleConfig(); };
            if (i == 2) btn->setToggleState (true, juce::dontSendNotification);
            octaveButtons.add (btn);
            addAndMakeVisible (btn);
        }
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

        // Config strip on top
        auto configStrip = area.removeFromTop (70);

        // Row 1: scale buttons
        auto scaleRow = configStrip.removeFromTop (26);
        int scaleBtnW = scaleRow.getWidth() / numScales;
        for (int i = 0; i < scaleButtons.size(); ++i)
            scaleButtons[i]->setBounds (scaleRow.getX() + i * scaleBtnW, scaleRow.getY(),
                                        scaleBtnW - 1, 24);

        configStrip.removeFromTop (2);

        // Row 2: piano keys | octave +/- | range 1-4
        auto row2 = configStrip;

        // Octave controls on right
        auto octArea = row2.removeFromRight (64);
        auto octTop = octArea.removeFromTop (octArea.getHeight() / 2);
        auto octBot = octArea;

        int btnW = 22;
        int labelW = octTop.getWidth() - btnW * 2;
        octaveDownBtn.setBounds (octTop.getX(), octTop.getY(), btnW, octTop.getHeight());
        octaveLabel.setBounds (octTop.getX() + btnW, octTop.getY(), labelW, octTop.getHeight());
        octaveUpBtn.setBounds (octTop.getX() + btnW + labelW, octTop.getY(), btnW, octTop.getHeight());

        int octBtnW = octBot.getWidth() / 4;
        for (int i = 0; i < octaveButtons.size(); ++i)
            octaveButtons[i]->setBounds (octBot.getX() + i * octBtnW, octBot.getY(),
                                         octBtnW - 1, octBot.getHeight());

        row2.removeFromRight (2);

        // Piano keys fill remaining space
        pianoKeys.setBounds (row2);

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

        for (int i = 0; i < numScales; ++i)
            scaleButtons[i]->setToggleState (i == scaleIndex, juce::dontSendNotification);
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

    juce::OwnedArray<juce::TextButton> scaleButtons;
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

    void saveState (juce::PropertiesFile& props) { content.saveState (props); }
    void restoreState (const juce::PropertiesFile& props) { content.restoreState (props); }

    void resized() override
    {
        int pw = getWidth();   // portrait width  (~400)
        int ph = getHeight();  // portrait height (~600)

        // Size content to landscape: width=ph, height=pw
        content.setSize (ph, pw);

        // Rotate 90° CW so landscape content fits portrait frame.
        // rotation(+halfPi) maps (x,y) → (-y, x).
        // Then translate(pw, 0) → (pw - y, x) to keep it in positive coords.
        // Content corners (ph x pw):
        //   (0, 0)    → (pw, 0)    = portrait top-right
        //   (ph, 0)   → (pw, ph)   = portrait bottom-right
        //   (ph, pw)  → (0, ph)    = portrait bottom-left
        //   (0, pw)   → (0, 0)     = portrait top-left
        // When phone is rotated CW to landscape, portrait top-left becomes
        // landscape bottom-left, which makes content (0, pw) the landscape origin.
        // This matches: content left-to-right = landscape left-to-right.
        content.setTransform (
            juce::AffineTransform::rotation (
                juce::MathConstants<float>::halfPi, 0.0f, 0.0f)
            .translated ((float) pw, 0.0f));
    }

private:
    DualMPadLandscapeContent content;
};
