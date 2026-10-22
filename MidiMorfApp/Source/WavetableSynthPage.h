#pragma once

#include <JuceHeader.h>
#include "WavetableEngine.h"
#include "SynthSettingsPage.h"   // Knob, LabeledCombo, SectionCard, AuditionStrip
#include "SynthLookAndFeel.h"
#include "MidiMorfColors.h"

//==============================================================================
// WaveDisplay — the oscillator's table drawn as a stack of frames receding
// up and to the right, with the frame at the live (modulated) position drawn
// bright on top. Polls the engine at 30 Hz so tilt / LFO scanning is visible.
class WaveDisplay : public juce::Component, private juce::Timer
{
public:
    WaveDisplay (WavetableEngine& e, int oscIndex) : engine (e), osc (oscIndex)
    {
        startTimerHz (30);
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().reduced (1.5f);
        g.setColour (juce::Colour (0xff141417));
        g.fillRoundedRectangle (bounds, 5.0f);

        const auto* table = engine.getTable (osc);
        if (table != nullptr)
        {
            auto plot = bounds.reduced (8.0f, 6.0f);
            const int frames = table->getNumFrames();
            const float depthX = plot.getWidth() * 0.18f;
            const float depthY = plot.getHeight() * 0.30f;
            auto front = plot.withTrimmedRight (depthX).withTrimmedTop (depthY);

            // Background stack: up to 12 frames, back to front.
            const int shown = juce::jmin (frames, 12);
            for (int k = shown - 1; k >= 0; --k)
            {
                float t = shown > 1 ? (float) k / (float) (shown - 1) : 0.0f;
                int frame = (int) std::round (t * (float) (frames - 1));
                auto r = front.translated (t * depthX, -t * depthY);
                g.setColour (MidiMorfColors::textLight.withAlpha (0.10f + 0.08f * (1.0f - t)));
                g.strokePath (framePath (table->getDisplayFrame (frame), r), juce::PathStrokeType (1.0f));
            }

            // Live frame, interpolated the same way the voice reads it.
            const float pos = juce::jlimit (0.0f, 1.0f, lastPos);
            const float fp = pos * (float) (frames - 1);
            const int f0 = juce::jlimit (0, frames - 1, (int) fp);
            const int f1 = juce::jmin (f0 + 1, frames - 1);
            const float ff = fp - (float) f0;
            const float* a = table->getDisplayFrame (f0);
            const float* b = table->getDisplayFrame (f1);
            for (int i = 0; i < NumPoints; ++i)
            {
                int idx = i * Wavetable::FrameSize / NumPoints;
                live[i] = a[idx] + ff * (b[idx] - a[idx]);
            }

            auto r = front.translated (pos * depthX, -pos * depthY);
            juce::Path p;
            for (int i = 0; i < NumPoints; ++i)
            {
                float x = r.getX() + r.getWidth() * (float) i / (float) (NumPoints - 1);
                float y = r.getCentreY() - live[i] * r.getHeight() * 0.45f;
                if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
            }
            g.setColour (MidiMorfColors::accent.withAlpha (0.30f));
            g.strokePath (p, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved));
            g.setColour (MidiMorfColors::accent);
            g.strokePath (p, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved));

            g.setColour (MidiMorfColors::textLight.withAlpha (0.55f));
            g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
            g.drawText (juce::String (f0 + 1) + "/" + juce::String (frames),
                        bounds.reduced (8.0f, 4.0f), juce::Justification::topLeft);
        }

        g.setColour (juce::Colour (0xff3A3A40));
        g.drawRoundedRectangle (bounds, 5.0f, 1.0f);
    }

private:
    static constexpr int NumPoints = 128;

    static juce::Path framePath (const float* frame, juce::Rectangle<float> r)
    {
        juce::Path p;
        for (int i = 0; i < NumPoints; ++i)
        {
            float x = r.getX() + r.getWidth() * (float) i / (float) (NumPoints - 1);
            float y = r.getCentreY() - frame[i * Wavetable::FrameSize / NumPoints] * r.getHeight() * 0.45f;
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        return p;
    }

    void timerCallback() override
    {
        const auto* table = engine.getTable (osc);
        const float pos = engine.getEffectivePosition (osc);
        if (table != lastTable || std::abs (pos - lastPos) > 0.002f)
        {
            lastTable = table;
            lastPos = pos;
            repaint();
        }
    }

    WavetableEngine& engine;
    const int osc;
    const Wavetable* lastTable = nullptr;
    float lastPos = -1.0f;
    float live[NumPoints] {};
};

