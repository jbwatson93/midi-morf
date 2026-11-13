#pragma once

#include "Platform.h"

#include <JuceHeader.h>
#include "WavetableEngine.h"
#include "SynthWidgets.h"
#include "MidiMorfColors.h"

//==============================================================================
// WavetableEditor — draw your own wavetable, over the whole synth page.
//
// A table is a few KEYFRAMES (single cycles, up to 16), shown along the top.
// Draw a frame with a finger (DRAW), or set its harmonics as bars
// (HARMONICS); START gives a frame a basic wave to begin from. MORPH turns
// the keyframes into a smooth 64-frame table, crossfading or blending their
// spectra, so POS sweeps from one keyframe to the next.
//
// Every edit is heard as soon as the finger lifts: the oscillator plays the
// table being drawn. SAVE names it and writes a 2048-sample-per-frame .wav
// (the Serum format the importer reads) into Documents/Wavetables, which the
// patch then remembers like any imported table. CANCEL puts the old table
// back.
class WavetableEditor : public juce::Component
{
public:
    static constexpr int N = Wavetable::FrameSize;
    static constexpr int MaxKeyframes = 16;
    static constexpr int NumHarmonicBars = 32;

    explicit WavetableEditor (WavetableEngine& e) : engine (e)
    {
        title.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        title.setColour (juce::Label::textColourId, MidiMorfColors::textLight);
        addAndMakeVisible (title);

        for (auto* b : { &cancelButton, &saveButton, &addButton, &removeButton, &drawButton, &harmonicsButton,
                         &smoothButton, &normalizeButton, &invertButton })
            addAndMakeVisible (b);
        cancelButton.setButtonText ("CANCEL");
        saveButton.setButtonText ("SAVE");
        addButton.setButtonText ("+");
        removeButton.setButtonText (juce::String::fromUTF8 ("\xe2\x88\x92"));
        drawButton.setButtonText ("DRAW");
        harmonicsButton.setButtonText ("HARMONICS");
        smoothButton.setButtonText ("SMOOTH");
        normalizeButton.setButtonText ("NORMALIZE");
        invertButton.setButtonText ("INVERT");
        saveButton.setColour (juce::TextButton::buttonColourId, MidiMorfColors::accent);

        for (auto* b : { &drawButton, &harmonicsButton })
        {
            b->setRadioGroupId (3101);
            b->setClickingTogglesState (true);
        }
        drawButton.setToggleState (true, juce::dontSendNotification);
        drawButton.onClick      = [this] { harmonicsView = false; canvas.repaint(); };
        harmonicsButton.onClick = [this] { harmonicsView = true;  canvas.repaint(); };

        cancelButton.onClick    = [this] { cancel(); };
        saveButton.onClick      = [this] { promptSave(); };
        addButton.onClick       = [this] { addKeyframe(); };
        removeButton.onClick    = [this] { removeKeyframe(); };
        smoothButton.onClick    = [this] { smooth (frame()); edited(); };
        normalizeButton.onClick = [this] { normalize (frame()); edited(); };
        invertButton.onClick    = [this] { for (auto& v : frame()) v = -v; edited(); };

        start.addItemList ({ "Sine", "Triangle", "Saw", "Square", "Pulse 25%", "Noise" }, 1);
        start.setTextWhenNothingSelected ("START...");
        start.onChange = [this]
        {
            const int id = start.getSelectedId();
            if (id <= 0) return;
            fillBasic (frame(), id - 1);
            start.setSelectedId (0, juce::dontSendNotification);
            edited();
        };
        addAndMakeVisible (start);

        morph.addItemList ({ "Morph: off", "Morph: crossfade", "Morph: spectral" }, 1);
        morph.setSelectedId (2, juce::dontSendNotification);
        morph.onChange = [this] { rebuild(); };
        addAndMakeVisible (morph);

        addAndMakeVisible (strip);
        addAndMakeVisible (canvas);

        audition.onNoteOn  = [this] (int n, float v) { engine.noteOn (n, v); };
        audition.onNoteOff = [this] (int n)          { engine.noteOff (n); };
        addAndMakeVisible (audition);
    }

    std::function<void (int osc)> onTableChanged;

