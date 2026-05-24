#pragma once

#include <JuceHeader.h>
#include "SynthEngine.h"
#include "MidiMorfColors.h"

//==============================================================================
// Rotary knob with a label below. Wraps a juce::Slider in rotary mode.
class Knob : public juce::Component
{
public:
    Knob (const juce::String& name, float minV, float maxV, float val,
          float step = 0.01f, float skewMid = 0.0f)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 16);
        slider.setRange (minV, maxV, step);
        if (skewMid > 0.0f) slider.setSkewFactorFromMidPoint (skewMid);
        slider.setValue (val, juce::dontSendNotification);
        slider.setColour (juce::Slider::rotarySliderFillColourId,    MidiMorfColors::accent);
        slider.setColour (juce::Slider::rotarySliderOutlineColourId, MidiMorfColors::padBorder);
        slider.setColour (juce::Slider::thumbColourId,                MidiMorfColors::textLight);
        slider.setColour (juce::Slider::textBoxTextColourId,          MidiMorfColors::textLight);
        slider.setColour (juce::Slider::textBoxBackgroundColourId,    juce::Colours::transparentBlack);
        slider.setColour (juce::Slider::textBoxOutlineColourId,       juce::Colours::transparentBlack);
        addAndMakeVisible (slider);

        label.setText (name, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setColour (juce::Label::textColourId, MidiMorfColors::textLight);
        label.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        addAndMakeVisible (label);
    }

    void resized() override
    {
        auto a = getLocalBounds();
        label.setBounds (a.removeFromBottom (16));
        slider.setBounds (a);
    }

    juce::Slider& getSlider() noexcept { return slider; }

private:
    juce::Slider slider;
    juce::Label  label;
};

//==============================================================================
// A simple labelled combo box (used for LFO waveform/target + routing dests).
class LabeledCombo : public juce::Component
{
public:
    LabeledCombo (const juce::String& name)
    {
        combo.setColour (juce::ComboBox::backgroundColourId,    MidiMorfColors::padBackground);
        combo.setColour (juce::ComboBox::textColourId,          MidiMorfColors::textLight);
        combo.setColour (juce::ComboBox::outlineColourId,       MidiMorfColors::padBorder);
        combo.setColour (juce::ComboBox::arrowColourId,         MidiMorfColors::textLight);
        combo.setColour (juce::ComboBox::buttonColourId,        MidiMorfColors::buttonFace);
        addAndMakeVisible (combo);

        label.setText (name, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setColour (juce::Label::textColourId, MidiMorfColors::textLight);
        label.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        addAndMakeVisible (label);
    }

    void resized() override
    {
        auto a = getLocalBounds();
        label.setBounds (a.removeFromBottom (16));
        a.reduce (4, 4);
        combo.setBounds (a.withSizeKeepingCentre (a.getWidth(), 28));
    }

    juce::ComboBox& getCombo() noexcept { return combo; }

private:
    juce::ComboBox combo;
    juce::Label    label;
};

//==============================================================================
// OSC tab — saw/square mix, master gain, reverb mix, reverb size.
class OscTab : public juce::Component
{
public:
    OscTab (SynthEngine& e, std::function<void()> notify)
        : synth (e), onChange (std::move (notify)),
          waveform ("SAW/SQ", 0.0f,  1.0f,  e.getWaveformMix()),
          gain     ("GAIN",   0.01f, 0.4f,  e.getGain()),
          revMix   ("REVERB", 0.0f,  1.0f,  e.getReverbMix()),
          revSize  ("ROOM",   0.0f,  1.0f,  e.getReverbSize())
    {
        wire (waveform.getSlider(), [this] (float v) { synth.setWaveformMix (v); });
        wire (gain.getSlider(),     [this] (float v) { synth.setGain (v); });
        wire (revMix.getSlider(),   [this] (float v) { synth.setReverbMix (v); });
        wire (revSize.getSlider(),  [this] (float v) { synth.setReverbSize (v); });
        addAndMakeVisible (waveform);
        addAndMakeVisible (gain);
        addAndMakeVisible (revMix);
        addAndMakeVisible (revSize);
    }

    void resized() override
    {
        if (getWidth() <= 0 || getHeight() <= 0) return;
        layoutInRow ({ &waveform, &gain, &revMix, &revSize });
    }

