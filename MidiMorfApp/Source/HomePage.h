#pragma once

#include <JuceHeader.h>
#include "Glassmorphism.h"
#include "SynthLookAndFeel.h"
#include "TrackRail.h"
#include "MidiMorfProtocol.h"

static const PagePalette homePagePalette {
    juce::Colour (0xff22201E),
    juce::Colour (0xff100F0E),
    juce::Colour (0xCC34302C),
    juce::Colour (0xD8221F1C),
    juce::Colour (0xCC000000)
};

//==============================================================================
// Home: one row per track — what plays it (the instrument page), what it
// sounds like, and its volume — plus the pages that belong to no track.
// Later this grows into the Session grid; these rows are its track column.
//
// MainComponent owns the track setup and wires the callbacks.
class HomePage : public juce::Component
{
public:
    struct Callbacks
    {
        std::function<int (int track)> getInstrument;            // a ControllerMode, or -1
        std::function<void (int track, int mode)> setInstrument;
        std::function<int (int track)> getSound;                  // AudioEngine::Sound as int
        std::function<void (int track, int sound)> setSound;
        std::function<float (int track)> getVolume;
        std::function<void (int track, float gain)> setVolume;
        std::function<void (int pageIndex)> openPage;
        std::function<void()> changed;                            // persist
    };

    // The page indices the right-hand buttons open, and the Drum Grid's (an
    // instrument that isn't a ControllerMode).
    struct Pages { int transport, fx, settings, about, drumGrid; };

    HomePage (Callbacks cb, Pages p) : callbacks (std::move (cb)), pages (p)
    {
        setLookAndFeel (&laf);

        for (int t = 0; t < TrackRack::NumTracks; ++t)
            addAndMakeVisible (rows.add (new Row (*this, t)));

        const std::pair<const char*, int> links[] = {
            { "Transport", pages.transport }, { "Morf FX", pages.fx },
            { "Settings", pages.settings },   { "About", pages.about } };
        for (auto& [name, page] : links)
        {
            auto* b = linkButtons.add (new juce::TextButton (name));
            b->setLookAndFeel (&glassLnf);
            b->setColour (juce::TextButton::textColourOffId, MidiMorfColors::textLight);
            const int target = page;
            b->onClick = [this, target] { if (callbacks.openPage) callbacks.openPage (target); };
            addAndMakeVisible (b);
        }
    }

    ~HomePage() override
    {
        for (auto* b : linkButtons)
            b->setLookAndFeel (nullptr);
        setLookAndFeel (nullptr);
    }

    // Re-read everything — the setup can change elsewhere (e.g. Breaks moving).
    void refresh()
    {
        for (auto* r : rows)
            r->refresh();
    }

    void visibilityChanged() override { if (isVisible()) refresh(); }

