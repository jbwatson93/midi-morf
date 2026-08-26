#pragma once

#include <JuceHeader.h>
#include "SynthEngine.h"
#include "MidiMorfColors.h"
#include "SynthLookAndFeel.h"

//==============================================================================
// AuditionStrip — 2-octave horizontal mono ribbon. Drag X to choose a pitch,
// release to stop. Used as an audition keyboard at the bottom of the synth
// settings page so the user can hear edits while tweaking.
class AuditionStrip : public juce::Component
{
public:
    std::function<void (int midiNote, float velocity)> onNoteOn;
    std::function<void (int midiNote)>                  onNoteOff;

    void setRange (int low, int high)
    {
        lowNote  = low;
        highNote = high;
        repaint();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        int n = noteFromX ((float) e.x);
        currentNote = n;
        active = true;
        if (onNoteOn) onNoteOn (n, 0.85f);
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! active) return;
        int n = noteFromX ((float) e.x);
        if (n != currentNote)
        {
            int prev = currentNote;
            currentNote = n;
            if (onNoteOff) onNoteOff (prev);
            if (onNoteOn)  onNoteOn (n, 0.85f);
            repaint();
        }
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (! active) return;
        int prev = currentNote;
        active = false;
        currentNote = -1;
        if (onNoteOff) onNoteOff (prev);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().reduced (1.5f);

        // Recessed dark slot
        g.setColour (juce::Colour (0xff141417));
        g.fillRoundedRectangle (bounds, 5.0f);

        // Semitone tick marks (subtle, bottom of strip)
        const int range = juce::jmax (1, highNote - lowNote);
        g.setColour (juce::Colour (0xff2A2A30));
        for (int i = 0; i <= range; ++i)
        {
            float t = (float) i / (float) range;
            float x = bounds.getX() + t * bounds.getWidth();
            g.drawVerticalLine ((int) x, bounds.getBottom() - 5.0f, bounds.getBottom());
        }

        // Octave divider lines + C labels
        g.setColour (juce::Colour (0xff3A3A42));
        g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
        for (int n = lowNote; n <= highNote; n += 12)
        {
            float t = (float) (n - lowNote) / (float) range;
            float x = bounds.getX() + t * bounds.getWidth();
            g.setColour (juce::Colour (0xff3A3A42));
            g.drawVerticalLine ((int) x, bounds.getY() + 2.0f, bounds.getBottom() - 6.0f);

            int oct = (n / 12) - 1;  // MIDI convention: MIDI 60 = C4
            juce::String label = "C" + juce::String (oct);
            g.setColour (MidiMorfColors::textLight.withAlpha (0.45f));
            g.drawText (label,
                        juce::Rectangle<float> (x - 14.0f, bounds.getY() + 2.0f, 28.0f, 11.0f),
                        juce::Justification::centred);
        }

        // Active position indicator
        if (active && currentNote >= lowNote && currentNote <= highNote)
        {
            float t = (float) (currentNote - lowNote) / (float) range;
            float x = bounds.getX() + t * bounds.getWidth();

            // Soft glow halo
            g.setColour (MidiMorfColors::accent.withAlpha (0.25f));
            g.fillRect (juce::Rectangle<float> (x - 10.0f, bounds.getY(),
                                                 20.0f, bounds.getHeight()));

            // Hard accent line
            g.setColour (MidiMorfColors::accent);
            g.fillRect (juce::Rectangle<float> (x - 2.0f, bounds.getY(),
                                                 4.0f, bounds.getHeight()));

            // Note name readout (e.g. "C4", "E♭5")
            g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
            g.setColour (MidiMorfColors::textLight);
            g.drawText (noteName (currentNote),
                        juce::Rectangle<float> (x - 22.0f, bounds.getCentreY() - 7.0f,
                                                 44.0f, 14.0f),
                        juce::Justification::centred);
        }

        // Outer border
        g.setColour (juce::Colour (0xff3A3A40));
        g.drawRoundedRectangle (bounds, 5.0f, 1.0f);
    }

