#pragma once

#include <JuceHeader.h>
#include "SamplePage.h"
#include "Sampler.h"

//==============================================================================
// The sound page for a track's sampler (SamplePage has the shared layout):
//
//   • top:    how it plays (Slice / Keys / One-shot)
//   • middle: in Slice mode, BARS says how long the trimmed part is, so a
//             loop that isn't cut right can be fixed here
//   • bottom: two pages of knobs. PLAY: warp (Slice), pitch, speed, grain,
//             gain, velocity, loop. SHAPE: the filter, fades and (Keys) the
//             envelope.
class SamplerPage : public SamplePage
{
public:
    SamplerPage() : SamplePage (samplerPagePalette)
    {
        for (int m = 0; m < 3; ++m)
        {
            auto* b = modeButtons.add (new juce::TextButton (SamplerSource::modeNames()[m]));
            b->setClickingTogglesState (true);
            b->setRadioGroupId (5150);
            b->onClick = [this, m] { if (sampler != nullptr) { sampler->setMode ((SamplerSource::Mode) m); refresh(); changed(); } };
            addAndMakeVisible (b);
        }

        barsDown.onClick = [this] { stepBeats (-1); };
        barsUp.onClick   = [this] { stepBeats (1); };
        barsLabel.setJustificationType (juce::Justification::centred);
        barsLabel.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        for (auto* c : std::initializer_list<juce::Component*> { &barsDown, &barsUp, &barsLabel })
            addChildComponent (c);

        // PLAY
        warpCombo.addItemList (SamplerSource::warpNames(), 1);
        warpCombo.onChange = [this] { setParam (SamplerSource::Warp, (float) (warpCombo.getSelectedId() - 1)); refreshEnables(); changed(); };
        filterCombo.addItemList (SampleSource::filterNames(), 1);
        filterCombo.onChange = [this] { setParam (SamplerSource::FilterType, (float) (filterCombo.getSelectedId() - 1)); refreshEnables(); changed(); };
        styleCaption (warpLabel, "WARP");
        styleCaption (filterLabel, "FILTER");

        setUpKnob (transposeKnob, SamplerSource::Transpose, 1.0, [] (double v) { return (v > 0 ? "+" : "") + juce::String ((int) v) + " st"; });
        setUpKnob (detuneKnob, SamplerSource::Detune, 1.0, [] (double v) { return (v > 0 ? "+" : "") + juce::String ((int) v) + " ct"; });
        setUpKnob (speedKnob, SamplerSource::Speed, 0.01, [] (double v) { return juce::String (v, 2) + "x"; }, 1.0);
        setUpKnob (grainKnob, SamplerSource::GrainMs, 1.0, [] (double v) { return juce::String ((int) v) + " ms"; }, 40.0);
        setUpKnob (gainKnob, SamplerSource::Gain, 0.1, [] (double v) { return (v > 0 ? "+" : "") + juce::String (v, 1) + " dB"; });
        setUpKnob (velKnob, SamplerSource::VelSense, 0.01, [] (double v) { return juce::String (juce::roundToInt (v * 100)) + "%"; });
        setUpKnob (cutoffKnob, SamplerSource::Cutoff, 1.0, [] (double v) { return v >= 1000 ? juce::String (v / 1000.0, 1) + " k" : juce::String ((int) v) + " Hz"; }, 1000.0);
        setUpKnob (resKnob, SamplerSource::Resonance, 0.01, [] (double v) { return juce::String (juce::roundToInt (v * 100)) + "%"; });

        loopButton.setClickingTogglesState (true);
        loopButton.onClick = [this] { setParam (SamplerSource::Loop, loopButton.getToggleState() ? 1.0f : 0.0f); changed(); };

        rootDown.onClick = [this] { nudgeRoot (-1); };
        rootUp.onClick   = [this] { nudgeRoot (1); };
        rootLabel.setJustificationType (juce::Justification::centred);
        rootLabel.setFont (juce::FontOptions (22.0f, juce::Font::bold));
        rootLabel.setColour (juce::Label::textColourId, MidiMorfColors::textLight);
        rootLabel.setColour (juce::Label::backgroundColourId, juce::Colours::black.withAlpha (0.35f));
        rootLabel.setColour (juce::Label::outlineColourId, MidiMorfColors::textLight.withAlpha (0.2f));
        rootCaption.setJustificationType (juce::Justification::centred);
        rootCaption.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        rootCaption.setColour (juce::Label::textColourId, MidiMorfColors::textLight);

        const char* names4[] = { "A", "D", "S", "R" };
        for (int i = 0; i < 4; ++i)
        {
            auto* s = envSliders.add (new juce::Slider (juce::Slider::LinearVertical, juce::Slider::TextBoxBelow));
            s->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 44, 16);
            s->setRange (i == 2 ? 0.0 : 0.001, i == 2 ? 1.0 : (i == 3 ? 8.0 : 5.0), 0.001);
            if (i != 2) s->setSkewFactorFromMidPoint (0.4);
            s->onValueChange = [this] { applyEnvelope(); };
            s->onDragEnd = [this] { changed(); };

            auto* l = envLabels.add (new juce::Label ({}, names4[i]));
            l->setJustificationType (juce::Justification::centred);
            l->setFont (juce::FontOptions (11.0f, juce::Font::bold));
        }

