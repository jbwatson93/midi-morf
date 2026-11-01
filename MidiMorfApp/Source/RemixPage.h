#pragma once

#include <JuceHeader.h>
#include "Glassmorphism.h"
#include "SynthLookAndFeel.h"
#include "BreakImport.h"
#include "TrackRail.h"
#include "Mlr.h"

//==============================================================================
// The Remix grid's page: a monome grid, mlr style.
//
//   top row (control): G1–G4 stop a group, P1 P2 are the pattern recorders,
//                      R1–R4 recall, STOP stops every row, ALT
//   six rows:          16 pads each over the row's sample. Press one to play
//                      from there, hold one and press another to loop between
//                      them. The playhead lights up as it walks the row.
//   left of each row:  its sample, speed and group; tap for its settings.
//                      ■ stops it.
//
// ALT, then a pattern or recall key, clears it; ALT, then a row's pad, stops
// that row. Pressing a pad starts the track if it isn't playing.
class RemixPage : public juce::Component,
                  private juce::Timer
{
public:
    using Grid = MlrSource;

    RemixPage (MlrSource& m, LocalSequencers& s, int trackIndex) : mlr (m), seqs (s), track (trackIndex)
    {
        setLookAndFeel (&laf);

        playButton.setClickingTogglesState (false);
        playButton.onClick = [this]
        {
            seqs.setPlaying (track, ! seqs.isPlaying (track));
            repaint();
        };
        addAndMakeVisible (playButton);

        quantButton.onClick = [this] { showQuantizeMenu(); };
        addAndMakeVisible (quantButton);

        startTimerHz (30);
    }

    ~RemixPage() override
    {
        stopTimer();
        setLookAndFeel (nullptr);
    }

    int getTrack() const { return track; }

    std::function<void()> onChanged;   // persist

    // Re-reads the samples' waveforms and the settings shown.
    void refresh()
    {
        for (int r = 0; r < Grid::NumRows; ++r)
            peaks[(size_t) r] = mlr.getPeaks (r, 128);
        quantButton.setButtonText ("Q " + Grid::quantizeNames()[mlr.getQuantize()]);
        repaint();
    }

    void visibilityChanged() override { if (isVisible()) refresh(); }

    //==========================================================================
    void paint (juce::Graphics& g) override
    {
        Glassmorphism::paintPanel (g, getLocalBounds().toFloat(), remixPalette);

        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillRoundedRectangle (gridArea.toFloat().expanded (6.0f), 10.0f);

        const bool blinkSlow = (juce::Time::getMillisecondCounter() / 400) % 2 == 0;
        const bool blinkFast = (juce::Time::getMillisecondCounter() / 150) % 2 == 0;

        // The control row.
        for (int c = 0; c < Grid::Cols; ++c)
        {
            float level = 0.0f;
            juce::String label;
            if (c < 4)
            {
                label = "G" + juce::String (c + 1);
                level = mlr.isGroupPlaying (c + 1) ? 0.45f : 0.0f;
            }
            else if (c < 4 + Grid::NumPatterns)
            {
                const int p = c - 4;
                label = "P" + juce::String (p + 1);
                switch (mlr.getPatternState (p))
                {
                    case Grid::PatArmed:     level = blinkSlow ? 0.6f : 0.1f; break;
                    case Grid::PatRecording: level = blinkFast ? 1.0f : 0.3f; break;
                    case Grid::PatPlaying:   level = 1.0f; break;
                    case Grid::PatStopped:   level = 0.3f; break;
                    default:                 level = 0.0f; break;
                }
            }
            else if (c < 6 + Grid::NumRecalls)
            {
                const int slot = c - 6;
                label = "R" + juce::String (slot + 1);
                level = mlr.hasRecall (slot) ? 0.4f : 0.0f;
            }
            else if (c == StopCol)
            {
                label = "STOP";
                level = mlr.isAnyRowPlaying() ? 0.25f : 0.0f;
            }
            else if (c == AltCol)
            {
                label = "ALT";
                level = alt ? 1.0f : 0.0f;
            }
            paintPad (g, padBounds (0, c), level, isHeld (0, c), label, c >= 10 && c < StopCol);
        }

        // The sample rows.
        for (int r = 0; r < Grid::NumRows; ++r)
        {
            const auto v = mlr.getRowView (r);
            const int playCol = v.position >= 0.0f ? juce::jlimit (0, Grid::Cols - 1, (int) std::floor (v.position)) : -1;

            // The sample, faint, behind its pads.
            const auto row = rowArea (r).toFloat();
            const auto& pk = peaks[(size_t) r];
            if (! pk.empty())
            {
                const float mid = row.getCentreY(), half = row.getHeight() * 0.4f, w = row.getWidth() / (float) pk.size();
                g.setColour (TrackColours::get (track).withAlpha (v.playing ? 0.30f : 0.16f));
                for (size_t i = 0; i < pk.size(); ++i)
                    g.fillRect (row.getX() + (float) i * w, mid - pk[i] * half, juce::jmax (1.0f, w - 0.5f), juce::jmax (1.0f, pk[i] * half * 2.0f));
            }

            for (int c = 0; c < Grid::Cols; ++c)
            {
                float level = 0.0f;
                if (v.playing && v.looped && c >= v.loopA && c <= v.loopB) level = 0.22f;
                if (c == playCol) level = 1.0f;
                paintPad (g, padBounds (r + 1, c), level, isHeld (r + 1, c), {}, false);
            }

            paintRowHeader (g, r, v.playing);
        }

        // The control row's header: PLAY and the quantize button are
        // components; a tiny title under them.
        if (! mlr.hasSample (0) && ! mlr.hasSample (1))
        {
            g.setColour (MidiMorfColors::textLight.withAlpha (0.6f));
            g.setFont (juce::FontOptions (12.0f));
            g.drawFittedText ("Tap a row's name to load a break or a file into it.", gridArea.withTop (padBounds (1, 0).getY()).reduced (8),
                              juce::Justification::centred, 2);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (12, 10);
        headerArea = area.removeFromLeft (juce::jlimit (92, 140, area.getWidth() / 6));
        area.removeFromLeft (8);

        // Square-ish pads: as big as the height allows, the grid centred.
        const int rows = Grid::NumRows + 1;
        const int pitch = juce::jmin (area.getWidth() / Grid::Cols, (area.getHeight() - ControlGap) / rows);
        gridArea = juce::Rectangle<int> (area.getX(), area.getY(), pitch * Grid::Cols, pitch * rows + ControlGap)
                       .withCentre ({ area.getCentreX(), area.getCentreY() });
        headerArea = headerArea.withY (gridArea.getY()).withHeight (gridArea.getHeight());
        padPitch = pitch;

        auto controls = headerRect (-1);
        playButton.setBounds (controls.removeFromLeft (controls.getWidth() / 2).reduced (2));
        quantButton.setBounds (controls.reduced (2));
    }

    //==========================================================================
    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto p = e.getPosition();

        // A row's header: ■ stops it, the rest opens its settings.
        for (int r = 0; r < Grid::NumRows; ++r)
            if (headerRect (r).contains (p))
            {
                if (stopRect (r).contains (p)) { mlr.stopRow (r); repaint(); }
                else                           showRowSettings (r);
                return;
            }

        const auto [gr, c] = padAt (p);
        if (gr < 0)
            return;
        fingers[e.source.getIndex()] = { gr, c };

        if (gr == 0)
        {
            controlKey (c);
        }
        else
        {
            const int r = gr - 1;
            if (alt)
            {
                mlr.stopRow (r);
                alt = false;
            }
            else
            {
                ensurePlaying();
                // Another finger already on this row: loop between them.
                int other = -1;
                for (auto& [id, f] : fingers)
                    if (id != e.source.getIndex() && f.first == gr) other = f.second;
                if (other >= 0) mlr.loop (r, other, c);
                else            mlr.press (r, c);
            }
        }
        repaint();
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        fingers.erase (e.source.getIndex());
        repaint();
    }

private:
    static constexpr int StopCol = 14, AltCol = 15, ControlGap = 8;

    static inline const PagePalette remixPalette {
        juce::Colour (0xff1A1C20), juce::Colour (0xff08090B),
        juce::Colour (0xCC24272D), juce::Colour (0xD8121417), juce::Colour (0xCC000000)
    };

    // Grid rows: 0 is the control row, 1.. the sample rows.
    juce::Rectangle<int> padBounds (int gridRow, int col) const
    {
        const int y = gridArea.getY() + gridRow * padPitch + (gridRow > 0 ? ControlGap : 0);
        return { gridArea.getX() + col * padPitch, y, padPitch, padPitch };
    }

    juce::Rectangle<int> rowArea (int row) const
    {
        const auto first = padBounds (row + 1, 0);
        return first.withWidth (padPitch * Grid::Cols);
    }

    // -1 is the control row's header.
    juce::Rectangle<int> headerRect (int row) const
    {
        const auto pad = padBounds (row + 1, 0);
        return { headerArea.getX(), pad.getY(), headerArea.getWidth(), pad.getHeight() };
    }

    juce::Rectangle<int> stopRect (int row) const
    {
        auto h = headerRect (row);
        return h.removeFromRight (juce::jmin (30, h.getWidth() / 3));
    }

    std::pair<int, int> padAt (juce::Point<int> p) const
    {
        for (int gr = 0; gr <= Grid::NumRows; ++gr)
            for (int c = 0; c < Grid::Cols; ++c)
                if (padBounds (gr, c).contains (p))
                    return { gr, c };
        return { -1, -1 };
    }

    bool isHeld (int gridRow, int col) const
    {
        for (auto& [id, f] : fingers)
            if (f.first == gridRow && f.second == col) return true;
        return false;
    }

    // A monome key: dark, lit warm white by `level` (varibright).
    static void paintPad (juce::Graphics& g, juce::Rectangle<int> bounds, float level, bool held,
                          const juce::String& label, bool unused)
    {
        auto r = bounds.toFloat().reduced (juce::jmax (1.5f, bounds.getWidth() * 0.07f));
        const float corner = r.getWidth() * 0.16f;
        const auto lit = juce::Colour (0xffFFE9C2);

        g.setColour (juce::Colour (0xff2A2C30).withAlpha (unused ? 0.45f : 0.85f));
        g.fillRoundedRectangle (r, corner);
        if (level > 0.0f)
        {
            if (level >= 0.9f)
            {
                g.setColour (lit.withAlpha (0.25f));
                g.fillRoundedRectangle (r.expanded (2.5f), corner + 2.0f);   // glow
            }
            g.setColour (lit.withAlpha (0.12f + 0.88f * level));
            g.fillRoundedRectangle (r, corner);
        }
        g.setColour (held ? juce::Colours::white : juce::Colours::black.withAlpha (0.5f));
        g.drawRoundedRectangle (r, corner, held ? 2.0f : 1.0f);

        if (label.isNotEmpty())
        {
            g.setColour (level >= 0.5f ? juce::Colour (0xff1A1A1A) : MidiMorfColors::textLight.withAlpha (0.7f));
            g.setFont (juce::FontOptions (juce::jlimit (7.0f, 11.0f, r.getWidth() * 0.3f), juce::Font::bold));
            g.drawFittedText (label, r.toNearestInt(), juce::Justification::centred, 1, 0.6f);
        }
    }

    void paintRowHeader (juce::Graphics& g, int r, bool playing)
    {
        auto h = headerRect (r).toFloat().reduced (1.0f, 2.0f);
        const auto colour = TrackColours::get (track);
        g.setColour (juce::Colours::black.withAlpha (0.3f));
        g.fillRoundedRectangle (h, 6.0f);
        g.setColour (colour.withAlpha (playing ? 0.95f : 0.35f));
        g.fillRoundedRectangle (h.withWidth (4.0f), 2.0f);

        auto text = h.reduced (8.0f, 1.0f);
        const auto stop = stopRect (r).toFloat().reduced (5.0f, juce::jmax (4.0f, h.getHeight() * 0.28f));
        text.setRight (stop.getX() - 2.0f);

        const auto& o = mlr.getOrigin (r);
        const juce::String name = o.kind == Grid::Origin::None ? "Empty" : o.name;
        const float size = juce::jlimit (9.0f, 13.0f, h.getHeight() * 0.32f);
        g.setColour (MidiMorfColors::textLight.withAlpha (o.kind == Grid::Origin::None ? 0.4f : 0.9f));
        g.setFont (juce::FontOptions (size, juce::Font::bold));
        g.drawFittedText (juce::String (r + 1) + "  " + name, text.removeFromTop (text.getHeight() * 0.55f).toNearestInt(),
                          juce::Justification::bottomLeft, 1, 0.8f);

        juce::StringArray detail;
        detail.add (speedText (mlr.getSpeed (r)));
        if (mlr.getReverse (r)) detail.add ("REV");
        if (mlr.getGroup (r) > 0) detail.add ("G" + juce::String (mlr.getGroup (r)));
        g.setColour (MidiMorfColors::textLight.withAlpha (0.5f));
        g.setFont (juce::FontOptions (size * 0.82f));
        g.drawFittedText (detail.joinIntoString ("  "), text.toNearestInt(), juce::Justification::topLeft, 1, 0.8f);

        g.setColour (MidiMorfColors::textLight.withAlpha (playing ? 0.8f : 0.3f));
        const float s = juce::jmin (stop.getWidth(), stop.getHeight(), 12.0f);
        g.fillRoundedRectangle (stop.withSizeKeepingCentre (s, s), 2.0f);
    }

    static juce::String speedText (int octaves)
    {
        switch (octaves)
        {
            case -2: return juce::String::fromUTF8 ("\xc2\xbc" "x");
            case -1: return juce::String::fromUTF8 ("\xc2\xbd" "x");
            case 1:  return "2x";
            case 2:  return "4x";
            default: return "1x";
        }
    }

    void ensurePlaying()
    {
        if (! seqs.isPlaying (track))
            seqs.setPlaying (track, true);
    }

    void controlKey (int c)
    {
        if (c < 4)
        {
            mlr.stopGroup (c + 1);
        }
        else if (c < 4 + Grid::NumPatterns)
        {
            if (alt) { mlr.clearPattern (c - 4); alt = false; }
            else     { ensurePlaying(); mlr.patternKey (c - 4); }
        }
        else if (c < 6 + Grid::NumRecalls)
        {
            if (alt) { mlr.clearRecall (c - 6); alt = false; }
            else     { if (mlr.hasRecall (c - 6)) ensurePlaying(); mlr.recallKey (c - 6); }
        }
        else if (c == StopCol)
        {
            mlr.stopAll();
        }
        else if (c == AltCol)
        {
            alt = ! alt;
        }
    }

    void showQuantizeMenu()
    {
        juce::PopupMenu menu;
        menu.addSectionHeader ("Presses land on");
        for (int q = 0; q < Grid::NumQuantize; ++q)
            menu.addItem (q + 1, Grid::quantizeNames()[q], true, mlr.getQuantize() == q);
        juce::Component::SafePointer<RemixPage> safe (this);
        menu.showMenuAsync (PopupStyle::attached (menu, quantButton, 34), [safe] (int result)
        {
            if (safe == nullptr || result <= 0) return;
            safe->mlr.setQuantize (result - 1);
            safe->refresh();
            safe->changed();
        });
    }

    void changed() { if (onChanged) onChanged(); }

    //==========================================================================
    // A row's settings, in a callout beside its name.
    class RowSettings : public juce::Component
    {
    public:
        RowSettings (RemixPage& p, int r) : page (p), row (r)
        {
            auto& m = page.mlr;

            sample.addSectionHeading ("Breaks");
            sample.addItemList (BreakLibrary::builtInNames(), 1);
            sample.addSeparator();
            sample.addItem ("Import a file...", ImportId);
            sample.addItem ("Empty this row", EmptyId);
            sample.onChange = [this] { samplePicked(); };
            addAndMakeVisible (sample);

            auto setUp = [this] (juce::TextButton& b, std::function<void()> f) { b.onClick = std::move (f); addAndMakeVisible (b); };
            setUp (lengthDown, [this] { stepLength (-1); });
            setUp (lengthUp,   [this] { stepLength (1); });
            setUp (speedDown,  [this, &m] { m.setSpeed (row, m.getSpeed (row) - 1); update(); });
            setUp (speedUp,    [this, &m] { m.setSpeed (row, m.getSpeed (row) + 1); update(); });
            reverse.setClickingTogglesState (true);
            setUp (reverse,    [this, &m] { m.setReverse (row, reverse.getToggleState()); update(); });

            for (int g = 0; g <= Grid::NumGroups; ++g)
            {
                auto* b = groups.add (new juce::TextButton (g == 0 ? juce::String ("-") : juce::String (g)));
                b->setClickingTogglesState (true);
                b->setRadioGroupId (7171);
                b->onClick = [this, g, &m] { m.setGroup (row, g); update(); };
                addAndMakeVisible (b);
            }

            volume.setSliderStyle (juce::Slider::LinearHorizontal);
            volume.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            volume.setRange (0.0, 1.5, 0.01);
            volume.onValueChange = [this, &m] { m.setVolume (row, (float) volume.getValue()); };
            volume.onDragEnd = [this] { page.changed(); };
            addAndMakeVisible (volume);

            for (auto* l : { &lengthLabel, &speedLabel })
            {
                l->setJustificationType (juce::Justification::centred);
                l->setFont (juce::FontOptions (14.0f, juce::Font::bold));
                addAndMakeVisible (l);
            }

            setSize (300, 196);
            update();
            ready = true;
        }

        void paint (juce::Graphics& g) override
        {
            g.setColour (MidiMorfColors::textLight.withAlpha (0.6f));
            g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
            for (auto& [text, y] : captions)
                g.drawText (text, 10, y, 80, 30, juce::Justification::centredLeft);
        }

        void resized() override
        {
            auto area = getLocalBounds().reduced (10, 8);
            captions.clear();
            auto line = [&] (const char* caption)
            {
                auto r = area.removeFromTop (30);
                captions.push_back ({ caption, r.getY() });
                area.removeFromTop (6);
                return r.withTrimmedLeft (78);
            };

            sample.setBounds (line ("SAMPLE"));
            {
                auto r = line ("LENGTH");
                lengthDown.setBounds (r.removeFromLeft (36).reduced (1));
                lengthUp.setBounds (r.removeFromRight (36).reduced (1));
                lengthLabel.setBounds (r);
            }
            {
                auto r = line ("SPEED");
                reverse.setBounds (r.removeFromRight (56).reduced (1));
                r.removeFromRight (6);
                speedDown.setBounds (r.removeFromLeft (36).reduced (1));
                speedUp.setBounds (r.removeFromRight (36).reduced (1));
                speedLabel.setBounds (r);
            }
            volume.setBounds (line ("VOLUME"));
            {
                auto r = line ("GROUP");
                const int w = r.getWidth() / groups.size();
                for (auto* b : groups) b->setBounds (r.removeFromLeft (w).reduced (1));
            }
        }

    private:
        static constexpr int ImportId = 1000, EmptyId = 1001;

        void update()
        {
            auto& m = page.mlr;
            const auto& o = m.getOrigin (row);
            const int builtIn = o.kind == Grid::Origin::BuiltIn ? BreakLibrary::indexForName (o.name) : -1;
            if (builtIn >= 0) sample.setSelectedId (builtIn + 1, juce::dontSendNotification);
            else              sample.setText (o.kind == Grid::Origin::None ? juce::String ("Empty") : o.name, juce::dontSendNotification);

            lengthLabel.setText (beatsText (m.getLengthBeats (row)), juce::dontSendNotification);
            speedLabel.setText (speedText (m.getSpeed (row)), juce::dontSendNotification);
            reverse.setToggleState (m.getReverse (row), juce::dontSendNotification);
            volume.setValue (m.getVolume (row), juce::dontSendNotification);
            for (int g = 0; g < groups.size(); ++g)
                groups[g]->setToggleState (m.getGroup (row) == g, juce::dontSendNotification);
            page.refresh();
            if (ready) page.changed();   // not just for opening
        }

        static juce::String beatsText (double beats)
        {
            const int b = juce::roundToInt (beats);
            if (b >= 4 && b % 4 == 0) return juce::String (b / 4) + (b == 4 ? " bar" : " bars");
            return juce::String (b) + (b == 1 ? " beat" : " beats");
        }

        // The length the row's sample spans: if it isn't cut to whole bars,
        // set it here so it plays in time.
        void stepLength (int direction)
        {
            static const double steps[] { 1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64 };
            const double now = page.mlr.getLengthBeats (row);
            double next = now;
            if (direction > 0) { for (double s : steps) if (s > now + 0.01) { next = s; break; } }
            else               { for (int i = (int) std::size (steps) - 1; i >= 0; --i) if (steps[i] < now - 0.01) { next = steps[i]; break; } }
            page.mlr.setLengthBeats (row, next);
            update();
        }

        void samplePicked()
        {
            const int id = sample.getSelectedId();
            if (id == ImportId)      { page.importInto (row); update(); return; }
            if (id == EmptyId)       { page.mlr.clearSample (row); }
            else if (id > 0)         { page.mlr.setBuiltIn (row, id - 1); }
            update();
        }

        RemixPage& page;
        const int row;
        juce::ComboBox sample;
        juce::TextButton lengthDown { "-" }, lengthUp { "+" }, speedDown { "-" }, speedUp { "+" }, reverse { "REV" };
        juce::Label lengthLabel, speedLabel;
        juce::Slider volume;
        juce::OwnedArray<juce::TextButton> groups;
        std::vector<std::pair<juce::String, int>> captions;
        bool ready = false;
    };

    void showRowSettings (int r)
    {
        auto content = std::make_unique<RowSettings> (*this, r);
        content->setLookAndFeel (&laf);
        auto& box = juce::CallOutBox::launchAsynchronously (std::move (content), headerRect (r), this);
        box.setArrowSize (12.0f);
    }

    void importInto (int r)
    {
        juce::Component::SafePointer<RemixPage> safe (this);
        chooser = std::make_unique<juce::FileChooser> ("Import a sample", juce::File(), BreakImport::patterns);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [safe, r] (const juce::FileChooser& fc)
            {
                if (safe == nullptr) return;
                const auto url = fc.getURLResult();
                if (url.isEmpty()) return;
                juce::String error;
                const auto dest = BreakImport::copyIn (url, error);
                if (dest == juce::File() || ! safe->mlr.importFile (r, dest, error))
                {
                    juce::AlertWindow::showAsync (juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::WarningIcon)
                                                      .withTitle ("Import failed").withMessage (error).withButton ("OK"), nullptr);
                    return;
                }
                safe->refresh();
                safe->changed();
            });
    }

    void timerCallback() override
    {
        if (! isShowing())
            return;
        playButton.setButtonText (seqs.isPlaying (track) ? "STOP" : "PLAY");
        playButton.setToggleState (seqs.isPlaying (track), juce::dontSendNotification);
        repaint (gridArea.expanded (8));
        repaint (headerArea);
    }

    MlrSource& mlr;
    LocalSequencers& seqs;
    const int track;

    std::array<std::vector<float>, Grid::NumRows> peaks;
    std::map<int, std::pair<int, int>> fingers;   // touch id -> grid row, column
    bool alt = false;

    SynthLookAndFeel laf;   // declared before the children
    juce::TextButton playButton { "PLAY" }, quantButton { "Q 1/16" };
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<int> gridArea, headerArea;
    int padPitch = 30;
};
