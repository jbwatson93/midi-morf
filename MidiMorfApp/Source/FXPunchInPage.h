#pragma once

#include <JuceHeader.h>
#include "XYPad.h"
#include "FXButton.h"
#include "AxisSlider.h"
#include "EventRouter.h"
#include "MidiMorfColors.h"
#include "MidiMorfProtocol.h"
#include "Glassmorphism.h"

// Deep teal palette — studio rack effects vibe. Lives at file scope so
// FXButton instances can hold a pointer to it.
static const PagePalette fxPagePalette {
    juce::Colour (0xff0F2A33),       // substrate top — deep teal
    juce::Colour (0xff061418),       // substrate bottom — near-black with teal undertone
    juce::Colour (0xCC1F4550),       // acrylic top — translucent indigo-teal
    juce::Colour (0xD8112832),       // acrylic bottom — deeper translucent
    juce::Colour (0xCC000000)        // edge — translucent black
};

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
            btn->setPalette (&fxPagePalette);
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
        // FX page paints its own deep-teal glass panel over the PageContainer's
        // theme background — gives FX Punch-in a distinct identity from the
        // chord/instrument pages.
        Glassmorphism::paintPanel (g, getLocalBounds().toFloat(), fxPagePalette);
    }

    void resized() override
    {
        // Landscape layout:
        //   • XY pad fills the left, with the Y slider on its left edge
        //   • 2×2 FX button grid on the right
        //   • X slider runs along the bottom under the XY pad (full width up
        //     to the FX block)
        auto area = getLocalBounds().reduced (12);

        // Right column: FX buttons (2×2)
        const int fxColW = juce::jmax (140, area.getWidth() / 4);
        auto fxArea = area.removeFromRight (fxColW);

        int rowH = (fxArea.getHeight() - 6) / 2;
        int colW = (fxArea.getWidth()  - 6) / 2;
        fxButtons[0]->setBounds (fxArea.getX(),              fxArea.getY(),              colW, rowH);
        fxButtons[1]->setBounds (fxArea.getX() + colW + 6,   fxArea.getY(),              colW, rowH);
        fxButtons[2]->setBounds (fxArea.getX(),              fxArea.getY() + rowH + 6,   colW, rowH);
        fxButtons[3]->setBounds (fxArea.getX() + colW + 6,   fxArea.getY() + rowH + 6,   colW, rowH);

        area.removeFromRight (8);

        // X slider along the bottom of the remaining left region
        auto xSliderArea = area.removeFromBottom (36);
        xSlider.setBounds (xSliderArea);
        area.removeFromBottom (6);

        // Y slider on the left edge of the XY pad
        auto ySliderArea = area.removeFromLeft (44);
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
