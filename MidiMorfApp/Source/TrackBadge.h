#pragma once

#include <JuceHeader.h>
#include "MidiMorfColors.h"
#include "TrackRack.h"

//==============================================================================
// One colour per track, used wherever a track is shown (rail badge, the Sound
// page title, later the Session rows and mixer strips).
namespace TrackColours
{
    inline juce::Colour get (int track)
    {
        static const juce::Colour c[TrackRack::NumTracks] = {
            juce::Colour (0xffE0564A),   // T1 coral red
            juce::Colour (0xffA06CD5),   // T2 violet
            juce::Colour (0xffE0A030),   // T3 amber
            juce::Colour (0xff3FA7C9),   // T4 teal
        };
        return juce::isPositiveAndBelow (track, TrackRack::NumTracks) ? c[track] : juce::Colours::grey;
    }
}

//==============================================================================
// Rail card showing which track the current page plays ("T2" over "Bass").
// Tapping it opens the track picker. On pages that play nothing it shows a
// dim dash and ignores taps. Styled to match the Menu / Sound / Fave cards.
class TrackBadge : public juce::Button
{
public:
    TrackBadge() : juce::Button ("TrackBadge") {}

    // track < 0: this page has no track.
    void setTrack (int track, const juce::String& name)
    {
        currentTrack = track;
        trackName = name;
        setEnabled (track >= 0);
        repaint();
    }

protected:
    void paintButton (juce::Graphics& g, bool /*isHighlighted*/, bool isDown) override
    {
        auto bounds = getLocalBounds().toFloat().reduced (1.5f);
        const bool hasTrack = currentTrack >= 0;
        const auto colour = hasTrack ? TrackColours::get (currentTrack) : juce::Colours::grey;

        g.setColour (juce::Colour (0xff2A2A2E).withAlpha (0.85f));
        g.fillRoundedRectangle (bounds, 8.0f);
        g.setColour (colour.withAlpha (hasTrack ? 0.9f : 0.3f));
        g.drawRoundedRectangle (bounds.reduced (0.5f), 8.0f, hasTrack ? 1.6f : 1.0f);

        const float labelH = juce::jmin (16.0f, bounds.getHeight() * 0.28f);
        auto labelArea = bounds.removeFromBottom (labelH);

        g.setColour (hasTrack ? colour : juce::Colours::grey.withAlpha (0.5f));
        g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
        g.drawText (hasTrack ? "T" + juce::String (currentTrack + 1) : juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x93")),
                    bounds, juce::Justification::centred);

        g.setColour (MidiMorfColors::textLight.withAlpha (hasTrack ? 1.0f : 0.4f));
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (hasTrack ? trackName : juce::String ("Track"), labelArea, juce::Justification::centred);

        if (isDown && hasTrack)
        {
            g.setColour (colour.withAlpha (0.2f));
            g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (1.5f), 8.0f);
        }
    }

private:
    int currentTrack = -1;
    juce::String trackName;
};

//==============================================================================
// A track-volume slider that lives inside the track picker's popup menu.
// Shown in dB; the rack stores linear gain.
class TrackVolumeMenuItem : public juce::PopupMenu::CustomComponent
{
public:
    TrackVolumeMenuItem (TrackRack& r, int t, std::function<void()> changed)
        : juce::PopupMenu::CustomComponent (false), rack (r), track (t), onChange (std::move (changed))
    {
        slider.setSliderStyle (juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 22);
        slider.setRange (-40.0, juce::Decibels::gainToDecibels (TrackRack::MaxVolume), 0.1);
        slider.setDoubleClickReturnValue (true, 0.0);
        slider.textFromValueFunction = [] (double db) { return db <= -40.0 ? juce::String ("off") : juce::String (db, 1) + " dB"; };
        slider.setValue (juce::Decibels::gainToDecibels (rack.getVolume (track), -40.0f), juce::dontSendNotification);
        slider.setColour (juce::Slider::thumbColourId, TrackColours::get (track));
        slider.setColour (juce::Slider::trackColourId, TrackColours::get (track).withAlpha (0.6f));
        slider.onValueChange = [this]
        {
            const double db = slider.getValue();
            rack.setVolume (track, db <= -40.0 ? 0.0f : juce::Decibels::decibelsToGain ((float) db));
        };
        slider.onDragEnd = [this] { if (onChange) onChange(); };
        addAndMakeVisible (slider);

        label.setText ("T" + juce::String (track + 1) + " volume", juce::dontSendNotification);
        label.setFont (juce::FontOptions (13.0f));
        addAndMakeVisible (label);
    }

    void getIdealSize (int& w, int& h) override { w = 280; h = 64; }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12, 4);
        label.setBounds (r.removeFromTop (20));
        slider.setBounds (r);
    }

private:
    TrackRack& rack;
    const int track;
    std::function<void()> onChange;
    juce::Label label;
    juce::Slider slider;
};