    void refresh()
    {
        waveform.getSlider().setValue (synth.getWaveformMix(), juce::dontSendNotification);
        gain.getSlider().setValue (synth.getGain(), juce::dontSendNotification);
        revMix.getSlider().setValue (synth.getReverbMix(), juce::dontSendNotification);
        revSize.getSlider().setValue (synth.getReverbSize(), juce::dontSendNotification);
    }

private:
    void wire (juce::Slider& s, std::function<void(float)> f)
    {
        s.onValueChange = [this, &s, f]
        {
            f ((float) s.getValue());
            if (onChange) onChange();
        };
    }

    void layoutInRow (std::initializer_list<juce::Component*> components)
    {
        auto area = getLocalBounds().reduced (12);
        int n = (int) components.size();
        int w = area.getWidth() / n;
        int i = 0;
        for (auto* c : components)
        {
            auto cell = area.removeFromLeft (i == n - 1 ? area.getWidth() : w).reduced (6);
            c->setBounds (cell);
            ++i;
        }
    }

    SynthEngine& synth;
    std::function<void()> onChange;
    Knob waveform, gain, revMix, revSize;
};

//==============================================================================
// ADSR & LFO tab — 4 amp ADSR knobs + LFO controls.
class AdsrLfoTab : public juce::Component
{
public:
    AdsrLfoTab (SynthEngine& e, std::function<void()> notify)
        : synth (e), onChange (std::move (notify)),
          atk ("ATK", 0.001f, 2.0f, e.getAttack()),
          dec ("DEC", 0.01f,  2.0f, e.getDecay()),
          sus ("SUS", 0.0f,   1.0f, e.getSustain()),
          rel ("REL", 0.01f,  3.0f, e.getRelease()),
          lfoRate  ("RATE",  0.05f, 20.0f, e.getLFORate(), 0.01f, 2.0f),
          lfoDepth ("DEPTH", 0.0f,  1.0f,  e.getLFODepth()),
          lfoWave  ("WAVE"),
          lfoTgt   ("TARGET")
    {
        wire (atk.getSlider(),       [this] (float v) { synth.setAttack (v); });
        wire (dec.getSlider(),       [this] (float v) { synth.setDecay  (v); });
        wire (sus.getSlider(),       [this] (float v) { synth.setSustain (v); });
        wire (rel.getSlider(),       [this] (float v) { synth.setRelease (v); });
        wire (lfoRate.getSlider(),   [this] (float v) { synth.setLFORate  (v); });
        wire (lfoDepth.getSlider(),  [this] (float v) { synth.setLFODepth (v); });

        lfoWave.getCombo().addItemList ({ "Sine", "Triangle", "Saw", "Square" }, 1);
        lfoWave.getCombo().setSelectedId (e.getLFOWaveform() + 1, juce::dontSendNotification);
        lfoWave.getCombo().onChange = [this]
        {
            synth.setLFOWaveform (lfoWave.getCombo().getSelectedId() - 1);
            if (onChange) onChange();
        };

        lfoTgt.getCombo().addItemList (destNames(), 1);
        lfoTgt.getCombo().setSelectedId (e.getLFOTarget() + 1, juce::dontSendNotification);
        lfoTgt.getCombo().onChange = [this]
        {
            synth.setLFOTarget (lfoTgt.getCombo().getSelectedId() - 1);
            if (onChange) onChange();
        };

        addAndMakeVisible (atk);
        addAndMakeVisible (dec);
        addAndMakeVisible (sus);
        addAndMakeVisible (rel);
        addAndMakeVisible (lfoRate);
        addAndMakeVisible (lfoDepth);
        addAndMakeVisible (lfoWave);
        addAndMakeVisible (lfoTgt);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (12);

        // Top row: amp ADSR (4 knobs)
        auto top = area.removeFromTop (area.getHeight() / 2);
        layoutInRow (top, { &atk, &dec, &sus, &rel });

        // Bottom row: LFO Rate, Depth, Wave (combo), Target (combo)
        layoutInRow (area, { &lfoRate, &lfoDepth, &lfoWave, &lfoTgt });
    }

    void refresh()
    {
        atk.getSlider().setValue (synth.getAttack(),  juce::dontSendNotification);
        dec.getSlider().setValue (synth.getDecay(),   juce::dontSendNotification);
        sus.getSlider().setValue (synth.getSustain(), juce::dontSendNotification);
        rel.getSlider().setValue (synth.getRelease(), juce::dontSendNotification);
        lfoRate.getSlider().setValue  (synth.getLFORate(),  juce::dontSendNotification);
        lfoDepth.getSlider().setValue (synth.getLFODepth(), juce::dontSendNotification);
        lfoWave.getCombo().setSelectedId (synth.getLFOWaveform() + 1, juce::dontSendNotification);
        lfoTgt.getCombo().setSelectedId  (synth.getLFOTarget()   + 1, juce::dontSendNotification);
    }

