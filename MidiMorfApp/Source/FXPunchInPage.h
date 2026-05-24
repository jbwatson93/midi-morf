#pragma once

#include <JuceHeader.h>
#include "XYPad.h"
#include "FXButton.h"
#include "AxisSlider.h"
#include "EventRouter.h"
#include "MidiMorfColors.h"
#include "MidiMorfProtocol.h"

class FXPunchInPage : public juce::Component,
                      public XYPad::Listener,
                      public FXButton::Listener,
                      public AxisSlider::Listener
{
public:
    FXPunchInPage (EventRouter& client)
        : networkClient (client),
          ySlider (false, "Y", 21),
          xSlider (true,  "X", 20)
    {
        xyPad.setListener (this);
        addAndMakeVisible (xyPad);

        ySlider.setListener (this);
        addAndMakeVisible (ySlider);

        xSlider.setListener (this);
        addAndMakeVisible (xSlider);

        for (int i = 0; i < MidiMorf::NUM_SLOTS; ++i)
        {
            auto* btn = new FXButton (i);
            btn->setListener (this);
            fxButtons.add (btn);
            addAndMakeVisible (btn);
        }
    }

    void setButtonLabel (int slotIndex, const juce::String& name)
    {
        if (slotIndex >= 0 && slotIndex < fxButtons.size())
            fxButtons[slotIndex]->setLabel (name);
    }

    void paint (juce::Graphics& g) override
    {
        // Transparent — parent paints the background
    }

    void resized() override
    {
        auto area = getLocalBounds();

        // FX buttons at bottom in 2x2 grid, ~1/3 of available height
        int gridH = area.getHeight() / 3;
        auto buttonArea = area.removeFromBottom (gridH);
        int rowH = (gridH - 6) / 2;
        int colW = (buttonArea.getWidth() - 6) / 2;

        fxButtons[0]->setBounds (buttonArea.getX(),              buttonArea.getY(),              colW, rowH);
        fxButtons[1]->setBounds (buttonArea.getX() + colW + 6,   buttonArea.getY(),              colW, rowH);
        fxButtons[2]->setBounds (buttonArea.getX(),              buttonArea.getY() + rowH + 6,   colW, rowH);
        fxButtons[3]->setBounds (buttonArea.getX() + colW + 6,   buttonArea.getY() + rowH + 6,   colW, rowH);

        area.removeFromBottom (6);

        // X slider (horizontal) at bottom of remaining area
        auto xSliderArea = area.removeFromBottom (40);
        xSlider.setBounds (xSliderArea);

        area.removeFromBottom (6);

        // Y slider (vertical) on the left side
        auto ySliderArea = area.removeFromLeft (50);
        ySlider.setBounds (ySliderArea);

        area.removeFromLeft (6);

        // XY pad fills the rest
        xyPad.setBounds (area);
    }

    // XYPad::Listener
    void xyPadUpdated (float x, float y) override
    {
        DBG ("FXPunchInPage::xyPadUpdated x=" << x << " y=" << y);
        networkClient.sendXYUpdate (x, y);
        xSlider.setDisplayValue (x);
        ySlider.setDisplayValue (y);
    }

    // FXButton::Listener
    void fxButtonPressed (int slotIndex) override
    {
        lastPressedSlot = slotIndex;
        updateHighlights();
        networkClient.sendButtonDown (slotIndex);
    }

    void fxButtonReleased (int slotIndex) override
    {
        networkClient.sendButtonUp (slotIndex);

        // If released slot was last pressed, try to move to another held slot
        if (slotIndex == lastPressedSlot)
        {
            for (int i = 0; i < fxButtons.size(); ++i)
            {
                if (i != slotIndex && fxButtons[i]->isHeld())
                {
                    lastPressedSlot = i;
                    break;
                }
            }
        }
        updateHighlights();
    }

    // AxisSlider::Listener
    void axisSliderUpdated (bool isXAxis, float value) override
    {
        networkClient.sendAxisUpdate (isXAxis, value);
    }

private:
    void updateHighlights()
    {
        for (int i = 0; i < fxButtons.size(); ++i)
            fxButtons[i]->setHighlighted (i == lastPressedSlot);
    }

    EventRouter& networkClient;
    XYPad xyPad;
    AxisSlider ySlider;
    AxisSlider xSlider;
    juce::OwnedArray<FXButton> fxButtons;
    int lastPressedSlot = -1;
};
