#pragma once

#include <JuceHeader.h>
#include "EventRouter.h"
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
// App-level settings: connection status + WiFi/Local output mode toggle.
// MainComponent owns the network/router state and pushes updates here via
// setConnectionStatus / refreshFromRouter.
class SettingsPage : public juce::Component, private juce::Timer
{
public:
    SettingsPage (EventRouter& r) : router (r)
    {
        wifiBtn.setButtonText ("WiFi");
        wifiBtn.setClickingTogglesState (true);
        wifiBtn.setRadioGroupId (1);
        wifiBtn.setColour (juce::TextButton::buttonColourId,  juce::Colour (0xff3A434E));
        wifiBtn.setColour (juce::TextButton::textColourOffId, MidiMorfColors::textLight);
        wifiBtn.setColour (juce::TextButton::textColourOnId,  MidiMorfColors::textLight);
        wifiBtn.setLookAndFeel (&glassLnf);
        wifiBtn.onClick = [this]
        {
            if (wifiBtn.getToggleState())
            {
                router.setMode (EventRouter::Mode::WiFi);
                if (onModeChanged) onModeChanged();
            }
        };
        addAndMakeVisible (wifiBtn);

        localBtn.setButtonText ("Local");
        localBtn.setClickingTogglesState (true);
        localBtn.setRadioGroupId (1);
        localBtn.setColour (juce::TextButton::buttonColourId,  juce::Colour (0xff3A434E));
        localBtn.setColour (juce::TextButton::textColourOffId, MidiMorfColors::textLight);
        localBtn.setColour (juce::TextButton::textColourOnId,  MidiMorfColors::textLight);
        localBtn.setLookAndFeel (&glassLnf);
        localBtn.onClick = [this]
        {
            if (localBtn.getToggleState())
            {
                router.setMode (EventRouter::Mode::Local);
                if (onModeChanged) onModeChanged();
            }
        };
        addAndMakeVisible (localBtn);

        refreshFromRouter();

        tourBtn.setLookAndFeel (&glassLnf);
        tourBtn.setColour (juce::TextButton::textColourOffId, MidiMorfColors::textLight);
        tourBtn.onClick = [this] { if (onTour) onTour(); };
        addAndMakeVisible (tourBtn);
    }

    ~SettingsPage() override
    {
        wifiBtn.setLookAndFeel  (nullptr);
        localBtn.setLookAndFeel (nullptr);
        tourBtn.setLookAndFeel  (nullptr);
    }

    // Called by MainComponent on connect / disconnect / searching changes.
    void setConnectionStatus (bool isConnected, const juce::String& host)
    {
        connected     = isConnected;
        connectedHost = host;
        repaint();
    }

    // Called when the mode is changed elsewhere (e.g. the floating menu) so
    // the toggle buttons stay in sync.
    void refreshFromRouter()
    {
        const bool isLocal = (router.getMode() == EventRouter::Mode::Local);
        localBtn.setToggleState (isLocal,  juce::dontSendNotification);
        wifiBtn .setToggleState (! isLocal, juce::dontSendNotification);
        repaint();
    }

    std::function<void()> onTour;   // replay the walkthrough

    // MainComponent sets this so it can saveState() when the user toggles
    // mode from the settings page.
    std::function<void()> onModeChanged;

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

        // Status row — dot + message
        auto statusRow = area.removeFromTop (44);
        const float dotSize = 16.0f;
        auto dotArea = statusRow.removeFromLeft ((int) dotSize + 12);
        const float dotX = dotArea.getX() + 4.0f;
        const float dotY = dotArea.getCentreY() - dotSize * 0.5f;

        const juce::Colour dotCol = connected ? MidiMorfColors::statusGreen
                                              : MidiMorfColors::statusAmber;
        // Soft glow around the dot
        g.setColour (dotCol.withAlpha (0.25f));
        g.fillEllipse (dotX - 4.0f, dotY - 4.0f, dotSize + 8.0f, dotSize + 8.0f);
        g.setColour (dotCol);
        g.fillEllipse (dotX, dotY, dotSize, dotSize);

        const juce::String statusText = connected
            ? juce::String ("Connected") + (connectedHost.isNotEmpty()
                                                ? " to " + connectedHost
                                                : juce::String())
            : juce::String ("Searching for a host...");
        g.setColour (MidiMorfColors::textLight);
        g.setFont (juce::FontOptions (18.0f));
        g.drawText (statusText, statusRow, juce::Justification::centredLeft);

        area.removeFromTop (28);

        // Section label
        g.setColour (MidiMorfColors::textLight.withAlpha (0.6f));
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        auto labelArea = area.removeFromTop (22);
        g.drawText ("OUTPUT MODE", labelArea, juce::Justification::centredLeft);
        area.removeFromTop (4);

        // (Mode-toggle button bounds set in resized().)

        // Helper text under the mode toggle
        auto helperArea = area;
        helperArea.removeFromTop (56);                  // skip past the buttons
        g.setColour (MidiMorfColors::textLight.withAlpha (0.45f));
        g.setFont (juce::FontOptions (12.0f));
        const juce::String help = (router.getMode() == EventRouter::Mode::Local)
            ? "Local: the phone's built-in synth produces audio. WiFi server is ignored for synth-capable controllers."
            : "WiFi: notes + CCs are sent over WiFi to the macOS server, which routes MIDI to your DAW.";
        g.drawFittedText (help, helperArea.removeFromTop (44), juce::Justification::topLeft, 2);

        // Audio load: now, and the recent peak (what matters for dropouts).
        helperArea.removeFromTop (12);
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
        auto area = getLocalBounds().reduced (24);
        tourBtn.setBounds (area.withHeight (40).removeFromRight (200));
        area.removeFromTop (44 + 12);            // title + gap
        area.removeFromTop (44 + 28);            // status row + gap
        area.removeFromTop (22 + 4);             // section label + gap

        // Mode toggle: two buttons side-by-side
        auto toggleRow = area.removeFromTop (48);
        const int half = (toggleRow.getWidth() - 8) / 2;
        wifiBtn .setBounds (toggleRow.removeFromLeft (half));
        toggleRow.removeFromLeft (8);
        localBtn.setBounds (toggleRow.removeFromLeft (half));
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

    EventRouter& router;
    double load = 0.0, peakLoad = 0.0;
    GlassButtonLookAndFeel glassLnf { settingsPagePalette, MidiMorfColors::accent, 8.0f };

    juce::TextButton wifiBtn, localBtn, tourBtn { "Take the tour" };
    bool connected = false;
    juce::String connectedHost;
};
