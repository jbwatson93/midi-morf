#pragma once

#include <JuceHeader.h>
#include "Glassmorphism.h"
#include "SynthLookAndFeel.h"
#include "BreakImport.h"
#include "TrackRail.h"
#include "Sampler.h"
#include "SampleRecorder.h"

//==============================================================================
// The waveform of the sampler's sample. Slice mode draws the beats (where the
// slicer's grid falls); Keys mode marks nothing — the root is in the controls.
class SampleDisplay : public juce::Component
{
public:
    void setSample (const Break* b, bool showBeats, juce::Colour c)
    {
        colour = c;
        beats = (b != nullptr && showBeats) ? b->beats : 0.0;
        peaks.clear();
        if (b != nullptr)
        {
            const int n = b->audio.getNumSamples(), cols = 400;
            for (int x = 0; x < cols; ++x)
            {
                const int a = (int) ((juce::int64) n * x / cols), e = juce::jmax (a + 1, (int) ((juce::int64) n * (x + 1) / cols));
                auto range = b->audio.findMinMax (0, a, e - a);
                peaks.push_back ({ range.getStart(), range.getEnd() });
            }
        }
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRoundedRectangle (r, 6.0f);

        if (peaks.empty())
        {
            g.setColour (MidiMorfColors::textLight.withAlpha (0.5f));
            g.drawText ("No sample", r, juce::Justification::centred);
            return;
        }

        if (beats > 0.0)
        {
            const int total = juce::roundToInt (beats * 4.0);   // 16ths
            for (int i = 1; i < total; ++i)
            {
                const float x = r.getX() + r.getWidth() * (float) i / (float) total;
                g.setColour (MidiMorfColors::textLight.withAlpha (i % 4 == 0 ? 0.35f : 0.12f));
                g.drawVerticalLine ((int) x, r.getY(), r.getBottom());
            }
        }

        const float mid = r.getCentreY(), half = r.getHeight() * 0.45f;
        const float w = r.getWidth() / (float) peaks.size();
        g.setColour (colour);
        for (size_t i = 0; i < peaks.size(); ++i)
        {
            const float x = r.getX() + (float) i * w;
            g.fillRect (x, mid - peaks[i].second * half, juce::jmax (1.0f, w - 0.5f),
                        juce::jmax (1.0f, (peaks[i].second - peaks[i].first) * half));
        }
    }

private:
    std::vector<std::pair<float, float>> peaks;
    double beats = 0.0;
    juce::Colour colour { MidiMorfColors::accent };
};

//==============================================================================
// The big round record button: a red dot, a square while recording.
class RecordButton : public juce::Button
{
public:
    RecordButton() : juce::Button ("Record") {}

    void setRecording (bool r) { recording = r; repaint(); }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        auto r = getLocalBounds().toFloat();
        const float d = juce::jmin (r.getWidth(), r.getHeight()) - 4.0f;
        auto ring = r.withSizeKeepingCentre (d, d);

        g.setColour (juce::Colours::black.withAlpha (0.4f));
        g.fillEllipse (ring);
        g.setColour (MidiMorfColors::textLight.withAlpha (isEnabled() ? 0.5f : 0.2f));
        g.drawEllipse (ring, 2.0f);

        const auto red = juce::Colour (0xffE0342A).withMultipliedBrightness (down ? 0.8f : (over ? 1.1f : 1.0f));
        g.setColour (isEnabled() ? red : red.withSaturation (0.2f));
        if (recording)
            g.fillRoundedRectangle (ring.withSizeKeepingCentre (d * 0.36f, d * 0.36f), 4.0f);
        else
            g.fillEllipse (ring.reduced (d * 0.2f));
    }

private:
    bool recording = false;
};

