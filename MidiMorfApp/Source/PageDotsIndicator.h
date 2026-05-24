#pragma once

#include <JuceHeader.h>
#include "MidiMorfColors.h"

class PageDotsIndicator : public juce::Component
{
public:
    PageDotsIndicator() {}

    void setNumPages (int n) { numPages = n; repaint(); }
    void setCurrentPage (int p) { currentPage = p; repaint(); }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        float dotSize = 8.0f;
        float gap = 12.0f;
        float totalWidth = numPages * dotSize + (numPages - 1) * gap;
        float startX = bounds.getCentreX() - totalWidth * 0.5f;
        float centreY = bounds.getCentreY();

        for (int i = 0; i < numPages; ++i)
        {
            float x = startX + i * (dotSize + gap);
            if (i == currentPage)
                g.setColour (MidiMorfColors::accent);
            else
                g.setColour (MidiMorfColors::textLight.withAlpha (0.3f));

            g.fillEllipse (x, centreY - dotSize * 0.5f, dotSize, dotSize);
        }
    }

private:
    int numPages = 1;
    int currentPage = 0;
};
