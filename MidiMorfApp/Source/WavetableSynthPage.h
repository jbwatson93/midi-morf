#pragma once

#include "Platform.h"

#include <JuceHeader.h>
#include "WavetableEngine.h"
#include "PresetBrowser.h"
#include "SynthWidgets.h"
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
        const auto& names = WavetableLibrary::builtInNames();
        tableCombo.addSectionHeading ("ANALOG");
        for (int i = 0; i < WavetableLibrary::NumAnalogWaves; ++i)
            tableCombo.addItem (names[i], i + 1);
        tableCombo.addSectionHeading ("WAVETABLE");
        for (int i = WavetableLibrary::NumAnalogWaves; i < names.size(); ++i)
            tableCombo.addItem (names[i], i + 1);
        tableCombo.addSeparator();

        auto imported = engine.getImportedFile (osc);
        if (engine.getBuiltInIndex (osc) < 0 && imported != juce::File())
            tableCombo.addItem (imported.getFileNameWithoutExtension(), ImportedId);
        tableCombo.addItem ("Import .wav...", ImportId);

        tableCombo.setSelectedId (engine.getBuiltInIndex (osc) >= 0 ? engine.getBuiltInIndex (osc) + 1
                                                                    : ImportedId,
                                  juce::dontSendNotification);

        // An analog wave is a single frame, so POS has nothing to scan.
        const auto* table = engine.getTable (osc);
        pos.setEnabled (table == nullptr || table->getNumFrames() > 1);
        pos.setAlpha (pos.isEnabled() ? 1.0f : 0.35f);
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

                auto dir = Platform::documentsDirectory()
                               .getChildFile ("Wavetables");
                dir.createDirectory();
                auto name = Platform::importedFileName (url);
                if (name.isEmpty()) name = "Imported.wav";
                auto dest = dir.getChildFile (name);

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
// OSC tab — OSC A | OSC B | VOICE.
class WtOscTab : public WtTab
{
public:
    WtOscTab (WavetableEngine& e, std::function<void()> notify)
        : WtTab (e, notify),
          oscA ("OSC A", e, 0, notify),
          oscB ("OSC B", e, 1, notify),
          voiceCard ("VOICE"),
          unison  ("UNISON", 1.0f, (float) WavetableVoice::MaxUnison, (float) e.getUnison(), 1.0f),
          detune  ("DETUNE", 0.0f, 50.0f, e.getUnisonDetune(), 0.1f),
          sub     ("SUB",    0.0f, 1.0f,  e.getSubLevel()),
          gain    ("GAIN",   0.01f, 0.4f, e.getGain()),
          envPos  ("ENV>POS", -1.0f, 1.0f, e.getEnvToPosition())
    {
        addAndMakeVisible (oscA);
        addAndMakeVisible (oscB);
        addAndMakeVisible (voiceCard);

        wire (unison, [this] (float v) { engine.setUnison ((int) v); });
        wire (detune, [this] (float v) { engine.setUnisonDetune (v); });
        wire (sub,    [this] (float v) { engine.setSubLevel (v); });
        wire (gain,   [this] (float v) { engine.setGain (v); });
        wire (envPos, [this] (float v) { engine.setEnvToPosition (v); });
        for (auto* k : knobs())
            voiceCard.addAndMakeVisible (*k);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        const int gap = 6;
        const int w = (area.getWidth() - 2 * gap) * 2 / 5;
        oscA.setBounds (area.removeFromLeft (w));       area.removeFromLeft (gap);
        oscB.setBounds (area.removeFromLeft (w));       area.removeFromLeft (gap);
        voiceCard.setBounds (area);

        // 2 × 3 knob grid inside the voice card (ENV>POS last: the filter
        // envelope sweeping the tables, set on the FILTER tab).
        auto c = voiceCard.getContentArea();
        const int rowH = c.getHeight() / 3;
        layoutInRow (c.removeFromTop (rowH), { &unison, &detune }, 2);
        layoutInRow (c.removeFromTop (rowH), { &sub, &gain }, 2);
        envPos.setBounds (c.withSizeKeepingCentre (c.getWidth() / 2, c.getHeight()).reduced (2));
    }

    void refresh() override
    {
        oscA.refresh();
        oscB.refresh();
        unison.getSlider().setValue ((double) engine.getUnison(),   juce::dontSendNotification);
        detune.getSlider().setValue (engine.getUnisonDetune(),      juce::dontSendNotification);
        sub.getSlider().setValue    (engine.getSubLevel(),          juce::dontSendNotification);
        gain.getSlider().setValue   (engine.getGain(),              juce::dontSendNotification);
        envPos.getSlider().setValue (engine.getEnvToPosition(),     juce::dontSendNotification);
    }

private:
    std::vector<Knob*> knobs() { return { &unison, &detune, &sub, &gain, &envPos }; }

    OscCard oscA, oscB;
    SectionCard voiceCard;
    Knob unison, detune, sub, gain, envPos;
};

//==============================================================================
// FilterDisplay — the filter's response, Serum-style: the curve at the knob
// settings, filled, and while a note sounds a bright curve where the filter
// is right now (envelope, velocity, keytracking and mod matrix included).
// Drag on it: left/right is cutoff, up/down is resonance.
class FilterDisplay : public juce::Component, private juce::Timer
{
public:
    explicit FilterDisplay (WavetableEngine& e) : engine (e) { startTimerHz (30); }

