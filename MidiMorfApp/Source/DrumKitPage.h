#pragma once

#include <JuceHeader.h>
#include "Glassmorphism.h"
#include "SynthLookAndFeel.h"
#include "DrumSynth.h"
#include "TrackRail.h"

//==============================================================================
// The sound page for a track's drum synth: the kit (a factory kit to start
// from, then Drive and Dust for all of it), eight pads to hear and pick a
// drum, and the picked drum's Tune, Decay, Tone and Level.
class DrumKitPage : public juce::Component
{
public:
    DrumKitPage()
    {
        setLookAndFeel (&laf);

        kitCombo.setTextWhenNothingSelected ("Kit");
        int id = 1;
        for (auto& k : Drums::factoryKits())
            kitCombo.addItem (k.name, id++);
        kitCombo.onChange = [this]
        {
            const int i = kitCombo.getSelectedId() - 1;
            if (drums == nullptr || ! juce::isPositiveAndBelow (i, (int) Drums::factoryKits().size())) return;
            drums->loadKit (Drums::factoryKits()[(size_t) i]);
            refresh();
            if (onKitLoaded) onKitLoaded();
            changed();
        };
        addAndMakeVisible (kitCombo);

        setUpKnob (drive, "DRIVE", 0.0, 1.0, [this] (double v) { if (drums) drums->setDrive ((float) v); });
        setUpKnob (dust,  "DUST",  0.0, 1.0, [this] (double v) { if (drums) drums->setDust ((float) v); });

        for (int d = 0; d < Drums::NumDrums; ++d)
            addAndMakeVisible (pads.add (new Pad (*this, d)));

        setUpKnob (knobs[0], "TUNE",  -12.0, 12.0, [this] (double v) { setParam (DrumSource::Tune, v); });
        setUpKnob (knobs[1], "DECAY",  0.25, 4.0,  [this] (double v) { setParam (DrumSource::Decay, v); });
        setUpKnob (knobs[2], "TONE",   0.0,  1.0,  [this] (double v) { setParam (DrumSource::Tone, v); });
        setUpKnob (knobs[3], "LEVEL",  0.0,  1.5,  [this] (double v) { setParam (DrumSource::Level, v); });
        knobs[0].slider.setDoubleClickReturnValue (true, 0.0);
        knobs[0].slider.textFromValueFunction = [] (double v) { return (v > 0.05 ? "+" : "") + juce::String (v, 1) + " st"; };
        knobs[1].slider.setSkewFactorFromMidPoint (1.0);
        knobs[1].slider.setDoubleClickReturnValue (true, 1.0);
        knobs[1].slider.textFromValueFunction = [] (double v) { return "x" + juce::String (v, 2); };
        for (auto* k : { &knobs[2], &knobs[3], &drive, &dust })
            k->slider.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + "%"; };
        for (auto* k : { &knobs[0], &knobs[1], &knobs[2], &knobs[3], &drive, &dust })
            k->slider.updateText();
    }

    ~DrumKitPage() override { setLookAndFeel (nullptr); }

    std::function<void()> onChanged;                     // persist
    std::function<void()> onKitLoaded;                   // the sound's name changed
    std::function<void (int note, bool down)> onAudition;

    void edit (DrumSource& d, int trackIndex)
    {
        drums = &d;
        track = trackIndex;
        refresh();
    }

    int getTrack() const noexcept { return track; }

    void refresh()
    {
        if (drums == nullptr) return;
        const auto& kit = drums->getKit();
        int kitId = 0;
        for (size_t i = 0; i < Drums::factoryKits().size(); ++i)
            if (Drums::factoryKits()[i].name == kit.name)
                kitId = (int) i + 1;
        kitCombo.setSelectedId (kitId, juce::dontSendNotification);
        drive.slider.setValue (kit.drive, juce::dontSendNotification);
        dust.slider.setValue (kit.dust, juce::dontSendNotification);

        const auto& p = kit.drums[(size_t) selected];
        knobs[0].slider.setValue (p.tune,  juce::dontSendNotification);
        knobs[1].slider.setValue (p.decay, juce::dontSendNotification);
        knobs[2].slider.setValue (p.tone,  juce::dontSendNotification);
        knobs[3].slider.setValue (p.level, juce::dontSendNotification);

        for (auto* pad : pads) pad->repaint();
        repaint();
    }

    void visibilityChanged() override { if (isVisible()) refresh(); }

    void paint (juce::Graphics& g) override
    {
        Glassmorphism::paintPanel (g, getLocalBounds().toFloat(), drumKitPagePalette);

        g.setColour (juce::Colours::black.withAlpha (0.25f));
        g.fillRoundedRectangle (editArea.toFloat(), 8.0f);

        g.setColour (TrackColours::get (track));
        g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
        g.drawText (Drums::names()[selected], editTitle, juce::Justification::centredLeft);

        g.setColour (MidiMorfColors::textLight.withAlpha (0.55f));
        g.setFont (juce::FontOptions (11.5f));
        g.drawFittedText ("TONE: " + toneMeaning (selected), editHint, juce::Justification::centredLeft, 2);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (10, 8);

        auto top = area.removeFromTop (juce::jmin (78, area.getHeight() / 4));
        kitCombo.setBounds (top.removeFromLeft (juce::jmin (200, top.getWidth() / 3)).withSizeKeepingCentre (juce::jmin (200, top.getWidth() / 3), 34));
        top.removeFromLeft (12);
        drive.setBounds (top.removeFromLeft (76));
        dust.setBounds (top.removeFromLeft (76));
        area.removeFromTop (8);

        editArea = area.removeFromRight (juce::jmin (330, area.getWidth() * 45 / 100));
        area.removeFromRight (10);

        // Pads: 4 x 2.
        const int gap = 8, cols = 4, rows = 2;
        const int pw = (area.getWidth() - (cols - 1) * gap) / cols, ph = (area.getHeight() - (rows - 1) * gap) / rows;
        for (int d = 0; d < pads.size(); ++d)
            pads[d]->setBounds (area.getX() + (d % cols) * (pw + gap), area.getY() + (d / cols) * (ph + gap), pw, ph);

        auto e = editArea.reduced (10, 8);
        editTitle = e.removeFromTop (22);
        editHint = e.removeFromTop (30);
        e.removeFromTop (4);
        const int kw = e.getWidth() / 4;
        for (int i = 0; i < 4; ++i)
            knobs[i].setBounds (e.removeFromLeft (kw).withSizeKeepingCentre (kw, juce::jmin (e.getHeight(), 110)));
    }