        for (auto* c : allControls())
            addChildComponent (c);
    }

    void edit (SamplerSource& s, int trackIndex)
    {
        sampler = &s;
        editSample (s, trackIndex);
    }

private:
    static inline const PagePalette samplerPagePalette {
        juce::Colour (0xff2A2014), juce::Colour (0xff120D07),
        juce::Colour (0xCC3A2C1A), juce::Colour (0xD8241A0E), juce::Colour (0xCC000000)
    };

    // Lengths the BARS stepper offers, in beats.
    static const std::vector<double>& beatSteps()
    {
        static const std::vector<double> s { 1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64 };
        return s;
    }

    static juce::String beatsText (double beats)
    {
        const int b = juce::roundToInt (beats);
        if (b % 4 == 0) return juce::String (b / 4) + (b == 4 ? " bar" : " bars");
        return juce::String (b) + (b == 1 ? " beat" : " beats");
    }

    //==========================================================================
    // SamplePage
    void layoutTopLeft (juce::Rectangle<int>& top) override
    {
        for (auto* b : modeButtons)
            b->setBounds (top.removeFromLeft (70).reduced (2));
        top.removeFromLeft (6);
    }

    void layoutEditRow (juce::Rectangle<int>& row) override
    {
        auto bars = row.removeFromRight (150);
        barsDown.setBounds (bars.removeFromLeft (34).reduced (2));
        barsUp.setBounds (bars.removeFromRight (34).reduced (2));
        barsLabel.setBounds (bars);
    }

    void refreshControls() override
    {
        const auto mode = sampler->getMode();
        for (int m = 0; m < modeButtons.size(); ++m)
            modeButtons[m]->setToggleState (m == (int) mode, juce::dontSendNotification);

        for (auto& [knob, param] : paramKnobs)
            knob->getSlider().setValue (sampler->getParam (param), juce::dontSendNotification);
        warpCombo.setSelectedId (juce::roundToInt (sampler->getParam (SamplerSource::Warp)) + 1, juce::dontSendNotification);
        filterCombo.setSelectedId (juce::roundToInt (sampler->getParam (SamplerSource::FilterType)) + 1, juce::dontSendNotification);
        loopButton.setToggleState (sampler->getParam (SamplerSource::Loop) >= 0.5f, juce::dontSendNotification);

        rootLabel.setText (juce::MidiMessage::getMidiNoteName (sampler->getRootNote(), true, true, 4), juce::dontSendNotification);
        const auto env = sampler->getEnvelope();
        const float v[4] = { env.attack, env.decay, env.sustain, env.release };
        for (int i = 0; i < 4; ++i)
            envSliders[i]->setValue (v[i], juce::dontSendNotification);

        const auto* b = sampler->getBreak();
        barsLabel.setText (b != nullptr ? beatsText (b->beats) : juce::String(), juce::dontSendNotification);
        refreshEnables();
    }

    juce::String detailText (const Break& b) const override
    {
        if (sampler != nullptr && sampler->getMode() == SamplerSource::Slice)
            return juce::String (b.bpm(), 1) + " BPM, " + juce::String (b.lengthSeconds(), 2) + " s";
        return SamplePage::detailText (b);
    }

    double gridBeats() const override
    {
        const auto* b = sampler != nullptr ? sampler->getBreak() : nullptr;
        return sampler != nullptr && sampler->getMode() == SamplerSource::Slice && b != nullptr ? b->beats : 0.0;
    }

    // A rendered note or a recording is for playing, not slicing.
    void loadedToPlay (SampleSource& s) override
    {
        auto& target = static_cast<SamplerSource&> (s);   // this page only edits samplers
        if (target.getMode() == SamplerSource::Slice)
            target.setMode (SamplerSource::Keys);
    }

    std::vector<juce::Component*> allControls()
    {
        std::vector<juce::Component*> c { &warpCombo, &warpLabel, &filterCombo, &filterLabel, &loopButton,
                                          &rootDown, &rootUp, &rootLabel, &rootCaption, &fadeInKnob, &fadeOutKnob };
        for (auto& [knob, param] : paramKnobs) c.push_back (knob);
        for (auto* s : envSliders) c.push_back (s);
        for (auto* l : envLabels) c.push_back (l);
        return c;
    }

    // Shows this tab's controls for this mode and lays them out in a row.
    void layoutControls() override
    {
        for (auto* c : allControls())
            c->setVisible (false);
        if (sampler == nullptr || controlsArea.isEmpty())
            return;

        const auto mode = sampler->getMode();
        const bool slice = mode == SamplerSource::Slice, keys = mode == SamplerSource::Keys;
        const bool slicing = slice && sampler->getBreak() != nullptr;
        for (auto* c : std::initializer_list<juce::Component*> { &barsDown, &barsUp, &barsLabel })
            c->setVisible (slicing && ! isMicOpen());

        std::vector<Item> items;
        if (showingPlayTab())
        {
            if (slice) items.push_back ({ &warpCombo, &warpLabel, 1.5f });
            if (keys)  items.push_back ({ &rootLabel, nullptr, 1.1f, [this] (juce::Rectangle<int> cell) { placeRoot (cell); } });
            items.push_back ({ &transposeKnob });
            items.push_back ({ &detuneKnob });
            if (! slice) items.push_back ({ &speedKnob });
            items.push_back ({ &grainKnob });
            if (! slice) items.push_back ({ &loopButton, nullptr, 0.9f });
            items.push_back ({ &gainKnob });
            items.push_back ({ &velKnob });
        }
        else
        {
            items.push_back ({ &filterCombo, &filterLabel, 1.5f });
            items.push_back ({ &cutoffKnob });
            items.push_back ({ &resKnob });
            items.push_back ({ &fadeInKnob });
            items.push_back ({ &fadeOutKnob });
            if (keys)
                for (int i = 0; i < 4; ++i)
                    items.push_back ({ envSliders[i], envLabels[i], 0.6f });
        }
        layoutRow (items);
        refreshEnables();
    }

    // ROOT, laid out like a knob: the note in a display, − and + under it,
    // and the name along the bottom with the others.
    void placeRoot (juce::Rectangle<int> cell)
    {
        auto col = cell.withSizeKeepingCentre (juce::jmin (cell.getWidth(), 96), cell.getHeight());
        rootCaption.setBounds (col.removeFromBottom (18));
        col.removeFromBottom (4);
        auto buttons = col.removeFromBottom (30);
        rootDown.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2).reduced (2, 1));
        rootUp.setBounds (buttons.reduced (2, 1));
        col.removeFromBottom (6);
        rootLabel.setBounds (col.withSizeKeepingCentre (col.getWidth() - 4, juce::jmin (col.getHeight(), 40)));
        for (auto* c : std::initializer_list<juce::Component*> { &rootDown, &rootUp, &rootLabel, &rootCaption }) c->setVisible (true);
    }

    // Grain only matters when something is stretched; resonance only with a
    // filter.
    void refreshEnables()
    {
        if (sampler == nullptr) return;
        const bool slice = sampler->getMode() == SamplerSource::Slice;
        const bool grainy = slice ? juce::roundToInt (sampler->getParam (SamplerSource::Warp)) == BreakPlayer::Stretch
                                  : std::abs (sampler->getParam (SamplerSource::Speed) - 1.0f) > 0.001f;
        grainKnob.setEnabled (grainy);
        grainKnob.setAlpha (grainy ? 1.0f : 0.4f);
        const bool filtering = juce::roundToInt (sampler->getParam (SamplerSource::FilterType)) != BreakPlayer::FilterOff;
        for (auto* k : { &cutoffKnob, &resKnob })
        {
            k->setEnabled (filtering);
            k->setAlpha (filtering ? 1.0f : 0.4f);
        }
    }

    void setUpKnob (Knob& k, SamplerSource::Param p, double step, std::function<juce::String (double)> text, double skewMid = 0.0)
    {
        const auto& info = SamplerSource::paramInfo (p);
        auto& s = k.getSlider();
        s.setRange (info.min, info.max, step);
        if (skewMid > 0.0) s.setSkewFactorFromMidPoint (skewMid);
        s.setDoubleClickReturnValue (true, info.def);
        s.textFromValueFunction = text;
        s.updateText();
        s.onValueChange = [this, &s, p] { setParam (p, (float) s.getValue()); if (p == SamplerSource::Speed) refreshEnables(); };
        s.onDragEnd = [this] { changed(); };
        paramKnobs.push_back ({ &k, p });
    }

    void setParam (SamplerSource::Param p, float v) { if (sampler != nullptr) sampler->setParam (p, v); }

    // BARS − / +: the trimmed part's length, which sets its tempo.
    void stepBeats (int direction)
    {
        if (sampler == nullptr || sampler->getBreak() == nullptr) return;
        const double now = sampler->getBreak()->beats;
        const auto& steps = beatSteps();
        double next = now;
        if (direction > 0)
        {
            for (double s : steps) if (s > now + 0.01) { next = s; break; }
        }
        else
        {
            for (auto it = steps.rbegin(); it != steps.rend(); ++it) if (*it < now - 0.01) { next = *it; break; }
        }
        auto e = sampler->getEdit();
        e.beats = next;
        applyEdit (e);
    }

    void nudgeRoot (int semis)
    {
        if (sampler == nullptr) return;
        sampler->setRootNote (sampler->getRootNote() + semis);
        refresh();
        changed();
    }

    void applyEnvelope()
    {
        if (sampler != nullptr)
            sampler->setEnvelope ((float) envSliders[0]->getValue(), (float) envSliders[1]->getValue(),
                                  (float) envSliders[2]->getValue(), (float) envSliders[3]->getValue());
    }

    SamplerSource* sampler = nullptr;

    juce::OwnedArray<juce::TextButton> modeButtons;
    juce::TextButton barsDown { "-" }, barsUp { "+" };
    juce::Label barsLabel;

    juce::ComboBox warpCombo, filterCombo;
    juce::Label warpLabel, filterLabel;
    Knob transposeKnob { "TRANSPOSE", -24, 24, 0 }, detuneKnob { "DETUNE", -50, 50, 0 }, speedKnob { "SPEED", 0.25f, 4, 1 },
         grainKnob { "GRAIN", 5, 250, 40 }, gainKnob { "GAIN", -24, 12, 0 }, velKnob { "VEL", 0, 1, 1 },
         cutoffKnob { "CUTOFF", 20, 20000, 20000 }, resKnob { "RES", 0, 1, 0.2f };
    std::vector<std::pair<Knob*, SamplerSource::Param>> paramKnobs;
    juce::TextButton loopButton { "LOOP" };
    juce::TextButton rootDown { "-" }, rootUp { "+" };
    juce::Label rootLabel, rootCaption { {}, "ROOT" };
    juce::OwnedArray<juce::Slider> envSliders;
    juce::OwnedArray<juce::Label> envLabels;
};