//==============================================================================
// OscCard — one wavetable oscillator: table picker (built-ins + Import),
// wave display, POS / LEVEL / SEMI / FINE.
class OscCard : public SectionCard
{
public:
    OscCard (const juce::String& name, WavetableEngine& e, int oscIndex, std::function<void()> notify)
        : SectionCard (name), engine (e), osc (oscIndex), onChange (std::move (notify)),
          display (e, oscIndex),
          pos   ("POS",   0.0f,   1.0f,  e.getOscPosition (oscIndex)),
          level ("LEVEL", 0.0f,   1.0f,  e.getOscLevel (oscIndex)),
          semi  ("SEMI", -24.0f, 24.0f, (float) e.getOscSemitones (oscIndex), 1.0f),
          fine  ("FINE", -100.0f, 100.0f, e.getOscFineCents (oscIndex), 0.1f)
    {
        tableCombo.onChange = [this] { tableChosen(); };
        addAndMakeVisible (tableCombo);
        addAndMakeVisible (display);

        wire (pos,   [this] (float v) { engine.setOscPosition (osc, v); });
        wire (level, [this] (float v) { engine.setOscLevel (osc, v); });
        wire (semi,  [this] (float v) { engine.setOscSemitones (osc, (int) v); });
        wire (fine,  [this] (float v) { engine.setOscFineCents (osc, v); });
        for (auto* k : { &pos, &level, &semi, &fine })
            addAndMakeVisible (*k);

        refresh();
    }

    void resized() override
    {
        auto a = getContentArea();
        tableCombo.setBounds (a.removeFromTop (28));
        a.removeFromTop (6);
        auto knobs = a.removeFromBottom (juce::jlimit (78, 130, a.getHeight() / 3));
        display.setBounds (a.reduced (0, 2));

        const int w = knobs.getWidth() / 4;
        for (auto* k : { &pos, &level, &semi, &fine })
            k->setBounds (knobs.removeFromLeft (w).reduced (2));
    }

    void refresh()
    {
        rebuildCombo();
        pos.getSlider().setValue   (engine.getOscPosition (osc),          juce::dontSendNotification);
        level.getSlider().setValue (engine.getOscLevel (osc),             juce::dontSendNotification);
        semi.getSlider().setValue  ((double) engine.getOscSemitones (osc), juce::dontSendNotification);
        fine.getSlider().setValue  (engine.getOscFineCents (osc),         juce::dontSendNotification);
    }

private:
    static constexpr int ImportedId = 900;
    static constexpr int ImportId   = 901;

    void rebuildCombo()
    {
        tableCombo.clear (juce::dontSendNotification);
        tableCombo.addItemList (WavetableLibrary::builtInNames(), 1);
        tableCombo.addSeparator();

        auto imported = engine.getImportedFile (osc);
        if (engine.getBuiltInIndex (osc) < 0 && imported != juce::File())
            tableCombo.addItem (imported.getFileNameWithoutExtension(), ImportedId);
        tableCombo.addItem ("Import .wav...", ImportId);

        tableCombo.setSelectedId (engine.getBuiltInIndex (osc) >= 0 ? engine.getBuiltInIndex (osc) + 1
                                                                    : ImportedId,
                                  juce::dontSendNotification);
    }

    void tableChosen()
    {
        const int id = tableCombo.getSelectedId();
        if (id == ImportId)
        {
            rebuildCombo(); // put the selection back while the picker is up
            launchImport();
            return;
        }
        if (id >= 1 && id <= WavetableLibrary::builtInNames().size())
        {
            engine.setBuiltInTable (osc, id - 1);
            rebuildCombo();
            if (onChange) onChange();
        }
    }

    // iOS hands back a security-scoped URL, so the file is copied into the
    // app's Documents/Wavetables folder first. That copy is what the patch
    // remembers, so it still loads on the next launch.
    void launchImport()
    {
        chooser = std::make_unique<juce::FileChooser> ("Import wavetable",
                                                       juce::File(), "*.wav");
        juce::Component::SafePointer<OscCard> safe (this);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [safe] (const juce::FileChooser& fc)
            {
                if (safe == nullptr) return;
                auto url = fc.getURLResult();
                if (url.isEmpty()) return;

                auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                               .getChildFile ("Wavetables");
                dir.createDirectory();
                auto name = url.getFileName().isNotEmpty() ? url.getFileName() : juce::String ("Imported.wav");
                auto dest = dir.getChildFile (juce::URL::removeEscapeChars (name));

                bool copied = false;
                if (auto in = url.createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)))
                {
                    dest.deleteFile();
                    juce::FileOutputStream out (dest);
                    copied = out.openedOk() && out.writeFromInputStream (*in, -1) > 0;
                }

                juce::String error;
                if (! copied)
                    error = "Couldn't read " + name;
                else if (safe->engine.importTable (safe->osc, dest, error))
                {
                    safe->rebuildCombo();
                    if (safe->onChange) safe->onChange();
                    return;
                }

                juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                                  .withIconType (juce::MessageBoxIconType::WarningIcon)
                                                  .withTitle ("Import failed")
                                                  .withMessage (error)
                                                  .withButton ("OK"),
                                              nullptr);
            });
    }

    void wire (Knob& k, std::function<void (float)> f)
    {
        auto& s = k.getSlider();
        s.onValueChange = [this, &s, f]
        {
            f ((float) s.getValue());
            if (onChange) onChange();
        };
    }

    WavetableEngine& engine;
    const int osc;
    std::function<void()> onChange;
    juce::ComboBox tableCombo;
    WaveDisplay display;
    Knob pos, level, semi, fine;
    std::unique_ptr<juce::FileChooser> chooser;
};