    void paint (juce::Graphics& g) override
    {
        Glassmorphism::paintPanel (g, getLocalBounds().toFloat(), homePagePalette);

        g.setColour (MidiMorfColors::textLight.withAlpha (0.5f));
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText ("TRACK", columnHeads.withWidth (44), juce::Justification::centredLeft);
        g.drawText ("PLAYED BY", instrumentHead, juce::Justification::centredLeft);
        g.drawText ("SOUND", soundHead, juce::Justification::centredLeft);
        g.drawText ("VOLUME", volumeHead, juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (12, 8);

        auto right = area.removeFromRight (juce::jmin (130, area.getWidth() / 5));
        area.removeFromRight (10);
        const int linkH = juce::jmin (52, (right.getHeight() - 3 * 8) / 4);
        for (auto* b : linkButtons)
        {
            b->setBounds (right.removeFromTop (linkH));
            right.removeFromTop (8);
        }

        columnHeads = area.removeFromTop (18);
        const int rowH = juce::jmin (64, (area.getHeight() - 3 * 6) / TrackRack::NumTracks);
        for (auto* r : rows)
        {
            r->setBounds (area.removeFromTop (rowH));
            area.removeFromTop (6);
        }

        // Header labels line up with the first row's columns.
        if (! rows.isEmpty())
        {
            auto off = rows[0]->getPosition();
            instrumentHead = rows[0]->instrument.getBounds().translated (off.x, 0).withY (columnHeads.getY()).withHeight (columnHeads.getHeight());
            soundHead      = rows[0]->sound.getBounds().translated (off.x, 0).withY (columnHeads.getY()).withHeight (columnHeads.getHeight());
            volumeHead     = rows[0]->volume.getBounds().translated (off.x, 0).withY (columnHeads.getY()).withHeight (columnHeads.getHeight());
        }
    }

private:
    //==========================================================================
    // Instrument choices, as ControllerMode values. Combo ids are mode + 1;
    // "Nothing" is NoneId.
    static constexpr int NoneId = 100;
    std::vector<std::pair<int, const char*>> instruments() const
    {
        using CM = MidiMorf::ControllerMode;
        return { { (int) CM::MorfChord, "Morf-Chord" }, { (int) CM::RoseChord, "Rose-Chord" },
                 { (int) CM::Omnichord, "Strum-Chord" }, { (int) CM::MorfPad, "Morf-Pad" },
                 { (int) CM::DualMPad, "Dual M-Pad" },
                 { (int) CM::StepSequencer, "Step-Seq" }, { (int) CM::SliceSequencer, "Slice-Seq" },
                 { pages.drumGrid, "Drum Grid" } };
    }

    struct Row : public juce::Component
    {
        Row (HomePage& o, int t) : owner (o), track (t)
        {
            for (auto& [mode, name] : owner.instruments())
                instrument.addItem (name, mode + 1);
            instrument.addSeparator();
            instrument.addItem ("Nothing", NoneId);
            instrument.onChange = [this]
            {
                const int id = instrument.getSelectedId();
                if (owner.callbacks.setInstrument)
                    owner.callbacks.setInstrument (track, id == NoneId ? -1 : id - 1);
                owner.refresh();   // the instrument may have left another row
                if (owner.callbacks.changed) owner.callbacks.changed();
            };
            addAndMakeVisible (instrument);

            sound.addItem ("Synth", 1 + 1);
            sound.addItem ("Sampler", 2 + 1);
            sound.addItem ("Drums", 3 + 1);
            sound.addItem ("Empty", 0 + 1);
            sound.onChange = [this]
            {
                if (owner.callbacks.setSound)
                    owner.callbacks.setSound (track, sound.getSelectedId() - 1);
                owner.refresh();   // Breaks may have left another row
                if (owner.callbacks.changed) owner.callbacks.changed();
            };
            addAndMakeVisible (sound);

            volume.setSliderStyle (juce::Slider::LinearHorizontal);
            volume.setTextBoxStyle (juce::Slider::TextBoxRight, false, 62, 22);
            volume.setRange (-40.0, juce::Decibels::gainToDecibels (TrackRack::MaxVolume), 0.1);
            volume.setDoubleClickReturnValue (true, 0.0);
            volume.textFromValueFunction = [] (double db) { return db <= -40.0 ? juce::String ("off") : juce::String (db, 1) + " dB"; };
            volume.setColour (juce::Slider::thumbColourId, TrackColours::get (track));
            volume.setColour (juce::Slider::trackColourId, TrackColours::get (track).withAlpha (0.6f));
            volume.onValueChange = [this]
            {
                const double db = volume.getValue();
                if (owner.callbacks.setVolume)
                    owner.callbacks.setVolume (track, db <= -40.0 ? 0.0f : juce::Decibels::decibelsToGain ((float) db));
            };
            volume.onDragEnd = [this] { if (owner.callbacks.changed) owner.callbacks.changed(); };
            addAndMakeVisible (volume);
        }

        void refresh()
        {
            const int mode = owner.callbacks.getInstrument ? owner.callbacks.getInstrument (track) : -1;
            instrument.setSelectedId (mode >= 0 ? mode + 1 : NoneId, juce::dontSendNotification);
            sound.setSelectedId ((owner.callbacks.getSound ? owner.callbacks.getSound (track) : 0) + 1, juce::dontSendNotification);
            const float gain = owner.callbacks.getVolume ? owner.callbacks.getVolume (track) : 1.0f;
            volume.setValue (juce::Decibels::gainToDecibels (gain, -40.0f), juce::dontSendNotification);
        }

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat();
            const auto colour = TrackColours::get (track);
            g.setColour (juce::Colours::black.withAlpha (0.25f));
            g.fillRoundedRectangle (r, 8.0f);
            g.setColour (colour.withAlpha (0.8f));
            g.drawRoundedRectangle (r.reduced (0.75f), 8.0f, 1.5f);

            g.setColour (colour);
            g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
            g.drawText ("T" + juce::String (track + 1), r.removeFromLeft (44.0f), juce::Justification::centred);
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (6, 0);
            r.removeFromLeft (40);
            const int w = r.getWidth();
            const int comboH = juce::jmin (34, r.getHeight() - 12);
            instrument.setBounds (r.removeFromLeft (w * 30 / 100).withSizeKeepingCentre (w * 30 / 100 - 8, comboH));
            sound.setBounds (r.removeFromLeft (w * 24 / 100).withSizeKeepingCentre (w * 24 / 100 - 8, comboH));
            volume.setBounds (r.reduced (4, 0).withSizeKeepingCentre (r.getWidth() - 8, comboH));
        }

        HomePage& owner;
        const int track;
        juce::ComboBox instrument, sound;
        juce::Slider volume;
    };

    Callbacks callbacks;
    const Pages pages;
    SynthLookAndFeel laf;   // combos + sliders; declared before the children
    GlassButtonLookAndFeel glassLnf { homePagePalette, MidiMorfColors::accent, 8.0f };

    juce::OwnedArray<Row> rows;
    juce::OwnedArray<juce::TextButton> linkButtons;
    juce::Rectangle<int> columnHeads, instrumentHead, soundHead, volumeHead;
};