private:
    int noteFromX (float x) const
    {
        auto b = getLocalBounds().toFloat().reduced (1.5f);
        float t = juce::jlimit (0.0f, 1.0f, (x - b.getX()) / b.getWidth());
        const int range = juce::jmax (1, highNote - lowNote);
        return lowNote + (int) std::round (t * (float) range);
    }

    static juce::String noteName (int midiNote)
    {
        static const char* names[] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
        int oct = (midiNote / 12) - 1;
        int pc  = midiNote % 12;
        return juce::String (names[pc]) + juce::String (oct);
    }

    int lowNote  = 48;  // C3
    int highNote = 72;  // C5 — 2 octaves
    int currentNote = -1;
    bool active = false;
};

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
// Juno-style vertical fader (label below). Used for ADSR envelopes.
class Fader : public juce::Component
{
public:
    Fader (const juce::String& name, float minV, float maxV, float val,
           float step = 0.01f, float skewMid = 0.0f)
    {
        slider.setSliderStyle (juce::Slider::LinearVertical);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider.setRange (minV, maxV, step);
        if (skewMid > 0.0f) slider.setSkewFactorFromMidPoint (skewMid);
        slider.setValue (val, juce::dontSendNotification);
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
// SectionCard — labelled module card with accent stripe down the left, no
// on/off button. Used to group related controls on the OSC tab (OSC 1, OSC 2,
// AMP) so the page reads as distinct modules rather than a flat knob row.
class SectionCard : public juce::Component
{
public:
    SectionCard (const juce::String& name) : moduleName (name) {}

    // Children should be positioned inside getContentArea().
    juce::Rectangle<int> getContentArea() const
    {
        return getLocalBounds().withTrimmedTop (26).reduced (8, 6);
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().reduced (1.5f);

        g.setColour (juce::Colour (0xff232328));
        g.fillRoundedRectangle (bounds, 6.0f);

        g.setColour (MidiMorfColors::accent);
        g.fillRect (bounds.withWidth (3.0f));

        auto header = bounds.withHeight (22.0f);
        g.setColour (juce::Colour (0xff2A2A30));
        g.fillRoundedRectangle (header.withTrimmedLeft (3.0f), 6.0f);

        g.setColour (MidiMorfColors::textLight);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (moduleName, header.reduced (10.0f, 0.0f),
                    juce::Justification::centredLeft);

        g.setColour (juce::Colour (0xff3A3A40));
        g.drawRoundedRectangle (bounds.reduced (0.5f), 6.0f, 1.0f);
    }

private:
    juce::String moduleName;
};

//==============================================================================
// OSC tab — OSC 1 + OSC 2 + AMP cards.
class OscTab : public juce::Component
{
public:
    OscTab (SynthEngine& e, std::function<void()> notify)
        : synth (e), onChange (std::move (notify)),
          osc1Card ("OSC 1"),
          osc2Card ("OSC 2"),
          ampCard  ("AMP"),
          wave1    ("WAVE"),
          level1   ("LEVEL",   0.0f,   1.0f,  e.getOsc1Level()),
          wave2    ("WAVE"),
          level2   ("LEVEL",   0.0f,   1.0f,  e.getOsc2Level()),
          detune   ("DETUNE", -100.0f, 100.0f, e.getOsc2DetuneCents(), 0.1f),
          sub      ("SUB",     0.0f,   1.0f,  e.getSubLevel()),
          driveKb  ("DRIVE",   0.0f,   1.0f,  e.getDrive()),
          gain     ("GAIN",    0.01f,  0.4f,  e.getGain())
    {
        const juce::StringArray waves { "Sine", "Triangle", "Saw", "Square" };

        wave1.getCombo().addItemList (waves, 1);
        wave1.getCombo().setSelectedId (e.getMainWaveform() + 1, juce::dontSendNotification);
        wave1.getCombo().onChange = [this]
        {
            synth.setMainWaveform (wave1.getCombo().getSelectedId() - 1);
            if (onChange) onChange();
        };

        wave2.getCombo().addItemList (waves, 1);
        wave2.getCombo().setSelectedId (e.getOsc2Waveform() + 1, juce::dontSendNotification);
        wave2.getCombo().onChange = [this]
        {
            synth.setOsc2Waveform (wave2.getCombo().getSelectedId() - 1);
            if (onChange) onChange();
        };

        wire (level1.getSlider(),  [this] (float v) { synth.setOsc1Level       (v); });
        wire (level2.getSlider(),  [this] (float v) { synth.setOsc2Level       (v); });
        wire (detune.getSlider(),  [this] (float v) { synth.setOsc2DetuneCents (v); });
        wire (sub.getSlider(),     [this] (float v) { synth.setSubLevel        (v); });
        wire (driveKb.getSlider(), [this] (float v) { synth.setDrive           (v); });
        wire (gain.getSlider(),    [this] (float v) { synth.setGain            (v); });

        addAndMakeVisible (osc1Card);
        addAndMakeVisible (osc2Card);
        addAndMakeVisible (ampCard);

        osc1Card.addAndMakeVisible (wave1);
        osc1Card.addAndMakeVisible (level1);
        osc2Card.addAndMakeVisible (wave2);
        osc2Card.addAndMakeVisible (level2);
        osc2Card.addAndMakeVisible (detune);
        ampCard.addAndMakeVisible (sub);
        ampCard.addAndMakeVisible (driveKb);
        ampCard.addAndMakeVisible (gain);
    }

    void resized() override
    {
        if (getWidth() <= 0 || getHeight() <= 0) return;

        auto area = getLocalBounds().reduced (8);

        // 3 cards: OSC 1 (~22%), OSC 2 (~38%), AMP (~40%)
        const int totalW = area.getWidth();
        const int gap    = 6;
        const int w1     = (int) (totalW * 0.22f);
        const int w2     = (int) (totalW * 0.40f);

        auto a1 = area.removeFromLeft (w1); area.removeFromLeft (gap);
        auto a2 = area.removeFromLeft (w2); area.removeFromLeft (gap);
        auto a3 = area;

        osc1Card.setBounds (a1);
        osc2Card.setBounds (a2);
        ampCard.setBounds  (a3);

        layoutInRow (osc1Card.getContentArea(), { &wave1, &level1 });
        layoutInRow (osc2Card.getContentArea(), { &wave2, &level2, &detune });
        layoutInRow (ampCard.getContentArea(),  { &sub, &driveKb, &gain });
    }

    void refresh()
    {
        wave1.getCombo().setSelectedId  (synth.getMainWaveform() + 1, juce::dontSendNotification);
        level1.getSlider().setValue     (synth.getOsc1Level(),     juce::dontSendNotification);
        wave2.getCombo().setSelectedId  (synth.getOsc2Waveform() + 1, juce::dontSendNotification);
        level2.getSlider().setValue     (synth.getOsc2Level(),     juce::dontSendNotification);
        detune.getSlider().setValue     (synth.getOsc2DetuneCents(), juce::dontSendNotification);
        sub.getSlider().setValue        (synth.getSubLevel(),      juce::dontSendNotification);
        driveKb.getSlider().setValue    (synth.getDrive(),         juce::dontSendNotification);
        gain.getSlider().setValue       (synth.getGain(),          juce::dontSendNotification);
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

    static void layoutInRow (juce::Rectangle<int> area,
                              std::initializer_list<juce::Component*> components)
    {
        int n = (int) components.size();
        if (n == 0) return;
        int w = area.getWidth() / n;
        int i = 0;
        for (auto* c : components)
        {
            auto cell = area.removeFromLeft (i == n - 1 ? area.getWidth() : w).reduced (4);
            c->setBounds (cell);
            ++i;
        }
    }

    SynthEngine& synth;
    std::function<void()> onChange;
    SectionCard osc1Card, osc2Card, ampCard;
    LabeledCombo wave1;
    Knob level1;
    LabeledCombo wave2;
    Knob level2, detune;
    Knob sub, driveKb, gain;
};

//==============================================================================
// ADSR & LFO tab — 4 amp ADSR knobs + LFO controls.
class AdsrLfoTab : public juce::Component
{
public:
    AdsrLfoTab (SynthEngine& e, std::function<void()> notify)
        : synth (e), onChange (std::move (notify)),
          atk ("A", 0.001f, 2.0f, e.getAttack(),  0.001f, 0.2f),
          dec ("D", 0.01f,  2.0f, e.getDecay(),   0.01f,  0.3f),
          sus ("S", 0.0f,   1.0f, e.getSustain()),
          rel ("R", 0.01f,  3.0f, e.getRelease(), 0.01f,  0.5f),
          lfoRate  ("RATE",  0.05f, 20.0f, e.getLFORate(), 0.01f, 2.0f),
          lfoDepth ("DEPTH", 0.0f,  1.0f,  e.getLFODepth()),
          lfoWave  ("LFO WAVE")
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

        addAndMakeVisible (atk);
        addAndMakeVisible (dec);
        addAndMakeVisible (sus);
        addAndMakeVisible (rel);
        addAndMakeVisible (lfoRate);
        addAndMakeVisible (lfoDepth);
        addAndMakeVisible (lfoWave);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (12);

        // Left half: ADSR faders | Right half: LFO knobs (top) + wave combo (bottom)
        auto adsrArea = area.removeFromLeft (area.getWidth() / 2);
        area.removeFromLeft (8);
        layoutInRow (adsrArea, { &atk, &dec, &sus, &rel });

        // Right side: knobs fill the top, wave combo gets a short strip at the bottom
        auto waveArea = area.removeFromBottom (56);
        layoutInRow (area,    { &lfoRate, &lfoDepth });
        lfoWave.setBounds (waveArea.reduced (6, 0));
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
    }

    static juce::StringArray sourceNames()
    {
        return { "Off", "Mod Wheel", "Velocity", "Accel X", "Accel Y", "LFO" };
    }

    static juce::StringArray destNames()
    {
        return { "Off", "Filter Cutoff", "Resonance", "Env Amount", "Gain",
                 "Reverb Mix", "Pitch", "Dist Drive", "Delay Mix", "Delay Time" };
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
    Fader atk, dec, sus, rel;
    Knob lfoRate, lfoDepth;
    LabeledCombo lfoWave;
};

//==============================================================================
// Filter tab — cutoff, resonance, env amount + filter ADSR (4 knobs).
class FilterTab : public juce::Component
{
public:
    FilterTab (SynthEngine& e, std::function<void()> notify)
        : synth (e), onChange (std::move (notify)),
          cutoff ("FREQ", 20.0f, 20000.0f, e.getFilterCutoff(),    1.0f, 1000.0f),
          reso   ("RES",  0.0f,  4.0f,     e.getFilterResonance(), 0.01f),
          envAmt ("ENV",  0.0f,  10000.0f, e.getFilterEnvAmount(), 1.0f, 2000.0f),
          fAtk ("A", 0.001f, 2.0f, e.getFilterAttack(),  0.001f, 0.2f),
          fDec ("D", 0.01f,  2.0f, e.getFilterDecay(),   0.01f,  0.3f),
          fSus ("S", 0.0f,   1.0f, e.getFilterSustain()),
          fRel ("R", 0.01f,  3.0f, e.getFilterRelease(), 0.01f,  0.5f)
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

        // Left half: filter ADSR faders | Right half: filter knobs (cutoff/res/env)
        auto adsrArea = area.removeFromLeft (area.getWidth() / 2);
        area.removeFromLeft (8);
        layoutInRow (adsrArea, { &fAtk, &fDec, &fSus, &fRel });
        layoutInRow (area,     { &cutoff, &reso, &envAmt });
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
    Fader fAtk, fDec, fSus, fRel;
};

//==============================================================================
// Routing tab — 4 slots: Mod Wheel (Morf-Pad Y axis) → [destination] [amount].
class RoutingTab : public juce::Component
{
public:
    RoutingTab (SynthEngine& e, std::function<void()> notify)
        : synth (e), onChange (std::move (notify))
    {
        header.setText ("MODULATION ROUTING",
                        juce::dontSendNotification);
        header.setColour (juce::Label::textColourId, MidiMorfColors::accent);
        header.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        addAndMakeVisible (header);

        auto styleCombo = [] (juce::ComboBox& c)
        {
            c.setColour (juce::ComboBox::backgroundColourId, MidiMorfColors::padBackground);
            c.setColour (juce::ComboBox::textColourId,        MidiMorfColors::textLight);
            c.setColour (juce::ComboBox::outlineColourId,     MidiMorfColors::padBorder);
            c.setColour (juce::ComboBox::arrowColourId,       MidiMorfColors::textLight);
        };

        for (int i = 0; i < SynthEngine::NumRoutingSlots; ++i)
        {
            slots[i].slotLabel.setText (juce::String (i + 1),
                                        juce::dontSendNotification);
            slots[i].slotLabel.setColour (juce::Label::textColourId, MidiMorfColors::textLight);
            slots[i].slotLabel.setFont (juce::FontOptions (12.0f, juce::Font::bold));
            slots[i].slotLabel.setJustificationType (juce::Justification::centred);
            addAndMakeVisible (slots[i].slotLabel);

            slots[i].source.addItemList (AdsrLfoTab::sourceNames(), 1);
            styleCombo (slots[i].source);
            slots[i].source.setSelectedId (e.getRoutingSource (i) + 1, juce::dontSendNotification);
            slots[i].source.onChange = [this, i]
            {
                synth.setRoutingSource (i, slots[i].source.getSelectedId() - 1);
                if (onChange) onChange();
            };
            addAndMakeVisible (slots[i].source);

            slots[i].dest.addItemList (AdsrLfoTab::destNames(), 1);
            styleCombo (slots[i].dest);
            slots[i].dest.setSelectedId (e.getRoutingDest (i) + 1, juce::dontSendNotification);
            slots[i].dest.onChange = [this, i]
            {
                synth.setRoutingDest (i, slots[i].dest.getSelectedId() - 1);
                if (onChange) onChange();
            };
            addAndMakeVisible (slots[i].dest);

            slots[i].amount.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            slots[i].amount.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 50, 16);
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

        // 4 slots stacked: [#] [Source ▾] → [Target ▾] | [Depth knob]
        int slotH = area.getHeight() / SynthEngine::NumRoutingSlots;
        for (int i = 0; i < SynthEngine::NumRoutingSlots; ++i)
        {
            auto row = area.removeFromTop (slotH).reduced (2, 4);
            slots[i].slotLabel.setBounds (row.removeFromLeft (22));
            row.removeFromLeft (4);

            // Depth knob on the right
            slots[i].amount.setBounds (row.removeFromRight (juce::jmin (110, row.getHeight() + 50)));
            row.removeFromRight (6);

            // Source + Target dropdowns split the middle
            int half = row.getWidth() / 2;
            slots[i].source.setBounds (row.removeFromLeft (half - 4).withSizeKeepingCentre (half - 4, 28));
            row.removeFromLeft (6);
            slots[i].dest.setBounds   (row.withSizeKeepingCentre (row.getWidth(), 28));
        }
    }

    void refresh()
    {
        for (int i = 0; i < SynthEngine::NumRoutingSlots; ++i)
        {
            slots[i].source.setSelectedId (synth.getRoutingSource (i) + 1, juce::dontSendNotification);
            slots[i].dest.setSelectedId   (synth.getRoutingDest   (i) + 1, juce::dontSendNotification);
            slots[i].amount.setValue (synth.getRoutingAmount (i), juce::dontSendNotification);
        }
    }

private:
    struct Slot
    {
        juce::Label    slotLabel;
        juce::ComboBox source;
        juce::ComboBox dest;
        juce::Slider   amount;
    };

    SynthEngine& synth;
    std::function<void()> onChange;
    juce::Label header;
    Slot slots[SynthEngine::NumRoutingSlots];
};

//==============================================================================
// FxModule — Serum-style FX card. Header bar with the FX name + a power
// toggle on the right; knob area below. Bypassed modules dim their content
// and switch their accent stripe off.
class FxModule : public juce::Component
{
public:
    FxModule (const juce::String& name,
              std::function<bool()> getEnabled,
              std::function<void(bool)> setEnabled)
        : moduleName (name), enabledGetter (std::move (getEnabled)), enabledSetter (std::move (setEnabled))
    {
        powerBtn.setClickingTogglesState (true);
        powerBtn.setToggleState (enabledGetter(), juce::dontSendNotification);
        powerBtn.setColour (juce::TextButton::buttonColourId,   juce::Colour (0xff1F1F23));
        powerBtn.setColour (juce::TextButton::buttonOnColourId, MidiMorfColors::accent);
        powerBtn.setColour (juce::TextButton::textColourOffId,  juce::Colour (0xff666666));
        powerBtn.setColour (juce::TextButton::textColourOnId,   juce::Colours::white);
        powerBtn.onClick = [this]
        {
            bool on = powerBtn.getToggleState();
            enabledSetter (on);
            repaint();
            if (onEnabledChanged) onEnabledChanged();
        };
        refreshPowerLabel();
        addAndMakeVisible (powerBtn);
    }

    std::function<void()> onEnabledChanged;

    void refreshFromModel()
    {
        powerBtn.setToggleState (enabledGetter(), juce::dontSendNotification);
        refreshPowerLabel();
        repaint();
    }

    // Caller positions knob children inside getContentArea() after resized().
    juce::Rectangle<int> getContentArea() const
    {
        return getLocalBounds().withTrimmedTop (28).reduced (8, 6);
    }

    void paint (juce::Graphics& g) override
    {
        bool on = enabledGetter();
        auto bounds = getLocalBounds().toFloat().reduced (1.5f);

        // Card background
        g.setColour (juce::Colour (0xff232328));
        g.fillRoundedRectangle (bounds, 6.0f);

        // Left accent stripe when enabled
        if (on)
        {
            g.setColour (MidiMorfColors::accent);
            g.fillRect (bounds.withWidth (3.0f));
        }

        // Header bar
        auto header = bounds.withHeight (24.0f);
        g.setColour (juce::Colour (0xff2A2A30));
        g.fillRoundedRectangle (header.withTrimmedLeft (on ? 3.0f : 0.0f), 6.0f);

        g.setColour (on ? MidiMorfColors::textLight : juce::Colour (0xff666666));
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (moduleName, header.reduced (10.0f, 0.0f), juce::Justification::centredLeft);

        // Outer border
        g.setColour (juce::Colour (0xff3A3A40));
        g.drawRoundedRectangle (bounds.reduced (0.5f), 6.0f, 1.0f);

        // Dim the content area when bypassed
        if (! on)
        {
            g.setColour (juce::Colour (0x80000000));
            g.fillRoundedRectangle (bounds.withTrimmedTop (24.0f).reduced (4.0f), 4.0f);
        }
    }

    void resized() override
    {
        powerBtn.setBounds (getWidth() - 44, 4, 36, 18);
    }

private:
    void refreshPowerLabel()
    {
        powerBtn.setButtonText (powerBtn.getToggleState() ? "ON" : "OFF");
    }

    juce::String moduleName;
    std::function<bool()> enabledGetter;
    std::function<void(bool)> enabledSetter;
    juce::TextButton powerBtn;
};

//==============================================================================
// FX tab — three independent modules (Distortion, Delay, Reverb), each with
// its own ON/OFF bypass like Serum 2.
class FxTab : public juce::Component
{
public:
    FxTab (SynthEngine& e, std::function<void()> notify)
        : synth (e), onChange (std::move (notify)),
          distModule ("DISTORTION",
                      [this] { return synth.getDistEnabled(); },
                      [this] (bool on) { synth.setDistEnabled (on); }),
          delayModule ("DELAY",
                       [this] { return synth.getDelayEnabled(); },
                       [this] (bool on) { synth.setDelayEnabled (on); }),
          reverbModule ("REVERB",
                        [this] { return synth.getReverbEnabled(); },
                        [this] (bool on) { synth.setReverbEnabled (on); }),
          dist     ("DRIVE",   0.0f,    1.0f,    e.getDistDrive()),
          dlyTime  ("TIME ms", 5.0f,    2000.0f, e.getDelayTimeMs(), 1.0f, 250.0f),
          dlyFb    ("FB",      0.0f,    0.95f,   e.getDelayFeedback()),
          dlyMix   ("MIX",     0.0f,    1.0f,    e.getDelayMix()),
          revMix   ("MIX",     0.0f,    1.0f,    e.getReverbMix()),
          revSize  ("ROOM",    0.0f,    1.0f,    e.getReverbSize())
    {
        wire (dist.getSlider(),    [this] (float v) { synth.setDistDrive     (v); });
        wire (dlyTime.getSlider(), [this] (float v) { synth.setDelayTimeMs   (v); });
        wire (dlyFb.getSlider(),   [this] (float v) { synth.setDelayFeedback (v); });
        wire (dlyMix.getSlider(),  [this] (float v) { synth.setDelayMix      (v); });
        wire (revMix.getSlider(),  [this] (float v) { synth.setReverbMix     (v); });
        wire (revSize.getSlider(), [this] (float v) { synth.setReverbSize    (v); });

        auto pingChange = [this] { if (onChange) onChange(); };
        distModule.onEnabledChanged   = pingChange;
        delayModule.onEnabledChanged  = pingChange;
        reverbModule.onEnabledChanged = pingChange;

        addAndMakeVisible (distModule);
        addAndMakeVisible (delayModule);
        addAndMakeVisible (reverbModule);

        // Knobs are children of their respective modules so they layout inside
        // the card area and visually belong to that module.
        distModule.addAndMakeVisible (dist);
        delayModule.addAndMakeVisible (dlyTime);
        delayModule.addAndMakeVisible (dlyFb);
        delayModule.addAndMakeVisible (dlyMix);
        reverbModule.addAndMakeVisible (revMix);
        reverbModule.addAndMakeVisible (revSize);
    }

    void resized() override
    {
        if (getWidth() <= 0 || getHeight() <= 0) return;

        auto area = getLocalBounds().reduced (8);

        // Distortion: 1 knob → ~16% of width.
        // Delay: 3 knobs → ~46%.
        // Reverb: 2 knobs → ~36%.
        const int totalW = area.getWidth();
        const int distW  = (int) (totalW * 0.16f);
        const int dlyW   = (int) (totalW * 0.46f);
        const int gap    = 6;

        auto distArea  = area.removeFromLeft (distW);
        area.removeFromLeft (gap);
        auto delayArea = area.removeFromLeft (dlyW);
        area.removeFromLeft (gap);
        auto revArea   = area;

        distModule.setBounds (distArea);
        delayModule.setBounds (delayArea);
        reverbModule.setBounds (revArea);

        // Lay knobs inside each module's content area.
        layoutKnobs (distModule.getContentArea(), { &dist });
        layoutKnobs (delayModule.getContentArea(), { &dlyTime, &dlyFb, &dlyMix });
        layoutKnobs (reverbModule.getContentArea(), { &revMix, &revSize });
    }

    void refresh()
    {
        distModule.refreshFromModel();
        delayModule.refreshFromModel();
        reverbModule.refreshFromModel();
        dist.getSlider().setValue    (synth.getDistDrive(),     juce::dontSendNotification);
        dlyTime.getSlider().setValue (synth.getDelayTimeMs(),   juce::dontSendNotification);
        dlyFb.getSlider().setValue   (synth.getDelayFeedback(), juce::dontSendNotification);
        dlyMix.getSlider().setValue  (synth.getDelayMix(),      juce::dontSendNotification);
        revMix.getSlider().setValue  (synth.getReverbMix(),     juce::dontSendNotification);
        revSize.getSlider().setValue (synth.getReverbSize(),    juce::dontSendNotification);
    }

private:
    void wire (juce::Slider& s, std::function<void(float)> f)
    {
        s.onValueChange = [this, &s, f] { f ((float) s.getValue()); if (onChange) onChange(); };
    }

    // Note: components are laid out inside their parent module's local coord
    // system (which getContentArea() returns relative to that module).
    static void layoutKnobs (juce::Rectangle<int> area, std::initializer_list<juce::Component*> knobs)
    {
        int n = (int) knobs.size();
        if (n == 0) return;
        int w = area.getWidth() / n;
        int i = 0;
        for (auto* c : knobs)
        {
            auto cell = area.removeFromLeft (i == n - 1 ? area.getWidth() : w).reduced (4);
            c->setBounds (cell);
            ++i;
        }
    }

    SynthEngine& synth;
    std::function<void()> onChange;
    FxModule distModule, delayModule, reverbModule;
    Knob dist, dlyTime, dlyFb, dlyMix, revMix, revSize;
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
          routingTab(engine, notify),
          fxTab     (engine, notify)
    {
        // Apply the themed LookAndFeel to this whole tree (knobs, combos, tabs).
        setLookAndFeel (&synthLaf);

        const juce::StringArray names = { "OSC", "ADSR & LFO", "FILTER", "ROUTING", "FX" };
        for (int i = 0; i < 5; ++i)
        {
            // No explicit colours — SynthLookAndFeel paints these.
            auto* b = new juce::TextButton (names[i]);
            b->setRadioGroupId (1001);
            b->setClickingTogglesState (true);
            b->onClick = [this, i] { showTab (i); };
            tabButtons.add (b);
            addAndMakeVisible (b);
        }

        clearButton.setButtonText ("CLEAR");
        clearButton.onClick = [this]
        {
            synth.resetToDefaults();
            refreshAll();
            if (onChange) onChange();
        };
        addAndMakeVisible (clearButton);

        addChildComponent (oscTab);
        addChildComponent (adsrTab);
        addChildComponent (filterTab);
        addChildComponent (routingTab);
        addChildComponent (fxTab);

        // Audition strip — 2-octave horizontal ribbon along the bottom that
        // drives the synth directly (not via EventRouter) so editing always
        // produces sound regardless of WiFi/Local mode.
        auditionStrip.onNoteOn  = [this] (int n, float v) { synth.noteOn  (n, v); };
        auditionStrip.onNoteOff = [this] (int n)          { synth.noteOff (n); };
        addAndMakeVisible (auditionStrip);

        tabButtons[0]->setToggleState (true, juce::dontSendNotification);
        showTab (0);
    }

    ~SynthSettingsLandscapeContent() override
    {
        // Detach the LookAndFeel before this component (and its embedded
        // synthLaf member) is destroyed — prevents children dereferencing a
        // dead LAF during their own destruction.
        setLookAndFeel (nullptr);
    }

    void showTab (int idx)
    {
        currentTab = idx;
        oscTab.setVisible    (idx == 0);
        adsrTab.setVisible   (idx == 1);
        filterTab.setVisible (idx == 2);
        routingTab.setVisible(idx == 3);
        fxTab.setVisible     (idx == 4);
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();

        // 1. Theme substrate — sits behind the translucent acrylic so the
        //    panel reads cleanly regardless of what's behind.
        {
            juce::ColourGradient base (
                MidiMorfColors::panelSubstrateTop,    0.0f, 0.0f,
                MidiMorfColors::panelSubstrateBottom, 0.0f, bounds.getHeight(),
                false);
            g.setGradientFill (base);
            g.fillAll();
        }

        // 2. Translucent themed acrylic — main plastic body. Two-stop gradient
        //    with alpha < 1.0 so the substrate below shows through.
        juce::ColourGradient plastic (
            MidiMorfColors::panelAcrylicTop,    0.0f, 0.0f,
            MidiMorfColors::panelAcrylicBottom, 0.0f, bounds.getHeight(),
            false);
        g.setGradientFill (plastic);
        g.fillAll();

        // 3. Glassy top sheen — diagonal highlight to suggest a curved
        //    plastic surface catching light from the top.
        auto sheen = bounds.withHeight (bounds.getHeight() * 0.22f);
        juce::ColourGradient sg (juce::Colour (0x55FFFFFF), sheen.getX(), sheen.getY(),
                                  juce::Colour (0x00FFFFFF), sheen.getX(), sheen.getBottom(),
                                  false);
        g.setGradientFill (sg);
        g.fillRect (sheen);

        // 4. Subtle bottom shadow — gives the panel depth.
        auto shadow = bounds.withTrimmedTop (bounds.getHeight() * 0.65f);
        juce::ColourGradient sh (juce::Colour (0x00000000), shadow.getX(), shadow.getY(),
                                  juce::Colour (0x44000000), shadow.getX(), shadow.getBottom(),
                                  false);
        g.setGradientFill (sh);
        g.fillRect (shadow);

        // 5. Soft themed edge glow — completes the acrylic vibe.
        g.setColour (MidiMorfColors::panelEdgeGlow);
        g.drawRoundedRectangle (bounds.reduced (1.0f), 8.0f, 1.5f);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (8);

        // Tab bar at the top — tabs share the width except for a CLEAR button
        // pinned to the right.
        auto bar = area.removeFromTop (36);
        const int clearW = 64;
        clearButton.setBounds (bar.removeFromRight (clearW).reduced (3));
        bar.removeFromRight (4);

        int n = tabButtons.size();
        int w = bar.getWidth() / n;
        for (int i = 0; i < n; ++i)
        {
            auto cell = bar.removeFromLeft (i == n - 1 ? bar.getWidth() : w).reduced (3);
            tabButtons[i]->setBounds (cell);
        }
        area.removeFromTop (8);

        // Audition strip at the bottom (2-octave ribbon)
        auto auditionArea = area.removeFromBottom (42);
        auditionStrip.setBounds (auditionArea.reduced (2));
        area.removeFromBottom (6);

        // Tab content fills the middle
        oscTab.setBounds (area);
        adsrTab.setBounds (area);
        filterTab.setBounds (area);
        routingTab.setBounds (area);
        fxTab.setBounds (area);
    }

    void refreshAll()
    {
        oscTab.refresh();
        adsrTab.refresh();
        filterTab.refresh();
        routingTab.refresh();
        fxTab.refresh();
    }

private:
    SynthEngine& synth;
    std::function<void()> onChange;

    juce::OwnedArray<juce::TextButton> tabButtons;
    juce::TextButton clearButton;
    int currentTab = 0;

    OscTab     oscTab;
    AdsrLfoTab adsrTab;
    FilterTab  filterTab;
    RoutingTab routingTab;
    FxTab      fxTab;

    AuditionStrip    auditionStrip;
    SynthLookAndFeel synthLaf;
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
        content.setBounds (getLocalBounds());
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
        props.setValue ("synth_mainWave",   synth.getMainWaveform());
        props.setValue ("synth_osc1Level",  synth.getOsc1Level());
        props.setValue ("synth_osc2Wave",   synth.getOsc2Waveform());
        props.setValue ("synth_osc2Level",  synth.getOsc2Level());
        props.setValue ("synth_osc2Detune", synth.getOsc2DetuneCents());
        props.setValue ("synth_subLevel",   synth.getSubLevel());
        props.setValue ("synth_drive",      synth.getDrive());
        props.setValue ("synth_gain",       synth.getGain());
        props.setValue ("synth_reverbMix",  synth.getReverbMix());
        props.setValue ("synth_reverbSize", synth.getReverbSize());

        props.setValue ("synth_distDrive",     synth.getDistDrive());
        props.setValue ("synth_delayTimeMs",   synth.getDelayTimeMs());
        props.setValue ("synth_delayFeedback", synth.getDelayFeedback());
        props.setValue ("synth_delayMix",      synth.getDelayMix());
        props.setValue ("synth_distOn",        synth.getDistEnabled());
        props.setValue ("synth_delayOn",       synth.getDelayEnabled());
        props.setValue ("synth_reverbOn",      synth.getReverbEnabled());

        // LFO
        props.setValue ("synth_lfoRate",  synth.getLFORate());
        props.setValue ("synth_lfoDepth", synth.getLFODepth());
        props.setValue ("synth_lfoWave",  synth.getLFOWaveform());
        // Routing (source + dest + amount per slot)
        for (int i = 0; i < SynthEngine::NumRoutingSlots; ++i)
        {
            props.setValue ("synth_routeSrc_"    + juce::String (i), synth.getRoutingSource (i));
            props.setValue ("synth_routeDest_"   + juce::String (i), synth.getRoutingDest   (i));
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
        synth.setMainWaveform    (props.getIntValue    ("synth_mainWave",  synth.getMainWaveform()));
        synth.setOsc1Level       ((float) props.getDoubleValue ("synth_osc1Level",  synth.getOsc1Level()));
        synth.setOsc2Waveform    (props.getIntValue    ("synth_osc2Wave",  synth.getOsc2Waveform()));
        synth.setOsc2Level       ((float) props.getDoubleValue ("synth_osc2Level",  synth.getOsc2Level()));
        synth.setOsc2DetuneCents ((float) props.getDoubleValue ("synth_osc2Detune", synth.getOsc2DetuneCents()));
        synth.setSubLevel        ((float) props.getDoubleValue ("synth_subLevel",   synth.getSubLevel()));
        synth.setDrive           ((float) props.getDoubleValue ("synth_drive",      synth.getDrive()));
        synth.setGain            ((float) props.getDoubleValue ("synth_gain",       synth.getGain()));
        synth.setReverbMix       ((float) props.getDoubleValue ("synth_reverbMix",  synth.getReverbMix()));
        synth.setReverbSize      ((float) props.getDoubleValue ("synth_reverbSize", synth.getReverbSize()));

        synth.setDistDrive       ((float) props.getDoubleValue ("synth_distDrive",     synth.getDistDrive()));
        synth.setDelayTimeMs     ((float) props.getDoubleValue ("synth_delayTimeMs",   synth.getDelayTimeMs()));
        synth.setDelayFeedback   ((float) props.getDoubleValue ("synth_delayFeedback", synth.getDelayFeedback()));
        synth.setDelayMix        ((float) props.getDoubleValue ("synth_delayMix",      synth.getDelayMix()));
        synth.setDistEnabled     (props.getBoolValue ("synth_distOn",   synth.getDistEnabled()));
        synth.setDelayEnabled    (props.getBoolValue ("synth_delayOn",  synth.getDelayEnabled()));
        synth.setReverbEnabled   (props.getBoolValue ("synth_reverbOn", synth.getReverbEnabled()));

        synth.setLFORate     ((float) props.getDoubleValue ("synth_lfoRate",  synth.getLFORate()));
        synth.setLFODepth    ((float) props.getDoubleValue ("synth_lfoDepth", synth.getLFODepth()));
        synth.setLFOWaveform (props.getIntValue ("synth_lfoWave", synth.getLFOWaveform()));
        for (int i = 0; i < SynthEngine::NumRoutingSlots; ++i)
        {
            synth.setRoutingSource (i, props.getIntValue ("synth_routeSrc_"    + juce::String (i), synth.getRoutingSource (i)));
            synth.setRoutingDest   (i, props.getIntValue ("synth_routeDest_"   + juce::String (i), synth.getRoutingDest   (i)));
            synth.setRoutingAmount (i, (float) props.getDoubleValue ("synth_routeAmount_" + juce::String (i), synth.getRoutingAmount (i)));
        }

        content.refreshAll();
    }

private:
    SynthEngine& synth;
    SynthSettingsLandscapeContent content;
};
