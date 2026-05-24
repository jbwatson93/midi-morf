#pragma once

#include <JuceHeader.h>
#include "EventRouter.h"
#include "ChordGrid.h"
#include "StrumPlate.h"
#include "MidiMorfColors.h"

class OmnichordPage : public juce::Component,
                      public ChordGrid::Listener,
                      public StrumPlate::Listener
{
public:
    OmnichordPage (EventRouter& client)
        : networkClient (client)
    {
        chordGrid.setListener (this);
        addAndMakeVisible (chordGrid);

        strumPlate.setListener (this);
        addAndMakeVisible (strumPlate);
    }

    void paint (juce::Graphics&) override {}

    void resized() override
    {
        auto area = getLocalBounds();

        // Strum plate strip at bottom
        auto strumArea = area.removeFromBottom (95);
        strumPlate.setBounds (strumArea);

        area.removeFromBottom (4);

        // Chord grid fills the rest on top
        chordGrid.setBounds (area);
    }

    // ChordGrid::Listener
    void chordSelected (uint8_t rootNote, uint8_t quality) override
    {
        networkClient.sendChordSelect (rootNote, quality);
    }

    void chordReleased() override
    {
        networkClient.sendChordRelease();
    }

    // StrumPlate::Listener
    void strumGesture (float position, float velocity) override
    {
        networkClient.sendStrumGesture (position, velocity);
    }

private:
    EventRouter& networkClient;
    ChordGrid chordGrid;
    StrumPlate strumPlate;
};
