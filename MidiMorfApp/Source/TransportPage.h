#pragma once

#include <JuceHeader.h>
#include "EventRouter.h"
#include "MidiMorfColors.h"

class TransportPage : public juce::Component
{
public:
    TransportPage (EventRouter& client)
        : networkClient (client)
    {
    }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds().reduced (8);

        // Left column: utility buttons
        auto leftCol = area.removeFromLeft (70);
        int utilGap = 6;
        int utilH = (leftCol.getHeight() - (numUtils - 1) * utilGap) / numUtils;

        for (int i = 0; i < numUtils; ++i)
        {
            auto b = juce::Rectangle<float> (
                (float) leftCol.getX(),
                (float) (leftCol.getY() + i * (utilH + utilGap)),
                (float) leftCol.getWidth(),
                (float) utilH);
            utilBounds[i] = b;

            bool isPressed = (pressedUtil == i);
            drawY2KButton (g, b, isPressed, juce::Colour (0xFF2A3A5C), 6.0f);

            // Rotated text for sideways use
            g.setColour (isPressed ? juce::Colours::white : juce::Colour (0xFFB0C4E8));
            g.setFont (juce::FontOptions (11.0f, juce::Font::bold));

            juce::Graphics::ScopedSaveState ss (g);
            g.addTransform (juce::AffineTransform::rotation (
                juce::MathConstants<float>::halfPi, b.getCentreX(), b.getCentreY()));
            g.drawText (utilLabels[i], b, juce::Justification::centred, false);
        }

        area.removeFromLeft (8);

        // Right area: 3 big transport buttons
        int bigGap = 10;
        int bigH = (area.getHeight() - 2 * bigGap) / 3;