//==============================================================================
// WtTab — shared plumbing for the wavetable page's tabs: every control writes
// straight to the engine, then pings onChange so the patch gets saved.
class WtTab : public juce::Component
{
public:
    WtTab (WavetableEngine& e, std::function<void()> notify)
        : engine (e), onChange (std::move (notify)) {}

    virtual void refresh() = 0;

protected:
    void wire (juce::Slider& s, std::function<void (float)> f)
    {
        s.onValueChange = [this, &s, f]
        {
            f ((float) s.getValue());
            if (onChange) onChange();
        };
    }
    void wire (Knob& k, std::function<void (float)> f)  { wire (k.getSlider(), std::move (f)); }
    void wire (Fader& k, std::function<void (float)> f) { wire (k.getSlider(), std::move (f)); }

    void changed() { if (onChange) onChange(); }

    static void layoutInRow (juce::Rectangle<int> row, std::initializer_list<juce::Component*> components,
                             int pad = 4)
    {
        const int n = (int) components.size();
        if (n == 0) return;
        const int w = row.getWidth() / n;
        int i = 0;
        for (auto* c : components)
            c->setBounds (row.removeFromLeft (++i == n ? row.getWidth() : w).reduced (pad));
    }

    WavetableEngine& engine;
    std::function<void()> onChange;
};

//==============================================================================
// OSC tab — OSC A | OSC B | VOICE · FILTER. The filter lives here rather than
// on its own tab: on a wavetable synth the oscillators and the filter are
// what you tweak together.
class WtOscTab : public WtTab
{
public:
    WtOscTab (WavetableEngine& e, std::function<void()> notify)
        : WtTab (e, notify),
          oscA ("OSC A", e, 0, notify),
          oscB ("OSC B", e, 1, notify),
          voiceCard ("VOICE / FILTER"),
          unison  ("UNISON", 1.0f, (float) WavetableVoice::MaxUnison, (float) e.getUnison(), 1.0f),
          detune  ("DETUNE", 0.0f, 50.0f, e.getUnisonDetune(), 0.1f),
          sub     ("SUB",    0.0f, 1.0f,  e.getSubLevel()),
          gain    ("GAIN",   0.01f, 0.4f, e.getGain()),
          cutoff  ("CUTOFF", 20.0f, 20000.0f, e.getFilterCutoff(), 1.0f, 1000.0f),
          reso    ("RESO",   0.0f, 4.0f,  e.getFilterResonance()),
          envAmt  ("ENV",    0.0f, 10000.0f, e.getFilterEnvAmount(), 1.0f, 2000.0f),
          envPos  ("ENV>POS", -1.0f, 1.0f, e.getEnvToPosition())
    {
        addAndMakeVisible (oscA);
        addAndMakeVisible (oscB);
        addAndMakeVisible (voiceCard);

        wire (unison, [this] (float v) { engine.setUnison ((int) v); });
        wire (detune, [this] (float v) { engine.setUnisonDetune (v); });
        wire (sub,    [this] (float v) { engine.setSubLevel (v); });
        wire (gain,   [this] (float v) { engine.setGain (v); });
        wire (cutoff, [this] (float v) { engine.setFilterCutoff (v); });
        wire (reso,   [this] (float v) { engine.setFilterResonance (v); });
        wire (envAmt, [this] (float v) { engine.setFilterEnvAmount (v); });
        wire (envPos, [this] (float v) { engine.setEnvToPosition (v); });
        for (auto* k : knobs())
            voiceCard.addAndMakeVisible (*k);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        const int gap = 6;
        const int w = (area.getWidth() - 2 * gap) / 3;
        oscA.setBounds (area.removeFromLeft (w));       area.removeFromLeft (gap);
        oscB.setBounds (area.removeFromLeft (w));       area.removeFromLeft (gap);
        voiceCard.setBounds (area);

        // 4 × 2 knob grid inside the voice card.
        auto c = voiceCard.getContentArea();
        auto top = c.removeFromTop (c.getHeight() / 2);
        layoutInRow (top, { &unison, &detune, &sub, &gain }, 2);
        layoutInRow (c,   { &cutoff, &reso, &envAmt, &envPos }, 2);
    }

    void refresh() override
    {
        oscA.refresh();
        oscB.refresh();
        unison.getSlider().setValue ((double) engine.getUnison(),   juce::dontSendNotification);
        detune.getSlider().setValue (engine.getUnisonDetune(),      juce::dontSendNotification);
        sub.getSlider().setValue    (engine.getSubLevel(),          juce::dontSendNotification);
        gain.getSlider().setValue   (engine.getGain(),              juce::dontSendNotification);
        cutoff.getSlider().setValue (engine.getFilterCutoff(),      juce::dontSendNotification);
        reso.getSlider().setValue   (engine.getFilterResonance(),   juce::dontSendNotification);
        envAmt.getSlider().setValue (engine.getFilterEnvAmount(),   juce::dontSendNotification);
        envPos.getSlider().setValue (engine.getEnvToPosition(),     juce::dontSendNotification);
    }

private:
    std::vector<Knob*> knobs() { return { &unison, &detune, &sub, &gain, &cutoff, &reso, &envAmt, &envPos }; }

    OscCard oscA, oscB;
    SectionCard voiceCard;
    Knob unison, detune, sub, gain, cutoff, reso, envAmt, envPos;
};