    // Starts editing oscillator `osc`'s current table.
    void open (int oscIndex)
    {
        osc = oscIndex;
        originalBuiltIn = engine.getBuiltInIndex (osc);
        originalFile = engine.getImportedFile (osc);
        title.setText (juce::String ("WAVETABLE EDITOR - OSC ") + (osc == 0 ? "A" : "B"), juce::dontSendNotification);

        keyframes.clear();
        if (const auto* t = engine.getTable (osc))
        {
            const int n = t->getNumFrames();
            const int take = juce::jmin (n, MaxKeyframes);
            for (int k = 0; k < take; ++k)
            {
                const int f = take > 1 ? (k * (n - 1)) / (take - 1) : 0;
                const float* src = t->getDisplayFrame (f);
                keyframes.emplace_back (src, src + N);
            }
        }
        if (keyframes.empty())
        {
            keyframes.emplace_back (N, 0.0f);
            fillBasic (keyframes[0], 0);
        }
        selected = 0;
        morph.setSelectedId (keyframes.size() > 1 ? 2 : 1, juce::dontSendNotification);
        setVisible (true);
        toFront (true);
        strip.repaint();
        canvas.repaint();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xf01A1A1E));
        g.setColour (MidiMorfColors::accent);
        g.drawRect (getLocalBounds(), 1);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (10);
        auto top = area.removeFromTop (34);
        saveButton.setBounds (top.removeFromRight (80).reduced (2));
        cancelButton.setBounds (top.removeFromRight (80).reduced (2));
        title.setBounds (top);
        area.removeFromTop (6);

        auto frames = area.removeFromTop (58);
        morph.setBounds (frames.removeFromRight (150).withSizeKeepingCentre (150, 30));
        frames.removeFromRight (6);
        removeButton.setBounds (frames.removeFromRight (40).withSizeKeepingCentre (40, 30));
        addButton.setBounds (frames.removeFromRight (40).withSizeKeepingCentre (40, 30));
        frames.removeFromRight (6);
        strip.setBounds (frames);
        area.removeFromTop (6);

        audition.setBounds (area.removeFromBottom (40));
        area.removeFromBottom (6);

        auto tools = area.removeFromLeft (110);
        area.removeFromLeft (6);
        canvas.setBounds (area);
        const int h = juce::jmin (40, tools.getHeight() / 7);
        for (auto* c : std::initializer_list<juce::Component*> { &drawButton, &harmonicsButton, &start, &smoothButton,
                                                                 &normalizeButton, &invertButton })
            c->setBounds (tools.removeFromTop (h).reduced (0, 3));
    }

