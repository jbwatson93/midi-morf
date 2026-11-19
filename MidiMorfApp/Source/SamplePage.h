#pragma once

#include <JuceHeader.h>
#include "Glassmorphism.h"
#include "SynthLookAndFeel.h"
#include "SynthWidgets.h"
#include "BreakImport.h"
#include "TrackRail.h"
#include "SampleSource.h"
#include "SampleEditor.h"
#include "SampleRecorder.h"

//==============================================================================
// What the Sampler and Grain pages share, laid out like Ableton's Simpler:
//
//   • top:    the page's own buttons on the left (the Sampler's modes), one
//             dropdown of the built-in breaks (it shows the name of anything
//             else loaded), and IMPORT: a file (WAV, AIFF, MP3, M4A), the
//             mic, or a note rendered from a synth preset. The mic opens a
//             record bar in place of the edit row until DONE
//   • middle: the sample editor, and under it REVERSE, the sample's name and
//             details, the page's own buttons, and the PLAY / SHAPE tabs
//   • bottom: a row of the page's controls for the tab that's showing.
//
// A page says what's its own through the hooks below.
class SamplePage : public juce::Component,
                   private juce::Timer
{
public:
    ~SamplePage() override
    {
        stopTimer();
        setLookAndFeel (nullptr);
    }

    std::function<void()> onChanged;                         // persist
    std::function<void (int track, const Break*)> onLoaded;  // a new sample

    // The mic. onMicNeeded asks for permission and opens the input, then
    // calls back with whether it can hear; onMicDone closes it again.
    std::function<void (std::function<void (bool ok)>)> onMicNeeded;
    std::function<void()> onMicDone;
    void setRecorder (SampleRecorder& r) { recorder = &r; }

    int getTrack() const noexcept { return track; }

    void refresh()
    {
        if (sample == nullptr)
            return;

        breakCombo.setSelectedId (sample->getBuiltInBreakIndex() + 1, juce::dontSendNotification);
        if (sample->getBuiltInBreakIndex() < 0 && sample->getBreak() != nullptr)
            breakCombo.setText (sample->getSampleName(), juce::dontSendNotification);

        const auto& e = sample->getEdit();
        reverseButton.setToggleState (e.reverse, juce::dontSendNotification);
        fadeInKnob.getSlider().setValue (e.fadeIn, juce::dontSendNotification);
        fadeOutKnob.getSlider().setValue (e.fadeOut, juce::dontSendNotification);

        refreshControls();
        updateTimer();
        editor.setSample (sample->getSource(), e, gridBeats(), TrackColours::get (track));

        resized();
        repaint();
    }

    void visibilityChanged() override
    {
        if (isVisible())
        {
            refresh();
            return;
        }

        // Leaving the page keeps what was recorded and closes the mic.
        closeRecorder();
        updateTimer();
    }

    void paint (juce::Graphics& g) override
    {
        Glassmorphism::paintPanel (g, getLocalBounds().toFloat(), palette);

        if (sample == nullptr) return;

        // Name over the details, beside REVERSE.
        const auto* b = sample->getBreak();
        g.setColour (MidiMorfColors::textLight.withAlpha (0.8f));
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawFittedText (sample->getSampleName(), infoArea.withTrimmedBottom (infoArea.getHeight() / 2), juce::Justification::bottomLeft, 1);
        g.setFont (juce::FontOptions (11.0f));
        g.setColour (MidiMorfColors::textLight.withAlpha (0.55f));
        g.drawFittedText (b != nullptr ? detailText (*b) : juce::String(),
                          infoArea.withTrimmedTop (infoArea.getHeight() / 2), juce::Justification::topLeft, 1);

        // Behind the controls.
        g.setColour (juce::Colours::black.withAlpha (0.18f));
        g.fillRoundedRectangle (controlsArea.toFloat(), 8.0f);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (10, 8);

        // What plays and where it comes from.
        auto top = area.removeFromTop (34);
        layoutTopLeft (top);
        importButton.setBounds (top.removeFromRight (110).reduced (2));
        top.removeFromRight (4);
        breakCombo.setBounds (top.reduced (2));
        area.removeFromTop (6);

        // Controls at the bottom, the edit row above them, the editor fills
        // the rest.
        controlsArea = area.removeFromBottom (juce::jlimit (96, 132, area.getHeight() * 38 / 100));
        area.removeFromBottom (4);
        auto row = area.removeFromBottom (micOpen ? 44 : 30);
        area.removeFromBottom (4);
        editor.setBounds (area);

        // While the mic is open the edit row is the record bar: a big
        // record button, what it's doing, and DONE to close the mic.
        for (auto* c : std::initializer_list<juce::Component*> { &recordButton, &recordStatus, &recordDone })
            c->setVisible (micOpen);
        for (auto* c : std::initializer_list<juce::Component*> { &reverseButton, &playTab, &shapeTab })
            c->setVisible (! micOpen);
        if (micOpen)
        {
            recordButton.setBounds (row.removeFromLeft (52));
            row.removeFromLeft (8);
            recordDone.setBounds (row.removeFromRight (90).reduced (2, 6));
            recordStatus.setBounds (row);
            infoArea = {};
            layoutControls();
            return;
        }

        reverseButton.setBounds (row.removeFromLeft (64).reduced (2));
        row.removeFromLeft (6);
        shapeTab.setBounds (row.removeFromRight (70).reduced (2));
        playTab.setBounds (row.removeFromRight (70).reduced (2));
        row.removeFromRight (8);
        layoutEditRow (row);
        infoArea = row.reduced (4, 0);

        layoutControls();
    }

protected:
    explicit SamplePage (const PagePalette& p) : palette (p)
    {
        setLookAndFeel (&laf);

        // The one dropdown of what's loaded: the built-in breaks, or the name
        // of whatever came in through Import.
        breakCombo.setTextWhenNothingSelected ("Choose a break");
        breakCombo.addSectionHeading ("Breaks");
        breakCombo.addItemList (BreakLibrary::builtInNames(), 1);
        breakCombo.onChange = [this]
        {
            if (sample != nullptr && breakCombo.getSelectedId() > 0
                && breakCombo.getSelectedId() - 1 != sample->getBuiltInBreakIndex()
                && sample->setBuiltInBreak (breakCombo.getSelectedId() - 1) != nullptr)
                loaded();
        };
        addAndMakeVisible (breakCombo);

        // Everything else comes in through Import: a file, the mic or a synth.
        importButton.onClick = [this] { showImportMenu(); };
        addAndMakeVisible (importButton);

        // Recording: a bar that takes the place of the edit row while the
        // mic is open.
        recordButton.onClick = [this] { recordPressed(); };
        recordStatus.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        recordStatus.setColour (juce::Label::textColourId, MidiMorfColors::textLight);
        recordDone.onClick = [this] { closeRecorder(); };
        for (auto* c : std::initializer_list<juce::Component*> { &recordButton, &recordStatus, &recordDone })
            addChildComponent (c);

        editor.onTrimmed = [this] (double start, double end)
        {
            if (sample == nullptr) return;
            auto e = sample->getEdit();
            e.start = start;
            e.end = end;
            applyEdit (e);
        };
        addAndMakeVisible (editor);

        reverseButton.setClickingTogglesState (true);
        reverseButton.onClick = [this]
        {
            if (sample == nullptr) return;
            auto e = sample->getEdit();
            e.reverse = reverseButton.getToggleState();
            applyEdit (e);
        };
        addAndMakeVisible (reverseButton);

        for (auto* b : { &playTab, &shapeTab })
        {
            b->setClickingTogglesState (true);
            b->setRadioGroupId (5151);
            b->onClick = [this] { layoutControls(); };
            addAndMakeVisible (b);
        }
        playTab.setToggleState (true, juce::dontSendNotification);

        // Fades are edits: they rebuild the sample, so only on release.
        for (auto* k : { &fadeInKnob, &fadeOutKnob })
        {
            auto& s = k->getSlider();
            s.setRange (0.0, 2000.0, 1.0);
            s.setSkewFactorFromMidPoint (150.0);
            s.textFromValueFunction = [] (double v) { return juce::String ((int) v) + " ms"; };
            s.updateText();
            s.onDragEnd = [this] { applyFades(); };
            s.onValueChange = [this, &s] { if (! s.isMouseButtonDown()) applyFades(); };
            addChildComponent (k);
        }
    }

    // The page now edits `s`, on `trackIndex`.
    void editSample (SampleSource& s, int trackIndex)
    {
        if (&s != sample && isRecording())
            finishRecording();   // the take belongs to the track it started on
        sample = &s;
        track = trackIndex;
        refresh();
    }

    //==========================================================================
    // Hooks.

    // The page's own buttons, from the left of the top row.
    virtual void layoutTopLeft (juce::Rectangle<int>&) {}

    // The page's own buttons in the edit row, from the right.
    virtual void layoutEditRow (juce::Rectangle<int>&) {}

    // Shows this tab's controls and lays them out (layoutRow).
    virtual void layoutControls() = 0;

    // Sets the page's own controls from the source.
    virtual void refreshControls() {}

    // Under the sample's name.
    virtual juce::String detailText (const Break& b) const { return juce::String (b.lengthSeconds(), 2) + " s"; }

    // Beats of grid to draw over the trimmed part; 0 for none.
    virtual double gridBeats() const { return 0.0; }

    // Whether the page needs the 30 Hz timer while it's showing (drawing).
    virtual bool wantsTimer() const { return false; }

    // A mic recording or a rendered synth note just loaded into `s`: a
    // sound for playing, where the page may want to change how it plays.
    virtual void loadedToPlay (SampleSource&) {}

    //==========================================================================
    // Each item: a component (or a pair stacked label-over-control) and its
    // share of the row. `place` lays out an item that isn't one component.
    struct Item
    {
        juce::Component* c;
        juce::Component* label = nullptr;
        float weight = 1.0f;
        std::function<void (juce::Rectangle<int>)> place = {};
    };

    bool showingPlayTab() const { return playTab.getToggleState(); }

    void layoutRow (const std::vector<Item>& items)
    {
        float totalWeight = 0.0f;
        for (auto& it : items) totalWeight += it.weight;
        auto row = controlsArea.reduced (8, 6);
        const float unit = (float) row.getWidth() / juce::jmax (1.0f, totalWeight);
        float x = (float) row.getX();
        for (auto& it : items)
        {
            const int w = (int) (unit * it.weight);
            auto cell = juce::Rectangle<int> ((int) x, row.getY(), w, row.getHeight()).reduced (3, 0);
            x += unit * it.weight;

            if (it.place)
            {
                it.place (cell);
                continue;
            }
            if (it.label != nullptr)
            {
                it.label->setBounds (cell.removeFromTop (18));
                it.label->setVisible (true);
                if (dynamic_cast<juce::ComboBox*> (it.c) != nullptr)
                    cell = cell.withSizeKeepingCentre (cell.getWidth(), 30).withY (cell.getY() + 6);
            }
            else if (dynamic_cast<juce::TextButton*> (it.c) != nullptr)
            {
                cell = cell.withSizeKeepingCentre (cell.getWidth(), 32);
            }
            it.c->setBounds (cell);
            it.c->setVisible (true);
        }
    }

    static void styleCaption (juce::Label& l, const juce::String& text)
    {
        l.setText (text, juce::dontSendNotification);
        l.setJustificationType (juce::Justification::centred);
        l.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        l.setColour (juce::Label::textColourId, MidiMorfColors::textLight);
    }

    void changed() { if (onChanged) onChanged(); }

    // An edit rebuilds the sample; whatever follows it (the slicer) hears.
    void applyEdit (const SampleSource::Edit& e)
    {
        if (sample == nullptr || e == sample->getEdit()) return;
        sample->setEdit (e);
        loaded();
    }

    void loaded()
    {
        if (onLoaded) onLoaded (track, sample->getBreak());
        refresh();
        changed();
    }

    void updateTimer()
    {
        if (micOpen || (isVisible() && sample != nullptr && wantsTimer())) startTimerHz (30);
        else                                                                stopTimer();
    }

    bool isMicOpen() const noexcept { return micOpen; }

    SynthLookAndFeel laf;   // declared before the children

    SampleEditor editor;
    juce::TextButton playTab { "PLAY" }, shapeTab { "SHAPE" };
    Knob fadeInKnob { "FADE IN", 0, 2000, 0 }, fadeOutKnob { "FADE OUT", 0, 2000, 0 };
    juce::Rectangle<int> controlsArea;
    int track = 2;

private:
    void applyFades()
    {
        if (sample == nullptr) return;
        auto e = sample->getEdit();
        e.fadeIn = (float) fadeInKnob.getSlider().getValue();
        e.fadeOut = (float) fadeOutKnob.getSlider().getValue();
        applyEdit (e);
    }

    bool isRecording() const { return recordTarget != nullptr; }

    // Import: a file, a recording from the mic, or a note rendered from one
    // of the synth's presets.
    void showImportMenu()
    {
        if (sample == nullptr) return;

        enum { FromFile = 1, FromMic, FirstPreset = 100 };
        juce::PopupMenu menu;
        menu.addItem (FromFile, "Audio file (" + Platform::importableAudioNames() + ")...");
        menu.addItem (FromMic, "Record from the mic", ! micOpen);
        juce::PopupMenu synths;
        const auto& presets = WavetablePresets::factory();
        for (auto& cat : WavetablePresets::categories())
        {
            juce::PopupMenu sub;
            for (int i = 0; i < (int) presets.size(); ++i)
                if (presets[(size_t) i].category == cat)
                    sub.addItem (FirstPreset + i, presets[(size_t) i].name, true,
                                 sample->isFromPreset() && sample->getSampleName() == presets[(size_t) i].name);
            synths.addSubMenu (cat, sub);
        }
        menu.addSubMenu ("Render a synth preset", synths);

        juce::Component::SafePointer<SamplePage> safe (this);
        menu.showMenuAsync (PopupStyle::attached (menu, importButton, 36), [safe] (int result)
        {
            if (safe == nullptr || safe->sample == nullptr || result <= 0) return;
            if (result == FromFile)      safe->chooseFile();
            else if (result == FromMic)  safe->openRecorder();
            else if (result >= FirstPreset)
            {
                const auto& p = WavetablePresets::factory();
                if (result - FirstPreset < (int) p.size())
                    safe->renderPreset (p[(size_t) (result - FirstPreset)].name);
            }
        });
    }

    void chooseFile()
    {
        importButton.setEnabled (false);
        // The importer loads into whichever source: give it this one.
        pendingImport = sample;
        juce::Component::SafePointer<SamplePage> safe (this);
        chooser = std::make_unique<juce::FileChooser> ("Import a sample", juce::File(), BreakImport::patterns);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [safe] (const juce::FileChooser& fc) { if (safe != nullptr) safe->imported (fc.getURLResult()); });
    }

    void renderPreset (const juce::String& name)
    {
        if (sample->renderFromPreset (name) == nullptr) return;
        loadedToPlay (*sample);
        loaded();
    }

    // Opens the mic (iOS asks the first time) and shows the record bar,
    // armed: nothing records until the record button is tapped.
    void openRecorder()
    {
        if (micOpen || recorder == nullptr || onMicNeeded == nullptr)
            return;

        importButton.setEnabled (false);
        juce::Component::SafePointer<SamplePage> safe (this);
        onMicNeeded ([safe] (bool ok)
        {
            if (safe == nullptr) return;
            safe->importButton.setEnabled (true);
            if (! ok)
            {
                alert ("No microphone", "Midi-Morf can't use the mic. Allow it in " + Platform::microphoneSettingsPath() + ".");
                return;
            }
            if (! safe->isShowing() || safe->sample == nullptr)
            {
                if (safe->onMicDone) safe->onMicDone();
                return;
            }
            safe->micOpen = true;
            safe->recordStatus.setText ("Ready. Tap the red button to record.", juce::dontSendNotification);
            safe->startTimerHz (30);
            safe->resized();
        });
    }

    // DONE, or leaving the page: keep what was recorded and close the mic.
    void closeRecorder()
    {
        if (isRecording())
            finishRecording();
        if (! micOpen)
            return;
        micOpen = false;
        updateTimer();
        meter = 0.0f;
        editor.setOverlay ({}, 0.0f, false);
        if (onMicDone) onMicDone();
        refresh();
    }

    // Each tap starts or stops a take; every take replaces the sample.
    void recordPressed()
    {
        if (isRecording())
        {
            finishRecording();
            return;
        }
        if (! micOpen || sample == nullptr)
            return;
        recordTarget = sample;
        recorder->start();
        recordButton.setRecording (true);
    }

    // Stop, and load the take into the source it was recorded for.
    void finishRecording()
    {
        auto* target = std::exchange (recordTarget, nullptr);
        recordButton.setRecording (false);
        const auto audio = recorder->stop();
        editor.setOverlay ({}, 0.0f, false);
        recordStatus.setText ("Got it. Tap the red button for another take, or DONE.", juce::dontSendNotification);
        if (target == nullptr)
            return;

        if (audio.getNumSamples() == 0)
        {
            alert ("Nothing recorded", "No sound came in from the mic. If it's blocked, allow it in " + Platform::microphoneSettingsPath() + ".");
            return;
        }

        juce::String error;
        if (target->loadRecording (audio, recorder->getSampleRate(), error) == nullptr)
        {
            alert ("Recording failed", error);
            return;
        }
        loadedToPlay (*target);
        if (target == sample)
            loaded();
        else
            changed();
    }

    void timerCallback() override
    {
        editor.tickClouds();
        if (! micOpen || recorder == nullptr)
            return;
        meter = juce::jmax (recorder->takeLevel(), meter * 0.85f);
        if (isRecording() && ! recorder->isRecording())
            finishRecording();   // hit the time limit
        if (isRecording())
        {
            const auto secs = juce::String (recorder->getSecondsRecorded(), 1) + " s";
            editor.setOverlay ("Recording  " + secs, meter, true);
            recordStatus.setText ("Recording " + secs + ". Tap to stop.", juce::dontSendNotification);
        }
        else if (micOpen)
            editor.setOverlay ("Mic is listening", meter, false);
    }

    static void alert (const juce::String& title, const juce::String& message)
    {
        juce::AlertWindow::showAsync (juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::WarningIcon)
                                                              .withTitle (title).withMessage (message).withButton ("OK"),
                                      nullptr);
    }

    // Copy the picked file into Documents/Breaks (iOS hands back a
    // security-scoped URL), then load it into the source this page had when
    // the import started.
    void imported (const juce::URL& url)
    {
        importButton.setEnabled (true);
        auto* target = std::exchange (pendingImport, nullptr);
        if (url.isEmpty() || target == nullptr)
            return;

        juce::String error;
        const auto dest = BreakImport::copyIn (url, error);
        if (dest != juce::File() && target->importBreak (dest, error) != nullptr)
        {
            if (target == sample) loaded();
            return;
        }
        alert ("Import failed", error);
    }

    const PagePalette& palette;

    SampleSource* sample = nullptr;
    SampleSource* pendingImport = nullptr;

    SampleRecorder* recorder = nullptr;
    SampleSource* recordTarget = nullptr;   // non-null while recording
    bool micOpen = false;
    float meter = 0.0f;

    juce::TextButton importButton { "IMPORT" };
    juce::ComboBox breakCombo;
    RecordButton recordButton;
    juce::Label recordStatus;
    juce::TextButton recordDone { "DONE" };
    juce::TextButton reverseButton { "REV" };

    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<int> infoArea;
};