        for (int i = 0; i < 3; ++i)
        {
            auto b = juce::Rectangle<float> (
                (float) area.getX(),
                (float) (area.getY() + i * (bigH + bigGap)),
                (float) area.getWidth(),
                (float) bigH);
            bigBounds[i] = b;

            bool isPressed = (pressedBig == i);

            // Candy-bright Y2K colors
            juce::Colour baseCol, glowCol;
            switch (i)
            {
                case 0: baseCol = juce::Colour (0xFF00CC55); glowCol = juce::Colour (0xFF55FFaa); break;  // Play - vivid green
                case 1: baseCol = juce::Colour (0xFFFF8800); glowCol = juce::Colour (0xFFFFCC44); break;  // Stop - bright orange
                case 2: baseCol = juce::Colour (0xFFEE2233); glowCol = juce::Colour (0xFFFF6677); break;  // Record - hot red
                default: baseCol = MidiMorfColors::buttonFace; glowCol = MidiMorfColors::accent; break;
            }

            auto darkCol = baseCol.darker (0.5f);

            // Outer glow when pressed
            if (isPressed)
            {
                g.setColour (glowCol.withAlpha (0.35f));
                g.fillRoundedRectangle (b.expanded (6.0f), 18.0f);
            }

            // Dark base layer (visible through translucent top)
            g.setColour (darkCol);
            g.fillRoundedRectangle (b, 14.0f);

            // Main translucent body — gradient top-to-bottom
            {
                auto col = isPressed ? glowCol : baseCol;
                juce::ColourGradient grad (
                    col.withAlpha (0.85f), b.getX(), b.getY(),
                    col.withAlpha (0.35f), b.getX(), b.getBottom(), false);
                g.setGradientFill (grad);
                g.fillRoundedRectangle (b, 14.0f);
            }

            // Big glassy highlight — top half, strong white fade
            {
                auto highlight = b.withHeight (b.getHeight() * 0.45f);
                juce::ColourGradient sheen (
                    juce::Colours::white.withAlpha (isPressed ? 0.25f : 0.45f),
                    highlight.getX(), highlight.getY(),
                    juce::Colours::white.withAlpha (0.0f),
                    highlight.getX(), highlight.getBottom(), false);
                g.setGradientFill (sheen);
                g.fillRoundedRectangle (highlight.reduced (3.0f, 0.0f), 12.0f);
            }

            // White border — plastic rim
            g.setColour (juce::Colours::white.withAlpha (isPressed ? 0.35f : 0.2f));
            g.drawRoundedRectangle (b.reduced (1.0f), 14.0f, 1.5f);

            // Bottom inner shadow
            {
                auto shadowStrip = b.withTrimmedTop (b.getHeight() * 0.65f);
                juce::ColourGradient bottomShadow (
                    juce::Colours::black.withAlpha (0.0f), shadowStrip.getX(), shadowStrip.getY(),
                    juce::Colours::black.withAlpha (0.25f), shadowStrip.getX(), shadowStrip.getBottom(), false);
                g.setGradientFill (bottomShadow);
                g.fillRoundedRectangle (shadowStrip.reduced (2.0f, 0.0f), 10.0f);
            }

            // Icon — big and bold
            float cx = b.getCentreX();
            float cy = b.getCentreY() + 4.0f;  // nudge slightly below center (above gloss)
            float iconSize = juce::jmin (b.getWidth(), b.getHeight()) * 0.40f;

            switch (i)
            {
                case 0: // Play — triangle
                {
                    juce::Path tri;
                    tri.addTriangle (cx - iconSize * 0.55f, cy - iconSize * 0.6f,
                                     cx + iconSize * 0.55f, cy - iconSize * 0.6f,
                                     cx, cy + iconSize * 0.65f);
                    g.setColour (juce::Colours::black.withAlpha (0.25f));
                    g.fillPath (tri, juce::AffineTransform::translation (3.0f, 3.0f));
                    g.setColour (isPressed ? juce::Colours::white : juce::Colours::white.withAlpha (0.9f));
                    g.fillPath (tri);
                    break;
                }
                case 1: // Stop — rounded square
                {
                    float s = iconSize * 0.7f;
                    g.setColour (juce::Colours::black.withAlpha (0.25f));
                    g.fillRoundedRectangle (cx - s * 0.5f + 3.0f, cy - s * 0.5f + 3.0f, s, s, 4.0f);
                    g.setColour (isPressed ? juce::Colours::white : juce::Colours::white.withAlpha (0.9f));
                    g.fillRoundedRectangle (cx - s * 0.5f, cy - s * 0.5f, s, s, 4.0f);
                    break;
                }
                case 2: // Record — circle
                {
                    g.setColour (juce::Colours::black.withAlpha (0.25f));
                    g.fillEllipse (cx - iconSize * 0.5f + 3.0f, cy - iconSize * 0.5f + 3.0f, iconSize, iconSize);
                    g.setColour (isPressed ? juce::Colours::white : juce::Colours::white.withAlpha (0.9f));
                    g.fillEllipse (cx - iconSize * 0.5f, cy - iconSize * 0.5f, iconSize, iconSize);
                    break;
                }
            }
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        for (int i = 0; i < 3; ++i)
        {
            if (bigBounds[i].contains (e.position))
            {
                pressedBig = i;
                networkClient.sendTransportButton ((uint8_t) i);
                repaint();
                return;
            }
        }

        for (int i = 0; i < numUtils; ++i)
        {
            if (utilBounds[i].contains (e.position))
            {
                pressedUtil = i;
                networkClient.sendTransportButton (utilIds[i]);
                repaint();
                return;
            }
        }
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (pressedBig >= 0 || pressedUtil >= 0)
        {
            pressedBig = -1;
            pressedUtil = -1;
            repaint();
        }
    }

private:
    void drawY2KButton (juce::Graphics& g, juce::Rectangle<float> b,
                        bool isPressed, juce::Colour baseCol, float radius)
    {
        // Gradient body
        juce::ColourGradient grad (
            baseCol.brighter (isPressed ? 0.5f : 0.3f), b.getX(), b.getY(),
            baseCol.darker (0.2f), b.getX(), b.getBottom(), false);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (b, radius);

        // Glassy top highlight
        auto highlight = b.withHeight (b.getHeight() * 0.4f);
        juce::ColourGradient sheen (
            juce::Colours::white.withAlpha (isPressed ? 0.08f : 0.18f),
            highlight.getX(), highlight.getY(),
            juce::Colours::white.withAlpha (0.0f),
            highlight.getX(), highlight.getBottom(), false);
        g.setGradientFill (sheen);
        g.fillRoundedRectangle (highlight.reduced (1.0f, 0.0f), radius);

        // Border
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.drawRoundedRectangle (b.reduced (0.5f), radius, 1.0f);
    }

    EventRouter& networkClient;

    // Big transport: Play, Stop, Record
    juce::Rectangle<float> bigBounds[3];
    int pressedBig = -1;

    // Utility buttons
    static constexpr int numUtils = 5;
    juce::Rectangle<float> utilBounds[5];
    int pressedUtil = -1;

    const juce::String utilLabels[5] = { "TAP", "LOOP", "CLIP/DEV", "MIX/SCENE", "METRO" };
    const uint8_t utilIds[5] = { 7, 5, 8, 9, 10 };
};
