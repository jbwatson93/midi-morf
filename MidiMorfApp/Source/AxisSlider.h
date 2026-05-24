#pragma once

#include <JuceHeader.h>
#include "MidiMorfColors.h"
#include "BinaryData.h"

class AxisSlider : public juce::Component
{
public:
    struct Listener
    {
        virtual ~Listener() = default;
        virtual void axisSliderUpdated (bool isXAxis, float value) = 0;
    };

    AxisSlider (bool horizontal, const juce::String& labelText, int ccNum)
        : isHorizontal (horizontal), label (labelText), ccNumber (ccNum)
    {
        ledTypeface = juce::Typeface::createSystemTypefaceFor (
            BinaryData::DSEG14ModernMiniRegular_ttf,
            BinaryData::DSEG14ModernMiniRegular_ttfSize);
    }

    void setListener (Listener* l) { listener = l; }
    void setCCNumber (int cc) { ccNumber = cc; repaint(); }
    void setDisplayValue (float v) { currentValue = juce::jlimit (0.0f, 1.0f, v); showingExternal = true; repaint(); }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();

        g.setColour (MidiMorfColors::padBackground);
        g.fillRoundedRectangle (bounds, 6.0f);

        g.setColour (MidiMorfColors::padBorder);
        g.drawRoundedRectangle (bounds.reduced (0.5f), 6.0f, 1.0f);

        // Fill bar showing current value
        if (touching || showingExternal)
        {
            g.setColour (MidiMorfColors::accent.withAlpha (touching ? 0.6f : 0.35f));
            if (isHorizontal)
            {
                float barW = currentValue * bounds.getWidth();
                g.fillRoundedRectangle (bounds.getX(), bounds.getY(), barW, bounds.getHeight(), 6.0f);
            }
            else
            {
                float barH = currentValue * bounds.getHeight();
                g.fillRoundedRectangle (bounds.getX(), bounds.getBottom() - barH, bounds.getWidth(), barH, 6.0f);
            }
        }

        // Label
        g.setColour (MidiMorfColors::textLight.withAlpha (0.7f));
        g.setFont (juce::FontOptions (12.0f));
        juce::String text = label + " - CC " + juce::String (ccNumber);
        if (isHorizontal)
            g.drawText (text, bounds.reduced (6.0f), juce::Justification::centred);
        else
            g.drawFittedText (text, bounds.reduced (4.0f).toNearestInt(), juce::Justification::centredTop, 2);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        touching = true;
        showingExternal = false;
        updateValue (e.position);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        updateValue (e.position);
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        touching = false;
        repaint();
    }

private:
    void updateValue (juce::Point<float> pos)
    {
        auto bounds = getLocalBounds().toFloat();
        if (isHorizontal)
            currentValue = juce::jlimit (0.0f, 1.0f, (pos.x - bounds.getX()) / bounds.getWidth());
        else
            currentValue = juce::jlimit (0.0f, 1.0f, 1.0f - (pos.y - bounds.getY()) / bounds.getHeight());

        if (listener != nullptr)
            listener->axisSliderUpdated (isHorizontal, currentValue);

        repaint();
    }

    bool isHorizontal;
    juce::String label;
    int ccNumber;
    float currentValue = 0.0f;
    bool touching = false;
    bool showingExternal = false;
    Listener* listener = nullptr;
    juce::Typeface::Ptr ledTypeface;
};