//==============================================================================
// ENV & LFO tab — AMP envelope | FILTER · POS envelope | LFO.
// The second envelope drives both the filter (ENV on the OSC tab) and the
// table position (ENV>POS), so it's named for both.
class WtEnvLfoTab : public WtTab
{
public:
    WtEnvLfoTab (WavetableEngine& e, std::function<void()> notify)
        : WtTab (e, notify),
          ampCard ("AMP ENV"), modCard ("FILTER / POS ENV"), lfoCard ("LFO"),
          aA ("A", 0.001f, 2.0f, 0.01f, 0.001f, 0.2f), aD ("D", 0.01f, 2.0f, 0.4f, 0.01f, 0.3f),
          aS ("S", 0.0f, 1.0f, 0.7f),                   aR ("R", 0.01f, 3.0f, 0.6f, 0.01f, 0.5f),
          fA ("A", 0.001f, 2.0f, 0.01f, 0.001f, 0.2f), fD ("D", 0.01f, 2.0f, 0.5f, 0.01f, 0.3f),
          fS ("S", 0.0f, 1.0f, 0.3f),                   fR ("R", 0.01f, 3.0f, 0.5f, 0.01f, 0.5f),
          rate  ("RATE",  0.05f, 20.0f, e.getLFORate(), 0.01f, 2.0f),
          depth ("DEPTH", 0.0f,  1.0f,  e.getLFODepth()),
          wave  ("WAVE")
    {
        for (auto* f : { &aA, &aD, &aS, &aR })
        {
            wire (*f, [this] (float) { pushAmp(); });
            ampCard.addAndMakeVisible (*f);
        }
        for (auto* f : { &fA, &fD, &fS, &fR })
        {
            wire (*f, [this] (float) { pushFilter(); });
            modCard.addAndMakeVisible (*f);
        }

        wire (rate,  [this] (float v) { engine.setLFORate (v); });
        wire (depth, [this] (float v) { engine.setLFODepth (v); });
        wave.getCombo().addItemList ({ "Sine", "Triangle", "Saw", "Square" }, 1);
        wave.getCombo().onChange = [this]
        {
            engine.setLFOWaveform (wave.getCombo().getSelectedId() - 1);
            changed();
        };
        lfoCard.addAndMakeVisible (rate);
        lfoCard.addAndMakeVisible (depth);
        lfoCard.addAndMakeVisible (wave);

        addAndMakeVisible (ampCard);
        addAndMakeVisible (modCard);
        addAndMakeVisible (lfoCard);
        refresh();
    }

    void resized() override
    {
        auto area = getLocalBounds();
        const int gap = 6;
        const int w = (int) (area.getWidth() * 0.36f);
        ampCard.setBounds (area.removeFromLeft (w)); area.removeFromLeft (gap);
        modCard.setBounds (area.removeFromLeft (w)); area.removeFromLeft (gap);
        lfoCard.setBounds (area);

        layoutInRow (ampCard.getContentArea(), { &aA, &aD, &aS, &aR });
        layoutInRow (modCard.getContentArea(), { &fA, &fD, &fS, &fR });

        auto l = lfoCard.getContentArea();
        wave.setBounds (l.removeFromBottom (56));
        layoutInRow (l, { &rate, &depth });
    }

    void refresh() override
    {
        float a, d, s, r;
        engine.getAmpADSR (a, d, s, r);
        setFaders ({ &aA, &aD, &aS, &aR }, a, d, s, r);
        engine.getFilterADSR (a, d, s, r);
        setFaders ({ &fA, &fD, &fS, &fR }, a, d, s, r);
        rate.getSlider().setValue  (engine.getLFORate(),  juce::dontSendNotification);
        depth.getSlider().setValue (engine.getLFODepth(), juce::dontSendNotification);
        wave.getCombo().setSelectedId (engine.getLFOWaveform() + 1, juce::dontSendNotification);
    }

private:
    static void setFaders (std::initializer_list<Fader*> faders, float a, float d, float s, float r)
    {
        const float v[] = { a, d, s, r };
        int i = 0;
        for (auto* f : faders)
            f->getSlider().setValue (v[i++], juce::dontSendNotification);
    }

    static float val (Fader& f) { return (float) f.getSlider().getValue(); }
    void pushAmp()    { engine.setAmpADSR    (val (aA), val (aD), val (aS), val (aR)); }
    void pushFilter() { engine.setFilterADSR (val (fA), val (fD), val (fS), val (fR)); }

    SectionCard ampCard, modCard, lfoCard;
    Fader aA, aD, aS, aR, fA, fD, fS, fR;
    Knob rate, depth;
    LabeledCombo wave;
};

