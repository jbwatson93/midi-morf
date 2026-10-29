#pragma once

#include <JuceHeader.h>
#include "EventRouter.h"
#include "ChordGrid.h"
#include "StrumPlate.h"
#include "MidiMorfColors.h"
#include "Glassmorphism.h"

// Deep midnight indigo — dark enough that the rainbow cells stay the visual
// focus, with a slight blue undertone so it doesn't read flat black.
static const PagePalette strumChordPagePalette {
    juce::Colour (0xff0F1228),       // substrate top — deep midnight indigo
    juce::Colour (0xff050610),       // substrate bottom — near-black
    juce::Colour (0xCC1A1F40),       // acrylic top — translucent indigo
    juce::Colour (0xD80F1228),       // acrylic bottom — deeper translucent
    juce::Colour (0xCC000000)        // edge — translucent black
};

//==============================================================================
// Portrait-aspect content (chord grid on top, strum plate at the bottom).
// Hosted inside OmnichordPage which rotates this 90° CCW into landscape.
class OmnichordPortraitContent : public juce::Component,
                                 public ChordGrid::Listener,
                                 public StrumPlate::Listener
{
public:
    OmnichordPortraitContent (EventRouter& client)
        : networkClient (client)
    {
        chordGrid.setListener (this);
        addAndMakeVisible (chordGrid);

        strumPlate.setListener (this);
        addAndMakeVisible (strumPlate);
    }

    void resized() override
    {
        auto area = getLocalBounds();

        auto strumArea = area.removeFromBottom (95);
        strumPlate.setBounds (strumArea);

        area.removeFromBottom (4);
        chordGrid.setBounds (area);
    }

    void chordSelected (uint8_t rootNote, uint8_t quality) override
    {
        networkClient.sendChordSelect (rootNote, quality);
    }

    void chordReleased() override
    {
        networkClient.sendChordRelease();
    }

    void strumGesture (float position, float velocity) override
    {
        networkClient.sendStrumGesture (position, velocity);
    }

private:
    EventRouter& networkClient;
    ChordGrid chordGrid;
    StrumPlate strumPlate;
};

//==============================================================================
// Outer page — landscape container that rotates the portrait content 90° CCW
// so the user tilts their head left to read it.
class OmnichordPage : public juce::Component
{
public:
    OmnichordPage (EventRouter& client) : content (client)
    {
        addAndMakeVisible (content);
    }

    void paint (juce::Graphics& g) override
    {
        // Page-level paint is landscape-native — the rotated content paints
        // on top of this dark indigo glass panel.
        Glassmorphism::paintPanel (g, getLocalBounds().toFloat(), strumChordPagePalette);
    }

    void resized() override
    {
        int landW = getWidth();
        int landH = getHeight();
        if (landW <= 0 || landH <= 0) return;

        // Portrait content: width = landscape height, height = landscape width.
        content.setSize (landH, landW);

        // Rotate -90° (CCW) so the portrait top becomes the landscape left edge.
        content.setTransform (
            juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi, 0.0f, 0.0f)
            .translated (0.0f, (float) landH));
    }

private:
    OmnichordPortraitContent content;
};
