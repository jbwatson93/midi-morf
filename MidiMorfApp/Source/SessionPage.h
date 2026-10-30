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
//     pattern. On a controller track (Morf-Chord, Morf-Pad...) clips are
//     notes you record: tap an empty slot to record into it (tap again to
//     stop), hold a take for Edit / Record over / Quantize / Undo last pass.
//   • the track column — what plays the track, its sound, its volume, ■.
//   • MIXER (instead of the grid) — per track: its sound (tap to edit it),
//     pan, the delay and reverb sends, the insert (filter, drive, crush),
//     mute and solo; the master level and the send buses along the top.
//     Volume is the fader in the track column.
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
        std::function<int (int track)> recordingScene;          // -1 unless recording there
        std::function<bool()> isCountingIn;
        std::function<void (int track, int scene)> recordClip, quantizeClip;
        std::function<void()> stopRecording;
        std::function<void (int track)> undoPass;
        std::function<bool (int track, int scene)> canUndo;
        std::function<int()> getRecBars;
        std::function<void (int)> setRecBars;
        std::function<bool()> getRecQuantize, getMetronome;
        std::function<void (bool)> setRecQuantize, setMetronome;
        std::function<int()> getLaunchQuantize;                  // 0 bar, 1 beat, 2 off
        std::function<void (int)> setLaunchQuantize;

        // Mixer. Knob params: MixParam. Master params: MasterParam.
        std::function<float (int track, int param)> getMix;
        std::function<void (int track, int param, float value)> setMix;
        std::function<bool (int track)> getSolo;
        std::function<void (int track, bool solo)> setSolo;
        std::function<float (int param)> getMaster;
        std::function<void (int param, float value)> setMaster;
        std::function<juce::String (int track)> soundName;
        std::function<void (int track)> openSound;
        std::function<bool (int track)> getMuted;
        std::function<void (int track, bool muted)> setMuted;

        std::function<void (int pageIndex)> openPage;
        std::function<void()> openProjects;
        std::function<juce::String()> projectName;
        std::function<void()> changed;                            // persist
    };

    enum MixParam { Pan = 0, SendDelay, SendReverb, Filter, Drive, Crush, NumKnobs };
    enum MasterParam { MasterVolume = 0, DelayTime, DelayFeedback, ReverbSize };

    // sequencerOf's answer for a track whose instrument takes no clips.
    static constexpr int NoClips = -2;

    // The page indices the bottom buttons open, and the Drum Grid's (an
    // instrument that isn't a ControllerMode).
    struct Pages { int transport, fx, settings, about, drumGrid, remix; };

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

        recButton.onClick = [this] { showRecMenu(); };
        addAndMakeVisible (recButton);

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

        // The master section, over the strips in MIXER.
        for (int m : { MasterVolume, DelayFeedback, ReverbSize })
        {
            auto* sl = masterSliders.add (new juce::Slider (juce::Slider::LinearHorizontal, juce::Slider::NoTextBox));
            sl->setRange (0.0, m == MasterVolume ? 1.5 : (m == DelayFeedback ? 0.9 : 1.0), 0.01);
            sl->setDoubleClickReturnValue (m == MasterVolume, 1.0);
            sl->setPopupDisplayEnabled (true, true, this);
            sl->textFromValueFunction = [m] (double v)
            {
                return m == MasterVolume ? juce::String (juce::Decibels::gainToDecibels (v, -60.0), 1) + " dB"
                                         : juce::String (juce::roundToInt (v * 100.0)) + "%";
            };
            sl->onValueChange = [this, m, sl] { if (callbacks.setMaster) callbacks.setMaster (m, (float) sl->getValue()); };
            sl->onDragEnd = [this] { if (callbacks.changed) callbacks.changed(); };
            addChildComponent (sl);
        }
        delayTimeCombo.addItemList (SendBuses::delayTimeNames(), 1);
        delayTimeCombo.onChange = [this]
        {
            if (callbacks.setMaster) callbacks.setMaster (DelayTime, (float) (delayTimeCombo.getSelectedId() - 1));
            if (callbacks.changed) callbacks.changed();
        };
        addChildComponent (delayTimeCombo);

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
        if (callbacks.getMaster)
        {
            masterSliders[0]->setValue (callbacks.getMaster (MasterVolume), juce::dontSendNotification);
            masterSliders[1]->setValue (callbacks.getMaster (DelayFeedback), juce::dontSendNotification);
            masterSliders[2]->setValue (callbacks.getMaster (ReverbSize), juce::dontSendNotification);
            delayTimeCombo.setSelectedId (juce::roundToInt (callbacks.getMaster (DelayTime)) + 1, juce::dontSendNotification);
        }
        quantize.setSelectedId ((callbacks.getLaunchQuantize ? callbacks.getLaunchQuantize() : 0) + 1, juce::dontSendNotification);
        const int recBars = callbacks.getRecBars ? callbacks.getRecBars() : 2;
        recButton.setButtonText ("REC " + juce::String (recBars) + (recBars == 1 ? " bar" : " bars"));
        projectButton.setButtonText ("Project: " + (callbacks.projectName ? callbacks.projectName() : juce::String()));
        grid.repaint();
    }

    void visibilityChanged() override { if (isVisible()) refresh(); }

    // For the walkthrough to point at.
    juce::Rectangle<int> gridBounds() const          { return viewport.getBounds(); }
    juce::Rectangle<int> projectButtonBounds() const { return projectButton.getBounds(); }
    juce::Rectangle<int> instrumentBounds (int t) const { return rows[t]->instrument.getBounds() + rows[t]->getPosition(); }
    juce::Rectangle<int> soundBounds (int t) const      { return rows[t]->sound.getBounds() + rows[t]->getPosition(); }

    void paint (juce::Graphics& g) override
    {
        Glassmorphism::paintPanel (g, getLocalBounds().toFloat(), sessionPagePalette);

        if (mixerShown)
        {
            g.setColour (MidiMorfColors::textLight.withAlpha (0.55f));
            g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
            for (auto& [text, r] : masterLabels)
                g.drawText (text, r, juce::Justification::centredRight);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (8, 6);

        auto bottom = area.removeFromBottom (32);
        area.removeFromBottom (6);
        projectButton.setBounds (bottom.removeFromLeft (juce::jmin (150, bottom.getWidth() / 5)).reduced (2));
        bottom.removeFromLeft (4);
        for (auto* b : viewButtons)
            b->setBounds (bottom.removeFromLeft (58).reduced (2));
        bottom.removeFromLeft (4);
        quantize.setBounds (bottom.removeFromLeft (118).reduced (2));
        recButton.setBounds (bottom.removeFromLeft (86).reduced (2));
        bottom.removeFromLeft (4);
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

        // MASTER [====]  DELAY [1/8 dotted] FB [===]  REVERB [===]
        auto head = mixerHead.reduced (0, 2);
        masterLabels.clear();
        auto label = [&] (const char* text, int w) { masterLabels.push_back ({ text, head.removeFromLeft (w) }); head.removeFromLeft (4); };
        const int unit = head.getWidth() / 10;
        label ("MASTER", 50);
        masterSliders[0]->setBounds (head.removeFromLeft (unit * 2));
        label ("DELAY", 42);
        delayTimeCombo.setBounds (head.removeFromLeft (unit * 2 + 6));
        label ("FB", 18);
        masterSliders[1]->setBounds (head.removeFromLeft (unit + 10));
        label ("REVERB", 50);
        masterSliders[2]->setBounds (head);
        const int sceneW = juce::jmax (84, area.getWidth() / 4);
        grid.setSize (sceneW * SessionModel::NumScenes, area.getHeight());
        grid.sceneW = sceneW;
        grid.rowH = rowH;
    }

private:
    static constexpr int SceneHeaderH = 30;

    // Settings for new recordings.
    void showRecMenu()
    {
        juce::PopupMenu m;
        const int bars = callbacks.getRecBars ? callbacks.getRecBars() : 2;
        m.addSectionHeader ("New takes");
        for (int b : { 1, 2, 4, 8 })
            m.addItem (b, juce::String (b) + (b == 1 ? " bar" : " bars"), true, bars == b);
        m.addSeparator();
        m.addItem (100, "Quantize to 1/16", true, callbacks.getRecQuantize && callbacks.getRecQuantize());
        m.addItem (101, "Metronome", true, callbacks.getMetronome && callbacks.getMetronome());
        juce::Component::SafePointer<SessionPage> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&recButton), [safe] (int r)
        {
            if (safe == nullptr || r == 0) return;
            auto& cb = safe->callbacks;
            if (r <= 8 && cb.setRecBars) cb.setRecBars (r);
            if (r == 100 && cb.setRecQuantize) cb.setRecQuantize (! cb.getRecQuantize());
            if (r == 101 && cb.setMetronome) cb.setMetronome (! cb.getMetronome());
            safe->refresh();
        });
    }

    void showMixer (bool m)
    {
        mixerShown = m;
        viewport.setVisible (! m);
        for (auto* st : strips)
        {
            st->setVisible (m);
            st->refresh();
        }
        for (auto* sl : masterSliders)
            sl->setVisible (m);
        delayTimeCombo.setVisible (m);
        repaint();
    }

    //==========================================================================
    // A track's mixer strip: its sound (tap to edit), then knobs for pan,
    // the delay and reverb sends, and the insert (filter, drive, crush), then
    // mute and solo. Volume is the fader in the track column.
    struct Strip : public juce::Component
    {
        Strip (SessionPage& o, int t) : owner (o), track (t)
        {
            sound.onClick = [this] { if (owner.callbacks.openSound) owner.callbacks.openSound (track); };
            sound.setColour (juce::TextButton::textColourOffId, TrackColours::get (t).brighter (0.3f));
            addAndMakeVisible (sound);

            static const char* names[NumKnobs] = { "PAN", "DLY", "REV", "FILT", "DRV", "CRSH" };
            for (int k = 0; k < NumKnobs; ++k)
            {
                auto* s = knobs.add (new juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox));
                const bool bipolar = k == Pan || k == Filter;
                s->setRange (bipolar ? -1.0 : 0.0, 1.0, 0.01);
                s->setDoubleClickReturnValue (true, 0.0);
                s->setPopupDisplayEnabled (true, true, &owner);
                s->setColour (juce::Slider::rotarySliderFillColourId, TrackColours::get (t));
                s->textFromValueFunction = [k] (double v)
                {
                    if (k == Pan)    return std::abs (v) < 0.005 ? juce::String ("C") : juce::String (juce::roundToInt (std::abs (v) * 100.0)) + (v < 0 ? "L" : "R");
                    if (k == Filter) return std::abs (v) < 0.005 ? juce::String ("off") : (v < 0 ? "LP " : "HP ") + juce::String (juce::roundToInt (std::abs (v) * 100.0));
                    return juce::String (juce::roundToInt (v * 100.0)) + "%";
                };
                s->onValueChange = [this, k, s] { if (owner.callbacks.setMix) owner.callbacks.setMix (track, k, (float) s->getValue()); };
                s->onDragEnd = [this] { if (owner.callbacks.changed) owner.callbacks.changed(); };
                addAndMakeVisible (s);

                auto* l = labels.add (new juce::Label ({}, names[k]));
                l->setJustificationType (juce::Justification::centred);
                l->setFont (juce::FontOptions (9.5f, juce::Font::bold));
                l->setColour (juce::Label::textColourId, MidiMorfColors::textLight.withAlpha (0.55f));
                l->setInterceptsMouseClicks (false, false);
                addAndMakeVisible (l);
            }

            for (auto* b : { &mute, &solo })
            {
                b->setClickingTogglesState (true);
                addAndMakeVisible (b);
            }
            mute.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffB03A2E));
            solo.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffD4A72C));
            mute.onClick = [this]
            {
                if (owner.callbacks.setMuted) owner.callbacks.setMuted (track, mute.getToggleState());
                if (owner.callbacks.changed) owner.callbacks.changed();
            };
            solo.onClick = [this]
            {
                if (owner.callbacks.setSolo) owner.callbacks.setSolo (track, solo.getToggleState());
                if (owner.callbacks.changed) owner.callbacks.changed();
            };
        }

        void refresh()
        {
            const auto name = owner.callbacks.soundName ? owner.callbacks.soundName (track) : juce::String();
            sound.setButtonText (name);   // no pencil glyph: iOS draws those as emoji
            sound.setEnabled (owner.callbacks.getSound && owner.callbacks.getSound (track) != 0);
            for (int k = 0; k < NumKnobs; ++k)
                knobs[k]->setValue (owner.callbacks.getMix ? owner.callbacks.getMix (track, k) : 0.0f, juce::dontSendNotification);
            mute.setToggleState (owner.callbacks.getMuted && owner.callbacks.getMuted (track), juce::dontSendNotification);
            solo.setToggleState (owner.callbacks.getSolo && owner.callbacks.getSolo (track), juce::dontSendNotification);
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
            auto r = getLocalBounds().reduced (8, 3);
            const int btnH = juce::jmin (28, r.getHeight() / 2 - 1);
            auto buttons = r.removeFromRight (40);
            mute.setBounds (buttons.removeFromTop (r.getHeight() / 2).withSizeKeepingCentre (40, btnH));
            solo.setBounds (buttons.withSizeKeepingCentre (40, btnH));
            r.removeFromRight (4);
            sound.setBounds (r.removeFromLeft (juce::jmin (110, r.getWidth() / 4)).withSizeKeepingCentre (juce::jmin (110, r.getWidth() / 4), juce::jmin (34, r.getHeight())));
            r.removeFromLeft (4);
            const int kw = r.getWidth() / NumKnobs;
            for (int k = 0; k < NumKnobs; ++k)
            {
                auto cell = r.removeFromLeft (kw);
                labels[k]->setBounds (cell.removeFromTop (12));
                knobs[k]->setBounds (cell.withSizeKeepingCentre (juce::jmin (kw, cell.getHeight()), cell.getHeight()));
            }
        }

        SessionPage& owner;
        const int track;
        juce::TextButton sound, mute { "M" }, solo { "S" };
        juce::OwnedArray<juce::Slider> knobs;
        juce::OwnedArray<juce::Label> labels;
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

                const int kind = seq >= 0 ? seq : SessionModel::NotesClip;
                const int recScene = cb.recordingScene ? cb.recordingScene (t) : -1;

                for (int s = 0; s < SessionModel::NumScenes; ++s)
                {
                    auto r = cellBounds (t, s).reduced (3, 2).toFloat();

                    if (seq == NoClips)   // played live only (the Remix Lane)
                    {
                        g.setColour (juce::Colours::black.withAlpha (0.12f));
                        g.fillRoundedRectangle (r, 6.0f);
                        continue;
                    }

                    const auto& c = m.clip (t, s);
                    if (! c.has_value())
                    {
                        g.setColour (juce::Colours::black.withAlpha (0.25f));
                        g.fillRoundedRectangle (r, 6.0f);
                        if (seq < 0)
                        {
                            // An empty slot on a controller track records.
                            g.setColour (juce::Colour (0xffE0342A).withAlpha (0.45f));
                            g.fillEllipse (r.withSizeKeepingCentre (10.0f, 10.0f));
                        }
                        else
                        {
                            g.setColour (MidiMorfColors::textLight.withAlpha (0.25f));
                            g.setFont (juce::FontOptions (18.0f));
                            g.drawText ("+", r, juce::Justification::centred);
                        }
                        continue;
                    }

                    if (s == recScene)
                    {
                        const bool countIn = cb.isCountingIn && cb.isCountingIn();
                        g.setColour (juce::Colour (0xffE0342A).withAlpha (countIn && ! blink ? 0.45f : 0.9f));
                        g.fillRoundedRectangle (r, 6.0f);
                        g.setColour (juce::Colours::white);
                        g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
                        g.drawFittedText ((countIn ? "Count-in  " : "Recording  ") + c->name, r.reduced (6.0f, 2.0f).toNearestInt(), juce::Justification::centredLeft, 2);
                        continue;
                    }

                    const bool fits    = c->sequencer == kind;
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
            else if (juce::isPositiveAndBelow (t, TrackRack::NumTracks) && ! (cb.sequencerOf && cb.sequencerOf (t) == NoClips))
            {
                auto& m = owner.model;
                if (cb.recordingScene && cb.recordingScene (t) == s)      { if (cb.stopRecording) cb.stopRecording(); }
                else if (! m.hasClip (t, s))                               { if (cb.newClip) cb.newClip (t, s); }
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

            const bool notes = owner.model.clip (t, s)->isNotes();
            const auto& cb = owner.callbacks;
            juce::PopupMenu menu;
            menu.addItem (1, "Edit");
            if (notes)
            {
                menu.addItem (4, "Record over it");
                menu.addItem (5, "Quantize to 1/16");
                menu.addItem (6, "Undo last pass", cb.canUndo && cb.canUndo (t, s));
            }
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
                    if (result == 4 && cb.recordClip)    cb.recordClip (track, scene);
                    if (result == 5 && cb.quantizeClip)  cb.quantizeClip (track, scene);
                    if (result == 6 && cb.undoPass)      cb.undoPass (track);
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
                 { (int) CM::StepSequencer, "Step-Seq" }, { (int) CM::SliceSequencer, "Breaker" },
                 { pages.drumGrid, "Drum Grid" }, { pages.remix, "Remix Lane" } };
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
    juce::TextButton recButton;
    juce::OwnedArray<Strip> strips;
    juce::OwnedArray<juce::Slider> masterSliders;   // master volume, delay feedback, reverb size
    juce::ComboBox delayTimeCombo;
    std::vector<std::pair<juce::String, juce::Rectangle<int>>> masterLabels;
    juce::Rectangle<int> mixerHead;
    bool mixerShown = false;
    juce::OwnedArray<juce::TextButton> linkButtons;
};