    static juce::StringArray destNames()
    {
        return { "Off", "Filter Cutoff", "Resonance", "Env Amount", "Gain", "Reverb Mix" };
    }

private:
    void wire (juce::Slider& s, std::function<void(float)> f)
    {
        s.onValueChange = [this, &s, f]
        {
            f ((float) s.getValue());
            if (onChange) onChange();
        };
    }
    void layoutInRow (juce::Rectangle<int> row, std::initializer_list<juce::Component*> components)
    {
        int n = (int) components.size();
        int w = row.getWidth() / n;
        int i = 0;
        for (auto* c : components)
        {
            auto cell = row.removeFromLeft (i == n - 1 ? row.getWidth() : w).reduced (6);
            c->setBounds (cell);
            ++i;
        }
    }
    SynthEngine& synth;
    std::function<void()> onChange;
    Knob atk, dec, sus, rel;
    Knob lfoRate, lfoDepth;
    LabeledCombo lfoWave, lfoTgt;
};

//==============================================================================
// Filter tab — cutoff, resonance, env amount + filter ADSR (4 knobs).
class FilterTab : public juce::Component
{
public:
    FilterTab (SynthEngine& e, std::function<void()> notify)
        : synth (e), onChange (std::move (notify)),
          cutoff ("FREQ", 20.0f, 20000.0f, e.getFilterCutoff(),    1.0f, 1000.0f),
          reso   ("RES",  0.5f,  20.0f,    e.getFilterResonance(), 0.1f, 3.0f),
          envAmt ("ENV",  0.0f,  10000.0f, e.getFilterEnvAmount(), 1.0f, 2000.0f),
          fAtk ("F.ATK", 0.001f, 2.0f, e.getFilterAttack()),
          fDec ("F.DEC", 0.01f,  2.0f, e.getFilterDecay()),
          fSus ("F.SUS", 0.0f,   1.0f, e.getFilterSustain()),
          fRel ("F.REL", 0.01f,  3.0f, e.getFilterRelease())
    {
        wire (cutoff.getSlider(), [this] (float v) { synth.setFilterCutoff (v); });
        wire (reso.getSlider(),   [this] (float v) { synth.setFilterResonance (v); });
        wire (envAmt.getSlider(), [this] (float v) { synth.setFilterEnvAmount (v); });
        wire (fAtk.getSlider(),   [this] (float v) { synth.setFilterAttack (v); });
        wire (fDec.getSlider(),   [this] (float v) { synth.setFilterDecay  (v); });
        wire (fSus.getSlider(),   [this] (float v) { synth.setFilterSustain (v); });
        wire (fRel.getSlider(),   [this] (float v) { synth.setFilterRelease (v); });
        addAndMakeVisible (cutoff);
        addAndMakeVisible (reso);
        addAndMakeVisible (envAmt);
        addAndMakeVisible (fAtk);
        addAndMakeVisible (fDec);
        addAndMakeVisible (fSus);
        addAndMakeVisible (fRel);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (12);
        auto top  = area.removeFromTop (area.getHeight() / 2);
        layoutInRow (top, { &cutoff, &reso, &envAmt });
        layoutInRow (area, { &fAtk, &fDec, &fSus, &fRel });
    }

