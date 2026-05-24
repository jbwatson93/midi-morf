#pragma once

#include <JuceHeader.h>
#include "MidiMorfColors.h"
#include "BinaryData.h"

class AboutPage : public juce::Component
{
public:
    AboutPage()
    {
        ledTypeface = juce::Typeface::createSystemTypefaceFor (
            BinaryData::DSEG14ModernMiniRegular_ttf,
            BinaryData::DSEG14ModernMiniRegular_ttfSize);
    }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat().reduced (16.0f);

        // LED screen title
        auto titleArea = area.removeFromTop (46.0f);
        {
            auto ledRect = titleArea;

            // Orange LCD background
            g.setColour (MidiMorfColors::accent);
            g.fillRoundedRectangle (ledRect, 6.0f);

            // Black border
            g.setColour (juce::Colour (0xFF000000));
            g.drawRoundedRectangle (ledRect.reduced (1.0f), 6.0f, 3.5f);

            // Inner shadow
            float shadowSize = 8.0f;
            auto inner = ledRect.reduced (2.0f);
            g.setGradientFill (juce::ColourGradient (
                juce::Colour (0xAA000000), inner.getX(), inner.getY(),
                juce::Colour (0x00000000), inner.getX(), inner.getY() + shadowSize, false));
            g.fillRect (inner.getX(), inner.getY(), inner.getWidth(), shadowSize);
            g.setGradientFill (juce::ColourGradient (
                juce::Colour (0xAA000000), inner.getX(), inner.getBottom(),
                juce::Colour (0x00000000), inner.getX(), inner.getBottom() - shadowSize, false));
            g.fillRect (inner.getX(), inner.getBottom() - shadowSize, inner.getWidth(), shadowSize);
            g.setGradientFill (juce::ColourGradient (
                juce::Colour (0xAA000000), inner.getX(), inner.getY(),
                juce::Colour (0x00000000), inner.getX() + shadowSize, inner.getY(), false));
            g.fillRect (inner.getX(), inner.getY(), shadowSize, inner.getHeight());
            g.setGradientFill (juce::ColourGradient (
                juce::Colour (0xAA000000), inner.getRight(), inner.getY(),
                juce::Colour (0x00000000), inner.getRight() - shadowSize, inner.getY(), false));
            g.fillRect (inner.getRight() - shadowSize, inner.getY(), shadowSize, inner.getHeight());

            // Dark text on orange
            g.setColour (juce::Colour (0xFF2A1A00));
            g.setFont (juce::Font (ledTypeface).withHeight (28.0f));
            g.drawText ("MIDI-MORF", ledRect, juce::Justification::centred);
        }

        area.removeFromTop (10.0f);

        // Subtitle
        g.setFont (juce::FontOptions (13.0f));
        g.setColour (MidiMorfColors::textDark.withAlpha (0.7f));
        auto subArea = area.removeFromTop (20.0f);
        g.drawText ("Expressive MIDI Controllers for iOS", subArea, juce::Justification::centred);

        area.removeFromTop (20.0f);

        // Info sections
        g.setColour (MidiMorfColors::textDark);
        float sectionGap = 18.0f;

        drawSection (g, area, "Getting Started",
            "1. Download the MidiMorf Server app on your Mac\n"
            "2. Open the server app - it creates a virtual MIDI\n"
            "   port called \"Midi-Morf\"\n"
            "3. In your DAW, select \"Midi-Morf\" as a MIDI input\n"
            "4. For Transport controls, set \"Midi-Morf\" as a\n"
            "   Mackie Control surface in your DAW\n"
            "5. Make sure your iPhone and Mac are on the\n"
            "   same WiFi network\n"
            "6. The app will auto-connect to the server");

        area.removeFromTop (sectionGap);

        drawSection (g, area, "Server Download",
            "Get the free MidiMorf Server app at:\n"
            "www.retro-relic.com/downloads");

        area.removeFromTop (sectionGap);

        drawSection (g, area, "Compatibility",
            "- iOS 15+ (iPhone)\n"
            "- macOS 12+ (server)\n"
            "- Any DAW that accepts MIDI input");

        area.removeFromTop (sectionGap);

        drawSection (g, area, "Troubleshooting",
            "- Not connecting? Check that both devices are\n"
            "  on the same WiFi network\n"
            "- No MIDI in DAW? Select \"Midi-Morf\" as a\n"
            "  MIDI input on the track\n"
            "- Server not finding port? Restart the server app");
    }

    void resized() override {}

private:
    juce::Typeface::Ptr ledTypeface;

    void drawSection (juce::Graphics& g, juce::Rectangle<float>& area,
                      const juce::String& heading, const juce::String& body)
    {
        g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        g.setColour (MidiMorfColors::accent);
        auto headingArea = area.removeFromTop (20.0f);
        g.drawText (heading, headingArea, juce::Justification::centredLeft);

        area.removeFromTop (4.0f);

        g.setFont (juce::FontOptions (13.0f));
        g.setColour (MidiMorfColors::textDark);

        juce::StringArray lines;
        lines.addTokens (body, "\n", "");

        for (auto& line : lines)
        {
            auto lineArea = area.removeFromTop (18.0f);
            g.drawText (line, lineArea, juce::Justification::centredLeft);
        }
    }
};
