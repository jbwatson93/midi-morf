#pragma once

#include <JuceHeader.h>
#include "Glassmorphism.h"
#include "SynthLookAndFeel.h"
#include "TrackRail.h"
#include "MidiMorfProtocol.h"
#include "SessionModel.h"

static const PagePalette sessionPagePalette {
    juce::Colour (0xff22201E),
    juce::Colour (0xff100F0E),
    juce::Colour (0xCC34302C),
    juce::Colour (0xD8221F1C),
    juce::Colour (0xCC000000)
};

//==============================================================================
// Session (what Home grew into): the clips, and each track's setup.
//
//   • the grid — tracks are rows, scenes run left to right (it scrolls).
//     A ▶ over each scene launches the whole column. Tap a clip to launch it
//     on the next bar; tap the playing one to edit it on its page; hold one
//     for Duplicate / Delete. "+" makes a new clip from the track's current
//     pattern. Clips belong to sequencer tracks (Step-Seq, Slice-Seq, Drum
//     Grid); controller tracks get theirs when recording arrives.
//   • the track column — what plays the track, its sound, its volume, ■.
//   • MIXER (instead of the grid) — per track: its sound (tap to edit it),
//     pan and mute. Volume is in the track column.
//   • the bottom strip — the project, CLIPS / MIXER, launch quantize, and
//     the pages that belong to no track.
//
// MainComponent owns the tracks and the clips and wires the callbacks.
class SessionPage : public juce::Component,
                    private juce::Timer
{
public:
    struct Callbacks
    {
        // Track setup
        std::function<int (int track)> getInstrument;            // a page index, or -1
        std::function<void (int track, int mode)> setInstrument;
        std::function<int (int track)> getSound;                  // AudioEngine::Sound as int
        std::function<void (int track, int sound)> setSound;
        std::function<float (int track)> getVolume;
        std::function<void (int track, float gain)> setVolume;

        // Clips
        std::function<int (int track)> sequencerOf;              // LocalSequencers::Which, or -1 for a controller
        std::function<bool (int track)> isRunning;               // its sequencer is playing
        std::function<void (int track, int scene)> launchClip, newClip, editClip, duplicateClip, deleteClip;
        std::function<void (int scene)> launchScene;
        std::function<void (int track)> stopTrack;
        std::function<int()> getLaunchQuantize;                  // 0 bar, 1 beat, 2 off
        std::function<void (int)> setLaunchQuantize;

        // Mixer
        std::function<juce::String (int track)> soundName;
        std::function<void (int track)> openSound;
        std::function<float (int track)> getPan;
        std::function<void (int track, float pan)> setPan;
        std::function<bool (int track)> getMuted;
        std::function<void (int track, bool muted)> setMuted;

        std::function<void (int pageIndex)> openPage;
        std::function<void()> openProjects;
        std::function<juce::String()> projectName;
        std::function<void()> changed;                            // persist
    };

    // The page indices the bottom buttons open, and the Drum Grid's (an
    // instrument that isn't a ControllerMode).
    struct Pages { int transport, fx, settings, about, drumGrid; };

    SessionPage (SessionModel& m, Callbacks cb, Pages p)
        : model (m), callbacks (std::move (cb)), pages (p), grid (*this)
    {
        setLookAndFeel (&laf);

        viewport.setViewedComponent (&grid, false);
        viewport.setScrollBarsShown (false, false, false, true);
        viewport.setScrollOnDragMode (juce::Viewport::ScrollOnDragMode::all);
        addAndMakeVisible (viewport);

        for (int t = 0; t < TrackRack::NumTracks; ++t)
            addAndMakeVisible (rows.add (new Row (*this, t)));

        projectButton.onClick = [this] { if (callbacks.openProjects) callbacks.openProjects(); };
        addAndMakeVisible (projectButton);

        for (int v = 0; v < 2; ++v)
        {
            auto* b = viewButtons.add (new juce::TextButton (v == 0 ? "CLIPS" : "MIXER"));
            b->setClickingTogglesState (true);
            b->setRadioGroupId (8181);
            b->onClick = [this, v] { showMixer (v == 1); };
            addAndMakeVisible (b);
        }
        viewButtons[0]->setToggleState (true, juce::dontSendNotification);

        quantize.addItem ("Launch: bar", 1);
        quantize.addItem ("Launch: beat", 2);
        quantize.addItem ("Launch: now", 3);
        quantize.onChange = [this]
        {
            if (callbacks.setLaunchQuantize) callbacks.setLaunchQuantize (quantize.getSelectedId() - 1);
            if (callbacks.changed) callbacks.changed();
        };
        addAndMakeVisible (quantize);

        for (int t = 0; t < TrackRack::NumTracks; ++t)
            addChildComponent (strips.add (new Strip (*this, t)));

        const std::pair<const char*, int> links[] = {
            { "Transport", pages.transport }, { "Morf FX", pages.fx },
            { "Settings", pages.settings },   { "About", pages.about } };
        for (auto& [name, page] : links)
        {
            auto* b = linkButtons.add (new juce::TextButton (name));
            b->setLookAndFeel (&glassLnf);
            b->setColour (juce::TextButton::textColourOffId, MidiMorfColors::textLight);
            const int target = page;
            b->onClick = [this, target] { if (callbacks.openPage) callbacks.openPage (target); };
            addAndMakeVisible (b);
        }

        startTimerHz (15);   // playing / queued clips
    }

    ~SessionPage() override
    {
        stopTimer();
        for (auto* b : linkButtons)
            b->setLookAndFeel (nullptr);
        setLookAndFeel (nullptr);
    }

    // Re-read everything: the setup and the clips can change elsewhere.
    void refresh()
    {
        for (auto* r : rows)
            r->refresh();
        for (auto* st : strips)
            st->refresh();
        quantize.setSelectedId ((callbacks.getLaunchQuantize ? callbacks.getLaunchQuantize() : 0) + 1, juce::dontSendNotification);
        projectButton.setButtonText ("Project: " + (callbacks.projectName ? callbacks.projectName() : juce::String()));
        grid.repaint();
    }

    void visibilityChanged() override { if (isVisible()) refresh(); }

    void paint (juce::Graphics& g) override
    {
        Glassmorphism::paintPanel (g, getLocalBounds().toFloat(), sessionPagePalette);

        if (mixerShown && ! strips.isEmpty())
        {
            // Column heads over the strips, from the first strip's layout.
            const auto off = strips[0]->getPosition();
            g.setColour (MidiMorfColors::textLight.withAlpha (0.5f));
            g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
            for (auto [label, r] : { std::pair { "SOUND", strips[0]->sound.getBounds() },
                                     std::pair { "PAN", strips[0]->pan.getBounds() },
                                     std::pair { "MUTE", strips[0]->mute.getBounds() } })
                g.drawText (label, r.translated (off.x, 0).withY (mixerHead.getY()).withHeight (mixerHead.getHeight()),
                            juce::Justification::centredLeft);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (8, 6);

        auto bottom = area.removeFromBottom (32);
        area.removeFromBottom (6);
        projectButton.setBounds (bottom.removeFromLeft (juce::jmin (180, bottom.getWidth() / 4)).reduced (2));
        bottom.removeFromLeft (6);
        for (auto* b : viewButtons)
            b->setBounds (bottom.removeFromLeft (64).reduced (2));
        bottom.removeFromLeft (6);
        quantize.setBounds (bottom.removeFromLeft (124).reduced (2));
        bottom.removeFromLeft (6);
        const int lw = bottom.getWidth() / juce::jmax (1, linkButtons.size());
        for (auto* b : linkButtons)
            b->setBounds (bottom.removeFromLeft (lw).reduced (2));

        // Track column on the right, lined up with the grid's rows.
        auto column = area.removeFromRight (juce::jmin (270, area.getWidth() * 38 / 100));
        area.removeFromRight (8);

        const int headH = SceneHeaderH;
        column.removeFromTop (headH);
        const int rowH = column.getHeight() / TrackRack::NumTracks;
        for (int t = 0; t < rows.size(); ++t)
            rows[t]->setBounds (column.getX(), column.getY() + t * rowH, column.getWidth(), rowH - 4);

        viewport.setBounds (area);
        for (int t = 0; t < strips.size(); ++t)
            strips[t]->setBounds (area.getX(), area.getY() + headH + t * rowH, area.getWidth(), rowH - 4);
        mixerHead = area.withHeight (headH);
        const int sceneW = juce::jmax (84, area.getWidth() / 4);
        grid.setSize (sceneW * SessionModel::NumScenes, area.getHeight());
        grid.sceneW = sceneW;
        grid.rowH = rowH;
    }

private:
    static constexpr int SceneHeaderH = 30;

    void showMixer (bool m)
    {
        mixerShown = m;
        viewport.setVisible (! m);
        for (auto* st : strips)
        {
            st->setVisible (m);
            st->refresh();
        }
        repaint();
    }

    //==========================================================================
    // A track's mixer strip (compact): its sound, pan, mute. Sends and insert
    // FX come with the full mixer.
    struct Strip : public juce::Component
    {
        Strip (SessionPage& o, int t) : owner (o), track (t)
        {
            sound.onClick = [this] { if (owner.callbacks.openSound) owner.callbacks.openSound (track); };
            sound.setColour (juce::TextButton::textColourOffId, TrackColours::get (t).brighter (0.3f));
            addAndMakeVisible (sound);

            pan.setSliderStyle (juce::Slider::LinearHorizontal);
            pan.setTextBoxStyle (juce::Slider::TextBoxRight, false, 44, 20);
            pan.setRange (-1.0, 1.0, 0.01);
            pan.setDoubleClickReturnValue (true, 0.0);
            pan.textFromValueFunction = [] (double v)
            {
                const int pct = juce::roundToInt (std::abs (v) * 100.0);
                return pct == 0 ? juce::String ("C") : juce::String (pct) + (v < 0 ? "L" : "R");
            };
            pan.updateText();
            pan.setColour (juce::Slider::thumbColourId, TrackColours::get (t));
            pan.onValueChange = [this] { if (owner.callbacks.setPan) owner.callbacks.setPan (track, (float) pan.getValue()); };
            pan.onDragEnd = [this] { if (owner.callbacks.changed) owner.callbacks.changed(); };
            addAndMakeVisible (pan);

            mute.setClickingTogglesState (true);
            mute.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffB03A2E));
            mute.onClick = [this]
            {
                if (owner.callbacks.setMuted) owner.callbacks.setMuted (track, mute.getToggleState());
                if (owner.callbacks.changed) owner.callbacks.changed();
            };
            addAndMakeVisible (mute);
        }

        void refresh()
        {
            const auto name = owner.callbacks.soundName ? owner.callbacks.soundName (track) : juce::String();
            sound.setButtonText (name);   // no pencil glyph: iOS draws those as emoji
            sound.setEnabled (owner.callbacks.getSound && owner.callbacks.getSound (track) != 0);
            pan.setValue (owner.callbacks.getPan ? owner.callbacks.getPan (track) : 0.0f, juce::dontSendNotification);
            mute.setToggleState (owner.callbacks.getMuted && owner.callbacks.getMuted (track), juce::dontSendNotification);
        }

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat();
            g.setColour (juce::Colours::black.withAlpha (0.25f));
            g.fillRoundedRectangle (r, 8.0f);
            g.setColour (TrackColours::get (track).withAlpha (0.8f));
            g.fillRoundedRectangle (r.removeFromLeft (5.0f), 2.0f);
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (10, 6);
            mute.setBounds (r.removeFromRight (64).withSizeKeepingCentre (64, juce::jmin (36, r.getHeight())));
            r.removeFromRight (10);
            sound.setBounds (r.removeFromLeft (r.getWidth() * 45 / 100).withSizeKeepingCentre (r.getWidth() * 45 / 100, juce::jmin (36, r.getHeight())));
            r.removeFromLeft (10);
            pan.setBounds (r.withSizeKeepingCentre (r.getWidth(), juce::jmin (36, r.getHeight())));
        }

        SessionPage& owner;
        const int track;
        juce::TextButton sound, mute { "MUTE" };
        juce::Slider pan;
    };

    //==========================================================================
    // The clip grid: scene ▶ buttons on top, a cell per track and scene.
    struct Grid : public juce::Component
    {
        explicit Grid (SessionPage& o) : owner (o) {}

        juce::Rectangle<int> cellBounds (int track, int scene) const
        {
            return { scene * sceneW, SceneHeaderH + track * rowH, sceneW, rowH - 4 };
        }

        juce::Rectangle<int> sceneButton (int scene) const
        {
            return { scene * sceneW, 0, sceneW, SceneHeaderH - 4 };
        }

        void paint (juce::Graphics& g) override
        {
            auto& m = owner.model;
            const auto& cb = owner.callbacks;

            for (int s = 0; s < SessionModel::NumScenes; ++s)
            {
                auto b = sceneButton (s).reduced (3, 0).toFloat();
                g.setColour (juce::Colours::black.withAlpha (0.35f));
                g.fillRoundedRectangle (b, 5.0f);
                g.setColour (MidiMorfColors::textLight.withAlpha (0.8f));
                g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
                auto label = b.withSizeKeepingCentre (34.0f, b.getHeight());
                playIcon (g, label.removeFromLeft (12.0f).withSizeKeepingCentre (9.0f, 10.0f));
                g.drawText (juce::String (s + 1), label, juce::Justification::centredRight);
            }

            const bool blink = (juce::Time::getMillisecondCounter() / 300) % 2 == 0;

            for (int t = 0; t < TrackRack::NumTracks; ++t)
            {
                const auto colour = TrackColours::get (t);
                const int seq = cb.sequencerOf ? cb.sequencerOf (t) : -1;
                const bool running = cb.isRunning && cb.isRunning (t);

                for (int s = 0; s < SessionModel::NumScenes; ++s)
                {
                    auto r = cellBounds (t, s).reduced (3, 2).toFloat();

                    if (seq < 0)
                    {
                        g.setColour (juce::Colours::black.withAlpha (0.15f));
                        g.fillRoundedRectangle (r, 6.0f);
                        continue;
                    }

                    const auto& c = m.clip (t, s);
                    if (! c.has_value())
                    {
                        g.setColour (juce::Colours::black.withAlpha (0.25f));
                        g.fillRoundedRectangle (r, 6.0f);
                        g.setColour (MidiMorfColors::textLight.withAlpha (0.25f));
                        g.setFont (juce::FontOptions (18.0f));
                        g.drawText ("+", r, juce::Justification::centred);
                        continue;
                    }

                    const bool fits    = c->sequencer == seq;
                    const bool playing = m.getPlaying (t) == s;
                    const bool queued  = m.getQueued (t) == s;

                    g.setColour (fits ? colour.withAlpha (playing ? 0.85f : 0.4f) : juce::Colours::grey.withAlpha (0.25f));
                    g.fillRoundedRectangle (r, 6.0f);
                    if (queued && blink)
                    {
                        g.setColour (juce::Colours::white.withAlpha (0.8f));
                        g.drawRoundedRectangle (r.reduced (1.0f), 6.0f, 2.0f);
                    }

                    auto text = r.reduced (6.0f, 2.0f);
                    if (playing)
                    {
                        // ▶ when it's sounding, ■ when it's the clip on the page but stopped.
                        g.setColour (juce::Colours::white);
                        auto icon = text.removeFromLeft (14.0f).withSizeKeepingCentre (9.0f, 10.0f).withX (text.getX() - 14.0f);
                        if (running) playIcon (g, icon);
                        else         g.fillRect (icon.withSizeKeepingCentre (8.0f, 8.0f));
                    }
                    g.setColour (fits ? juce::Colours::white.withAlpha (playing ? 1.0f : 0.85f) : MidiMorfColors::textLight.withAlpha (0.4f));
                    g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
                    g.drawFittedText (c->name, text.toNearestInt(), juce::Justification::centredLeft, 2);
                }

                if (seq < 0)
                {
                    // A controller track: say why there's nothing here, once.
                    auto row = juce::Rectangle<int> (0, SceneHeaderH + t * rowH, sceneW * 3, rowH - 4).reduced (10, 0);
                    g.setColour (MidiMorfColors::textLight.withAlpha (0.35f));
                    g.setFont (juce::FontOptions (12.0f));
                    g.drawFittedText ("Played live. Clips for this track come with recording.", row, juce::Justification::centredLeft, 2);
                }
            }
        }

        // Drawn, not typed: iOS shows the ▶ character as a blue emoji.
        static void playIcon (juce::Graphics& g, juce::Rectangle<float> r)
        {
            juce::Path p;
            p.addTriangle (r.getX(), r.getY(), r.getX(), r.getBottom(), r.getRight(), r.getCentreY());
            g.fillPath (p);
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            downAt = e.getPosition();
            held = false;
            pressStart = juce::Time::getMillisecondCounter();
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            // Holding still for half a second is "hold"; moving is scrolling.
            if (! held && e.getDistanceFromDragStart() < 8 && juce::Time::getMillisecondCounter() - pressStart > 500)
            {
                held = true;
                hold (downAt);
            }
        }

        void mouseUp (const juce::MouseEvent& e) override
        {
            if (held || e.getDistanceFromDragStart() >= 8)
                return;
            if (juce::Time::getMillisecondCounter() - pressStart > 500)
            {
                hold (downAt);
                return;
            }
            tap (downAt);
        }

        std::pair<int, int> cellAt (juce::Point<int> p) const
        {
            if (p.y < SceneHeaderH) return { -1, p.x / juce::jmax (1, sceneW) };
            return { (p.y - SceneHeaderH) / juce::jmax (1, rowH), p.x / juce::jmax (1, sceneW) };
        }

        void tap (juce::Point<int> p)
        {
            const auto& cb = owner.callbacks;
            const auto [t, s] = cellAt (p);
            if (! juce::isPositiveAndBelow (s, SessionModel::NumScenes)) return;

            if (t < 0)
            {
                if (cb.launchScene) cb.launchScene (s);
            }
            else if (juce::isPositiveAndBelow (t, TrackRack::NumTracks) && cb.sequencerOf && cb.sequencerOf (t) >= 0)
            {
                auto& m = owner.model;
                if (! m.hasClip (t, s))                                    { if (cb.newClip) cb.newClip (t, s); }
                else if (m.getPlaying (t) == s && cb.isRunning && cb.isRunning (t)) { if (cb.editClip) cb.editClip (t, s); }
                else                                                       { if (cb.launchClip) cb.launchClip (t, s); }
            }
            repaint();
        }

        void hold (juce::Point<int> p)
        {
            const auto [t, s] = cellAt (p);
            if (! juce::isPositiveAndBelow (t, TrackRack::NumTracks) || ! owner.model.hasClip (t, s))
                return;

            juce::PopupMenu menu;
            menu.addItem (1, "Edit");
            menu.addItem (2, "Duplicate");
            menu.addSeparator();
            menu.addItem (3, "Delete");
            juce::Component::SafePointer<Grid> safe (this);
            const int track = t, scene = s;
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (cellBounds (t, s))),
                [safe, track, scene] (int result)
                {
                    if (safe == nullptr) return;
                    const auto& cb = safe->owner.callbacks;
                    if (result == 1 && cb.editClip)      cb.editClip (track, scene);
                    if (result == 2 && cb.duplicateClip) cb.duplicateClip (track, scene);
                    if (result == 3 && cb.deleteClip)    cb.deleteClip (track, scene);
                    safe->repaint();
                });
        }

        SessionPage& owner;
        int sceneW = 100, rowH = 60;
        juce::Point<int> downAt;
        juce::uint32 pressStart = 0;
        bool held = false;
    };

    //==========================================================================
    // Instrument choices, as page indices. Combo ids are mode + 1; "Nothing"
    // is NoneId.
    static constexpr int NoneId = 100;
    std::vector<std::pair<int, const char*>> instruments() const
    {
        using CM = MidiMorf::ControllerMode;
        return { { (int) CM::MorfChord, "Morf-Chord" }, { (int) CM::RoseChord, "Rose-Chord" },
                 { (int) CM::Omnichord, "Strum-Chord" }, { (int) CM::MorfPad, "Morf-Pad" },
                 { (int) CM::DualMPad, "Dual M-Pad" },
                 { (int) CM::StepSequencer, "Step-Seq" }, { (int) CM::SliceSequencer, "Slice-Seq" },
                 { pages.drumGrid, "Drum Grid" } };
    }

    // A track's column: T1, what plays it and its sound side by side, then
    // its volume and ■.
    struct Row : public juce::Component
    {
        Row (SessionPage& o, int t) : owner (o), track (t)
        {
            for (auto& [mode, name] : owner.instruments())
                instrument.addItem (name, mode + 1);
            instrument.addSeparator();
            instrument.addItem ("Nothing", NoneId);
            instrument.onChange = [this]
            {
                const int id = instrument.getSelectedId();
                if (owner.callbacks.setInstrument)
                    owner.callbacks.setInstrument (track, id == NoneId ? -1 : id - 1);
                owner.refresh();   // the instrument may have left another row
                if (owner.callbacks.changed) owner.callbacks.changed();
            };
            addAndMakeVisible (instrument);

            sound.addItem ("Synth", 1 + 1);
            sound.addItem ("Sampler", 2 + 1);
            sound.addItem ("Drums", 3 + 1);
            sound.addItem ("Empty", 0 + 1);
            sound.onChange = [this]
            {
                if (owner.callbacks.setSound)
                    owner.callbacks.setSound (track, sound.getSelectedId() - 1);
                owner.refresh();
                if (owner.callbacks.changed) owner.callbacks.changed();
            };
            addAndMakeVisible (sound);

            volume.setSliderStyle (juce::Slider::LinearHorizontal);
            volume.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            volume.setRange (-40.0, juce::Decibels::gainToDecibels (TrackRack::MaxVolume), 0.1);
            volume.setDoubleClickReturnValue (true, 0.0);
            volume.setColour (juce::Slider::thumbColourId, TrackColours::get (track));
            volume.setColour (juce::Slider::trackColourId, TrackColours::get (track).withAlpha (0.6f));
            volume.onValueChange = [this]
            {
                const double db = volume.getValue();
                if (owner.callbacks.setVolume)
                    owner.callbacks.setVolume (track, db <= -40.0 ? 0.0f : juce::Decibels::decibelsToGain ((float) db));
            };
            volume.onDragEnd = [this] { if (owner.callbacks.changed) owner.callbacks.changed(); };
            addAndMakeVisible (volume);

            stop.onClick = [this] { if (owner.callbacks.stopTrack) owner.callbacks.stopTrack (track); owner.grid.repaint(); };
            addAndMakeVisible (stop);
        }

        void refresh()
        {
            const int mode = owner.callbacks.getInstrument ? owner.callbacks.getInstrument (track) : -1;
            instrument.setSelectedId (mode >= 0 ? mode + 1 : NoneId, juce::dontSendNotification);
            sound.setSelectedId ((owner.callbacks.getSound ? owner.callbacks.getSound (track) : 0) + 1, juce::dontSendNotification);
            const float gain = owner.callbacks.getVolume ? owner.callbacks.getVolume (track) : 1.0f;
            volume.setValue (juce::Decibels::gainToDecibels (gain, -40.0f), juce::dontSendNotification);
            stop.setVisible (owner.callbacks.sequencerOf && owner.callbacks.sequencerOf (track) >= 0);
        }

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat();
            const auto colour = TrackColours::get (track);
            g.setColour (juce::Colours::black.withAlpha (0.25f));
            g.fillRoundedRectangle (r, 8.0f);
            g.setColour (colour.withAlpha (0.8f));
            g.drawRoundedRectangle (r.reduced (0.75f), 8.0f, 1.5f);

            g.setColour (colour);
            g.setFont (juce::FontOptions (17.0f, juce::Font::bold));
            g.drawText ("T" + juce::String (track + 1), r.removeFromLeft (34.0f), juce::Justification::centred);
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (4, 3);
            r.removeFromLeft (30);
            auto top = r.removeFromTop (r.getHeight() / 2);
            const int w = top.getWidth();
            instrument.setBounds (top.removeFromLeft (w / 2).reduced (2, 1));
            sound.setBounds (top.reduced (2, 1));
            stop.setBounds (r.removeFromRight (34).reduced (2, 1));
            volume.setBounds (r.reduced (2, 0));
        }

        SessionPage& owner;
        const int track;
        juce::ComboBox instrument, sound;
        juce::Slider volume;
        juce::TextButton stop { juce::String::fromUTF8 ("\xe2\x96\xa0") };
    };

    void timerCallback() override
    {
        if (isShowing())
            grid.repaint();
    }

    SessionModel& model;
    Callbacks callbacks;
    const Pages pages;
    SynthLookAndFeel laf;   // combos + sliders; declared before the children
    GlassButtonLookAndFeel glassLnf { sessionPagePalette, MidiMorfColors::accent, 8.0f };

    juce::Viewport viewport;
    Grid grid;
    juce::OwnedArray<Row> rows;
    juce::TextButton projectButton;
    juce::OwnedArray<juce::TextButton> viewButtons;
    juce::ComboBox quantize;
    juce::OwnedArray<Strip> strips;
    juce::Rectangle<int> mixerHead;
    bool mixerShown = false;
    juce::OwnedArray<juce::TextButton> linkButtons;
};