private:
    static inline const PagePalette drumKitPagePalette {
        juce::Colour (0xff281A14), juce::Colour (0xff0D0806),
        juce::Colour (0xCC3A261C), juce::Colour (0xD81E130D), juce::Colour (0xCC000000)
    };

    static juce::String toneMeaning (int d)
    {
        switch (d)
        {
            case Drums::Kick:      return "click and pitch sweep";
            case Drums::Snare:     return "snappy noise against the body";
            case Drums::Clap:      return "where the band sits";
            case Drums::ClosedHat:
            case Drums::OpenHat:   return "brightness";
            case Drums::Tom:       return "stick on the attack";
            case Drums::Rim:       return "click against wood";
            case Drums::Perc:      return "brightness of the bell";
            default:               return {};
        }
    }

    //==========================================================================
    // A knob with its name over it.
    struct Knob : public juce::Component
    {
        Knob()
        {
            slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 18);
            addAndMakeVisible (slider);
            label.setJustificationType (juce::Justification::centred);
            label.setFont (juce::FontOptions (11.0f, juce::Font::bold));
            label.setColour (juce::Label::textColourId, MidiMorfColors::textLight.withAlpha (0.7f));
            addAndMakeVisible (label);
        }
        void resized() override
        {
            auto r = getLocalBounds();
            label.setBounds (r.removeFromTop (16));
            slider.setBounds (r);
        }
        juce::Slider slider;
        juce::Label label;
    };

    void setUpKnob (Knob& k, const juce::String& name, double lo, double hi, std::function<void (double)> apply)
    {
        k.label.setText (name, juce::dontSendNotification);
        k.slider.setRange (lo, hi, 0.0);
        k.slider.onValueChange = [&k, apply] { apply (k.slider.getValue()); };
        k.slider.onDragEnd = [this] { changed(); };
        addAndMakeVisible (k);
    }

    void setParam (DrumSource::Param p, double v)
    {
        if (drums != nullptr)
            drums->setParam (selected, p, (float) v);
    }

    void changed() { if (onChanged) onChanged(); }

    //==========================================================================
    struct Pad : public juce::Component
    {
        Pad (DrumKitPage& o, int d) : owner (o), drum (d) {}

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat().reduced (1.0f);
            const auto colour = TrackColours::get (owner.track);
            const bool sel = owner.selected == drum;
            g.setColour (down ? colour.withAlpha (0.7f) : juce::Colours::black.withAlpha (0.4f));
            g.fillRoundedRectangle (r, 8.0f);
            g.setColour (sel ? colour : MidiMorfColors::textLight.withAlpha (0.25f));
            g.drawRoundedRectangle (r, 8.0f, sel ? 2.5f : 1.0f);
            g.setColour (sel ? colour.brighter (0.3f) : MidiMorfColors::textLight.withAlpha (0.85f));
            g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
            g.drawFittedText (Drums::shortNames()[drum], getLocalBounds().reduced (4), juce::Justification::centred, 1);
        }

        void mouseDown (const juce::MouseEvent&) override
        {
            down = true;
            if (owner.selected != drum)
            {
                owner.selected = drum;
                owner.refresh();
            }
            if (owner.onAudition) owner.onAudition (Drums::noteFor (drum), true);
            repaint();
        }

        void mouseUp (const juce::MouseEvent&) override
        {
            down = false;
            if (owner.onAudition) owner.onAudition (Drums::noteFor (drum), false);
            repaint();
        }

        DrumKitPage& owner;
        const int drum;
        bool down = false;
    };

    DrumSource* drums = nullptr;
    int track = 3, selected = 0;

    SynthLookAndFeel laf;   // declared before the children
    juce::ComboBox kitCombo;
    Knob drive, dust;
    std::array<Knob, 4> knobs;
    juce::OwnedArray<Pad> pads;
    juce::Rectangle<int> editArea, editTitle, editHint;
};
