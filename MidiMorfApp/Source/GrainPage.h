#pragma once

#include <JuceHeader.h>
#include "SamplePage.h"
#include "GrainSource.h"

//==============================================================================
// The sound page for a track's Grain (SamplePage has the shared layout). The
// waveform is played, not trimmed: each finger is a cloud, and its height
// bends the FINGER UP setting. HOLD keeps a lifted finger's cloud playing;
// FREEZE stops every cloud scanning.
//
//   • PLAY:  density, size, spray, scan, loop, pitch, jitter, the finger's
//            Y axis and gain
//   • SHAPE: the filter, spread, window, reverse, release and velocity
class GrainPage : public SamplePage
{
public:
    GrainPage() : SamplePage (grainPagePalette)
    {
        filterCombo.addItemList (SampleSource::filterNames(), 1);
        filterCombo.onChange = [this] { setParam (GrainSource::FilterType, (float) (filterCombo.getSelectedId() - 1)); refreshEnables(); changed(); };
        styleCaption (filterLabel, "FILTER");

        auto percent = [] (double v) { return juce::String (juce::roundToInt (v * 100)) + "%"; };
        auto ms = [] (double v) { return v >= 1000 ? juce::String (v / 1000.0, 2) + " s" : juce::String ((int) v) + " ms"; };
        setUpKnob (transposeKnob, GrainSource::Transpose, 1.0, [] (double v) { return (v > 0 ? "+" : "") + juce::String ((int) v) + " st"; });
        setUpKnob (gainKnob, GrainSource::Gain, 0.1, [] (double v) { return (v > 0 ? "+" : "") + juce::String (v, 1) + " dB"; });
        setUpKnob (velKnob, GrainSource::VelSense, 0.01, percent);
        setUpKnob (cutoffKnob, GrainSource::Cutoff, 1.0, [] (double v) { return v >= 1000 ? juce::String (v / 1000.0, 1) + " k" : juce::String ((int) v) + " Hz"; }, 1000.0);
        setUpKnob (resKnob, GrainSource::Resonance, 0.01, percent);
        setUpKnob (densityKnob, GrainSource::Density, 0.1, [] (double v) { return juce::String (v, v < 10 ? 1 : 0) + " /s"; }, 20.0);
        setUpKnob (sizeKnob, GrainSource::Size, 1.0, [] (double v) { return juce::String ((int) v) + " ms"; }, 80.0);
        setUpKnob (sprayKnob, GrainSource::Spray, 0.01, percent);
        setUpKnob (scanKnob, GrainSource::Scan, 0.01, [] (double v) { return std::abs (v) < 0.005 ? juce::String ("still") : juce::String (v, 2) + "x"; });
        setUpKnob (loopLenKnob, GrainSource::Loop, 1.0, ms, 300.0);
        setUpKnob (jitterKnob, GrainSource::Jitter, 0.1, [] (double v) { return juce::String (v, 1) + " st"; });
        setUpKnob (spreadKnob, GrainSource::Spread, 0.01, percent);
        setUpKnob (windowKnob, GrainSource::Window, 0.01, [] (double v) { return v < 0.02 ? juce::String ("smooth") : v > 0.98 ? juce::String ("hard") : juce::String (juce::roundToInt (v * 100)) + "%"; });
        setUpKnob (reverseKnob, GrainSource::Reverse, 0.01, percent);
        setUpKnob (releaseKnob, GrainSource::Release, 1.0, ms, 400.0);

        yCombo.addItemList (GrainSource::yAxisNames(), 1);
        yCombo.onChange = [this] { setParam (GrainSource::YAxis, (float) (yCombo.getSelectedId() - 1)); changed(); };
        styleCaption (yLabel, "FINGER UP");

        for (auto* b : { &holdButton, &freezeButton })
        {
            b->setClickingTogglesState (true);
            addAndMakeVisible (b);
        }
        holdButton.onClick = [this]
        {
            setParam (GrainSource::Hold, holdButton.getToggleState() ? 1.0f : 0.0f);
            editor.setHold (holdButton.getToggleState());
            changed();
        };
        freezeButton.onClick = [this] { setParam (GrainSource::Freeze, freezeButton.getToggleState() ? 1.0f : 0.0f); changed(); };

        // A finger on the waveform plays its cloud; notes play where the
        // last one went down.
        editor.onCloud = [this] (int cloud, bool down, float x, float y)
        {
            if (grain == nullptr) return;
            grain->grains().touch (cloud, down, x, y);
            if (down)
                grain->setParam (GrainSource::Position, x);
            else
                changed();
        };

        for (auto* c : allControls())
            addChildComponent (c);
    }

    void edit (GrainSource& g, int trackIndex)
    {
        grain = &g;
        editSample (g, trackIndex);
    }

private:
    static inline const PagePalette grainPagePalette {
        juce::Colour (0xff1C1A2E), juce::Colour (0xff0B0A14),
        juce::Colour (0xCC2A2640), juce::Colour (0xD8171527), juce::Colour (0xCC000000)
    };

