#pragma once

#include <JuceHeader.h>
#include "MidiMorfColors.h"
#include "BinaryData.h"

class XYPad : public juce::Component
{
public:
    struct Listener
    {
        virtual ~Listener() = default;
        virtual void xyPadUpdated (float x, float y) = 0;
    };

    XYPad()
    {
        touchImage = juce::ImageCache::getFromMemory (BinaryData::inBloom_png, BinaryData::inBloom_pngSize);
    }

    void setListener (Listener* l) { listener = l; }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();

        g.setColour (MidiMorfColors::padBackground);
        g.fillRoundedRectangle (bounds, 8.0f);

        g.setColour (MidiMorfColors::padBorder);
        g.drawRoundedRectangle (bounds.reduced (1.0f), 8.0f, 1.5f);

        // Position of touch point (current or last)
        float px = bounds.getX() + currentX * bounds.getWidth();
        float py = bounds.getY() + (1.0f - currentY) * bounds.getHeight();

        if (touching)
        {
            // Crosshair lines
            g.setColour (MidiMorfColors::crosshair);
            g.drawHorizontalLine ((int) py, bounds.getX(), bounds.getRight());
            g.drawVerticalLine ((int) px, bounds.getY(), bounds.getBottom());
        }

        // Touch image — preserve aspect ratio, show at last position even after release
        float imgSize = 100.0f;
        float imgW = (float) touchImage.getWidth();
        float imgH = (float) touchImage.getHeight();
        float aspect = imgW / imgH;
        float drawW, drawH;
        if (aspect >= 1.0f)
        {
            drawW = imgSize;
            drawH = imgSize / aspect;
        }
        else
        {
            drawH = imgSize;
            drawW = imgSize * aspect;
        }

        float alpha = touching ? 1.0f : 0.4f;
        g.setOpacity (alpha);
        g.drawImage (touchImage,
                     px - drawW * 0.5f, py - drawH * 0.5f, drawW, drawH,
                     0, 0, (int) imgW, (int) imgH);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        touching = true;
        updatePosition (e.position);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        updatePosition (e.position);
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        touching = false;
        repaint();
    }

private:
    void updatePosition (juce::Point<float> pos)
    {
        auto bounds = getLocalBounds().toFloat();
        currentX = juce::jlimit (0.0f, 1.0f, (pos.x - bounds.getX()) / bounds.getWidth());
        currentY = juce::jlimit (0.0f, 1.0f, 1.0f - (pos.y - bounds.getY()) / bounds.getHeight());

        if (listener != nullptr)
            listener->xyPadUpdated (currentX, currentY);

        repaint();
    }

    Listener* listener = nullptr;
    float currentX = 0.5f;
    float currentY = 0.5f;
    bool touching = false;
    juce::Image touchImage;
};