//==============================================================================
// ROUTING tab — 4 slots of [source] → [destination] [depth], same model as
// the classic synth, with the two table positions as destinations.
class WtRoutingTab : public WtTab
{
public:
    WtRoutingTab (WavetableEngine& e, std::function<void()> notify)
        : WtTab (e, notify)
    {
        header.setText ("MODULATION ROUTING", juce::dontSendNotification);
        header.setColour (juce::Label::textColourId, MidiMorfColors::accent);
        header.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        addAndMakeVisible (header);

        for (int i = 0; i < WavetableEngine::NumRoutingSlots; ++i)
        {
            auto& s = slots[i];
            s.number.setText (juce::String (i + 1), juce::dontSendNotification);
            s.number.setFont (juce::FontOptions (12.0f, juce::Font::bold));
            s.number.setJustificationType (juce::Justification::centred);
            addAndMakeVisible (s.number);

            s.source.addItemList (WavetableEngine::sourceNames(), 1);
            s.source.onChange = [this, i]
            {
                engine.setRoutingSource (i, slots[i].source.getSelectedId() - 1);
                changed();
            };
            addAndMakeVisible (s.source);

            s.dest.addItemList (WavetableEngine::destNames(), 1);
            s.dest.onChange = [this, i]
            {
                engine.setRoutingDest (i, slots[i].dest.getSelectedId() - 1);
                changed();
            };
            addAndMakeVisible (s.dest);

            s.amount.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            s.amount.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 50, 16);
            s.amount.setRange (-1.0, 1.0, 0.01);
            s.amount.setColour (juce::Slider::rotarySliderFillColourId,    MidiMorfColors::accent);
            s.amount.setColour (juce::Slider::rotarySliderOutlineColourId, MidiMorfColors::padBorder);
            s.amount.setColour (juce::Slider::thumbColourId,               MidiMorfColors::textLight);
            s.amount.setColour (juce::Slider::textBoxTextColourId,         MidiMorfColors::textLight);
            s.amount.setColour (juce::Slider::textBoxBackgroundColourId,   juce::Colours::transparentBlack);
            s.amount.setColour (juce::Slider::textBoxOutlineColourId,      juce::Colours::transparentBlack);
            wire (s.amount, [this, i] (float v) { engine.setRoutingAmount (i, v); });
            addAndMakeVisible (s.amount);
        }
        refresh();
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (4);
        header.setBounds (area.removeFromTop (22));
        area.removeFromTop (4);

        const int slotH = area.getHeight() / WavetableEngine::NumRoutingSlots;
        for (auto& s : slots)
        {
            auto row = area.removeFromTop (slotH).reduced (2, 3);
            s.number.setBounds (row.removeFromLeft (22));
            row.removeFromLeft (4);
            s.amount.setBounds (row.removeFromRight (juce::jmin (110, row.getHeight() + 50)));
            row.removeFromRight (6);
            const int half = row.getWidth() / 2;
            s.source.setBounds (row.removeFromLeft (half - 4).withSizeKeepingCentre (half - 4, 28));
            row.removeFromLeft (6);
            s.dest.setBounds (row.withSizeKeepingCentre (row.getWidth(), 28));
        }
    }

    void refresh() override
    {
        for (int i = 0; i < WavetableEngine::NumRoutingSlots; ++i)
        {
            slots[i].source.setSelectedId (engine.getRoutingSource (i) + 1, juce::dontSendNotification);
            slots[i].dest.setSelectedId   (engine.getRoutingDest (i) + 1,   juce::dontSendNotification);
            slots[i].amount.setValue      (engine.getRoutingAmount (i),     juce::dontSendNotification);
        }
    }

private:
    struct Slot
    {
        juce::Label    number;
        juce::ComboBox source, dest;
        juce::Slider   amount;
    };

    juce::Label header;
    Slot slots[WavetableEngine::NumRoutingSlots];
};

//==============================================================================
// FX tab — DISTORTION | DELAY | REVERB on MasterFx, each with ON/OFF.
// DISTORTION also carries the per-voice PRE drive (before the filter) next to
// the bus's POST drive, so both kinds of dirt sit in one place.
class WtFxTab : public WtTab
{
public:
    WtFxTab (WavetableEngine& e, std::function<void()> notify)
        : WtTab (e, notify),
          distModule   ("DISTORTION", [this] { return fx().distEnabled; },   [this] (bool on) { fx().distEnabled = on; }),
          delayModule  ("DELAY",      [this] { return fx().delayEnabled; },  [this] (bool on) { fx().delayEnabled = on; }),
          reverbModule ("REVERB",     [this] { return fx().reverbEnabled; }, [this] (bool on) { fx().reverbEnabled = on; }),
          pre     ("PRE",     0.0f, 1.0f,    e.getDrive()),
          post    ("POST",    0.0f, 1.0f,    e.getFx().distDrive),
          dlyTime ("TIME ms", 5.0f, 2000.0f, e.getFx().delayTimeMs, 1.0f, 250.0f),
          dlyFb   ("FB",      0.0f, 0.95f,   e.getFx().delayFeedback),
          dlyMix  ("MIX",     0.0f, 1.0f,    e.getFx().delayMix),
          revMix  ("MIX",     0.0f, 1.0f,    e.getFx().reverbWet),
          revSize ("ROOM",    0.0f, 1.0f,    e.getFx().reverbSize)
    {
        wire (pre,     [this] (float v) { engine.setDrive (v); });
        wire (post,    [this] (float v) { fx().distDrive     = v; });
        wire (dlyTime, [this] (float v) { fx().delayTimeMs   = v; });
        wire (dlyFb,   [this] (float v) { fx().delayFeedback = v; });
        wire (dlyMix,  [this] (float v) { fx().delayMix      = v; });
        wire (revMix,  [this] (float v) { fx().reverbWet     = v; });
        wire (revSize, [this] (float v) { fx().reverbSize    = v; });

        for (auto* m : { &distModule, &delayModule, &reverbModule })
        {
            m->onEnabledChanged = [this] { changed(); };
            addAndMakeVisible (*m);
        }

        distModule.addAndMakeVisible (pre);
        distModule.addAndMakeVisible (post);
        delayModule.addAndMakeVisible (dlyTime);
        delayModule.addAndMakeVisible (dlyFb);
        delayModule.addAndMakeVisible (dlyMix);
        reverbModule.addAndMakeVisible (revMix);
        reverbModule.addAndMakeVisible (revSize);
    }