    std::function<void (float cutoffHz, float reso)> onDrag;

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().reduced (1.5f);
        g.setColour (juce::Colour (0xff141417));
        g.fillRoundedRectangle (bounds, 5.0f);
        plot = bounds.reduced (6.0f, 6.0f);

        // Decades, and the 0 dB line.
        g.setColour (MidiMorfColors::textLight.withAlpha (0.08f));
        for (float hz : { 100.0f, 1000.0f, 10000.0f })
            g.drawVerticalLine ((int) xForHz (hz), plot.getY(), plot.getBottom());
        g.drawHorizontalLine ((int) yForDb (0.0f), plot.getX(), plot.getRight());

        g.setColour (MidiMorfColors::textLight.withAlpha (0.3f));
        g.setFont (juce::FontOptions (9.0f));
        static const std::pair<float, const char*> marks[] = { { 100.0f, "100" }, { 1000.0f, "1k" }, { 10000.0f, "10k" } };
        for (auto [hz, label] : marks)
            g.drawText (label, juce::Rectangle<float> (xForHz (hz) + 3.0f, plot.getBottom() - 12.0f, 30.0f, 12.0f),
                        juce::Justification::centredLeft);

        const bool on = engine.isFilterEnabled();
        const int type = engine.getFilterType();
        const float reso = engine.getFilterResonance();
        const float cutoff = engine.getFilterCutoff();

        auto curve = responsePath (on ? type : -1, cutoff, reso);
        juce::Path fill (curve);
        fill.lineTo (plot.getRight(), plot.getBottom());
        fill.lineTo (plot.getX(), plot.getBottom());
        fill.closeSubPath();
        g.setColour (MidiMorfColors::accent.withAlpha (on ? 0.22f : 0.08f));
        g.fillPath (fill);
        g.setColour (MidiMorfColors::accent.withAlpha (on ? 0.9f : 0.3f));
        g.strokePath (curve, juce::PathStrokeType (1.5f));

        if (on && live > 0.0f)
        {
            g.setColour (MidiMorfColors::textLight.withAlpha (0.85f));
            g.strokePath (responsePath (type, live, reso), juce::PathStrokeType (1.2f));
        }

        if (on)
        {
            // The handle sits on the curve at the cutoff.
            const float x = xForHz (cutoff);
            const float y = yForDb (dbAt (type, cutoff, cutoff, reso));
            g.setColour (MidiMorfColors::textLight);
            g.fillEllipse (x - 4.5f, y - 4.5f, 9.0f, 9.0f);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override { drag (e); }
    void mouseDrag (const juce::MouseEvent& e) override { drag (e); }

private:
    static constexpr float MinDb = -42.0f, MaxDb = 24.0f;

    void timerCallback() override
    {
        const float now = engine.getLiveCutoff();
        if (std::abs (now - live) > 0.5f || (now < 0.0f) != (live < 0.0f))
        {
            live = now;
            repaint();
        }
        else if (engine.getFilterCutoff() != lastCutoff || engine.getFilterResonance() != lastReso
                 || engine.getFilterType() != lastType || engine.isFilterEnabled() != lastOn)
        {
            lastCutoff = engine.getFilterCutoff(); lastReso = engine.getFilterResonance();
            lastType = engine.getFilterType();     lastOn = engine.isFilterEnabled();
            repaint();
        }
    }

    void drag (const juce::MouseEvent& e)
    {
        if (plot.isEmpty() || ! onDrag) return;
        const float t = juce::jlimit (0.0f, 1.0f, (e.position.x - plot.getX()) / plot.getWidth());
        const float hz = 20.0f * std::pow (1000.0f, t);
        const float reso = juce::jlimit (0.0f, 1.0f, 1.0f - (e.position.y - plot.getY()) / plot.getHeight())
                         * SynthFilter::MaxReso;
        onDrag (hz, reso);
    }

    float xForHz (float hz) const { return plot.getX() + plot.getWidth() * std::log (hz / 20.0f) / std::log (1000.0f); }
    float yForDb (float db) const { return plot.getY() + plot.getHeight() * (MaxDb - juce::jlimit (MinDb, MaxDb, db)) / (MaxDb - MinDb); }

    static float dbAt (int type, float hz, float cutoff, float reso)
    {
        if (type < 0) return 0.0f;
        return juce::Decibels::gainToDecibels (SynthFilter::magnitude (type, hz, cutoff, reso), MinDb - 6.0f);
    }

    juce::Path responsePath (int type, float cutoff, float reso) const
    {
        juce::Path p;
        const int steps = juce::jmax (32, (int) plot.getWidth() / 2);
        for (int i = 0; i <= steps; ++i)
        {
            const float hz = 20.0f * std::pow (1000.0f, (float) i / (float) steps);
            const juce::Point<float> pt (xForHz (hz), yForDb (dbAt (type, hz, cutoff, reso)));
            if (i == 0) p.startNewSubPath (pt); else p.lineTo (pt);
        }
        return p;
    }

    WavetableEngine& engine;
    juce::Rectangle<float> plot;
    float live = -1.0f, lastCutoff = 0.0f, lastReso = 0.0f;
    int   lastType = -1;
    bool  lastOn = true;
};

//==============================================================================
// FILTER tab — FILTER (type, response, knobs, power) | FILTER ENV.
//
// Cutoff moves in octaves. ENV is how far the envelope moves it (up or down);
// VEL is how much playing softly shrinks that sweep, so every note still
// closes back to CUTOFF; KEY makes higher notes brighter, as on an analog.
// DRIVE is the per-voice drive into the filter.
class WtFilterTab : public WtTab
{
public:
    WtFilterTab (WavetableEngine& e, std::function<void()> notify)
        : WtTab (e, notify),
          filterCard ("FILTER", [this] { return engine.isFilterEnabled(); },
                                [this] (bool on) { engine.setFilterEnabled (on); }),
          envCard ("FILTER ENV"),
          display (e),
          cutoff ("CUTOFF", 20.0f, 20000.0f, e.getFilterCutoff(), 1.0f, 1000.0f),
          reso   ("RESO",   0.0f, SynthFilter::MaxReso, e.getFilterResonance()),
          drive  ("DRIVE",  0.0f, 1.0f, e.getDrive()),
          envAmt ("ENV",   -1.0f, 1.0f, e.getFilterEnvAmount()),
          vel    ("VEL",    0.0f, 1.0f, e.getFilterVelocity()),
          key    ("KEY",    0.0f, 1.0f, e.getFilterKeytrack()),
          fA ("A", 0.001f, 2.0f, 0.01f, 0.001f, 0.2f), fD ("D", 0.01f, 2.0f, 0.5f, 0.01f, 0.3f),
          fS ("S", 0.0f, 1.0f, 0.3f),                   fR ("R", 0.01f, 3.0f, 0.5f, 0.01f, 0.5f)
    {
        type.addItemList (SynthFilter::typeNames(), 1);
        type.onChange = [this]
        {
            engine.setFilterType (type.getSelectedId() - 1);
            changed();
        };
        filterCard.addAndMakeVisible (type);
        filterCard.addAndMakeVisible (display);
        filterCard.onEnabledChanged = [this] { changed(); };

        display.onDrag = [this] (float hz, float r)
        {
            cutoff.getSlider().setValue (hz, juce::sendNotificationSync);
            reso.getSlider().setValue (r, juce::sendNotificationSync);
        };

        wire (cutoff, [this] (float v) { engine.setFilterCutoff (v); });
        wire (reso,   [this] (float v) { engine.setFilterResonance (v); });
        wire (drive,  [this] (float v) { engine.setDrive (v); });
        wire (envAmt, [this] (float v) { engine.setFilterEnvAmount (v); });
        wire (vel,    [this] (float v) { engine.setFilterVelocity (v); });
        wire (key,    [this] (float v) { engine.setFilterKeytrack (v); });
        for (auto* k : knobs())
            filterCard.addAndMakeVisible (*k);

        for (auto* f : { &fA, &fD, &fS, &fR })
        {
            wire (*f, [this] (float) { pushEnv(); });
            envCard.addAndMakeVisible (*f);
        }

        addAndMakeVisible (filterCard);
        addAndMakeVisible (envCard);
        refresh();
    }

