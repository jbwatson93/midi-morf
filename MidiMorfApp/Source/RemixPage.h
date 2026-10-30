#pragma once

#include <JuceHeader.h>
#include "Glassmorphism.h"
#include "SynthLookAndFeel.h"
#include "SeqTransportBar.h"
#include "TrackRail.h"

//==============================================================================
// Remix Lane: the track's sample laid across 16 positions, playing in time
// with the playhead walking along it — monome mlr style.
//
//   • tap a position  — playback jumps there on the next 16th and plays on
//   • hold two        — loops between them while held; let go to play on
//   • SYNC            — back in line with the bar
//
// The sample is the track's Sampler's (the BREAK picker loads one). The
// engine is LocalSequencers' Remix slot; this page only steers it.
class RemixPage : public juce::Component,
                  private juce::Timer
{
public:
    static constexpr int Positions = 16;

    explicit RemixPage (EventRouter& r) : router (r), seqs (r.getSequencers())
    {
        setLookAndFeel (&laf);

        transport.onBreakChanged = [this] { updateLength(); };
        addAndMakeVisible (transport);

        syncButton.onClick = [this] { seqs.remixSync (track); };
        addAndMakeVisible (syncButton);

        startTimerHz (30);
    }

    ~RemixPage() override
    {
        stopTimer();
        setLookAndFeel (nullptr);
    }

    void setTrack (int t) { track = t; }
    int  getTrack() const { return track; }

    // The lane follows the sample's length (in 16ths); also re-reads the
    // waveform.
    void updateLength()
    {
        const auto* b = seqs.getBreak (track);
        shownBreak = b;
        peaks.clear();
        if (b == nullptr)
            return;

        seqs.setRemixLength (track, juce::jlimit (1, 128, juce::roundToInt (b->beats * 4.0)));
        const int n = b->audio.getNumSamples(), cols = 256;
        for (int x = 0; x < cols; ++x)
        {
            const int a = (int) ((juce::int64) n * x / cols), e = juce::jmax (a + 1, (int) ((juce::int64) n * (x + 1) / cols));
            peaks.push_back (b->audio.getMagnitude (0, a, e - a));
        }
        repaint();
    }

    void visibilityChanged() override { if (isVisible()) updateLength(); }

    void paint (juce::Graphics& g) override
    {
        Glassmorphism::paintPanel (g, getLocalBounds().toFloat(), remixPalette);

        const auto colour = TrackColours::get (track);
        auto lane = laneArea.toFloat();
        g.setColour (juce::Colours::black.withAlpha (0.4f));
        g.fillRoundedRectangle (lane, 8.0f);

        // The waveform, behind the positions.
        if (! peaks.empty())
        {
            const float mid = lane.getCentreY(), half = lane.getHeight() * 0.42f, w = lane.getWidth() / (float) peaks.size();
            g.setColour (colour.withAlpha (0.35f));
            for (size_t i = 0; i < peaks.size(); ++i)
                g.fillRect (lane.getX() + (float) i * w, mid - peaks[i] * half, juce::jmax (1.0f, w - 0.5f), juce::jmax (1.0f, peaks[i] * half * 2.0f));
        }

        const int len = seqs.getRemixLength (track);
        const int pos = seqs.getPlayhead (track);
        const auto loop = seqs.getRemixLoop (track);
        const int playCol = pos >= 0 ? pos * Positions / juce::jmax (1, len) : -1;

        for (int c = 0; c < Positions; ++c)
        {
            auto cell = cellBounds (c).toFloat().reduced (2.0f);
            const bool inLoop = loop.first >= 0 && c >= loop.first * Positions / len && c <= loop.second * Positions / len;
            const bool held = std::any_of (fingers.begin(), fingers.end(), [c] (auto& f) { return f.second == c; });

            if (inLoop)
            {
                g.setColour (colour.withAlpha (0.25f));
                g.fillRoundedRectangle (cell, 5.0f);
            }
            if (c == playCol)
            {
                g.setColour (colour.withAlpha (0.75f));
                g.fillRoundedRectangle (cell, 5.0f);
            }
            g.setColour (held ? juce::Colours::white : MidiMorfColors::textLight.withAlpha (c % 4 == 0 ? 0.35f : 0.15f));
            g.drawRoundedRectangle (cell, 5.0f, held ? 2.5f : 1.0f);
        }

        g.setColour (MidiMorfColors::textLight.withAlpha (0.55f));
        g.setFont (juce::FontOptions (12.0f));
        g.drawFittedText (seqs.getBreak (track) == nullptr
                              ? "Give this track a Sampler (on Session) to remix its sample."
                              : "Tap to jump. Hold two to loop between them. SYNC puts it back on the beat.",
                          hintArea, juce::Justification::centredLeft, 2);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (10, 8);
        transport.setBounds (area.removeFromBottom (30));
        area.removeFromBottom (6);
        auto top = area.removeFromTop (34);
        syncButton.setBounds (top.removeFromRight (90).reduced (2));
        hintArea = top.reduced (4, 0);
        area.removeFromTop (6);
        laneArea = area;
    }

    //==========================================================================
    // Fingers: one jumps, two loop, lifting plays on.
    void mouseDown (const juce::MouseEvent& e) override
    {
        const int c = columnAt (e.x);
        if (c < 0 || ! laneArea.contains (e.getPosition()))
            return;
        fingers[e.source.getIndex()] = c;
        applyFingers (true);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (fingers.erase (e.source.getIndex()) == 0)
            return;
        if (looping && fingers.size() < 2)
        {
            looping = false;
            seqs.remixLoop (track, -1, -1);
        }
        repaint();
    }

private:
    static inline const PagePalette remixPalette {
        juce::Colour (0xff1A2226), juce::Colour (0xff070A0C),
        juce::Colour (0xCC23313A), juce::Colour (0xD8121A1F), juce::Colour (0xCC000000)
    };

    juce::Rectangle<int> cellBounds (int c) const
    {
        const int x0 = laneArea.getX() + laneArea.getWidth() * c / Positions;
        const int x1 = laneArea.getX() + laneArea.getWidth() * (c + 1) / Positions;
        return { x0, laneArea.getY(), x1 - x0, laneArea.getHeight() };
    }

    int columnAt (int x) const
    {
        if (x < laneArea.getX() || x >= laneArea.getRight()) return -1;
        return juce::jlimit (0, Positions - 1, (x - laneArea.getX()) * Positions / laneArea.getWidth());
    }

    // A position as a 16th of the sample.
    int sixteenthOf (int column) const { return column * seqs.getRemixLength (track) / Positions; }

    void applyFingers (bool newFinger)
    {
        if (fingers.empty())
            return;

        if (fingers.size() == 1 && newFinger)
        {
            seqs.remixJump (track, sixteenthOf (fingers.begin()->second));
        }
        else if (fingers.size() >= 2)
        {
            int lo = Positions, hi = -1;
            for (auto& [id, c] : fingers) { lo = juce::jmin (lo, c); hi = juce::jmax (hi, c); }
            const int len = seqs.getRemixLength (track);
            seqs.remixLoop (track, sixteenthOf (lo), (hi + 1) * len / Positions - 1);
            looping = true;
        }
        repaint();
    }

    void timerCallback() override
    {
        if (! isShowing())
            return;
        if (seqs.getBreak (track) != shownBreak)
            updateLength();
        const int pos = seqs.getPlayhead (track);
        if (pos != lastPos)
        {
            lastPos = pos;
            repaint (laneArea);
        }
    }

    EventRouter& router;
    LocalSequencers& seqs;
    int track = 0, lastPos = -2;
    const Break* shownBreak = nullptr;
    std::vector<float> peaks;
    std::map<int, int> fingers;   // touch id -> column
    bool looping = false;

    SynthLookAndFeel laf;   // declared before the children
    SeqTransportBar transport { router, [this] { return track; }, true };
    juce::TextButton syncButton { "SYNC" };
    juce::Rectangle<int> laneArea, hintArea;
};