    void resized() override
    {
        // Widths follow knob count: 2 · 3 · 2.
        auto area = getLocalBounds();
        const int gap = 6;
        const int unit = (area.getWidth() - 2 * gap) / 7;
        distModule.setBounds (area.removeFromLeft (unit * 2));  area.removeFromLeft (gap);
        delayModule.setBounds (area.removeFromLeft (unit * 3)); area.removeFromLeft (gap);
        reverbModule.setBounds (area);

        layoutInRow (distModule.getContentArea(),   { &pre, &post });
        layoutInRow (delayModule.getContentArea(),  { &dlyTime, &dlyFb, &dlyMix });
        layoutInRow (reverbModule.getContentArea(), { &revMix, &revSize });
    }

    void refresh() override
    {
        distModule.refreshFromModel();
        delayModule.refreshFromModel();
        reverbModule.refreshFromModel();
        pre.getSlider().setValue     (engine.getDrive(),    juce::dontSendNotification);
        post.getSlider().setValue    (fx().distDrive,       juce::dontSendNotification);
        dlyTime.getSlider().setValue (fx().delayTimeMs,     juce::dontSendNotification);
        dlyFb.getSlider().setValue   (fx().delayFeedback,   juce::dontSendNotification);
        dlyMix.getSlider().setValue  (fx().delayMix,        juce::dontSendNotification);
        revMix.getSlider().setValue  (fx().reverbWet,       juce::dontSendNotification);
        revSize.getSlider().setValue (fx().reverbSize,      juce::dontSendNotification);
    }

private:
    MasterFx& fx() { return engine.getFx(); }

    FxModule distModule, delayModule, reverbModule;
    Knob pre, post, dlyTime, dlyFb, dlyMix, revMix, revSize;
};

//==============================================================================
// Landscape content: title + tab bar + CLEAR, the active tab, audition ribbon.
class WavetableSynthContent : public juce::Component
{
public:
    WavetableSynthContent (WavetableEngine& e, std::function<void()> notify)
        : engine (e), onChange (notify),
          oscTab (e, notify), envTab (e, notify), routingTab (e, notify), fxTab (e, notify)
    {
        setLookAndFeel (&laf);

        title.setText ("WAVETABLE", juce::dontSendNotification);
        // Accent, not textLight: it sits straight on the panel, which is light
        // in some themes and dark in others.
        title.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        title.setColour (juce::Label::textColourId, MidiMorfColors::accent);
        addAndMakeVisible (title);

        const juce::StringArray names { "OSC", "ENV & LFO", "ROUTING", "FX" };
        for (int i = 0; i < names.size(); ++i)
        {
            auto* b = tabButtons.add (new juce::TextButton (names[i]));
            b->setRadioGroupId (1002);
            b->setClickingTogglesState (true);
            b->onClick = [this, i] { showTab (i); };
            addAndMakeVisible (b);
        }

        clearButton.setButtonText ("CLEAR");
        clearButton.onClick = [this]
        {
            engine.resetToDefaults();
            refreshAll();
            if (onChange) onChange();
        };
        addAndMakeVisible (clearButton);

        for (auto* t : tabs())
            addChildComponent (t);

        // Plays this engine directly, whatever mode / engine is active, so
        // edits are always audible.
        auditionStrip.onNoteOn  = [this] (int n, float v) { engine.noteOn (n, v); };
        auditionStrip.onNoteOff = [this] (int n)          { engine.noteOff (n); };
        addAndMakeVisible (auditionStrip);

        tabButtons[0]->setToggleState (true, juce::dontSendNotification);
        showTab (0);
    }

    ~WavetableSynthContent() override
    {
        setLookAndFeel (nullptr);
    }

    void showTab (int idx)
    {
        auto ts = tabs();
        for (int i = 0; i < (int) ts.size(); ++i)
            ts[(size_t) i]->setVisible (i == idx);
    }

