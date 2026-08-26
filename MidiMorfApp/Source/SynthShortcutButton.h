#pragma once

#include <JuceHeader.h>
#include "MidiMorfColors.h"

//==============================================================================
// Floating shortcut that jumps to the Synth settings page. Renders a sine-wave
// glyph + "Synth" label, styled to match PianoKeysButton.
class SynthShortcutButton : public juce::Button
{
public:
    SynthShortcutButton() : juce::Button ("SynthShortcut") {}

protected:
    void paintButton (juce::Graphics& g, bool /*isHighlighted*/, bool isDown) override
    {
        auto bounds = getLocalBounds().toFloat().reduced (1.5f);

        // Card background — same look as the Menu button.
        g.setColour (juce::Colour (0xff2A2A2E).withAlpha (0.85f));
        g.fillRoundedRectangle (bounds, 8.0f);
        g.setColour (MidiMorfColors::accent.withAlpha (0.55f));
        g.drawRoundedRectangle (bounds.reduced (0.5f), 8.0f, 1.0f);

        // Reserve a bottom strip for the label.
        const float labelH = juce::jmin (16.0f, bounds.getHeight() * 0.28f);
        auto glyphArea = bounds.withTrimmedBottom (labelH).reduced (4.0f);
        auto labelArea = bounds.removeFromBottom (labelH);

        // Sine wave path — 1.5 cycles spanning the glyph area, centred.
        juce::Path wave;
        const int numPoints = 64;
        const float twoPi = juce::MathConstants<float>::twoPi;
        const float cycles = 1.5f;
        const float amp = glyphArea.getHeight() * 0.35f;
        const float cy = glyphArea.getCentreY();

        for (int i = 0; i <= numPoints; ++i)
        {
            float t = (float) i / (float) numPoints;
            float x = glyphArea.getX() + t * glyphArea.getWidth();
            float y = cy - std::sin (t * twoPi * cycles) * amp;
            if (i == 0) wave.startNewSubPath (x, y);
            else        wave.lineTo (x, y);
        }

        g.setColour (isDown ? MidiMorfColors::accent.withAlpha (0.7f)
                            : MidiMorfColors::accent);
        g.strokePath (wave, juce::PathStrokeType (2.2f,
                                                   juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));

        // Label
        g.setColour (MidiMorfColors::textLight);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText ("Synth", labelArea, juce::Justification::centred);

        if (isDown)
        {
            g.setColour (MidiMorfColors::accent.withAlpha (0.2f));
            g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (1.5f), 8.0f);
        }
    }
};
