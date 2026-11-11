#pragma once

#include <JuceHeader.h>
#include "Glassmorphism.h"
#include "SynthLookAndFeel.h"
#include "Halo.h"
#include "TrackRail.h"

//==============================================================================
// The sound page for a track's Halo: what it listens to, LATCH, the strings
// themselves (one per held note, shaking as hard as they ring), and the knobs.
class HaloPage : public juce::Component,
                 private juce::Timer
{
public:
    HaloPage()
    {
        setLookAndFeel (&laf);

        static const char* listenNames[] = { "ALL", "T1", "T2", "T3", "T4" };
        for (int i = 0; i < 5; ++i)
        {
            auto* b = listenButtons.add (new juce::TextButton (listenNames[i]));
            b->setClickingTogglesState (true);
            b->setRadioGroupId (7070);
            b->onClick = [this, i] { set (HaloSource::Listen, (float) i); changed(); };
            addAndMakeVisible (b);
        }
        listenLabel.setText ("LISTENS TO", juce::dontSendNotification);
        listenLabel.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        listenLabel.setColour (juce::Label::textColourId, MidiMorfColors::textLight.withAlpha (0.7f));
        addAndMakeVisible (listenLabel);

        latchButton.setClickingTogglesState (true);
        latchButton.onClick = [this] { set (HaloSource::Latch, latchButton.getToggleState() ? 1.0f : 0.0f); changed(); };
        addAndMakeVisible (latchButton);

        auto percent = [] (double v) { return juce::String (juce::roundToInt (v * 100)) + "%"; };
        auto seconds = [] (double v) { return v < 1.0 ? juce::String (juce::roundToInt (v * 1000)) + " ms" : juce::String (v, 1) + " s"; };
        setUpKnob (HaloSource::Drive,  "DRIVE",  percent);
        setUpKnob (HaloSource::Pluck,  "PLUCK",  percent);
        setUpKnob (HaloSource::Bow,    "BOW",    percent);
        setUpKnob (HaloSource::Decay,  "DECAY",  seconds, 2.0);
        setUpKnob (HaloSource::Damp,   "DAMP",   seconds, 0.4);
        setUpKnob (HaloSource::Bright, "BRIGHT", percent);
        setUpKnob (HaloSource::Metal,  "METAL",  [] (double v) { return v < 0.02 ? juce::String ("string") : v < 0.5 ? juce::String ("bell ") + juce::String (juce::roundToInt (v * 100)) : juce::String ("gong ") + juce::String (juce::roundToInt (v * 100)); });
        setUpKnob (HaloSource::Width,  "WIDTH",  percent);
        setUpKnob (HaloSource::Level,  "LEVEL",  [] (double v) { return (v > 0 ? "+" : "") + juce::String (v, 1) + " dB"; });
    }

    ~HaloPage() override
    {
        stopTimer();
        setLookAndFeel (nullptr);
    }

    std::function<void()> onChanged;   // persist

    void edit (HaloSource& h, int trackIndex)
    {
        halo = &h;
        track = trackIndex;
        refresh();
    }

    int getTrack() const noexcept { return track; }

    void refresh()
    {
        if (halo == nullptr) return;
        const int listen = juce::roundToInt (halo->getParam (HaloSource::Listen));
        for (int i = 0; i < listenButtons.size(); ++i)
        {
            listenButtons[i]->setToggleState (i == listen, juce::dontSendNotification);
            listenButtons[i]->setEnabled (i != track + 1);   // it can't hear itself
        }
        latchButton.setToggleState (halo->getParam (HaloSource::Latch) >= 0.5f, juce::dontSendNotification);
        for (auto& [k, p] : knobs)
            k->slider.setValue (halo->getParam (p), juce::dontSendNotification);
        repaint();
    }

    void visibilityChanged() override
    {
        if (isVisible()) { refresh(); startTimerHz (30); }
        else             stopTimer();
    }

    void paint (juce::Graphics& g) override
    {
        Glassmorphism::paintPanel (g, getLocalBounds().toFloat(), haloPalette);

        g.setColour (juce::Colours::black.withAlpha (0.3f));
        g.fillRoundedRectangle (stringsArea.toFloat(), 8.0f);
        g.setColour (juce::Colours::black.withAlpha (0.18f));
        g.fillRoundedRectangle (knobsArea.toFloat(), 8.0f);

        const auto colour = TrackColours::get (track);
        auto title = titleArea;
        g.setColour (colour);
        g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
        g.drawText ("HALO", title.removeFromTop (24), juce::Justification::centredLeft);
        g.setColour (MidiMorfColors::textLight.withAlpha (0.6f));
        g.setFont (juce::FontOptions (11.5f));
        g.drawFittedText ("Hold notes: the other tracks ring them.", title, juce::Justification::topLeft, 2);

        paintStrings (g, stringsArea.toFloat().reduced (10.0f, 8.0f), colour);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (10, 8);

        auto top = area.removeFromTop (44);
        titleArea = top.removeFromLeft (juce::jmin (260, top.getWidth() / 3));
        latchButton.setBounds (top.removeFromRight (80).reduced (2, 5));
        top.removeFromRight (10);
        auto listen = top.removeFromRight (juce::jmin (320, top.getWidth()));
        listenLabel.setBounds (listen.removeFromLeft (80));
        const int bw = listen.getWidth() / 5;
        for (auto* b : listenButtons)
            b->setBounds (listen.removeFromLeft (bw).reduced (2, 5));
        area.removeFromTop (6);

        knobsArea = area.removeFromBottom (juce::jlimit (96, 128, area.getHeight() * 40 / 100));
        area.removeFromBottom (6);
        stringsArea = area;

        auto row = knobsArea.reduced (8, 6);
        const int kw = row.getWidth() / juce::jmax (1, (int) knobs.size());
        for (auto& [k, p] : knobs)
            k->setBounds (row.removeFromLeft (kw));
    }

private:
    static inline const PagePalette haloPalette {
        juce::Colour (0xff15182A), juce::Colour (0xff07080F),
        juce::Colour (0xCC1C2238), juce::Colour (0xD80E1120), juce::Colour (0xCC000000)
    };

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

    void setUpKnob (HaloSource::Param p, const juce::String& name, std::function<juce::String (double)> text, double skewMid = 0.0)
    {
        auto* k = ownedKnobs.add (new Knob());
        const auto& info = HaloSource::paramInfo (p);
        k->label.setText (name, juce::dontSendNotification);
        k->slider.setRange (info.min, info.max, 0.0);
        if (skewMid > 0.0) k->slider.setSkewFactorFromMidPoint (skewMid);
        k->slider.setDoubleClickReturnValue (true, info.def);
        k->slider.textFromValueFunction = std::move (text);
        k->slider.updateText();
        k->slider.onValueChange = [this, k, p] { set (p, (float) k->slider.getValue()); };
        k->slider.onDragEnd = [this] { changed(); };
        addAndMakeVisible (k);
        knobs.push_back ({ k, p });
    }

    void set (HaloSource::Param p, float v) { if (halo != nullptr) halo->setParam (p, v); }
    void changed() { if (onChanged) onChanged(); }

    // One string per sounding note, low notes at the bottom, each a standing
    // wave whose height follows how hard it rings.
    void paintStrings (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour colour)
    {
        if (halo == nullptr) return;

        struct S { int note; float energy; };
        std::vector<S> strings;
        for (int v = 0; v < HaloSource::NumVoices; ++v)
            if (const int n = halo->voiceNote (v); n >= 0)
                strings.push_back ({ n, halo->voiceEnergy (v) });
        std::sort (strings.begin(), strings.end(), [] (const S& a, const S& b) { return a.note > b.note; });

        if (strings.empty())
        {
            g.setColour (MidiMorfColors::textLight.withAlpha (0.4f));
            g.setFont (juce::FontOptions (14.0f));
            g.drawFittedText ("Play notes on this track. Each one becomes a string\nthat the drums, the breaks and the synths set ringing.",
                              r.toNearestInt(), juce::Justification::centred, 3);
            return;
        }

        const float rowH = r.getHeight() / (float) strings.size();
        const float phaseNow = (float) (juce::Time::getMillisecondCounterHiRes() * 0.001);
        for (size_t i = 0; i < strings.size(); ++i)
        {
            const auto& s = strings[i];
            const float y = r.getY() + rowH * ((float) i + 0.5f);
            const float amp = juce::jmin (rowH * 0.45f, 120.0f * std::sqrt (s.energy));

            g.setColour (MidiMorfColors::textLight.withAlpha (0.7f));
            g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
            g.drawText (juce::MidiMessage::getMidiNoteName (s.note, true, true, 4), juce::Rectangle<float> (r.getX(), y - 9.0f, 40.0f, 18.0f), juce::Justification::centredLeft);

            const float x0 = r.getX() + 44.0f, x1 = r.getRight();
            // The string wobbles at a rate tied to its pitch, slowed way down.
            const float wobble = std::sin (phaseNow * (2.0f + (float) (s.note % 12) * 0.7f));
            juce::Path p;
            p.startNewSubPath (x0, y);
            for (int k = 1; k <= 48; ++k)
            {
                const float u = (float) k / 48.0f;
                const float shape = std::sin (juce::MathConstants<float>::pi * u) + 0.35f * std::sin (2.0f * juce::MathConstants<float>::pi * u) * std::cos (phaseNow * 5.1f);
                p.lineTo (x0 + u * (x1 - x0), y + amp * wobble * shape);
            }
            g.setColour (colour.withAlpha (0.25f + juce::jmin (0.75f, amp / 20.0f)));
            g.strokePath (p, juce::PathStrokeType (1.5f + juce::jmin (2.5f, amp / 12.0f)));
        }
    }

    void timerCallback() override { repaint (stringsArea); }

    HaloSource* halo = nullptr;
    int track = 0;

    SynthLookAndFeel laf;   // declared before the children
    juce::OwnedArray<juce::TextButton> listenButtons;
    juce::Label listenLabel;
    juce::TextButton latchButton { "LATCH" };
    juce::OwnedArray<Knob> ownedKnobs;
    std::vector<std::pair<Knob*, HaloSource::Param>> knobs;
    juce::Rectangle<int> titleArea, stringsArea, knobsArea;
};