    void paint (juce::Graphics& g) override
    {
        // Same 5-layer acrylic as the classic synth page.
        auto bounds = getLocalBounds().toFloat();
        g.setGradientFill ({ MidiMorfColors::panelSubstrateTop, 0.0f, 0.0f,
                             MidiMorfColors::panelSubstrateBottom, 0.0f, bounds.getHeight(), false });
        g.fillAll();
        g.setGradientFill ({ MidiMorfColors::panelAcrylicTop, 0.0f, 0.0f,
                             MidiMorfColors::panelAcrylicBottom, 0.0f, bounds.getHeight(), false });
        g.fillAll();

        auto sheen = bounds.withHeight (bounds.getHeight() * 0.22f);
        g.setGradientFill ({ juce::Colour (0x55FFFFFF), 0.0f, sheen.getY(),
                             juce::Colour (0x00FFFFFF), 0.0f, sheen.getBottom(), false });
        g.fillRect (sheen);

        auto shadow = bounds.withTrimmedTop (bounds.getHeight() * 0.65f);
        g.setGradientFill ({ juce::Colour (0x00000000), 0.0f, shadow.getY(),
                             juce::Colour (0x44000000), 0.0f, shadow.getBottom(), false });
        g.fillRect (shadow);

        g.setColour (MidiMorfColors::panelEdgeGlow);
        g.drawRoundedRectangle (bounds.reduced (1.0f), 8.0f, 1.5f);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (8);

        auto bar = area.removeFromTop (36);
        clearButton.setBounds (bar.removeFromRight (64).reduced (3));
        bar.removeFromRight (4);
        title.setBounds (bar.removeFromLeft (juce::jmin (110, bar.getWidth() / 5)));
        const int n = tabButtons.size();
        const int w = bar.getWidth() / n;
        for (int i = 0; i < n; ++i)
            tabButtons[i]->setBounds (bar.removeFromLeft (i == n - 1 ? bar.getWidth() : w).reduced (3));
        area.removeFromTop (6);

        auditionStrip.setBounds (area.removeFromBottom (42).reduced (2));
        area.removeFromBottom (6);

        for (auto* t : tabs())
            t->setBounds (area);
    }

    void refreshAll()
    {
        for (auto* t : tabs())
            t->refresh();
    }

private:
    std::vector<WtTab*> tabs() { return { &oscTab, &envTab, &routingTab, &fxTab }; }

    WavetableEngine& engine;
    std::function<void()> onChange;
    SynthLookAndFeel laf;   // declared before the children so it outlives them

    juce::Label title;
    juce::OwnedArray<juce::TextButton> tabButtons;
    juce::TextButton clearButton;
    WtOscTab     oscTab;
    WtEnvLfoTab  envTab;
    WtRoutingTab routingTab;
    WtFxTab      fxTab;
    AuditionStrip auditionStrip;
};

//==============================================================================
class WavetableSynthPage : public juce::Component
{
public:
    WavetableSynthPage (WavetableEngine& e)
        : engine (e), content (e, [this] { if (onPatchChanged) onPatchChanged(); })
    {
        addAndMakeVisible (content);
    }

    std::function<void()> onPatchChanged;

    void resized() override { content.setBounds (getLocalBounds()); }

    void saveState (juce::PropertiesFile& props) const
    {
        for (int o = 0; o < 2; ++o)
        {
            const juce::String p = "wt_osc" + juce::String (o) + "_";
            props.setValue (p + "table",    engine.getBuiltInIndex (o));
            props.setValue (p + "file",     engine.getImportedFile (o).getFullPathName());
            props.setValue (p + "level",    engine.getOscLevel (o));
            props.setValue (p + "pos",      engine.getOscPosition (o));
            props.setValue (p + "semi",     engine.getOscSemitones (o));
            props.setValue (p + "fine",     engine.getOscFineCents (o));
        }
        props.setValue ("wt_unison",  engine.getUnison());
        props.setValue ("wt_detune",  engine.getUnisonDetune());
        props.setValue ("wt_sub",     engine.getSubLevel());
        props.setValue ("wt_drive",   engine.getDrive());
        props.setValue ("wt_gain",    engine.getGain());
        props.setValue ("wt_cutoff",  engine.getFilterCutoff());
        props.setValue ("wt_reso",    engine.getFilterResonance());
        props.setValue ("wt_filtEnv", engine.getFilterEnvAmount());
        props.setValue ("wt_envPos",  engine.getEnvToPosition());

        float a, d, s, r;
        engine.getAmpADSR (a, d, s, r);
        props.setValue ("wt_amp", juce::String (a) + "," + juce::String (d) + "," + juce::String (s) + "," + juce::String (r));
        engine.getFilterADSR (a, d, s, r);
        props.setValue ("wt_fenv", juce::String (a) + "," + juce::String (d) + "," + juce::String (s) + "," + juce::String (r));

        props.setValue ("wt_lfoRate",  engine.getLFORate());
        props.setValue ("wt_lfoDepth", engine.getLFODepth());
        props.setValue ("wt_lfoWave",  engine.getLFOWaveform());
        for (int i = 0; i < WavetableEngine::NumRoutingSlots; ++i)
        {
            props.setValue ("wt_routeSrc_"    + juce::String (i), engine.getRoutingSource (i));
            props.setValue ("wt_routeDest_"   + juce::String (i), engine.getRoutingDest (i));
            props.setValue ("wt_routeAmount_" + juce::String (i), engine.getRoutingAmount (i));
        }

        auto& fx = engine.getFx();
        props.setValue ("wt_distDrive", fx.distDrive);
        props.setValue ("wt_distOn",    fx.distEnabled);
        props.setValue ("wt_delayTime", fx.delayTimeMs);
        props.setValue ("wt_delayFb",   fx.delayFeedback);
        props.setValue ("wt_delayMix",  fx.delayMix);
        props.setValue ("wt_delayOn",   fx.delayEnabled);
        props.setValue ("wt_revMix",    fx.reverbWet);
        props.setValue ("wt_revSize",   fx.reverbSize);
        props.setValue ("wt_revOn",     fx.reverbEnabled);
    }

