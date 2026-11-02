#pragma once

#include <JuceHeader.h>
#include "MidiMorfColors.h"
#include "Glassmorphism.h"

// Dark slate — system-y feel, distinct from the instrument page palettes.
static const PagePalette settingsPagePalette {
    juce::Colour (0xff202428),
    juce::Colour (0xff0E1014),
    juce::Colour (0xCC30353C),
    juce::Colour (0xD8202428),
    juce::Colour (0xCC000000)
};

//==============================================================================
// App-level settings: the walkthrough and the audio load.
class SettingsPage : public juce::Component, private juce::Timer
{
public:
    SettingsPage()
    {
        tourBtn.setLookAndFeel (&glassLnf);
        tourBtn.setColour (juce::TextButton::textColourOffId, MidiMorfColors::textLight);
        tourBtn.onClick = [this] { if (onTour) onTour(); };
        addAndMakeVisible (tourBtn);
    }

    ~SettingsPage() override
    {
        tourBtn.setLookAndFeel (nullptr);
    }

    std::function<void()> onTour;   // replay the walkthrough

    // Share of the audio callback's time budget in use, 0..1 — MainComponent
    // points this at the audio device. Shown while the page is visible.
    std::function<double()> getAudioLoad;

    void paint (juce::Graphics& g) override
    {
        Glassmorphism::paintPanel (g, getLocalBounds().toFloat(), settingsPagePalette);

        auto area = getLocalBounds().reduced (24);

        // Title
        g.setColour (MidiMorfColors::textLight);
        g.setFont (juce::FontOptions (28.0f, juce::Font::bold));
        auto titleArea = area.removeFromTop (44);
        g.drawText ("SETTINGS", titleArea, juce::Justification::centredLeft);
        area.removeFromTop (12);

        auto helperArea = area;

        // Audio load: now, and the recent peak (what matters for dropouts).
        g.setColour (MidiMorfColors::textLight.withAlpha (0.6f));
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText ("AUDIO LOAD", helperArea.removeFromTop (22), juce::Justification::centredLeft);

        auto meter = helperArea.removeFromTop (14).withWidth (juce::jmin (320, helperArea.getWidth())).toFloat();
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRoundedRectangle (meter, 4.0f);
        const auto loadColour = peakLoad < 0.5 ? MidiMorfColors::statusGreen
                              : peakLoad < 0.8 ? MidiMorfColors::statusAmber
                                               : juce::Colours::red;
        g.setColour (loadColour);
        g.fillRoundedRectangle (meter.withWidth (meter.getWidth() * (float) juce::jlimit (0.0, 1.0, load)), 4.0f);
        g.setColour (juce::Colours::white.withAlpha (0.8f));
        const float px = meter.getX() + meter.getWidth() * (float) juce::jlimit (0.0, 1.0, peakLoad);
        g.fillRect (px - 1.0f, meter.getY(), 2.0f, meter.getHeight());

        g.setColour (MidiMorfColors::textLight);
        g.setFont (juce::FontOptions (13.0f));
        g.drawText (juce::String (juce::roundToInt (load * 100.0)) + "%   peak " + juce::String (juce::roundToInt (peakLoad * 100.0)) + "%",
                    helperArea.removeFromTop (22), juce::Justification::centredLeft);
    }

    void resized() override
    {
        tourBtn.setBounds (getLocalBounds().reduced (24).withHeight (40).removeFromRight (200));
    }

private:
    void visibilityChanged() override
    {
        if (isShowing()) { peakLoad = 0.0; startTimerHz (5); }
        else             stopTimer();
    }

    // The peak falls back over ~5 s, so a spike stays readable.
    void timerCallback() override
    {
        if (! getAudioLoad)
            return;
        load = getAudioLoad();
        peakLoad = juce::jmax (load, peakLoad * 0.97);
        repaint();
    }

    double load = 0.0, peakLoad = 0.0;
    GlassButtonLookAndFeel glassLnf { settingsPagePalette, MidiMorfColors::accent, 8.0f };

    juce::TextButton tourBtn { "Take the tour" };
};
