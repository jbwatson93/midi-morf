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
        virtual void fxButtonMenu (int /*slotIndex*/) {}
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

    // Local mode: a corner chevron picks the effect, and a latched effect
    // stays lit after the finger lifts.
    void setShowsMenu (bool shouldShow) { showsMenu = shouldShow; repaint(); }
    void setLatched (bool shouldBeLatched) { latched = shouldBeLatched; repaint(); }
    bool isLatched() const { return latched; }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().reduced (2.0f);
        const bool lit = held || latched;

        if (palette != nullptr)
            Glassmorphism::paintButton (g, bounds, *palette,
                                         MidiMorfColors::accent,
                                         lit, highlighted, 8.0f);

        // Label — dark text on the bright held substrate, light text on the
        // dark idle substrate so the label always reads.
        g.setColour (lit ? MidiMorfColors::textDark : MidiMorfColors::textLight);
        g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        g.drawFittedText (label, bounds.reduced (6.0f, 0.0f).toNearestInt(), juce::Justification::centred, 2);

        if (latched)
        {
            g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
            g.drawText ("LATCHED", bounds.removeFromBottom (18.0f), juce::Justification::centred);
        }

        if (showsMenu)
        {
            auto chevron = menuArea().toFloat().reduced (8.0f, 9.0f);
            juce::Path p;
            p.startNewSubPath (chevron.getX(), chevron.getY());
            p.lineTo (chevron.getCentreX(), chevron.getBottom());
            p.lineTo (chevron.getRight(), chevron.getY());
            g.setColour ((lit ? MidiMorfColors::textDark : MidiMorfColors::textLight).withAlpha (0.8f));
            g.strokePath (p, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (showsMenu && menuArea().contains (e.getPosition()))
        {
            menuTouch = true;
            if (listener != nullptr)
                listener->fxButtonMenu (slotIndex);
            return;
        }

        held = true;
        repaint();
        if (listener != nullptr)
            listener->fxButtonPressed (slotIndex);
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (std::exchange (menuTouch, false))
            return;

        held = false;
        repaint();
        if (listener != nullptr)
            listener->fxButtonReleased (slotIndex);
    }

private:
    juce::Rectangle<int> menuArea() const
    {
        return getLocalBounds().removeFromTop (34).removeFromRight (40);
    }

    int slotIndex;
    juce::String label;
    bool held = false;
    bool highlighted = false;
    bool showsMenu = false, latched = false, menuTouch = false;
    Listener* listener = nullptr;
    juce::Typeface::Ptr ledTypeface;
    const PagePalette* palette = nullptr;
};
