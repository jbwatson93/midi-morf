#pragma once

#include <JuceHeader.h>
#include "EventRouter.h"
#include "LocalSequencers.h"
#include "BreakImport.h"

//==============================================================================
// The strip along the bottom of both sequencer pages.
//
// DAW sync: the DAW owns the clock, so it is just a status line.
// Local sync: PLAY for this page's sequencer, plus (slice page only) the
// BREAK picker — built-in breaks, a previously imported loop, or
// "Import .wav...". Tempo and the sync source live on the Transport page,
// the global transport; this strip just shows the tempo.
//
// Polls at 30 Hz: that covers the sync source flipping on the Transport page,
// the local playhead, and the tempo changing when a break is picked.
class SeqTransportBar : public juce::Component, private juce::Timer
{
public:
    // trackOf: the track this bar's page plays (pages are per track).
    SeqTransportBar (EventRouter& r, std::function<int()> trackOf, bool withBreakPicker)
        : router (r), seqs (r.getSequencers()), track (std::move (trackOf)), hasBreaks (withBreakPicker)
    {
        playButton.setClickingTogglesState (true);
        playButton.onClick = [this] { seqs.setPlaying (track(), playButton.getToggleState()); refreshPlayText(); };
        addAndMakeVisible (playButton);

        if (hasBreaks)
        {
            breakCombo.setTextWhenNothingSelected ("Break");
            breakCombo.onChange = [this] { breakPicked(); };
            addAndMakeVisible (breakCombo);
        }

        statusLabel.setJustificationType (juce::Justification::centredRight);
        statusLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.45f));
        statusLabel.setFont (juce::Font (juce::FontOptions (12.0f)));
        addAndMakeVisible (statusLabel);

        refreshPlayText();
        timerCallback();
        startTimerHz (30);
    }

    // Local mode only: the playhead to draw, -1 when stopped.
    std::function<void (int)> onLocalPlayhead;
    // A newly picked break — the slice page re-grids if its length differs.
    std::function<void()> onBreakChanged;
    // Break changed — worth saving.
    std::function<void()> onStateChanged;

    // DAW sync: the server's playhead says whether DAW clock is running.
    void setRemoteRunning (bool isRunning)
    {
        remoteRunning = isRunning;
        if (! isLocal)
            statusLabel.setText (isRunning ? "Running" : "Waiting for DAW clock", juce::dontSendNotification);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        statusLabel.setBounds (area.removeFromRight (juce::jmin (220, area.getWidth() / 3)));

        playButton.setBounds (area.removeFromLeft (70));
        area.removeFromLeft (8);

        if (hasBreaks)
            breakCombo.setBounds (area.removeFromLeft (juce::jmin (190, area.getWidth())));
    }

private:
    void timerCallback() override
    {
        if (! isShowing())
            return;   // one bar per track's page: only the one on screen polls

        const bool local = router.isLocalSync();

        if (local != isLocal || firstPoll)
        {
            isLocal = local;
            playButton.setVisible (isLocal);
            breakCombo.setVisible (isLocal && hasBreaks);

            shownBpm = -1.0;   // force the local readout on the next pass
            if (! isLocal)
                setRemoteRunning (remoteRunning);

            // Hand the playhead back to whichever side now owns it.
            lastPlayhead = -2;
            if (! isLocal && onLocalPlayhead)
                onLocalPlayhead (-1);
        }

        firstPoll = false;

        if (! isLocal)
            return;

        const int step = seqs.getPlayhead (track());
        if (step != lastPlayhead)
        {
            lastPlayhead = step;
            if (onLocalPlayhead)
                onLocalPlayhead (step);
        }

        if (playButton.getToggleState() != seqs.isPlaying (track()))
        {
            playButton.setToggleState (seqs.isPlaying (track()), juce::dontSendNotification);
            refreshPlayText();
        }

        if (seqs.getBpm() != shownBpm)
        {
            shownBpm = seqs.getBpm();
            statusLabel.setText ("Local clock  " + juce::String (juce::roundToInt (shownBpm)) + " BPM",
                                 juce::dontSendNotification);
        }

        // Only while this track really is a Breaker on a sampler: a sampler
        // used another way must keep its sample.
        const auto kind = seqs.getKind (track());
        const bool slicing = hasBreaks && (kind == LocalSequencers::Kind::Slice || kind == LocalSequencers::Kind::Remix);
        if (slicing && seqs.sliceSampler (track()) != nullptr && seqs.getBreak (track()) == nullptr)
            seqs.setBuiltInBreak (track(), 0);   // no saved settings to restore from

        // Also catches the break restored at launch, so the slice grid
        // matches its length without the user having to pick it again.
        if (slicing && seqs.getBreak (track()) != shownBreak)
        {
            shownBreak = seqs.getBreak (track());
            rebuildBreakCombo();
            if (onBreakChanged) onBreakChanged();
        }
    }

    void refreshPlayText()
    {
        playButton.setButtonText (playButton.getToggleState() ? "STOP" : "PLAY");
    }

    //==========================================================================
    // Break picker. Built-ins are ids 1..N, a loaded import is 900.
    static constexpr int ImportedId = 900, ImportId = 901;

    void rebuildBreakCombo()
    {
        breakCombo.clear (juce::dontSendNotification);

        const auto names = BreakLibrary::builtInNames();
        breakCombo.addSectionHeading ("BUILT-IN");
        for (int i = 0; i < names.size(); ++i)
            breakCombo.addItem (names[i], i + 1);

        breakCombo.addSeparator();
        if (seqs.getImportedBreak (track()) != juce::File() && seqs.getBreak (track()) != nullptr)
            breakCombo.addItem (seqs.getBreak (track())->name, ImportedId);
        breakCombo.addItem ("Import .wav...", ImportId);

        const int idx = seqs.getBuiltInBreakIndex (track());
        breakCombo.setSelectedId (idx >= 0 ? idx + 1 : ImportedId, juce::dontSendNotification);
    }

    void breakPicked()
    {
        const int id = breakCombo.getSelectedId();

        if (id == ImportId)
        {
            rebuildBreakCombo();   // put the selection back until a file arrives
            launchImport();
            return;
        }

        if (id >= 1 && id - 1 != seqs.getBuiltInBreakIndex (track()))
        {
            seqs.setBuiltInBreak (track(), id - 1);
            breakChanged();
        }
    }

    void breakChanged()
    {
        shownBreak = seqs.getBreak (track());
        rebuildBreakCombo();
        if (onBreakChanged) onBreakChanged();
        if (onStateChanged) onStateChanged();
    }

    void launchImport()
    {
        juce::Component::SafePointer<SeqTransportBar> safe (this);
        BreakImport::launch (chooser, seqs, track(), [safe] { if (safe != nullptr) safe->breakChanged(); });
    }

    EventRouter& router;
    LocalSequencers& seqs;
    const std::function<int()> track;
    const bool hasBreaks;

    juce::TextButton playButton { "PLAY" };
    juce::Label statusLabel;
    juce::ComboBox breakCombo;
    std::unique_ptr<juce::FileChooser> chooser;

    bool   isLocal = false, firstPoll = true, remoteRunning = false;
    int    lastPlayhead = -2;
    double shownBpm = -1.0;
    const Break* shownBreak = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SeqTransportBar)
};