    void refresh()
    {
        cutoff.getSlider().setValue (synth.getFilterCutoff(),    juce::dontSendNotification);
        reso.getSlider().setValue   (synth.getFilterResonance(), juce::dontSendNotification);
        envAmt.getSlider().setValue (synth.getFilterEnvAmount(), juce::dontSendNotification);
        fAtk.getSlider().setValue (synth.getFilterAttack(),  juce::dontSendNotification);
        fDec.getSlider().setValue (synth.getFilterDecay(),   juce::dontSendNotification);
        fSus.getSlider().setValue (synth.getFilterSustain(), juce::dontSendNotification);
        fRel.getSlider().setValue (synth.getFilterRelease(), juce::dontSendNotification);
    }

private:
    void wire (juce::Slider& s, std::function<void(float)> f)
    {
        s.onValueChange = [this, &s, f] { f ((float) s.getValue()); if (onChange) onChange(); };
    }
    void layoutInRow (juce::Rectangle<int> row, std::initializer_list<juce::Component*> components)
    {
        int n = (int) components.size();
        int w = row.getWidth() / n;
        int i = 0;
        for (auto* c : components)
        {
            auto cell = row.removeFromLeft (i == n - 1 ? row.getWidth() : w).reduced (6);
            c->setBounds (cell);
            ++i;
        }
    }
    SynthEngine& synth;
    std::function<void()> onChange;
    Knob cutoff, reso, envAmt;
    Knob fAtk, fDec, fSus, fRel;
};

//==============================================================================
// Routing tab — 4 slots: Mod Wheel (Morf-Pad Y axis) → [destination] [amount].
class RoutingTab : public juce::Component
{
public:
    RoutingTab (SynthEngine& e, std::function<void()> notify)
        : synth (e), onChange (std::move (notify))
    {
        header.setText ("MOD WHEEL ROUTING  (Morf-Pad Y axis controls these destinations)",
                        juce::dontSendNotification);
        header.setColour (juce::Label::textColourId, MidiMorfColors::accent);
        header.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        addAndMakeVisible (header);

        for (int i = 0; i < SynthEngine::NumRoutingSlots; ++i)
        {
            slots[i].slotLabel.setText ("Slot " + juce::String (i + 1) + ":",
                                        juce::dontSendNotification);
            slots[i].slotLabel.setColour (juce::Label::textColourId, MidiMorfColors::textLight);
            slots[i].slotLabel.setFont (juce::FontOptions (12.0f));
            addAndMakeVisible (slots[i].slotLabel);

            slots[i].dest.addItemList (AdsrLfoTab::destNames(), 1);
            slots[i].dest.setColour (juce::ComboBox::backgroundColourId, MidiMorfColors::padBackground);
            slots[i].dest.setColour (juce::ComboBox::textColourId,        MidiMorfColors::textLight);
            slots[i].dest.setColour (juce::ComboBox::outlineColourId,     MidiMorfColors::padBorder);
            slots[i].dest.setColour (juce::ComboBox::arrowColourId,       MidiMorfColors::textLight);
            slots[i].dest.setSelectedId (e.getRoutingDest (i) + 1, juce::dontSendNotification);
            slots[i].dest.onChange = [this, i]
            {
                synth.setRoutingDest (i, slots[i].dest.getSelectedId() - 1);
                if (onChange) onChange();
            };
            addAndMakeVisible (slots[i].dest);

            slots[i].amount.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            slots[i].amount.setTextBoxStyle (juce::Slider::TextBoxRight, false, 50, 18);
            slots[i].amount.setRange (-1.0, 1.0, 0.01);
            slots[i].amount.setValue (e.getRoutingAmount (i), juce::dontSendNotification);
            slots[i].amount.setColour (juce::Slider::rotarySliderFillColourId,    MidiMorfColors::accent);
            slots[i].amount.setColour (juce::Slider::rotarySliderOutlineColourId, MidiMorfColors::padBorder);
            slots[i].amount.setColour (juce::Slider::thumbColourId,                MidiMorfColors::textLight);
            slots[i].amount.setColour (juce::Slider::textBoxTextColourId,          MidiMorfColors::textLight);
            slots[i].amount.setColour (juce::Slider::textBoxBackgroundColourId,    juce::Colours::transparentBlack);
            slots[i].amount.setColour (juce::Slider::textBoxOutlineColourId,       juce::Colours::transparentBlack);
            slots[i].amount.onValueChange = [this, i]
            {
                synth.setRoutingAmount (i, (float) slots[i].amount.getValue());
                if (onChange) onChange();
            };
            addAndMakeVisible (slots[i].amount);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (12);
        header.setBounds (area.removeFromTop (22));
        area.removeFromTop (6);

        int slotH = area.getHeight() / SynthEngine::NumRoutingSlots;
        for (int i = 0; i < SynthEngine::NumRoutingSlots; ++i)
        {
            auto row = area.removeFromTop (slotH).reduced (4, 6);
            slots[i].slotLabel.setBounds (row.removeFromLeft (60));
            row.removeFromLeft (6);
            slots[i].amount.setBounds (row.removeFromRight (170));
            row.removeFromRight (8);
            slots[i].dest.setBounds (row.withSizeKeepingCentre (row.getWidth(), 28));
        }
    }

    void refresh()
    {
        for (int i = 0; i < SynthEngine::NumRoutingSlots; ++i)
        {
            slots[i].dest.setSelectedId (synth.getRoutingDest (i) + 1, juce::dontSendNotification);
            slots[i].amount.setValue (synth.getRoutingAmount (i), juce::dontSendNotification);
        }
    }

private:
    struct Slot
    {
        juce::Label    slotLabel;
        juce::ComboBox dest;
        juce::Slider   amount;
    };

    SynthEngine& synth;
    std::function<void()> onChange;
    juce::Label header;
    Slot slots[SynthEngine::NumRoutingSlots];
};

//==============================================================================
// Landscape content: tab bar + active tab.
class SynthSettingsLandscapeContent : public juce::Component
{
public:
    SynthSettingsLandscapeContent (SynthEngine& engine, std::function<void()> notify)
        : synth (engine),
          onChange (notify),
          oscTab    (engine, notify),
          adsrTab   (engine, notify),
          filterTab (engine, notify),
          routingTab(engine, notify)
    {
        const juce::StringArray names = { "OSC", "ADSR & LFO", "FILTER", "ROUTING" };
        for (int i = 0; i < 4; ++i)
        {
            auto* b = new juce::TextButton (names[i]);
            b->setRadioGroupId (1001);
            b->setClickingTogglesState (true);
            b->setColour (juce::TextButton::buttonColourId,   MidiMorfColors::buttonFace);
            b->setColour (juce::TextButton::buttonOnColourId, MidiMorfColors::accent);
            b->setColour (juce::TextButton::textColourOnId,   MidiMorfColors::textLight);
            b->setColour (juce::TextButton::textColourOffId,  MidiMorfColors::textDark);
            b->onClick = [this, i] { showTab (i); };
            tabButtons.add (b);
            addAndMakeVisible (b);
        }

        addChildComponent (oscTab);
        addChildComponent (adsrTab);
        addChildComponent (filterTab);
        addChildComponent (routingTab);

        tabButtons[0]->setToggleState (true, juce::dontSendNotification);
        showTab (0);
    }