private:
    using Frame = std::vector<float>;

    Frame& frame() { return keyframes[(size_t) juce::jlimit (0, (int) keyframes.size() - 1, selected)]; }

    //==========================================================================
    // Frame helpers.
    static void fillBasic (Frame& f, int kind)
    {
        juce::Random r;
        for (int i = 0; i < N; ++i)
        {
            const float x = (float) i / (float) N;
            switch (kind)
            {
                case 0:  f[(size_t) i] = std::sin (x * juce::MathConstants<float>::twoPi); break;
                case 1:  f[(size_t) i] = x < 0.25f ? x * 4.0f : x < 0.75f ? 2.0f - x * 4.0f : x * 4.0f - 4.0f; break;
                case 2:  f[(size_t) i] = 2.0f * x - 1.0f; break;
                case 3:  f[(size_t) i] = x < 0.5f ? 1.0f : -1.0f; break;
                case 4:  f[(size_t) i] = x < 0.25f ? 1.0f : -0.33f; break;
                default: f[(size_t) i] = r.nextFloat() * 2.0f - 1.0f; break;
            }
        }
    }

    static void smooth (Frame& f)
    {
        Frame out (f.size());
        const int radius = 24;
        for (int i = 0; i < N; ++i)
        {
            float sum = 0.0f;
            for (int k = -radius; k <= radius; ++k)
                sum += f[(size_t) ((i + k + N) % N)];
            out[(size_t) i] = sum / (float) (2 * radius + 1);
        }
        f = std::move (out);
    }

    static void normalize (Frame& f)
    {
        float mean = 0.0f;
        for (auto v : f) mean += v;
        mean /= (float) N;
        float peak = 0.0f;
        for (auto& v : f) { v -= mean; peak = juce::jmax (peak, std::abs (v)); }
        if (peak > 1.0e-6f)
            for (auto& v : f) v /= peak;
    }

    // Harmonic magnitudes 1..NumHarmonicBars, normalised to the loudest.
    static std::vector<float> harmonicsOf (const Frame& f)
    {
        std::vector<float> h ((size_t) NumHarmonicBars + 1, 0.0f);
        for (int k = 1; k <= NumHarmonicBars; ++k)
        {
            double re = 0.0, im = 0.0;
            for (int i = 0; i < N; i += 2)   // every other sample: plenty for 32 harmonics
            {
                const double a = juce::MathConstants<double>::twoPi * k * i / N;
                re += f[(size_t) i] * std::cos (a);
                im += f[(size_t) i] * std::sin (a);
            }
            h[(size_t) k] = (float) std::sqrt (re * re + im * im);
        }
        const float peak = *std::max_element (h.begin(), h.end());
        if (peak > 1.0e-6f)
            for (auto& v : h) v /= peak;
        return h;
    }

    static void fromHarmonics (Frame& f, const std::vector<float>& h)
    {
        std::fill (f.begin(), f.end(), 0.0f);
        for (int k = 1; k < (int) h.size(); ++k)
            if (h[(size_t) k] > 0.0005f)
                for (int i = 0; i < N; ++i)
                    f[(size_t) i] += h[(size_t) k] * std::sin (juce::MathConstants<float>::twoPi * (float) k * (float) i / (float) N);
        normalize (f);
    }

    //==========================================================================
    void addKeyframe()
    {
        if ((int) keyframes.size() >= MaxKeyframes) return;
        keyframes.insert (keyframes.begin() + selected + 1, frame());
        ++selected;
        if (keyframes.size() == 2) morph.setSelectedId (3, juce::dontSendNotification);
        edited();
    }

    void removeKeyframe()
    {
        if (keyframes.size() <= 1) return;
        keyframes.erase (keyframes.begin() + selected);
        selected = juce::jmin (selected, (int) keyframes.size() - 1);
        edited();
    }

    void edited()
    {
        strip.repaint();
        canvas.repaint();
        rebuild();
    }

    // The table as it will sound: the keyframes, morphed out to 64 frames.
    std::vector<Wavetable::Spectrum> buildSpectra() const
    {
        std::vector<Wavetable::Spectrum> keys;
        for (auto& f : keyframes)
            keys.push_back (WavetableLibrary::fromSamples (f.data(), N));

        const int mode = morph.getSelectedId() - 1;
        if (mode <= 0 || keys.size() < 2)
            return keys;

        std::vector<Wavetable::Spectrum> out;
        const int total = Wavetable::MaxFrames;
        for (int i = 0; i < total; ++i)
        {
            const float pos = (float) i / (float) (total - 1) * (float) (keys.size() - 1);
            const int k = juce::jmin ((int) pos, (int) keys.size() - 2);
            const float t = pos - (float) k;
            const auto& a = keys[(size_t) k];
            const auto& b = keys[(size_t) k + 1];
            Wavetable::Spectrum s (a.size());
            for (size_t h = 0; h < a.size(); ++h)
            {
                if (mode == 1)   // crossfade
                    s[h] = a[h] * (1.0f - t) + b[h] * t;
                else             // spectral: blend each harmonic's level, keep the nearer frame's phase
                {
                    const float mag = std::abs (a[h]) * (1.0f - t) + std::abs (b[h]) * t;
                    const float ph = std::arg (t < 0.5f ? a[h] : b[h]);
                    s[h] = std::polar (mag, ph);
                }
            }
            out.push_back (std::move (s));
        }
        return out;
    }

    // Lets the oscillator play what's drawn.
    void rebuild()
    {
        engine.previewTable (osc, std::make_shared<const Wavetable> ("Drawn", buildSpectra()));
        if (onTableChanged) onTableChanged (osc);
    }

    void cancel()
    {
        juce::String error;
        if (originalBuiltIn >= 0)
            engine.setBuiltInTable (osc, originalBuiltIn);
        else if (! originalFile.existsAsFile() || ! engine.importTable (osc, originalFile, error))
            engine.setBuiltInTable (osc, WavetableLibrary::indexForName ("Morph"));
        if (onTableChanged) onTableChanged (osc);
        setVisible (false);
    }

    void promptSave()
    {
        auto* w = new juce::AlertWindow ("Save wavetable", "Saved tables go in Wavetables, in the app's files.", juce::MessageBoxIconType::NoIcon);
        w->addTextEditor ("name", originalBuiltIn < 0 && originalFile != juce::File() ? originalFile.getFileNameWithoutExtension()
                                                                                       : juce::String ("My Table"));
        w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        juce::Component::SafePointer<WavetableEditor> safe (this);
        w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w] (int result)
        {
            if (safe == nullptr || result != 1) return;
            auto name = juce::File::createLegalFileName (w->getTextEditorContents ("name").trim());
            if (name.isEmpty()) name = "My Table";
            safe->save (name);
        }), true);
    }

    void save (const juce::String& name)
    {
        auto dir = Platform::documentsDirectory().getChildFile ("Wavetables");
        dir.createDirectory();
        auto file = dir.getChildFile (name + ".wav");

        // Render each frame of the finished table back to samples.
        const auto table = std::make_shared<const Wavetable> (name, buildSpectra());
        const int frames = table->getNumFrames();
        juce::AudioBuffer<float> buf (1, frames * N);
        for (int f = 0; f < frames; ++f)
            std::copy (table->getDisplayFrame (f), table->getDisplayFrame (f) + N, buf.getWritePointer (0, f * N));

        juce::String error;
        file.deleteFile();
        bool ok = false;
        if (auto out = file.createOutputStream())
        {
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::OutputStream> stream (out.release());
            auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (48000.0)
                                                                                         .withNumChannels (1)
                                                                                         .withBitsPerSample (32)
                                                                                         .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
            ok = writer != nullptr && writer->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
        }

        if (ok && engine.importTable (osc, file, error))
        {
            if (onTableChanged) onTableChanged (osc);
            setVisible (false);
            return;
        }
        juce::AlertWindow::showAsync (juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::WarningIcon)
                                          .withTitle ("Couldn't save").withMessage (error.isNotEmpty() ? error : "Couldn't write " + file.getFileName())
                                          .withButton ("OK"), nullptr);
    }

    //==========================================================================
    // The keyframes along the top. Tap one to edit it.
    struct FrameStrip : public juce::Component
    {
        explicit FrameStrip (WavetableEditor& o) : owner (o) {}

        juce::Rectangle<int> cell (int i) const
        {
            const int w = juce::jmin (96, getWidth() / juce::jmax (8, (int) owner.keyframes.size()));
            return { i * w, 0, w - 4, getHeight() };
        }

        void paint (juce::Graphics& g) override
        {
            for (int i = 0; i < (int) owner.keyframes.size(); ++i)
            {
                auto r = cell (i).toFloat();
                g.setColour (juce::Colour (0xff141417));
                g.fillRoundedRectangle (r, 4.0f);
                g.setColour (i == owner.selected ? MidiMorfColors::accent : juce::Colour (0xff3A3A40));
                g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, i == owner.selected ? 2.0f : 1.0f);

                juce::Path p;
                const auto& f = owner.keyframes[(size_t) i];
                auto plot = r.reduced (4.0f, 6.0f);
                for (int x = 0; x <= (int) plot.getWidth(); ++x)
                {
                    const float v = f[(size_t) juce::jmin (N - 1, (int) ((float) x / plot.getWidth() * (float) N))];
                    const juce::Point<float> pt (plot.getX() + (float) x, plot.getCentreY() - v * plot.getHeight() * 0.5f);
                    if (x == 0) p.startNewSubPath (pt); else p.lineTo (pt);
                }
                g.setColour (MidiMorfColors::textLight.withAlpha (0.8f));
                g.strokePath (p, juce::PathStrokeType (1.0f));
                g.setFont (juce::FontOptions (9.0f));
                g.drawText (juce::String (i + 1), r.reduced (3.0f), juce::Justification::topLeft);
            }
        }

        void mouseUp (const juce::MouseEvent& e) override
        {
            for (int i = 0; i < (int) owner.keyframes.size(); ++i)
                if (cell (i).contains (e.getPosition()))
                {
                    owner.selected = i;
                    repaint();
                    owner.canvas.repaint();
                }
        }

        WavetableEditor& owner;
    };

    //==========================================================================
    // The selected frame, drawn big. DRAW: paint the wave. HARMONICS: set
    // each harmonic's level.
    struct Canvas : public juce::Component
    {
        explicit Canvas (WavetableEditor& o) : owner (o) {}

        juce::Rectangle<float> plot() const { return getLocalBounds().toFloat().reduced (10.0f); }

        void paint (juce::Graphics& g) override
        {
            auto bounds = getLocalBounds().toFloat().reduced (1.5f);
            g.setColour (juce::Colour (0xff141417));
            g.fillRoundedRectangle (bounds, 5.0f);
            auto p = plot();
            const auto& f = owner.frame();

            if (owner.harmonicsView)
            {
                const auto h = harmonicsOf (f);
                const float w = p.getWidth() / (float) NumHarmonicBars;
                for (int k = 1; k <= NumHarmonicBars; ++k)
                {
                    const float v = h[(size_t) k];
                    auto bar = juce::Rectangle<float> (p.getX() + (float) (k - 1) * w + 1.0f, p.getBottom() - v * p.getHeight(),
                                                       w - 2.0f, v * p.getHeight());
                    g.setColour (MidiMorfColors::accent.withAlpha (0.25f + 0.75f * v));
                    g.fillRect (bar);
                }
                g.setColour (MidiMorfColors::textLight.withAlpha (0.4f));
                g.setFont (juce::FontOptions (10.0f));
                g.drawText ("1", p.withHeight (14.0f).translated (2.0f, p.getHeight() - 14.0f), juce::Justification::left);
                g.drawText (juce::String (NumHarmonicBars), p.withHeight (14.0f).translated (-2.0f, p.getHeight() - 14.0f), juce::Justification::right);
                return;
            }

            g.setColour (MidiMorfColors::textLight.withAlpha (0.12f));
            g.drawHorizontalLine ((int) p.getCentreY(), p.getX(), p.getRight());
            for (int i = 1; i < 4; ++i)
                g.drawVerticalLine ((int) (p.getX() + p.getWidth() * (float) i / 4.0f), p.getY(), p.getBottom());

            juce::Path wave;
            const int steps = (int) p.getWidth();
            for (int x = 0; x <= steps; ++x)
            {
                const float v = f[(size_t) juce::jmin (N - 1, (int) ((float) x / (float) steps * (float) N))];
                const juce::Point<float> pt (p.getX() + (float) x, p.getCentreY() - juce::jlimit (-1.0f, 1.0f, v) * p.getHeight() * 0.5f);
                if (x == 0) wave.startNewSubPath (pt); else wave.lineTo (pt);
            }
            g.setColour (MidiMorfColors::accent);
            g.strokePath (wave, juce::PathStrokeType (2.0f));
        }

        void mouseDown (const juce::MouseEvent& e) override { last = e.position; touch (e.position, e.position); }
        void mouseDrag (const juce::MouseEvent& e) override { touch (last, e.position); last = e.position; }
        void mouseUp (const juce::MouseEvent&) override     { owner.edited(); }

        void touch (juce::Point<float> from, juce::Point<float> to)
        {
            auto p = plot();
            auto& f = owner.frame();
            if (owner.harmonicsView)
            {
                auto h = harmonicsOf (f);
                const float w = p.getWidth() / (float) NumHarmonicBars;
                const int k = juce::jlimit (1, NumHarmonicBars, 1 + (int) ((to.x - p.getX()) / w));
                h[(size_t) k] = juce::jlimit (0.0f, 1.0f, (p.getBottom() - to.y) / p.getHeight());
                fromHarmonics (f, h);
            }
            else
            {
                // Fill every sample between the last touch and this one.
                auto index = [&] (float x) { return juce::jlimit (0, N - 1, (int) ((x - p.getX()) / p.getWidth() * (float) N)); };
                auto value = [&] (float y) { return juce::jlimit (-1.0f, 1.0f, (p.getCentreY() - y) / (p.getHeight() * 0.5f)); };
                int i0 = index (from.x), i1 = index (to.x);
                float v0 = value (from.y), v1 = value (to.y);
                if (i1 < i0) { std::swap (i0, i1); std::swap (v0, v1); }
                for (int i = i0; i <= i1; ++i)
                    f[(size_t) i] = i1 == i0 ? v1 : v0 + (v1 - v0) * (float) (i - i0) / (float) (i1 - i0);
            }
            repaint();
            owner.strip.repaint();
        }

        WavetableEditor& owner;
        juce::Point<float> last;
    };

    WavetableEngine& engine;
    int osc = 0;
    int originalBuiltIn = 0;
    juce::File originalFile;

    std::vector<Frame> keyframes;
    int selected = 0;
    bool harmonicsView = false;

    juce::Label title;
    juce::TextButton cancelButton, saveButton, addButton, removeButton, drawButton, harmonicsButton,
                     smoothButton, normalizeButton, invertButton;
    juce::ComboBox start, morph;
    FrameStrip strip { *this };
    Canvas canvas { *this };
    AuditionStrip audition;
};
