#pragma once

#include <JuceHeader.h>
#include "MidiMorfColors.h"

class StrumPlate : public juce::Component
{
public:
    struct Listener
    {
        virtual ~Listener() = default;
        virtual void strumGesture (float position, float velocity) = 0;
    };

    StrumPlate() {}

    void setListener (Listener* l) { listener = l; }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();

        g.setColour (MidiMorfColors::padBackground);
        g.fillRoundedRectangle (bounds, 8.0f);

        g.setColour (MidiMorfColors::padBorder);
        g.drawRoundedRectangle (bounds.reduced (1.0f), 8.0f, 1.5f);

        // Draw vertical string lines
        int numLines = 24;
        float lineSpacing = bounds.getWidth() / (numLines + 1);
        g.setColour (MidiMorfColors::crosshair.withAlpha (0.3f));
        for (int i = 1; i <= numLines; ++i)
        {
            float x = bounds.getX() + i * lineSpacing;
            g.drawVerticalLine ((int) x, bounds.getY() + 6.0f, bounds.getBottom() - 6.0f);
        }

        // Draw touch indicator
        if (touching)
        {
            float px = bounds.getX() + lastPosition * bounds.getWidth();
            g.setColour (MidiMorfColors::accent.withAlpha (0.6f));
            g.fillRect (px - 2.0f, bounds.getY(), 4.0f, bounds.getHeight());
        }

        g.setColour (MidiMorfColors::textLight.withAlpha (0.3f));
        g.setFont (juce::FontOptions (12.0f));
        g.drawText ("SONIC STRINGS", bounds.reduced (10.0f), juce::Justification::topLeft);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        touching = true;
        lastX = e.position.x;
        lastTime = juce::Time::getMillisecondCounterHiRes();
        updateStrum (e.position);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (touching)
            updateStrum (e.position);
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        touching = false;
        repaint();

        // Send zero-velocity to signal strum release
        if (listener != nullptr)
            listener->strumGesture (lastPosition, 0.0f);
    }

private:
    void updateStrum (juce::Point<float> pos)
    {
        auto bounds = getLocalBounds().toFloat();
        float position = juce::jlimit (0.0f, 1.0f, (pos.x - bounds.getX()) / bounds.getWidth());

        double now = juce::Time::getMillisecondCounterHiRes();
        double dt = now - lastTime;
        float dx = std::abs (pos.x - lastX);
        float speed = (dt > 0.0) ? (float) (dx / dt) : 0.0f;
        float velocity = juce::jlimit (0.0f, 1.0f, speed * 2.0f);

        lastX = pos.x;
        lastTime = now;
        lastPosition = position;

        repaint();

        if (listener != nullptr)
            listener->strumGesture (position, velocity);
    }

    Listener* listener = nullptr;
    bool touching = false;
    float lastX = 0.0f;
    double lastTime = 0.0;
    float lastPosition = 0.5f;
};
