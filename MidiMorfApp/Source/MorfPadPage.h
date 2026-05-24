#pragma once

#include <JuceHeader.h>
#include "EventRouter.h"
#include "MidiMorfColors.h"
#include "PianoKeySelector.h"
#include <vector>
#include <algorithm>

//==============================================================================
class MorfPadPage : public juce::Component
{
public:
    MorfPadPage (EventRouter& client)
        : networkClient (client)
    {
        // Scale selector
        for (int i = 0; i < numScales; ++i)
        {
            auto* btn = new juce::TextButton (scaleNames[i]);
            btn->setRadioGroupId (1);
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
            btn->setRadioGroupId (3);
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
        // Draw the XY pad area
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
        auto area = getLocalBounds();

        // Top strip: scale buttons, then piano + controls
        auto topStrip = area.removeFromTop (110);
        auto scaleRow = topStrip.removeFromTop (36);
        int scaleBtnW = scaleRow.getWidth() / numScales;
        for (int i = 0; i < scaleButtons.size(); ++i)
            scaleButtons[i]->setBounds (scaleRow.getX() + i * scaleBtnW, scaleRow.getY(), scaleBtnW - 2, 34);

        topStrip.removeFromTop (4);

        // Right column: octave +/- on top row, range 1-4 on bottom row
        auto rightCol = topStrip.removeFromRight (80);
        auto topRight = rightCol.removeFromTop (rightCol.getHeight() / 2);
        auto botRight = rightCol;

        // Top right: - label +
        int btnW = 26;
        int labelW = topRight.getWidth() - btnW * 2;
        octaveDownBtn.setBounds (topRight.getX(), topRight.getY(), btnW, topRight.getHeight());
        octaveLabel.setBounds (topRight.getX() + btnW, topRight.getY(), labelW, topRight.getHeight());
        octaveUpBtn.setBounds (topRight.getX() + btnW + labelW, topRight.getY(), btnW, topRight.getHeight());

        // Bottom right: octave range 1-4
        int octBtnW = botRight.getWidth() / 4;
        for (int i = 0; i < octaveButtons.size(); ++i)
            octaveButtons[i]->setBounds (botRight.getX() + i * octBtnW, botRight.getY(), octBtnW - 1, botRight.getHeight());

        topStrip.removeFromRight (4);

        // Piano keys fill the left, spanning both rows
        pianoKeys.setBounds (topStrip);

        area.removeFromTop (4);

        // XY pad fills the rest
        xyArea = area;
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! xyArea.toFloat().contains (e.position))
            return;

        int srcIdx = e.source.getIndex();

        // Store this touch
        TouchInfo ti;
        ti.sourceIndex = srcIdx;
        posFromEvent (e.position, ti.x, ti.y);
        activeTouches.push_back (ti);

        // New finger becomes active — send up for old, down for new
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

        // Update stored position for this finger
        for (auto& t : activeTouches)
        {
            if (t.sourceIndex == srcIdx)
            {
                posFromEvent (e.position, t.x, t.y);
                break;
            }
        }

        // Only send move if this is the most recent (active) finger
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

        // Remove this touch from the stack
        activeTouches.erase (
            std::remove_if (activeTouches.begin(), activeTouches.end(),
                [srcIdx] (const TouchInfo& t) { return t.sourceIndex == srcIdx; }),
            activeTouches.end());

        if (wasActive)
        {
            networkClient.sendMorfPadTouchUp();

            if (! activeTouches.empty())
            {
                // Fall back to previous finger
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

    void saveState (juce::PropertiesFile& props)
    {
        props.setValue ("morfPad_scaleIndex", (int) scaleIndex);
        props.setValue ("morfPad_rootNote", (int) rootNote);
        props.setValue ("morfPad_octaveRange", (int) octaveRange);
        props.setValue ("morfPad_baseOctave", (int) baseOctave);
    }

    void restoreState (const juce::PropertiesFile& props)
    {
        scaleIndex = (uint8_t) props.getIntValue ("morfPad_scaleIndex", 0);
        rootNote = (uint8_t) props.getIntValue ("morfPad_rootNote", 0);
        octaveRange = (uint8_t) props.getIntValue ("morfPad_octaveRange", 2);
        baseOctave = (uint8_t) props.getIntValue ("morfPad_baseOctave", 2);

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
        networkClient.sendMorfPadScaleSelect (scaleIndex, rootNote, octaveRange, baseOctave);
    }

    void updateOctaveLabel()
    {
        octaveLabel.setText ("C" + juce::String ((int) baseOctave), juce::dontSendNotification);
    }

    EventRouter& networkClient;

    static constexpr int numScales = 5;
    const juce::String scaleNames[5] = { "Major", "Minor", "Pentatonic", "Blues", "Chromatic" };

    juce::OwnedArray<juce::TextButton> scaleButtons;
    PianoKeySelector pianoKeys;
    juce::TextButton octaveDownBtn, octaveUpBtn;
    juce::Label octaveLabel;
    juce::OwnedArray<juce::TextButton> octaveButtons;

    uint8_t scaleIndex = 0;
    uint8_t rootNote = 0;
    uint8_t octaveRange = 2;
    uint8_t baseOctave = 2;  // Octave number (C2 default). Server computes MIDI note.

    juce::Rectangle<int> xyArea;
    bool touching = false;
    float touchX = 0.5f;
    float touchY = 0.5f;
    std::vector<TouchInfo> activeTouches;
};