    void resized() override
    {
        auto area = getLocalBounds();
        const int gap = 6;
        envCard.setBounds (area.removeFromRight ((int) (area.getWidth() * 0.3f)));
        area.removeFromRight (gap);
        filterCard.setBounds (area);

        // Type picker and response on the left, knobs in a 3 × 2 grid on the
        // right, so the curve gets the height.
        auto c = filterCard.getContentArea();
        auto knobArea = c.removeFromRight (c.getWidth() * 9 / 20);
        c.removeFromRight (6);
        type.setBounds (c.removeFromTop (28));
        c.removeFromTop (4);
        display.setBounds (c);

        auto top = knobArea.removeFromTop (knobArea.getHeight() / 2);
        layoutInRow (top,      { &cutoff, &reso, &drive }, 2);
        layoutInRow (knobArea, { &envAmt, &vel, &key }, 2);

        layoutInRow (envCard.getContentArea(), { &fA, &fD, &fS, &fR });
    }

    void refresh() override
    {
        filterCard.refreshFromModel();
        type.setSelectedId (engine.getFilterType() + 1, juce::dontSendNotification);
        cutoff.getSlider().setValue (engine.getFilterCutoff(),     juce::dontSendNotification);
        reso.getSlider().setValue   (engine.getFilterResonance(),  juce::dontSendNotification);
        drive.getSlider().setValue  (engine.getDrive(),            juce::dontSendNotification);
        envAmt.getSlider().setValue (engine.getFilterEnvAmount(),  juce::dontSendNotification);
        vel.getSlider().setValue    (engine.getFilterVelocity(),   juce::dontSendNotification);
        key.getSlider().setValue    (engine.getFilterKeytrack(),   juce::dontSendNotification);

        float a, d, s, r;
        engine.getFilterADSR (a, d, s, r);
        const float v[] = { a, d, s, r };
        int i = 0;
        for (auto* f : { &fA, &fD, &fS, &fR })
            f->getSlider().setValue (v[i++], juce::dontSendNotification);
        display.repaint();
    }

private:
    std::vector<Knob*> knobs() { return { &cutoff, &reso, &drive, &envAmt, &vel, &key }; }

    static float val (Fader& f) { return (float) f.getSlider().getValue(); }
    void pushEnv() { engine.setFilterADSR (val (fA), val (fD), val (fS), val (fR)); }