//==============================================================================
// The sound page for a track's sampler: how it plays (Slice / Keys /
// One-shot), what it plays (a built-in break, a note rendered from a synth
// preset — both dropdowns — a recording from the mic, or an imported file),
// and in Keys mode the root note and envelope.
class SamplerPage : public juce::Component,
                    private juce::Timer
{
public:
    SamplerPage()
    {
        setLookAndFeel (&laf);

        for (int m = 0; m < 3; ++m)
        {
            auto* b = modeButtons.add (new juce::TextButton (SamplerSource::modeNames()[m]));
            b->setClickingTogglesState (true);
            b->setRadioGroupId (5150);
            b->onClick = [this, m] { if (sampler != nullptr) { sampler->setMode ((SamplerSource::Mode) m); refresh(); changed(); } };
            addAndMakeVisible (b);
        }

        importButton.onClick = [this]
        {
            if (sampler == nullptr) return;
            importButton.setEnabled (false);
            // The importer loads into whichever sampler: give it this one.
            pendingImport = sampler;
            juce::Component::SafePointer<SamplerPage> safe (this);
            chooser = std::make_unique<juce::FileChooser> ("Import a sample", juce::File(), "*.wav;*.aif;*.aiff");
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                [safe] (const juce::FileChooser& fc) { if (safe != nullptr) safe->imported (fc.getURLResult()); });
        };
        addAndMakeVisible (importButton);

        synthCombo.setTextWhenNothingSelected ("From a synth preset");
        int id = 1;
        for (auto& cat : WavetablePresets::categories())
        {
            synthCombo.addSectionHeading (cat);
            for (auto& p : WavetablePresets::factory())
                if (p.category == cat)
                    synthCombo.addItem (p.name, id++);
        }
        synthCombo.onChange = [this]
        {
            if (sampler == nullptr || synthCombo.getSelectedId() == 0) return;
            if (sampler->renderFromPreset (synthCombo.getText()) != nullptr)
            {
                if (sampler->getMode() == SamplerSource::Slice)
                    sampler->setMode (SamplerSource::Keys);   // a rendered note is for playing, not slicing
                loaded();
            }
        };
        addAndMakeVisible (synthCombo);

        breakCombo.setTextWhenNothingSelected ("Built-in breaks");
        breakCombo.addItemList (BreakLibrary::builtInNames(), 1);
        breakCombo.onChange = [this]
        {
            if (sampler != nullptr && breakCombo.getSelectedId() > 0
                && breakCombo.getSelectedId() - 1 != sampler->getBuiltInBreakIndex()
                && sampler->setBuiltInBreak (breakCombo.getSelectedId() - 1) != nullptr)
                loaded();
        };
        addAndMakeVisible (breakCombo);

        addAndMakeVisible (display);

        recordButton.onClick = [this] { recordPressed(); };
        addAndMakeVisible (recordButton);

        rootDown.onClick = [this] { nudgeRoot (-1); };
        rootUp.onClick   = [this] { nudgeRoot (1); };
        rootLabel.setJustificationType (juce::Justification::centred);
        for (auto* c : std::initializer_list<juce::Component*> { &rootDown, &rootUp, &rootLabel })
            addChildComponent (c);

        const char* names4[] = { "A", "D", "S", "R" };
        for (int i = 0; i < 4; ++i)
        {
            auto* s = envSliders.add (new juce::Slider (juce::Slider::LinearVertical, juce::Slider::TextBoxBelow));
            s->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 48, 18);
            s->setRange (i == 2 ? 0.0 : 0.001, i == 2 ? 1.0 : (i == 3 ? 8.0 : 5.0), 0.001);
            if (i != 2) s->setSkewFactorFromMidPoint (0.4);
            s->onValueChange = [this] { applyEnvelope(); };
            s->onDragEnd = [this] { changed(); };
            addChildComponent (s);

            auto* l = envLabels.add (new juce::Label ({}, names4[i]));
            l->setJustificationType (juce::Justification::centred);
            addChildComponent (l);
        }
    }

    ~SamplerPage() override
    {
        stopTimer();
        setLookAndFeel (nullptr);
    }

    std::function<void()> onChanged;                         // persist
    std::function<void (int track, const Break*)> onLoaded;  // a new sample (the slicer may follow its tempo)

    // The mic. onMicNeeded asks for permission and opens the input, then
    // calls back with whether it can hear; onMicDone closes it again.
    std::function<void (std::function<void (bool ok)>)> onMicNeeded;
    std::function<void()> onMicDone;
    void setRecorder (SampleRecorder& r) { recorder = &r; }

    void edit (SamplerSource& s, int trackIndex)
    {
        if (&s != sampler && isRecording())
            finishRecording();   // the take belongs to the track it started on
        sampler = &s;
        track = trackIndex;
        refresh();
    }

    int getTrack() const noexcept { return track; }

    void refresh()
    {
        if (sampler == nullptr)
            return;

        const auto mode = sampler->getMode();
        for (int m = 0; m < modeButtons.size(); ++m)
            modeButtons[m]->setToggleState (m == (int) mode, juce::dontSendNotification);

        breakCombo.setSelectedId (sampler->getBuiltInBreakIndex() + 1, juce::dontSendNotification);

        synthCombo.setSelectedId (0, juce::dontSendNotification);
        if (sampler->isFromPreset())
            synthCombo.setText (sampler->getSampleName(), juce::dontSendNotification);

        const bool keys = mode == SamplerSource::Keys;
        for (auto* c : std::initializer_list<juce::Component*> { &rootDown, &rootUp, &rootLabel })
            c->setVisible (keys);
        for (auto* s : envSliders) s->setVisible (keys);
        for (auto* l : envLabels)  l->setVisible (keys);

        rootLabel.setText ("ROOT " + juce::MidiMessage::getMidiNoteName (sampler->getRootNote(), true, true, 4), juce::dontSendNotification);
        const auto env = sampler->getEnvelope();
        const float v[4] = { env.attack, env.decay, env.sustain, env.release };
        for (int i = 0; i < 4; ++i)
            envSliders[i]->setValue (v[i], juce::dontSendNotification);

        display.setSample (sampler->getBreak(), mode == SamplerSource::Slice, TrackColours::get (track));
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
        if (isRecording())
            finishRecording();
        if (micOpen)
        {
            micOpen = false;
            stopTimer();
            meter = 0.0f;
            if (onMicDone) onMicDone();
        }
    }

    void paint (juce::Graphics& g) override
    {
        Glassmorphism::paintPanel (g, getLocalBounds().toFloat(), samplerPagePalette);

        if (sampler == nullptr) return;
        g.setColour (MidiMorfColors::textLight.withAlpha (0.75f));
        g.setFont (juce::FontOptions (13.0f));
        // Name over the details, so neither gets cut off.
        juce::String detail;
        if (const auto* b = sampler->getBreak())
            detail = sampler->getMode() == SamplerSource::Slice
                         ? juce::String (b->bars()) + (b->bars() == 1 ? " bar, " : " bars, ") + juce::String (juce::roundToInt (b->bpm())) + " BPM"
                         : juce::String (b->lengthSeconds(), 1) + " s";
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawFittedText (sampler->getSampleName(), infoArea.withTrimmedBottom (infoArea.getHeight() / 2), juce::Justification::bottomLeft, 1);
        g.setFont (juce::FontOptions (11.0f));
        g.setColour (MidiMorfColors::textLight.withAlpha (0.55f));
        g.drawFittedText (detail, infoArea.withTrimmedTop (infoArea.getHeight() / 2), juce::Justification::topLeft, 1);

        // The record panel: input meter and what's happening.
        {
            auto bar = meterArea.toFloat();
            g.setColour (juce::Colours::black.withAlpha (0.4f));
            g.fillRoundedRectangle (bar, 3.0f);
            const float db = juce::Decibels::gainToDecibels (meter, -60.0f);
            const float fill = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
            g.setColour (meter > 0.9f ? juce::Colour (0xffE0342A) : TrackColours::get (track));
            g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * fill), 3.0f);

            juce::String status;
            if (isRecording())
                status = "Recording  " + juce::String (recorder->getSecondsRecorded(), 1) + " s";
            else if (micOpen)
                status = "Tap to record. Up to " + juce::String ((int) SampleRecorder::MaxSeconds) + " s";
            else
                status = "Record a sample from the mic";
            g.setColour (isRecording() ? juce::Colour (0xffFF6B5E) : MidiMorfColors::textLight.withAlpha (0.75f));
            g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
            g.drawFittedText (status, statusArea, juce::Justification::bottomLeft, 1);
        }

        if (sampler->getMode() == SamplerSource::OneShot)
        {
            g.setColour (MidiMorfColors::textLight.withAlpha (0.5f));
            g.drawFittedText ("Every note plays the whole sample at its own pitch.", sideArea, juce::Justification::centred, 3);
        }
        else if (sampler->getMode() == SamplerSource::Slice)
        {
            g.setColour (MidiMorfColors::textLight.withAlpha (0.5f));
            g.drawFittedText ("Slice-Seq plays it in slices. Its steps follow the lines.", sideArea, juce::Justification::centred, 3);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (10, 8);

        auto top = area.removeFromTop (34);
        for (auto* b : modeButtons)
            b->setBounds (top.removeFromLeft (86).reduced (2));
        top.removeFromLeft (6);
        synthCombo.setBounds (top.removeFromRight (180).reduced (2));
        breakCombo.setBounds (top.removeFromRight (160).reduced (2));
        infoArea = top.reduced (4, 0);
        area.removeFromTop (6);

        display.setBounds (area.removeFromTop (juce::jmin (110, area.getHeight() / 3)));
        area.removeFromTop (8);

        sideArea = area.removeFromRight (juce::jmin (260, area.getWidth() * 36 / 100));
        area.removeFromRight (8);

        // Keys controls in the side column: root on top, then A D S R.
        auto side = sideArea;
        auto rootRow = side.removeFromTop (30);
        rootDown.setBounds (rootRow.removeFromLeft (36).reduced (2));
        rootUp.setBounds (rootRow.removeFromRight (36).reduced (2));
        rootLabel.setBounds (rootRow);
        side.removeFromTop (4);
        auto labels = side.removeFromTop (16);
        const int sw = side.getWidth() / 4;
        for (int i = 0; i < 4; ++i)
        {
            envLabels[i]->setBounds (labels.removeFromLeft (sw));
            envSliders[i]->setBounds (side.getX() + i * sw, side.getY(), sw, side.getHeight());
        }

        // Recording and importing fill the rest: the record button, then the
        // status over the meter, and Import underneath.
        importButton.setBounds (area.removeFromBottom (36).removeFromLeft (170).reduced (2));
        area.removeFromBottom (6);
        const int size = juce::jlimit (40, 84, area.getHeight());
        auto row = area.withSizeKeepingCentre (area.getWidth(), size);
        recordButton.setBounds (row.removeFromLeft (size));
        row.removeFromLeft (12);
        auto text = row.withSizeKeepingCentre (row.getWidth(), 44);
        statusArea = text.removeFromTop (24);
        text.removeFromTop (8);
        meterArea = text.removeFromTop (10);
    }