    void showTab (int idx)
    {
        currentTab = idx;
        oscTab.setVisible    (idx == 0);
        adsrTab.setVisible   (idx == 1);
        filterTab.setVisible (idx == 2);
        routingTab.setVisible(idx == 3);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (MidiMorfColors::background);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (8);

        // Tab bar at the top
        auto bar = area.removeFromTop (36);
        int n = tabButtons.size();
        int w = bar.getWidth() / n;
        for (int i = 0; i < n; ++i)
        {
            auto cell = bar.removeFromLeft (i == n - 1 ? bar.getWidth() : w).reduced (3);
            tabButtons[i]->setBounds (cell);
        }
        area.removeFromTop (8);

        // Tab content fills the rest
        oscTab.setBounds (area);
        adsrTab.setBounds (area);
        filterTab.setBounds (area);
        routingTab.setBounds (area);
    }

    void refreshAll()
    {
        oscTab.refresh();
        adsrTab.refresh();
        filterTab.refresh();
        routingTab.refresh();
    }

private:
    SynthEngine& synth;
    std::function<void()> onChange;

    juce::OwnedArray<juce::TextButton> tabButtons;
    int currentTab = 0;

    OscTab     oscTab;
    AdsrLfoTab adsrTab;
    FilterTab  filterTab;
    RoutingTab routingTab;
};

//==============================================================================
// Outer page — fits in portrait container, rotates the landscape content 90° CW.
class SynthSettingsPage : public juce::Component
{
public:
    SynthSettingsPage (SynthEngine& engine)
        : synth (engine),
          content (engine, [this]
          {
              if (onPatchChanged) onPatchChanged();
          })
    {
        addAndMakeVisible (content);
    }

    std::function<void()> onPatchChanged;

    void resized() override
    {
        int pw = getWidth();
        int ph = getHeight();
        if (pw <= 0 || ph <= 0) return;

        content.setSize (ph, pw);
        content.setTransform (
            juce::AffineTransform::rotation (
                juce::MathConstants<float>::halfPi, 0.0f, 0.0f)
            .translated ((float) pw, 0.0f));
    }