    FxModule filterCard;
    SectionCard envCard;
    juce::ComboBox type;
    FilterDisplay display;
    Knob cutoff, reso, drive, envAmt, vel, key;
    Fader fA, fD, fS, fR;
};

//==============================================================================
// ENV & LFO tab — AMP envelope | LFO. The filter envelope is on the FILTER tab.
class WtEnvLfoTab : public WtTab
{
public:
    WtEnvLfoTab (WavetableEngine& e, std::function<void()> notify)
        : WtTab (e, notify),
          ampCard ("AMP ENV"), lfoCard ("LFO"),
          aA ("A", 0.001f, 2.0f, 0.01f, 0.001f, 0.2f), aD ("D", 0.01f, 2.0f, 0.4f, 0.01f, 0.3f),
          aS ("S", 0.0f, 1.0f, 0.7f),                   aR ("R", 0.01f, 3.0f, 0.6f, 0.01f, 0.5f),
          rate  ("RATE",  0.05f, 20.0f, e.getLFORate(), 0.01f, 2.0f),
          depth ("DEPTH", 0.0f,  1.0f,  e.getLFODepth()),
          wave  ("WAVE")
    {
        for (auto* f : { &aA, &aD, &aS, &aR })
        {
            wire (*f, [this] (float) { pushAmp(); });
            ampCard.addAndMakeVisible (*f);
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
        addAndMakeVisible (lfoCard);
        refresh();
    }

    void resized() override
    {
        auto area = getLocalBounds();
        const int gap = 6;
        ampCard.setBounds (area.removeFromLeft ((int) (area.getWidth() * 0.55f))); area.removeFromLeft (gap);
        lfoCard.setBounds (area);

        layoutInRow (ampCard.getContentArea(), { &aA, &aD, &aS, &aR });

        auto l = lfoCard.getContentArea();
        wave.setBounds (l.removeFromBottom (56));
        layoutInRow (l, { &rate, &depth });
    }

    void refresh() override
    {
        float a, d, s, r;
        engine.getAmpADSR (a, d, s, r);
        setFaders ({ &aA, &aD, &aS, &aR }, a, d, s, r);
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
    void pushAmp() { engine.setAmpADSR (val (aA), val (aD), val (aS), val (aR)); }

    SectionCard ampCard, lfoCard;
    Fader aA, aD, aS, aR;
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
// FX tab — the rack, Serum-style: the modules in order down the left, the
// selected one's controls on the right.
//
// Tap a module to edit it, tap its dot to bypass it, × to take it out, drag
// it up or down to reorder. + ADD puts a new module at the end. The order is
// the signal path, top to bottom.
class WtFxTab : public WtTab
{
public:
    WtFxTab (WavetableEngine& e, std::function<void()> notify)
        : WtTab (e, notify)
    {
        addButton.setButtonText ("+ ADD");
        addButton.onClick = [this] { showAddMenu(); };
        addAndMakeVisible (addButton);
        refresh();
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = listArea.toFloat().reduced (1.5f);
        g.setColour (juce::Colour (0xff232328));
        g.fillRoundedRectangle (bounds, 6.0f);
        g.setColour (juce::Colour (0xff3A3A40));
        g.drawRoundedRectangle (bounds.reduced (0.5f), 6.0f, 1.0f);

        auto header = bounds.withHeight (22.0f);
        g.setColour (juce::Colour (0xff2A2A30));
        g.fillRoundedRectangle (header, 6.0f);
        g.setColour (MidiMorfColors::textLight);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText ("FX RACK", header.reduced (10.0f, 0.0f), juce::Justification::centredLeft);
        g.setColour (MidiMorfColors::textLight.withAlpha (0.4f));
        g.drawText (juce::String (fx().size()) + "/" + juce::String (FxRack::MaxUnits),
                    header.reduced (10.0f, 0.0f), juce::Justification::centredRight);

        if (fx().size() == 0)
        {
            g.setColour (MidiMorfColors::textLight.withAlpha (0.45f));
            g.setFont (juce::FontOptions (12.0f));
            g.drawFittedText ("No effects yet.\nTap + ADD.", rowsArea, juce::Justification::centred, 2);
        }

        for (int i = 0; i < fx().size(); ++i)
            paintRow (g, i);

        if (dragFrom >= 0 && dragMoved)
        {
            // Where the module will land.
            const int y = rowsArea.getY() + dropIndex * rowHeight();
            g.setColour (MidiMorfColors::accent);
            g.fillRect (rowsArea.getX(), y - 1, rowsArea.getWidth(), 3);
        }

        if (fx().size() > 0 && editor == nullptr)
        {
            g.setColour (MidiMorfColors::textLight.withAlpha (0.45f));
            g.drawFittedText ("Tap a module to edit it.", editorArea, juce::Justification::centred, 1);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds();
        listArea = area.removeFromLeft (juce::jlimit (150, 220, area.getWidth() / 4));
        area.removeFromLeft (6);
        editorArea = area;

        auto list = listArea.withTrimmedTop (26).reduced (6, 4);
        addButton.setBounds (list.removeFromBottom (30).reduced (0, 2));
        list.removeFromBottom (4);
        rowsArea = list;

        if (editor != nullptr)
            layoutEditor();
    }

    void refresh() override
    {
        selected = juce::jlimit (-1, fx().size() - 1, selected < 0 && fx().size() > 0 ? 0 : selected);
        addButton.setEnabled (! fx().isFull());
        rebuildEditor();
        repaint();
    }

    //==========================================================================
    void mouseDown (const juce::MouseEvent& e) override
    {
        dragFrom = rowAt (e.getPosition());
        dragMoved = false;
        dropIndex = dragFrom;
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragFrom < 0) return;
        if (! dragMoved && e.getDistanceFromDragStart() < 8) return;
        dragMoved = true;
        const int slot = (int) std::round ((float) (e.y - rowsArea.getY()) / (float) rowHeight());
        dropIndex = juce::jlimit (0, fx().size(), slot);
        repaint();
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        const int row = dragFrom;
        dragFrom = -1;
        if (row < 0) return;

        if (dragMoved)
        {
            // Dropping below itself counts the gap it leaves.
            const int to = dropIndex > row ? dropIndex - 1 : dropIndex;
            if (to != row)
            {
                fx().move (row, to);
                selected = to;
                rebuildEditor();
                changed();
            }
            repaint();
            return;
        }

        const auto r = rowBounds (row);
        if (removeBounds (r).contains (e.getPosition()))
        {
            fx().remove (row);
            if (selected >= fx().size()) selected = fx().size() - 1;
            else if (selected > row) --selected;
            refresh();
            changed();
        }
        else if (powerBounds (r).contains (e.getPosition()))
        {
            auto* u = fx().get (row);
            u->enabled.store (! u->enabled.load());
            if (editor != nullptr && selected == row) editor->refreshFromModel();
            repaint();
            changed();
        }
        else if (row != selected)
        {
            selected = row;
            rebuildEditor();
            repaint();
        }
    }

private:
    FxRack& fx() const { return engine.getFx(); }

    int rowHeight() const
    {
        return juce::jlimit (24, 36, rowsArea.getHeight() / juce::jmax (5, fx().size()));
    }

    juce::Rectangle<int> rowBounds (int i) const
    {
        return { rowsArea.getX(), rowsArea.getY() + i * rowHeight(), rowsArea.getWidth(), rowHeight() };
    }

    static juce::Rectangle<int> removeBounds (juce::Rectangle<int> row) { return row.removeFromRight (28); }
    static juce::Rectangle<int> powerBounds (juce::Rectangle<int> row)  { return row.withTrimmedRight (28).removeFromRight (28); }

    int rowAt (juce::Point<int> p) const
    {
        for (int i = 0; i < fx().size(); ++i)
            if (rowBounds (i).contains (p))
                return i;
        return -1;
    }

    void paintRow (juce::Graphics& g, int i)
    {
        auto* u = fx().get (i);
        const bool on = u->enabled.load();
        auto r = rowBounds (i).reduced (0, 2).toFloat();

        g.setColour (i == selected ? juce::Colour (0xff34343C) : juce::Colour (0xff1C1C21));
        g.fillRoundedRectangle (r, 4.0f);
        if (i == selected)
        {
            g.setColour (MidiMorfColors::accent);
            g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);
        }
        g.setColour (on ? MidiMorfColors::accent : juce::Colour (0xff4A4A52));
        g.fillRect (r.withWidth (3.0f));

        g.setColour (on ? MidiMorfColors::textLight : juce::Colour (0xff77777F));
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (juce::String (i + 1) + "  " + u->name.toUpperCase(), r.withTrimmedLeft (10.0f).withTrimmedRight (56.0f),
                    juce::Justification::centredLeft);

        auto power = powerBounds (rowBounds (i)).toFloat().withSizeKeepingCentre (12.0f, 12.0f);
        if (on)
        {
            g.setColour (MidiMorfColors::accent);
            g.fillEllipse (power);
        }
        else
        {
            g.setColour (juce::Colour (0xff77777F));
            g.drawEllipse (power, 1.5f);
        }

        auto x = removeBounds (rowBounds (i)).toFloat().withSizeKeepingCentre (10.0f, 10.0f);
        g.setColour (MidiMorfColors::textLight.withAlpha (0.5f));
        g.drawLine ({ x.getTopLeft(), x.getBottomRight() }, 1.5f);
        g.drawLine ({ x.getBottomLeft(), x.getTopRight() }, 1.5f);
    }

    void showAddMenu()
    {
        juce::PopupMenu m;
        const auto types = Fx::typeIds();
        for (int i = 0; i < types.size(); ++i)
            m.addItem (i + 1, Fx::displayName (types[i]));

        juce::Component::SafePointer<WtFxTab> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&addButton),
                         [safe, types] (int result)
                         {
                             if (safe == nullptr || result <= 0) return;
                             const int at = safe->fx().add (types[result - 1]);
                             if (at < 0) return;
                             safe->selected = at;
                             safe->refresh();
                             safe->changed();
                         });
    }