private:
    static inline const PagePalette samplerPagePalette {
        juce::Colour (0xff2A2014), juce::Colour (0xff120D07),
        juce::Colour (0xCC3A2C1A), juce::Colour (0xD8241A0E), juce::Colour (0xCC000000)
    };

    void changed() { if (onChanged) onChanged(); }

    bool isRecording() const { return recordTarget != nullptr; }

    // First tap opens the mic (iOS asks the first time) and starts; the next
    // tap stops.
    void recordPressed()
    {
        if (isRecording())
        {
            finishRecording();
            return;
        }
        if (sampler == nullptr || recorder == nullptr || onMicNeeded == nullptr)
            return;

        recordButton.setEnabled (false);
        juce::Component::SafePointer<SamplerPage> safe (this);
        onMicNeeded ([safe] (bool ok)
        {
            if (safe == nullptr) return;
            safe->recordButton.setEnabled (true);
            if (! ok)
            {
                safe->alert ("No microphone", "Midi-Morf can't use the mic. Allow it in Settings > Midi-Morf > Microphone.");
                return;
            }
            if (! safe->isShowing() || safe->sampler == nullptr)
            {
                if (safe->onMicDone) safe->onMicDone();
                return;
            }
            safe->micOpen = true;
            safe->recordTarget = safe->sampler;
            safe->recorder->start();
            safe->recordButton.setRecording (true);
            safe->startTimerHz (30);
            safe->repaint();
        });
    }

    // Stop, and load the take into the sampler it was recorded for.
    void finishRecording()
    {
        auto* target = std::exchange (recordTarget, nullptr);
        recordButton.setRecording (false);
        const auto audio = recorder->stop();
        repaint();
        if (target == nullptr)
            return;

        if (audio.getNumSamples() == 0)
        {
            alert ("Nothing recorded", "No sound came in from the mic. If it's blocked, allow it in Settings > Midi-Morf > Microphone.");
            return;
        }

        juce::String error;
        if (target->loadRecording (audio, recorder->getSampleRate(), error) == nullptr)
        {
            alert ("Recording failed", error);
            return;
        }
        if (target->getMode() == SamplerSource::Slice)
            target->setMode (SamplerSource::Keys);   // a recording is for playing, not slicing
        if (target == sampler)
            loaded();
        else
            changed();
    }

    void timerCallback() override
    {
        meter = juce::jmax (recorder->takeLevel(), meter * 0.85f);
        if (isRecording() && ! recorder->isRecording())
            finishRecording();   // hit the time limit
        repaint (meterArea.getUnion (statusArea));
    }

    static void alert (const juce::String& title, const juce::String& message)
    {
        juce::AlertWindow::showAsync (juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::WarningIcon)
                                                              .withTitle (title).withMessage (message).withButton ("OK"),
                                      nullptr);
    }

    void loaded()
    {
        if (onLoaded) onLoaded (track, sampler->getBreak());
        refresh();
        changed();
    }

    void nudgeRoot (int semis)
    {
        if (sampler == nullptr) return;
        sampler->setRootNote (sampler->getRootNote() + semis);
        refresh();
        changed();
    }

    void applyEnvelope()
    {
        if (sampler != nullptr)
            sampler->setEnvelope ((float) envSliders[0]->getValue(), (float) envSliders[1]->getValue(),
                                  (float) envSliders[2]->getValue(), (float) envSliders[3]->getValue());
    }

    // Copy the picked file into Documents/Breaks (iOS hands back a
    // security-scoped URL), then load it into the sampler this page had when
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
            if (target == sampler) loaded();
            return;
        }
        alert ("Import failed", error);
    }

    SamplerSource* sampler = nullptr;
    SamplerSource* pendingImport = nullptr;
    int track = 2;

    SampleRecorder* recorder = nullptr;
    SamplerSource* recordTarget = nullptr;   // non-null while recording
    bool micOpen = false;
    float meter = 0.0f;

    SynthLookAndFeel laf;   // declared before the children

    juce::OwnedArray<juce::TextButton> modeButtons;
    juce::TextButton importButton { "Import a file..." };
    juce::ComboBox breakCombo, synthCombo;
    RecordButton recordButton;
    SampleDisplay display;
    juce::TextButton rootDown { "-" }, rootUp { "+" };
    juce::Label rootLabel;
    juce::OwnedArray<juce::Slider> envSliders;
    juce::OwnedArray<juce::Label> envLabels;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<int> infoArea, sideArea, statusArea, meterArea;
};
