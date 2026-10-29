#pragma once

#include <JuceHeader.h>
#include "MidiMorfColors.h"

//==============================================================================
// Small floating menu button that uses the inBloom.png "bloom" marker (same
// image as the FX Punch-in XY pad touch indicator) as its glyph.
class PianoKeysButton : public juce::Button
{
public:
    PianoKeysButton() : juce::Button ("MenuButton")
    {
        image = juce::ImageCache::getFromMemory (BinaryData::inBloom_png,
                                                  BinaryData::inBloom_pngSize);
    }

protected:
    void paintButton (juce::Graphics& g, bool /*isHighlighted*/, bool isDown) override
    {
        auto bounds = getLocalBounds().toFloat().reduced (1.5f);

        // Card background
        g.setColour (juce::Colour (0xff2A2A2E).withAlpha (0.85f));
        g.fillRoundedRectangle (bounds, 8.0f);
        g.setColour (MidiMorfColors::accent.withAlpha (0.55f));
        g.drawRoundedRectangle (bounds.reduced (0.5f), 8.0f, 1.0f);

        // Reserve the bottom strip for the "Menu" label, give the rest to the image.
        const float labelH = juce::jmin (16.0f, bounds.getHeight() * 0.28f);
        auto imgArea   = bounds.withTrimmedBottom (labelH).reduced (3.0f);
        auto labelArea = bounds.removeFromBottom (labelH);

        if (! image.isNull())
        {
            g.setOpacity (isDown ? 0.7f : 1.0f);
            g.drawImageWithin (image,
                               (int) imgArea.getX(), (int) imgArea.getY(),
                               (int) imgArea.getWidth(), (int) imgArea.getHeight(),
                               juce::RectanglePlacement::centred | juce::RectanglePlacement::onlyReduceInSize);
            g.setOpacity (1.0f);
        }

        g.setColour (MidiMorfColors::textLight);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText ("Menu", labelArea, juce::Justification::centred);

        if (isDown)
        {
            g.setColour (MidiMorfColors::accent.withAlpha (0.2f));
            g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (1.5f), 8.0f);
        }
    }

private:
    juce::Image image;
};