    //==========================================================================
    // The selected module's card: MODE (if it has modes) and one knob per
    // parameter, built from what the module declares.
    void rebuildEditor()
    {
        knobs.clear();
        modeCombo.reset();
        editor.reset();

        auto* u = fx().get (selected);
        if (u == nullptr) return;

        juce::Component::SafePointer<WtFxTab> safe (this);
        editor = std::make_unique<FxModule> (u->name.toUpperCase(),
                                             [u] { return u->enabled.load(); },
                                             [u] (bool on) { u->enabled.store (on); });
        editor->onEnabledChanged = [safe] { if (safe != nullptr) { safe->repaint(); safe->changed(); } };

        if (! u->modes.isEmpty())
        {
            modeCombo = std::make_unique<LabeledCombo> ("MODE");
            modeCombo->getCombo().addItemList (u->modes, 1);
            modeCombo->getCombo().setSelectedId (u->mode.load() + 1, juce::dontSendNotification);
            modeCombo->getCombo().onChange = [safe, u]
            {
                if (safe == nullptr) return;
                u->mode.store (safe->modeCombo->getCombo().getSelectedId() - 1);
                safe->changed();
            };
            editor->addAndMakeVisible (*modeCombo);
        }

        for (int i = 0; i < (int) u->params.size(); ++i)
        {
            const auto& p = u->params[(size_t) i];
            auto* k = knobs.add (new Knob (p.name, p.min, p.max, u->getValue (i), p.step, p.skewMid));
            wire (*k, [u, i] (float v) { u->setValue (i, v); });
            editor->addAndMakeVisible (*k);
        }

        addAndMakeVisible (*editor);
        layoutEditor();
    }