    //==========================================================================
    // SamplePage
    void layoutEditRow (juce::Rectangle<int>& row) override
    {
        auto buttons = row.removeFromRight (150);
        holdButton.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2).reduced (2));
        freezeButton.setBounds (buttons.reduced (2));
    }

    void refreshControls() override
    {
        for (auto& [knob, param] : paramKnobs)
            knob->getSlider().setValue (grain->getParam (param), juce::dontSendNotification);
        filterCombo.setSelectedId (juce::roundToInt (grain->getParam (GrainSource::FilterType)) + 1, juce::dontSendNotification);
        yCombo.setSelectedId (juce::roundToInt (grain->getParam (GrainSource::YAxis)) + 1, juce::dontSendNotification);
        holdButton.setToggleState (grain->getParam (GrainSource::Hold) >= 0.5f, juce::dontSendNotification);
        freezeButton.setToggleState (grain->getParam (GrainSource::Freeze) >= 0.5f, juce::dontSendNotification);
        editor.setPlayMode (&grain->grains());
        editor.setHold (holdButton.getToggleState());
        refreshEnables();
    }

    juce::String detailText (const Break& b) const override
    {
        return juce::String (b.lengthSeconds(), 2) + " s. Touch the wave to play";
    }

    bool wantsTimer() const override { return true; }   // the clouds

    std::vector<juce::Component*> allControls()
    {
        std::vector<juce::Component*> c { &filterCombo, &filterLabel, &yCombo, &yLabel };
        for (auto& [knob, param] : paramKnobs) c.push_back (knob);
        return c;
    }

    void layoutControls() override
    {
        for (auto* c : allControls())
            c->setVisible (false);
        for (auto* b : { &holdButton, &freezeButton })
            b->setVisible (! isMicOpen());
        if (grain == nullptr || controlsArea.isEmpty())
            return;

        std::vector<Item> items;
        if (showingPlayTab())
        {
            for (auto* k : { &densityKnob, &sizeKnob, &sprayKnob, &scanKnob, &loopLenKnob, &transposeKnob, &jitterKnob })
                items.push_back ({ k });
            items.push_back ({ &yCombo, &yLabel, 1.3f });
            items.push_back ({ &gainKnob });
        }
        else
        {
            items.push_back ({ &filterCombo, &filterLabel, 1.5f });
            for (auto* k : { &cutoffKnob, &resKnob, &spreadKnob, &windowKnob, &reverseKnob, &releaseKnob, &velKnob })
                items.push_back ({ k });
        }
        layoutRow (items);
        refreshEnables();
    }

    // Resonance and cutoff only matter with a filter.
    void refreshEnables()
    {
        if (grain == nullptr) return;
        const bool filtering = juce::roundToInt (grain->getParam (GrainSource::FilterType)) != BreakPlayer::FilterOff;
        for (auto* k : { &cutoffKnob, &resKnob })
        {
            k->setEnabled (filtering);
            k->setAlpha (filtering ? 1.0f : 0.4f);
        }
    }

    void setUpKnob (Knob& k, GrainSource::Param p, double step, std::function<juce::String (double)> text, double skewMid = 0.0)
    {
        const auto& info = GrainSource::paramInfo (p);
        auto& s = k.getSlider();
        s.setRange (info.min, info.max, step);
        if (skewMid > 0.0) s.setSkewFactorFromMidPoint (skewMid);
        s.setDoubleClickReturnValue (true, info.def);
        s.textFromValueFunction = text;
        s.updateText();
        s.onValueChange = [this, &s, p] { setParam (p, (float) s.getValue()); };
        s.onDragEnd = [this] { changed(); };
        paramKnobs.push_back ({ &k, p });
    }

    void setParam (GrainSource::Param p, float v) { if (grain != nullptr) grain->setParam (p, v); }

    GrainSource* grain = nullptr;

    juce::ComboBox filterCombo, yCombo;
    juce::Label filterLabel, yLabel;
    Knob transposeKnob { "PITCH", -24, 24, 0 }, gainKnob { "GAIN", -24, 12, 0 }, velKnob { "VEL", 0, 1, 1 },
         cutoffKnob { "CUTOFF", 20, 20000, 20000 }, resKnob { "RES", 0, 1, 0.2f },
         densityKnob { "DENSITY", 1, 100, 30 }, sizeKnob { "SIZE", 5, 500, 80 }, sprayKnob { "SPRAY", 0, 1, 0.1f },
         scanKnob { "SCAN", -2, 2, 1 }, loopLenKnob { "LOOP", 10, 2000, 300 }, jitterKnob { "JITTER", 0, 12, 0 },
         spreadKnob { "SPREAD", 0, 1, 0.5f }, windowKnob { "WINDOW", 0, 1, 0 }, reverseKnob { "REVERSE", 0, 1, 0 },
         releaseKnob { "RELEASE", 5, 4000, 300 };
    std::vector<std::pair<Knob*, GrainSource::Param>> paramKnobs;
    juce::TextButton holdButton { "HOLD" }, freezeButton { "FREEZE" };
};