    void restoreState (juce::PropertiesFile& props)
    {
        if (! props.containsKey ("wt_unison"))
            return; // never saved — keep the engine defaults

        for (int o = 0; o < 2; ++o)
        {
            const juce::String p = "wt_osc" + juce::String (o) + "_";
            const int table = props.getIntValue (p + "table", engine.getBuiltInIndex (o));
            const juce::File file (props.getValue (p + "file"));
            juce::String error;
            if (table >= 0)
                engine.setBuiltInTable (o, table);
            else if (! file.existsAsFile() || ! engine.importTable (o, file, error))
                engine.setBuiltInTable (o, 0); // imported file has gone — fall back

            engine.setOscLevel (o,      (float) props.getDoubleValue (p + "level", engine.getOscLevel (o)));
            engine.setOscPosition (o,   (float) props.getDoubleValue (p + "pos",   engine.getOscPosition (o)));
            engine.setOscSemitones (o,  props.getIntValue (p + "semi", engine.getOscSemitones (o)));
            engine.setOscFineCents (o,  (float) props.getDoubleValue (p + "fine",  engine.getOscFineCents (o)));
        }
        engine.setUnison          (props.getIntValue ("wt_unison", engine.getUnison()));
        engine.setUnisonDetune    ((float) props.getDoubleValue ("wt_detune",  engine.getUnisonDetune()));
        engine.setSubLevel        ((float) props.getDoubleValue ("wt_sub",     engine.getSubLevel()));
        engine.setDrive           ((float) props.getDoubleValue ("wt_drive",   engine.getDrive()));
        engine.setGain            ((float) props.getDoubleValue ("wt_gain",    engine.getGain()));
        engine.setFilterCutoff    ((float) props.getDoubleValue ("wt_cutoff",  engine.getFilterCutoff()));
        engine.setFilterResonance ((float) props.getDoubleValue ("wt_reso",    engine.getFilterResonance()));
        engine.setFilterEnvAmount ((float) props.getDoubleValue ("wt_filtEnv", engine.getFilterEnvAmount()));
        engine.setEnvToPosition   ((float) props.getDoubleValue ("wt_envPos",  engine.getEnvToPosition()));

        auto adsr = [&props] (const char* key, float* out)
        {
            auto parts = juce::StringArray::fromTokens (props.getValue (key), ",", "");
            if (parts.size() != 4) return false;
            for (int i = 0; i < 4; ++i) out[i] = parts[i].getFloatValue();
            return true;
        };
        float v[4];
        if (adsr ("wt_amp", v))  engine.setAmpADSR (v[0], v[1], v[2], v[3]);
        if (adsr ("wt_fenv", v)) engine.setFilterADSR (v[0], v[1], v[2], v[3]);

        engine.setLFORate     ((float) props.getDoubleValue ("wt_lfoRate",  engine.getLFORate()));
        engine.setLFODepth    ((float) props.getDoubleValue ("wt_lfoDepth", engine.getLFODepth()));
        engine.setLFOWaveform (props.getIntValue ("wt_lfoWave", engine.getLFOWaveform()));
        for (int i = 0; i < WavetableEngine::NumRoutingSlots; ++i)
        {
            engine.setRoutingSource (i, props.getIntValue ("wt_routeSrc_"  + juce::String (i), engine.getRoutingSource (i)));
            engine.setRoutingDest   (i, props.getIntValue ("wt_routeDest_" + juce::String (i), engine.getRoutingDest (i)));
            engine.setRoutingAmount (i, (float) props.getDoubleValue ("wt_routeAmount_" + juce::String (i), engine.getRoutingAmount (i)));
        }

        auto& fx = engine.getFx();
        fx.distDrive     = (float) props.getDoubleValue ("wt_distDrive", fx.distDrive);
        fx.distEnabled   = props.getBoolValue ("wt_distOn", fx.distEnabled);
        fx.delayTimeMs   = (float) props.getDoubleValue ("wt_delayTime", fx.delayTimeMs);
        fx.delayFeedback = (float) props.getDoubleValue ("wt_delayFb",   fx.delayFeedback);
        fx.delayMix      = (float) props.getDoubleValue ("wt_delayMix",  fx.delayMix);
        fx.delayEnabled  = props.getBoolValue ("wt_delayOn", fx.delayEnabled);
        fx.reverbWet     = (float) props.getDoubleValue ("wt_revMix",    fx.reverbWet);
        fx.reverbSize    = (float) props.getDoubleValue ("wt_revSize",   fx.reverbSize);
        fx.reverbEnabled = props.getBoolValue ("wt_revOn", fx.reverbEnabled);

        content.refreshAll();
    }

private:
    WavetableEngine& engine;
    WavetableSynthContent content;
};