    void layoutEditor()
    {
        editor->setBounds (editorArea);
        auto c = editor->getContentArea();
        if (modeCombo != nullptr)
        {
            auto column = c.removeFromLeft (juce::jlimit (110, 160, c.getWidth() / 5));
            modeCombo->setBounds (column.withSizeKeepingCentre (column.getWidth(), 64));
        }

        // Keep knobs a sensible size when a module only has a few.
        const int n = knobs.size();
        const int w = juce::jmin (c.getWidth() / juce::jmax (1, n), juce::jmax (90, c.getHeight()), 140);
        auto row = c.withSizeKeepingCentre (w * n, juce::jmin (c.getHeight(), w + 30));
        for (auto* k : knobs)
            k->setBounds (row.removeFromLeft (w).reduced (3));
    }

    juce::TextButton addButton;
    juce::Rectangle<int> listArea, rowsArea, editorArea;
    int selected = 0;
    int dragFrom = -1, dropIndex = -1;
    bool dragMoved = false;

    std::unique_ptr<FxModule> editor;
    std::unique_ptr<LabeledCombo> modeCombo;
    juce::OwnedArray<Knob> knobs;
};

//==============================================================================
// Landscape content: header slot (the preset bar) + tab bar + INIT, the active
// tab, audition ribbon.
class WavetableSynthContent : public juce::Component
{
public:
    WavetableSynthContent (WavetableEngine& e, std::function<void()> notify)
        : engine (e), onChange (notify),
          oscTab (e, notify), filterTab (e, notify), envTab (e, notify), routingTab (e, notify), fxTab (e, notify)
    {
        setLookAndFeel (&laf);

        const juce::StringArray names { "OSC", "FILTER", "ENV & LFO", "ROUTING", "FX" };
        for (int i = 0; i < names.size(); ++i)
        {
            auto* b = tabButtons.add (new juce::TextButton (names[i]));
            b->setRadioGroupId (1002);
            b->setClickingTogglesState (true);
            b->onClick = [this, i] { showTab (i); };
            addAndMakeVisible (b);
        }

        clearButton.setButtonText ("INIT");
        clearButton.onClick = [this]
        {
            if (onInit)
            {
                onInit();
                return;
            }
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
        if (header != nullptr)
            header->setBounds (bar.removeFromLeft (juce::jmin (330, bar.getWidth() * 45 / 100)));
        bar.removeFromLeft (4);
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

    // What sits left of the tabs — the Sound page's preset bar. Not owned.
    void setHeaderComponent (juce::Component* c)
    {
        header = c;
        if (c != nullptr)
            addAndMakeVisible (c);
        resized();
    }

    // INIT loads the Init preset when the page provides one; otherwise it
    // resets the engine.
    std::function<void()> onInit;

private:
    std::vector<WtTab*> tabs() { return { &oscTab, &filterTab, &envTab, &routingTab, &fxTab }; }

    WavetableEngine& engine;
    std::function<void()> onChange;
    SynthLookAndFeel laf;   // declared before the children so it outlives them

    juce::Component* header = nullptr;
    juce::OwnedArray<juce::TextButton> tabButtons;
    juce::TextButton clearButton;
    WtOscTab     oscTab;
    WtFilterTab  filterTab;
    WtEnvLfoTab  envTab;
    WtRoutingTab routingTab;
    WtFxTab      fxTab;
    AuditionStrip auditionStrip;
};

//==============================================================================
// The Sound page for any wavetable track. It edits one engine at a time —
// edit() points it at another track's synth by rebuilding the editor, whose
// controls all hold a reference to the engine they edit.
//
// Presets: the bar steps through them and opens the browser; SAVE writes a
// user preset; INIT loads Init. A star marks a sound changed since its
// preset was loaded.
//
// saveState / restoreState are for the engine it was built with (T1's synth),
// under the settings key that synth has always used — read only when moving
// an older install's settings into the project.
class WavetableSynthPage : public juce::Component
{
public:
    WavetableSynthPage (WavetableEngine& e) : engine (e)
    {
        bar.onStep   = [this] (int dir) { step (dir); };
        bar.onBrowse = [this] { browser.show (presetName, accent); };
        bar.onSave   = [this] { promptSave(); };
        browser.onPick = [this] (const WavetablePreset& p) { load (p); };
        addChildComponent (browser);

        edit (e, "Init", MidiMorfColors::accent);
    }

    std::function<void()> onPatchChanged;
    std::function<void (const juce::String& name)> onPresetChanged;   // a preset was loaded or saved

    void edit (WavetableEngine& e, const juce::String& currentPresetName, juce::Colour trackColour)
    {
        if (editing != &e || content == nullptr)
        {
            editing = &e;
            content = std::make_unique<WavetableSynthContent> (e, [this] { patchChanged(); });
            content->onInit = [this] { load (WavetablePresets::init()); };
            content->setHeaderComponent (&bar);
            addAndMakeVisible (*content);
            browser.toFront (false);
            resized();
        }
        accent = trackColour;
        presetName = currentPresetName;
        baseline = e.getState();
        bar.set (presetName, false, accent);
        content->refreshAll();
    }

    WavetableEngine& getEditedEngine() const noexcept { return *editing; }

    void resized() override
    {
        if (content != nullptr)
            content->setBounds (getLocalBounds());
        browser.setBounds (getLocalBounds());
    }

    void visibilityChanged() override
    {
        if (! isVisible())
            browser.setVisible (false);
    }

    // The patch lives in the engine as one ValueTree (WavetableEngine::
    // getState), stored here under a single settings key.
    void saveState (juce::PropertySet& props) const
    {
        if (auto xml = engine.getState().createXml())
            props.setValue ("wt_state", xml.get());

        // The patch used to be saved key by key; those keys are now stale.
        const auto keys = props.getAllProperties().getAllKeys();   // copy: removing while iterating
        for (auto& key : keys)
            if (key.startsWith ("wt_") && key != "wt_state")
                props.removeValue (key);
    }

    void restoreState (juce::PropertySet& props)
    {
        if (auto xml = props.getXmlValue ("wt_state"))
            engine.setState (juce::ValueTree::fromXml (*xml));
        else if (props.containsKey ("wt_unison"))
            engine.setState (stateFromLegacyKeys (props));
        else if (props.containsKey ("synth_mainWave"))
            migrateClassicPatch (props);   // updating from the original subtractive synth
        // else: a fresh install keeps the engine defaults

        if (&engine == editing)
            content->refreshAll();
    }

private:
    // A patch saved before the engine owned its state: the same names with a
    // "wt_" prefix, one settings key each.
    static juce::ValueTree stateFromLegacyKeys (juce::PropertySet& props)
    {
        juce::ValueTree t (WavetableEngine::stateType);
        const auto& all = props.getAllProperties();
        for (int i = 0; i < all.size(); ++i)
            if (all.getAllKeys()[i].startsWith ("wt_"))
                t.setProperty (all.getAllKeys()[i].substring (3), all.getAllValues()[i], nullptr);
        return t;
    }

    // Maps a patch saved by the original synth (SynthEngine, "synth_*" keys)
    // onto this engine. Its four waveforms are this engine's analog tables in
    // the same order, so OSC 1/2 come across as-is; unison is off and the
    // env-to-position amount is zero, so the voice is the same shape.
    // Fallbacks are that synth's factory defaults. The old keys are left in
    // place; once this synth saves, "wt_state" takes over.
    void migrateClassicPatch (juce::PropertySet& props)
    {
        auto f = [&props] (const char* key, double def) { return (float) props.getDoubleValue (key, def); };

        engine.setBuiltInTable  (0, juce::jlimit (0, 3, props.getIntValue ("synth_mainWave", 2)));
        engine.setOscLevel      (0, f ("synth_osc1Level", 1.0));
        engine.setOscPosition   (0, 0.0f);
        engine.setOscSemitones  (0, 0);
        engine.setOscFineCents  (0, 0.0f);
        engine.setBuiltInTable  (1, juce::jlimit (0, 3, props.getIntValue ("synth_osc2Wave", 0)));
        engine.setOscLevel      (1, f ("synth_osc2Level", 0.0));
        engine.setOscPosition   (1, 0.0f);
        engine.setOscSemitones  (1, 0);
        engine.setOscFineCents  (1, f ("synth_osc2Detune", 0.0));

        engine.setUnison (1);
        engine.setUnisonDetune (0.0f);
        engine.setSubLevel (f ("synth_subLevel", 0.3));
        engine.setDrive    (f ("synth_drive", 0.0));
        engine.setGain     (f ("synth_gain", 0.15));

        engine.setFilterCutoff    (f ("synth_cutoff", 1400.0));
        engine.setFilterResonance (f ("synth_reso", 1.0));
        // That synth's ENV was Hz at the envelope's peak; same peak in octaves.
        engine.setFilterType (SynthFilter::Ladder);
        engine.setFilterVelocity (0.0f);
        engine.setFilterKeytrack (0.0f);
        engine.setFilterEnvAmount (std::log2 (1.0f + f ("synth_filtEnv", 3000.0) / engine.getFilterCutoff())
                                   / WavetableVoice::EnvOctaves);
        engine.setEnvToPosition   (0.0f);
        engine.setAmpADSR    (f ("synth_attack", 0.02),   f ("synth_decay", 0.3),
                              f ("synth_sustain", 0.6),   f ("synth_release", 0.7));
        engine.setFilterADSR (f ("synth_fAttack", 0.01),  f ("synth_fDecay", 0.4),
                              f ("synth_fSustain", 0.3),  f ("synth_fRelease", 0.5));

        engine.setLFORate     (f ("synth_lfoRate", 1.0));
        engine.setLFODepth    (f ("synth_lfoDepth", 0.0));
        engine.setLFOWaveform (props.getIntValue ("synth_lfoWave", 0));

        // Old destination index -> this engine's. Env Amount has no
        // equivalent here and becomes Off.
        static const int destMap[] = {
            WavetableEngine::ModDestOff,       WavetableEngine::ModDestCutoff,
            WavetableEngine::ModDestResonance, WavetableEngine::ModDestOff,
            WavetableEngine::ModDestGain,      WavetableEngine::ModDestReverbMix,
            WavetableEngine::ModDestPitch,     WavetableEngine::ModDestDistDrive,
            WavetableEngine::ModDestDelayMix,  WavetableEngine::ModDestDelayTime
        };
        for (int i = 0; i < WavetableEngine::NumRoutingSlots; ++i)
        {
            const juce::String n (i);
            const int oldDest = props.getIntValue ("synth_routeDest_" + n, 0);
            engine.setRoutingSource (i, props.getIntValue ("synth_routeSrc_" + n, 0));
            engine.setRoutingDest   (i, oldDest >= 0 && oldDest < (int) std::size (destMap) ? destMap[oldDest] : 0);
            engine.setRoutingAmount (i, (float) props.getDoubleValue ("synth_routeAmount_" + n, 0.0));
        }

        // Its FX bus was the same as the wavetable synth's first one.
        juce::ValueTree bus (WavetableEngine::stateType);
        bus.setProperty ("distDrive", f ("synth_distDrive", 0.0), nullptr);
        bus.setProperty ("distOn",    props.getBoolValue ("synth_distOn", true), nullptr);
        bus.setProperty ("delayTime", f ("synth_delayTimeMs", 350.0), nullptr);
        bus.setProperty ("delayFb",   f ("synth_delayFeedback", 0.4), nullptr);
        bus.setProperty ("delayMix",  f ("synth_delayMix", 0.18), nullptr);
        bus.setProperty ("delayOn",   props.getBoolValue ("synth_delayOn", true), nullptr);
        bus.setProperty ("revMix",    f ("synth_reverbMix", 0.2), nullptr);
        bus.setProperty ("revSize",   f ("synth_reverbSize", 0.5), nullptr);
        bus.setProperty ("revOn",     props.getBoolValue ("synth_reverbOn", true), nullptr);
        engine.getFx().setState (WavetableEngine::rackFromBus (bus));

        if (&engine == editing)
            content->refreshAll();
    }

    //==========================================================================
    // Presets
    void patchChanged()
    {
        bar.set (presetName, ! editing->getState().isEquivalentTo (baseline), accent);
        if (onPatchChanged) onPatchChanged();
    }

    void load (const WavetablePreset& p)
    {
        editing->setState (p.state);
        content->refreshAll();
        presetName = p.name;
        baseline = editing->getState();
        bar.set (presetName, false, accent);
        if (onPresetChanged) onPresetChanged (presetName);
        if (onPatchChanged) onPatchChanged();
    }

    // Through every preset in browsing order, wrapping round.
    void step (int direction)
    {
        const auto all = WavetablePresets::all();
        int index = -1;
        for (int i = 0; i < (int) all.size(); ++i)
            if (all[(size_t) i].name == presetName) { index = i; break; }

        const int n = (int) all.size();
        load (all[(size_t) (((index < 0 ? 0 : index + direction) % n + n) % n)]);
    }

    void promptSave()
    {
        juce::String category = "Pad";
        for (auto& p : WavetablePresets::all())
            if (p.name == presetName && p.category.isNotEmpty())
                category = p.category;

        auto* w = new juce::AlertWindow ("Save preset", "Name your sound and pick where it goes.", juce::MessageBoxIconType::NoIcon);
        w->addTextEditor ("name", presetName == "Init" ? juce::String ("My Sound") : presetName, "Name");
        w->addComboBox ("category", WavetablePresets::categories(), "Category");
        if (auto* cb = w->getComboBoxComponent ("category"))
            cb->setText (category, juce::dontSendNotification);
        w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

        juce::Component::SafePointer<WavetableSynthPage> safe (this);
        w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w] (int result)
        {
            if (safe == nullptr || result != 1)
                return;

            const auto name = w->getTextEditorContents ("name").trim();
            const auto cat  = w->getComboBoxComponent ("category")->getText();
            juce::String error;
            if (! WavetablePresets::save (name, cat, safe->editing->getState(), error))
            {
                juce::AlertWindow::showAsync (juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::WarningIcon)
                                                                      .withTitle ("Couldn't save").withMessage (error).withButton ("OK"),
                                              nullptr);
                return;
            }

            safe->presetName = name;
            safe->baseline = safe->editing->getState();
            safe->bar.set (name, false, safe->accent);
            if (safe->onPresetChanged) safe->onPresetChanged (name);
        }), true);
    }

    WavetableEngine& engine;
    WavetableEngine* editing = nullptr;
    std::unique_ptr<WavetableSynthContent> content;

    PresetBar bar;

public:
    // For the walkthrough to point at.
    juce::Rectangle<int> presetBarBounds() const { return getLocalArea (&bar, bar.getLocalBounds()); }

private:
    PresetBrowser browser;
    juce::String presetName { "Init" };
    juce::ValueTree baseline;
    juce::Colour accent { MidiMorfColors::accent };
};
