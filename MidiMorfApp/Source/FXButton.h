#pragma once

#include <JuceHeader.h>
#include "MidiMorfColors.h"
#include "BinaryData.h"

class FXButton : public juce::Component
{
public:
    struct Listener
    {
        virtual ~Listener() = default;
        virtual void fxButtonPressed (int slotIndex) = 0;
        virtual void fxButtonReleased (int slotIndex) = 0;
    };

    FXButton (int index) : slotIndex (index)
    {
        label = "FX " + juce::String (index + 1);
        ledTypeface = juce::Typeface::createSystemTypefaceFor (
            BinaryData::DSEG14ModernMiniRegular_ttf,
            BinaryData::DSEG14ModernMiniRegular_ttfSize);
    }

    void setListener (Listener* l) { listener = l; }
    void setLabel (const juce::String& newLabel) { label = newLabel; repaint(); }
    void setHighlighted (bool h) { highlighted = h; repaint(); }
    int getSlotIndex() const { return slotIndex; }
    bool isHeld() const { return held; }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().reduced (2.0f);

        if (held)
        {
            g.setColour (MidiMorfColors::buttonFaceActive);
            g.fillRoundedRectangle (bounds, 8.0f);
            g.setColour (MidiMorfColors::textDark);
        }
        else
        {
            g.setColour (MidiMorfColors::buttonFace);
            g.fillRoundedRectangle (bounds, 8.0f);

            if (highlighted)
            {
                g.setColour (MidiMorfColors::accent);
                g.drawRoundedRectangle (bounds.reduced (1.0f), 8.0f, 2.0f);
            }
            else
            {
                g.setColour (MidiMorfColors::buttonBorder);
                g.drawRoundedRectangle (bounds.reduced (1.0f), 8.0f, 1.5f);
            }

            g.setColour (MidiMorfColors::textDark);
        }

        g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        g.drawText (label, bounds, juce::Justification::centred);
    }

    void mouseDown (const juce::MouseEvent&) override
    {
        held = true;
        repaint();
        if (listener != nullptr)
            listener->fxButtonPressed (slotIndex);
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        held = false;
        repaint();
        if (listener != nullptr)
            listener->fxButtonReleased (slotIndex);
    }

private:
    int slotIndex;
    juce::String label;
    bool held = false;
    bool highlighted = false;
    Listener* listener = nullptr;
    juce::Typeface::Ptr ledTypeface;
};