    void saveState (juce::PropertiesFile& props) const
    {
        props.setValue ("synth_attack",     synth.getAttack());
        props.setValue ("synth_decay",      synth.getDecay());
        props.setValue ("synth_sustain",    synth.getSustain());
        props.setValue ("synth_release",    synth.getRelease());
        props.setValue ("synth_cutoff",     synth.getFilterCutoff());
        props.setValue ("synth_reso",       synth.getFilterResonance());
        props.setValue ("synth_filtEnv",    synth.getFilterEnvAmount());
        props.setValue ("synth_fAttack",    synth.getFilterAttack());
        props.setValue ("synth_fDecay",     synth.getFilterDecay());
        props.setValue ("synth_fSustain",   synth.getFilterSustain());
        props.setValue ("synth_fRelease",   synth.getFilterRelease());
        props.setValue ("synth_waveform",   synth.getWaveformMix());
        props.setValue ("synth_gain",       synth.getGain());
        props.setValue ("synth_reverbMix",  synth.getReverbMix());
        props.setValue ("synth_reverbSize", synth.getReverbSize());

        // LFO
        props.setValue ("synth_lfoRate",  synth.getLFORate());
        props.setValue ("synth_lfoDepth", synth.getLFODepth());
        props.setValue ("synth_lfoWave",  synth.getLFOWaveform());
        props.setValue ("synth_lfoTgt",   synth.getLFOTarget());

        // Routing
        for (int i = 0; i < SynthEngine::NumRoutingSlots; ++i)
        {
            props.setValue ("synth_routeDest_" + juce::String (i),   synth.getRoutingDest (i));
            props.setValue ("synth_routeAmount_" + juce::String (i), synth.getRoutingAmount (i));
        }
    }

    void restoreState (juce::PropertiesFile& props)
    {
        synth.setAttack          ((float) props.getDoubleValue ("synth_attack",     synth.getAttack()));
        synth.setDecay           ((float) props.getDoubleValue ("synth_decay",      synth.getDecay()));
        synth.setSustain         ((float) props.getDoubleValue ("synth_sustain",    synth.getSustain()));
        synth.setRelease         ((float) props.getDoubleValue ("synth_release",    synth.getRelease()));
        synth.setFilterCutoff    ((float) props.getDoubleValue ("synth_cutoff",     synth.getFilterCutoff()));
        synth.setFilterResonance ((float) props.getDoubleValue ("synth_reso",       synth.getFilterResonance()));
        synth.setFilterEnvAmount ((float) props.getDoubleValue ("synth_filtEnv",    synth.getFilterEnvAmount()));
        synth.setFilterAttack    ((float) props.getDoubleValue ("synth_fAttack",    synth.getFilterAttack()));
        synth.setFilterDecay     ((float) props.getDoubleValue ("synth_fDecay",     synth.getFilterDecay()));
        synth.setFilterSustain   ((float) props.getDoubleValue ("synth_fSustain",   synth.getFilterSustain()));
        synth.setFilterRelease   ((float) props.getDoubleValue ("synth_fRelease",   synth.getFilterRelease()));
        synth.setWaveformMix     ((float) props.getDoubleValue ("synth_waveform",   synth.getWaveformMix()));
        synth.setGain            ((float) props.getDoubleValue ("synth_gain",       synth.getGain()));
        synth.setReverbMix       ((float) props.getDoubleValue ("synth_reverbMix",  synth.getReverbMix()));
        synth.setReverbSize      ((float) props.getDoubleValue ("synth_reverbSize", synth.getReverbSize()));

        synth.setLFORate     ((float) props.getDoubleValue ("synth_lfoRate",  synth.getLFORate()));
        synth.setLFODepth    ((float) props.getDoubleValue ("synth_lfoDepth", synth.getLFODepth()));
        synth.setLFOWaveform (props.getIntValue ("synth_lfoWave", synth.getLFOWaveform()));
        synth.setLFOTarget   (props.getIntValue ("synth_lfoTgt",  synth.getLFOTarget()));

        for (int i = 0; i < SynthEngine::NumRoutingSlots; ++i)
        {
            synth.setRoutingDest   (i, props.getIntValue    ("synth_routeDest_" + juce::String (i), synth.getRoutingDest (i)));
            synth.setRoutingAmount (i, (float) props.getDoubleValue ("synth_routeAmount_" + juce::String (i), synth.getRoutingAmount (i)));
        }

        content.refreshAll();
    }

private:
    SynthEngine& synth;
    SynthSettingsLandscapeContent content;
};
