#pragma once

#include <JuceHeader.h>
#include "MidiMorfColors.h"
#include "BinaryData.h"
#include "Glassmorphism.h"

// Deep coffee — harmonises with the wood frame, lets the chrome AppIcon pop.
static const PagePalette aboutPagePalette {
    juce::Colour (0xff241712),
    juce::Colour (0xff0E0805),
    juce::Colour (0xCC3C2A20),
    juce::Colour (0xD8241712),
    juce::Colour (0xCC000000)
};

class AboutPage : public juce::Component
{
public:
    AboutPage()
    {
        appIcon = juce::ImageCache::getFromMemory (BinaryData::Icon_png,
                                                    BinaryData::Icon_pngSize);
    }

    void paint (juce::Graphics& g) override
    {
        // Themed glass panel
        Glassmorphism::paintPanel (g, getLocalBounds().toFloat(), aboutPagePalette);

        auto area = getLocalBounds().toFloat().reduced (28.0f);

        //======================== Left column: hero icon =====================
        const float iconColW = 260.0f;
        auto iconCol = area.removeFromLeft (iconColW);
        area.removeFromLeft (28.0f);

        // Reserved icon area, centred vertically within the column
        const float iconBoxSize = juce::jmin (200.0f, iconCol.getWidth() - 16.0f);
        const float iconCornerRadius = iconBoxSize * 0.225f;   // iOS app-icon mask ratio
        auto iconBox = juce::Rectangle<float> (
            iconCol.getCentreX() - iconBoxSize * 0.5f,
            iconCol.getY() + 16.0f,
            iconBoxSize, iconBoxSize);

        if (! appIcon.isNull())
        {
            // Soft warm glow halo behind the icon
            {
                const float haloPad = 14.0f;
                auto halo = iconBox.expanded (haloPad);
                juce::ColourGradient haloGrad (
                    juce::Colour (0xff8C2A1F).withAlpha (0.55f), halo.getCentreX(), halo.getCentreY(),
                    juce::Colour (0xff8C2A1F).withAlpha (0.0f),  halo.getRight(),    halo.getCentreY(),
                    true);
                g.setGradientFill (haloGrad);
                g.fillEllipse (halo);
            }

            // Drop shadow
            {
                juce::Path shadowPath;
                shadowPath.addRoundedRectangle (iconBox.translated (0.0f, 8.0f), iconCornerRadius);
                g.setColour (juce::Colour (0x88000000));
                g.fillPath (shadowPath);
            }

            // Icon clipped to iOS-style rounded square
            {
                juce::Graphics::ScopedSaveState save (g);
                juce::Path clip;
                clip.addRoundedRectangle (iconBox, iconCornerRadius);
                g.reduceClipRegion (clip);
                g.drawImage (appIcon, iconBox, juce::RectanglePlacement::centred);
            }

            // Hairline edge stroke
            g.setColour (juce::Colour (0xff000000).withAlpha (0.6f));
            g.drawRoundedRectangle (iconBox, iconCornerRadius, 1.5f);
            g.setColour (juce::Colour (0xffFFFFFF).withAlpha (0.15f));
            g.drawRoundedRectangle (iconBox.reduced (1.5f), iconCornerRadius - 1.0f, 0.8f);
        }
        else
        {
            // Fallback: keep the page usable if BinaryData::AppIcon_png isn't generated yet
            g.setColour (juce::Colour (0xff402015));
            g.fillRoundedRectangle (iconBox, iconCornerRadius);
            g.setColour (MidiMorfColors::textLight.withAlpha (0.6f));
            g.setFont (juce::FontOptions (14.0f, juce::Font::italic));
            g.drawText ("[icon]", iconBox, juce::Justification::centred);
        }

        // Brand wordmark beneath the icon
        auto brandArea = juce::Rectangle<float> (iconCol.getX(),
                                                  iconBox.getBottom() + 22.0f,
                                                  iconCol.getWidth(), 18.0f);
        g.setColour (MidiMorfColors::accent);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText ("RETRO RELIC", brandArea, juce::Justification::centred);

        // Subtitle below brand
        auto taglineArea = brandArea.translated (0.0f, 20.0f).withHeight (16.0f);
        g.setColour (MidiMorfColors::textLight.withAlpha (0.55f));
        g.setFont (juce::FontOptions (11.0f, juce::Font::italic));
        g.drawText ("Expressive MIDI Controllers for iOS",
                    taglineArea, juce::Justification::centred);

        //======================== Right column: info sections ================
        // Vertical divider hairline between columns
        g.setColour (juce::Colour (0xffFFFFFF).withAlpha (0.08f));
        g.drawLine (area.getX() - 14.0f, area.getY() + 8.0f,
                    area.getX() - 14.0f, area.getBottom() - 8.0f, 1.0f);

        const float sectionGap = 16.0f;

        drawSection (g, area, "What's Inside",
            "7 expressive controllers + a built-in synth.\n"
            "Play standalone, or send MIDI to a DAW over WiFi.\n"
            "Tap the menu button to switch modes or jump\n"
            "between controllers.");
        area.removeFromTop (sectionGap);

        drawSection (g, area, "Local Play (no setup)",
            "Open Settings, choose Local mode, play any controller.\n"
            "The built-in synth makes the sound. Tap Synth to\n"
            "design your own patches.");
        area.removeFromTop (sectionGap);

        drawSection (g, area, "DAW Setup (WiFi mode)",
            "1. Download the MidiMorf Server for Mac\n"
            "2. Run it - a virtual MIDI port appears system-wide\n"
            "3. Choose \"Midi-Morf\" as a MIDI input in your DAW\n"
            "4. For Transport, set it as a Mackie Control surface\n"
            "5. Same WiFi network - the app auto-connects");
        area.removeFromTop (sectionGap);

        drawSection (g, area, "Server & Support",
            "retro-relic.com/downloads");
    }

    void resized() override {}

private:
    juce::Image appIcon;

    void drawSection (juce::Graphics& g, juce::Rectangle<float>& area,
                      const juce::String& heading, const juce::String& body)
    {
        // Heading — accent colour, small caps feel via uppercase + tracking
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.setColour (MidiMorfColors::accent);
        auto headingArea = area.removeFromTop (18.0f);
        g.drawText (heading.toUpperCase(), headingArea, juce::Justification::centredLeft);

        // Hairline under heading
        g.setColour (MidiMorfColors::accent.withAlpha (0.35f));
        g.drawLine (headingArea.getX(),  headingArea.getBottom() + 1.0f,
                    headingArea.getX() + 48.0f, headingArea.getBottom() + 1.0f, 1.0f);

        area.removeFromTop (8.0f);

        g.setFont (juce::FontOptions (13.0f));
        g.setColour (MidiMorfColors::textLight.withAlpha (0.88f));

        juce::StringArray lines;
        lines.addTokens (body, "\n", "");

        for (auto& line : lines)
        {
            auto lineArea = area.removeFromTop (18.0f);
            g.drawText (line, lineArea, juce::Justification::centredLeft);
        }
    }
};
