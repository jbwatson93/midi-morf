#pragma once

#include <JuceHeader.h>
#include "MidiMorfColors.h"
#include "Haptics.h"

//==============================================================================
// Floating quick-jump button. Holds an assigned page index; short tap fires
// onShortTap (navigate), long-press fires onLongPress (open the picker so the
// user can rebind which page this button jumps to). An empty slot draws a "+"
// glyph and routes both gestures to onLongPress so the first tap opens the
// picker — no long-press needed when there's nothing assigned yet.
class FavoriteButton : public juce::Component,
                       private juce::Timer
{
public:
    std::function<void()> onShortTap;
    std::function<void()> onLongPress;

    void setAssignment (int pageIndex, const juce::String& label)
    {
        assignedIndex = pageIndex;
        displayLabel  = label;
        repaint();
    }

    int  getAssignment() const noexcept { return assignedIndex; }
    bool hasAssignment() const noexcept { return assignedIndex >= 0; }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().reduced (1.5f);

        // Card background — matches Menu / Synth buttons.
        g.setColour (juce::Colour (0xff2A2A2E).withAlpha (0.85f));
        g.fillRoundedRectangle (bounds, 8.0f);
        g.setColour (MidiMorfColors::accent.withAlpha (0.55f));
        g.drawRoundedRectangle (bounds.reduced (0.5f), 8.0f, 1.0f);

        const float labelH = juce::jmin (16.0f, bounds.getHeight() * 0.28f);
        auto glyphArea = bounds.withTrimmedBottom (labelH).reduced (4.0f);
        auto labelArea = bounds.removeFromBottom (labelH);

        // Heart glyph in unit space, then scaled+translated into glyphArea.
        // Two cubic-bezier humps from a single bottom point — the symmetric
        // control points produce the classic rounded-shoulder heart silhouette.
        juce::Path heart;
        heart.startNewSubPath (0.5f, 0.95f);
        heart.cubicTo (0.0f,  0.70f, -0.05f, 0.10f, 0.5f, 0.28f);
        heart.cubicTo (1.05f, 0.10f,  1.0f,  0.70f, 0.5f, 0.95f);
        heart.closeSubPath();

        const float size = juce::jmin (glyphArea.getWidth(), glyphArea.getHeight()) * 0.92f;
        heart.applyTransform (juce::AffineTransform::scale (size, size)
                                  .translated (glyphArea.getCentreX() - size * 0.5f,
                                               glyphArea.getCentreY() - size * 0.5f));

        if (assignedIndex >= 0)
        {
            // Assigned: filled accent heart.
            g.setColour (MidiMorfColors::accent);
            g.fillPath (heart);
        }
        else
        {
            // Empty: outline heart at reduced opacity so the slot reads as
            // "tap to add a favourite".
            g.setColour (MidiMorfColors::accent.withAlpha (0.55f));
            g.strokePath (heart, juce::PathStrokeType (1.8f));
        }

        g.setColour (MidiMorfColors::textLight);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText ("Fave", labelArea, juce::Justification::centred);

        if (isPressed)
        {
            g.setColour (MidiMorfColors::accent.withAlpha (0.2f));
            g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (1.5f), 8.0f);
        }
    }

    void mouseDown (const juce::MouseEvent&) override
    {
        isPressed = true;
        longPressFired = false;
        repaint();
        startTimer (longPressMs);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        stopTimer();
        const bool wasPressed = isPressed;
        const bool wasInside  = getLocalBounds().contains (e.getPosition());
        isPressed = false;
        repaint();

        if (longPressFired || ! wasPressed || ! wasInside)
            return;

        // Short tap: navigate if assigned, otherwise open the picker so the
        // first interaction with an empty slot is immediately useful.
        if (assignedIndex >= 0)
        {
            if (onShortTap) onShortTap();
        }
        else
        {
            if (onLongPress) onLongPress();
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        // Finger dragged outside the button — treat like a release-cancel so a
        // wandering finger doesn't accidentally navigate or open the picker.
        if (isPressed && ! getLocalBounds().contains (e.getPosition()))
        {
            stopTimer();
            isPressed = false;
            longPressFired = true; // suppress the trailing mouseUp short-tap
            repaint();
        }
    }

private:
    void timerCallback() override
    {
        stopTimer();
        if (! isPressed) return;            // released before long-press hit
        longPressFired = true;
        isPressed = false;
        repaint();
        MidiMorfHaptics::impactMedium();    // tactile confirmation before the picker opens
        if (onLongPress) onLongPress();
    }

    int          assignedIndex   = -1;
    juce::String displayLabel;
    bool         isPressed       = false;
    bool         longPressFired  = false;

    static constexpr int longPressMs = 500;
};
