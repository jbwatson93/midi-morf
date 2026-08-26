#pragma once

#include <JuceHeader.h>
#include "MidiMorfColors.h"
#include "BinaryData.h"
#include "Glassmorphism.h"

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
    void setPalette (const PagePalette* p) { palette = p; repaint(); }
    int getSlotIndex() const { return slotIndex; }
    bool isHeld() const { return held; }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().reduced (2.0f);

        if (palette != nullptr)
            Glassmorphism::paintButton (g, bounds, *palette,
                                         MidiMorfColors::accent,
                                         held, highlighted, 8.0f);

        // Label — dark text on the bright held substrate, light text on the
        // dark idle substrate so the label always reads.
        g.setColour (held ? MidiMorfColors::textDark : MidiMorfColors::textLight);
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
    const PagePalette* palette = nullptr;
};
