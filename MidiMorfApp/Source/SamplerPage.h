#pragma once

#include <JuceHeader.h>
#include "Glassmorphism.h"
#include "SynthLookAndFeel.h"
#include "SynthWidgets.h"
#include "BreakImport.h"
#include "TrackRail.h"
#include "Sampler.h"
#include "SampleRecorder.h"

//==============================================================================
// The sample editor: the whole loaded sample, with the part that plays
// between two yellow handles (the same look as Step-Seq's range bracket).
// Drag a handle to trim; ends snap to the nearest zero crossing so they never
// click. Drag the waveform to scroll, pinch (or − / + / FIT) to zoom. In Slice
// mode the beat grid sits inside the trimmed part, so it's easy to see when
// the bars line up.
class SampleEditor : public juce::Component
{
public:
    std::function<void (double startSeconds, double endSeconds)> onTrimmed;   // on release

    SampleEditor()
    {
        for (auto* b : { &zoomOut, &zoomIn, &zoomFit })
        {
            b->setColour (juce::TextButton::buttonColourId, juce::Colours::black.withAlpha (0.45f));
            addAndMakeVisible (b);
        }
        zoomIn.onClick  = [this] { zoomBy (0.5, viewStart + viewLength * 0.5); };
        zoomOut.onClick = [this] { zoomBy (2.0, viewStart + viewLength * 0.5); };
        zoomFit.onClick = [this] { fitSelection(); };
    }

    // A new source resets the view to the whole sample.
    void setSample (const Break* src, const SamplerSource::Edit& edit, double gridBeats, juce::Colour c)
    {
        colour = c;
        beats = gridBeats;
        reversed = edit.reverse;
        if (src != source)
        {
            source = src;
            buildPeaks();
            viewStart = 0.0;
            viewLength = total();
        }
        if (source != nullptr && ! dragging)
        {
            selStart = juce::jlimit (0.0, total(), edit.start * source->sampleRate);
            selEnd = edit.end < 0.0 ? total() : juce::jlimit (selStart, total(), edit.end * source->sampleRate);
        }
        repaint();
    }

    //==========================================================================
    // GRAIN mode: the waveform is played, not edited. Each finger is a cloud
    // (GrainEngine); `x` is 0..1 through the trimmed sample as it plays (so
    // right to left when reversed), `y` 0..1 up the waveform. With HOLD a
    // lifted finger's cloud keeps playing; tapping it stops it, dragging it
    // moves it.
    std::function<void (int cloud, bool down, float x, float y)> onCloud;

    // nullptr leaves play mode. The clouds' slots start empty either way:
    // held clouds of another sampler stay with it.
    void setPlayMode (GrainEngine* g)
    {
        if (g == grains)
            return;
        // Leaving play mode lets go of every finger and held cloud.
        if (grains != nullptr && g == nullptr)
            for (int i = 0; i < (int) slots.size(); ++i)
                if (slots[(size_t) i].finger >= 0 || slots[(size_t) i].held)
                    grains->touch (i, false, slots[(size_t) i].x, slots[(size_t) i].y);
        grains = g;
        slots = {};
        touches.clear();
        grabbed = None;
        dragging = false;
        repaint();
    }

    bool isPlayMode() const noexcept { return grains != nullptr; }

    void setHold (bool h)
    {
        hold = h;
        if (! hold)
            for (int i = 0; i < (int) slots.size(); ++i)
                if (slots[(size_t) i].held && slots[(size_t) i].finger < 0)
                    letGo (i);
    }

    // Timer: forget held clouds the engine let go of (STOP), and redraw.
    void tickClouds()
    {
        if (grains == nullptr)
            return;
        for (int i = 0; i < (int) slots.size(); ++i)
            if (slots[(size_t) i].held && slots[(size_t) i].finger < 0 && ! grains->isTouchHeld (i))
                slots[(size_t) i].held = false;
        repaint();
    }

    // While the mic is open: what's happening, and the input level 0..1.
    void setOverlay (const juce::String& text, float level, bool alarm)
    {
        overlay = text;
        meter = level;
        overlayAlarm = alarm;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRoundedRectangle (r, 6.0f);

        if (source == nullptr || peaks.empty())
        {
            g.setColour (MidiMorfColors::textLight.withAlpha (0.5f));
            g.drawText ("No sample", r, juce::Justification::centred);
            paintOverlay (g);
            return;
        }

        const auto wave = waveArea().toFloat();
        const float xs = xOf (selStart), xe = xOf (selEnd);

        // The grid: 16ths across the trimmed part.
        if (beats > 0.0)
        {
            const int sixteenths = juce::roundToInt (beats * 4.0);
            for (int i = 1; i < sixteenths; ++i)
            {
                const float x = xOf (selStart + (selEnd - selStart) * i / sixteenths);
                if (x < wave.getX() || x > wave.getRight()) continue;
                g.setColour (MidiMorfColors::textLight.withAlpha (i % 16 == 0 ? 0.45f : i % 4 == 0 ? 0.28f : 0.1f));
                g.drawVerticalLine ((int) x, wave.getY(), wave.getBottom());
            }
        }

        // The waveform, one column per pixel; brighter inside the selection.
        const float mid = wave.getCentreY(), half = wave.getHeight() * 0.46f;
        for (int x = (int) wave.getX(); x < (int) wave.getRight(); ++x)
        {
            const double a = sampleAt ((float) x), e = sampleAt ((float) x + 1.0f);
            const auto range = rangeOf (a, e);
            if (range.isEmpty() && range.getStart() == 0.0f) continue;
            const bool inside = a >= selStart && a < selEnd;
            g.setColour (inside ? colour : colour.withAlpha (0.25f));
            g.fillRect ((float) x, mid - range.getEnd() * half, 1.0f, juce::jmax (1.0f, (range.getEnd() - range.getStart()) * half));
        }

        // Dim outside the selection.
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        if (xs > wave.getX()) g.fillRect (wave.withRight (juce::jmin (xs, wave.getRight())));
        if (xe < wave.getRight()) g.fillRect (wave.withLeft (juce::jmax (xe, wave.getX())));

        if (grains != nullptr)
        {
            paintClouds (g, wave);
            paintOverlay (g);
            return;
        }

        // The handles: a yellow line along the bottom of the selection, with
        // a triangle hanging from each end.
        g.setColour (handleColour);
        const float lineY = wave.getBottom() + 3.0f;
        g.fillRect (juce::jmax (xs, wave.getX()), lineY, juce::jmin (xe, wave.getRight()) - juce::jmax (xs, wave.getX()), 2.5f);
        for (float x : { xs, xe })
        {
            if (x < wave.getX() - 1.0f || x > wave.getRight() + 1.0f) continue;
            g.drawVerticalLine ((int) x, wave.getY(), lineY);
            juce::Path tri;
            tri.addTriangle (x - 9.0f, lineY, x + 9.0f, lineY, x, lineY + 13.0f);
            g.fillPath (tri);
        }

        paintOverlay (g);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (4);
        auto row = r.removeFromTop (24).removeFromRight (120);
        zoomFit.setBounds (row.removeFromRight (44).reduced (1));
        zoomIn.setBounds (row.removeFromRight (36).reduced (1));
        zoomOut.setBounds (row.removeFromRight (36).reduced (1));
    }

    //==========================================================================
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (source == nullptr) return;
        if (grains != nullptr) { playDown (e); return; }
        const int id = e.source.getIndex();
        touches[id] = e.position;

        if (touches.size() == 2)
        {
            // Second finger: pinch from here.
            grabbed = None;
            auto it = touches.begin();
            const auto a = it->second, b = std::next (it)->second;
            pinchDistance = juce::jmax (8.0f, std::abs (a.x - b.x));
            pinchLength = viewLength;
            pinchAnchor = sampleAt ((a.x + b.x) * 0.5f);
            return;
        }

        const float xs = xOf (selStart), xe = xOf (selEnd);
        const float ds = std::abs (e.position.x - xs), de = std::abs (e.position.x - xe);
        grabbed = juce::jmin (ds, de) < 26.0f ? (ds <= de ? Start : End) : Pan;
        // Both handles under the finger (a tiny selection): the side it's on.
        if (ds < 26.0f && de < 26.0f)
            grabbed = e.position.x < (xs + xe) * 0.5f ? Start : End;
        dragging = grabbed == Start || grabbed == End;
        panFrom = viewStart;
        downX = e.position.x;
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (source == nullptr) return;
        if (grains != nullptr) { playDrag (e); return; }
        touches[e.source.getIndex()] = e.position;

        if (touches.size() >= 2)
        {
            auto it = touches.begin();
            const auto a = it->second, b = std::next (it)->second;
            const float d = juce::jmax (8.0f, std::abs (a.x - b.x));
            const double len = juce::jlimit (minView(), total(), pinchLength * pinchDistance / d);
            const float cx = (a.x + b.x) * 0.5f;
            viewLength = len;
            viewStart = clampView (pinchAnchor - (cx - (float) waveArea().getX()) / (float) waveArea().getWidth() * len);
            repaint();
            return;
        }

        const double s = juce::jlimit (0.0, total(), sampleAt (e.position.x));
        const double minSel = source->sampleRate * 0.02;
        if (grabbed == Start)
            selStart = juce::jmin (s, selEnd - minSel);
        else if (grabbed == End)
            selEnd = juce::jmax (s, selStart + minSel);
        else if (grabbed == Pan)
            viewStart = clampView (panFrom - (e.position.x - downX) / (float) waveArea().getWidth() * viewLength);
        repaint();
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (grains != nullptr) { playUp (e); return; }
        touches.erase (e.source.getIndex());
        if (! dragging || source == nullptr)
        {
            if (touches.empty()) grabbed = None;
            return;
        }

        dragging = false;
        if (grabbed == Start) selStart = zeroCrossingNear (selStart);
        if (grabbed == End)   selEnd = selEnd >= total() - 1.0 ? total() : zeroCrossingNear (selEnd);
        grabbed = None;
        repaint();
        if (onTrimmed)
            onTrimmed (selStart / source->sampleRate, selEnd >= total() ? -1.0 : selEnd / source->sampleRate);
    }

private:
    enum Grab { None, Start, End, Pan };

    //==========================================================================
    // Play mode.

    struct Slot
    {
        int finger = -1;            // the touch playing it, or -1
        bool held = false;          // playing with no finger (HOLD)
        bool tookOver = false;      // the finger picked up a held cloud
        float x = 0.0f, y = 0.5f;   // as sent
        juce::Point<float> down;
    };

    // 0..1 through the sample as it plays ↔ a position in the source.
    double sourceAt (float u) const
    {
        const double span = selEnd - selStart;
        return reversed ? selEnd - u * span : selStart + u * span;
    }

    float playedAt (double sample) const
    {
        const double span = juce::jmax (1.0, selEnd - selStart);
        const double u = reversed ? (selEnd - sample) / span : (sample - selStart) / span;
        return (float) juce::jlimit (0.0, 1.0, u);
    }

    float heightAt (float y) const
    {
        const auto w = waveArea().toFloat();
        return juce::jlimit (0.0f, 1.0f, (w.getBottom() - y) / juce::jmax (1.0f, w.getHeight()));
    }

    int slotOf (int finger) const
    {
        for (int i = 0; i < (int) slots.size(); ++i)
            if (slots[(size_t) i].finger == finger)
                return i;
        return -1;
    }

    void send (int i, bool down)
    {
        if (onCloud)
            onCloud (i, down, slots[(size_t) i].x, slots[(size_t) i].y);
    }

    void letGo (int i)
    {
        auto& s = slots[(size_t) i];
        s.held = false;
        s.finger = -1;
        send (i, false);
    }

    // A finger on a held cloud picks it up; anywhere else it starts a new one
    // (taking the oldest held one's place when all five are busy).
    void playDown (const juce::MouseEvent& e)
    {
        const int finger = e.source.getIndex();
        const float u = playedAt (sampleAt (e.position.x)), v = heightAt (e.position.y);

        int pick = -1;
        bool tookOver = false;
        for (int i = 0; i < (int) slots.size() && pick < 0; ++i)
        {
            const auto& s = slots[(size_t) i];
            if (s.held && s.finger < 0 && std::abs (xOf (sourceAt (s.x)) - e.position.x) < 28.0f)
            {
                pick = i;
                tookOver = true;
            }
        }
        for (int i = 0; i < (int) slots.size() && pick < 0; ++i)
            if (slots[(size_t) i].finger < 0 && ! slots[(size_t) i].held)
                pick = i;
        for (int i = 0; i < (int) slots.size() && pick < 0; ++i)
            if (slots[(size_t) i].finger < 0)
                pick = i;
        if (pick < 0)
            return;

        auto& s = slots[(size_t) pick];
        s.finger = finger;
        s.tookOver = tookOver;
        s.down = e.position;
        if (! tookOver)
            s.held = false;
        s.x = u;
        s.y = v;
        send (pick, true);
        repaint();
    }

    void playDrag (const juce::MouseEvent& e)
    {
        const int i = slotOf (e.source.getIndex());
        if (i < 0)
            return;
        auto& s = slots[(size_t) i];
        if (s.tookOver && e.position.getDistanceFrom (s.down) < 10.0f)
            return;   // a tap, so far
        s.x = playedAt (sampleAt (e.position.x));
        s.y = heightAt (e.position.y);
        send (i, true);
    }

    void playUp (const juce::MouseEvent& e)
    {
        const int i = slotOf (e.source.getIndex());
        if (i < 0)
            return;
        auto& s = slots[(size_t) i];
        const bool tap = s.tookOver && e.position.getDistanceFrom (s.down) < 10.0f;
        if (tap || ! hold)
        {
            letGo (i);
        }
        else
        {
            s.held = true;
            s.finger = -1;
        }
        repaint();
    }

    // Each cloud: its loop as a band from where it starts, the read point as
    // a line, and a ring where the finger is (or was). Grains are sparks
    // scattered up the waveform, brighter while loud.
    void paintClouds (juce::Graphics& g, juce::Rectangle<float> wave)
    {
        static const juce::Colour cloudColours[GrainEngine::NumTouch] = {
            juce::Colour (0xffFFD23F), juce::Colour (0xff5EE0FF), juce::Colour (0xffFF6BCB), juce::Colour (0xff8CFF6B), juce::Colour (0xffFF9F43)
        };

        for (int i = 0; i < GrainEngine::NumGrains; ++i)
        {
            const float p = grains->grainPosition (i);
            if (p < 0.0f) continue;
            const float level = juce::jlimit (0.0f, 1.0f, grains->grainLevel (i) * 2.0f);
            const float x = xOf (sourceAt (p));
            const float y = wave.getY() + wave.getHeight() * (0.1f + 0.8f * (float) ((i * 37) % 97) / 97.0f);
            const float r = 2.0f + 3.0f * level;
            g.setColour (juce::Colours::white.withAlpha (0.25f + 0.6f * level));
            g.fillEllipse (x - r, y - r, 2.0f * r, 2.0f * r);
        }

        for (int i = 0; i < GrainEngine::NumClouds; ++i)
        {
            const float at = grains->cloudPosition (i);
            if (at < 0.0f) continue;
            const float level = grains->cloudLevel (i);
            const auto c = i < GrainEngine::NumTouch ? cloudColours[i] : colour.brighter (0.4f);

            const float a = xOf (sourceAt (grains->cloudAnchor (i))), b = xOf (sourceAt (grains->cloudLoopEnd (i)));
            g.setColour (c.withAlpha (0.16f * level));
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (juce::jmin (a, b), wave.getY(), juce::jmax (a, b) + 1.0f, wave.getBottom()));

            g.setColour (c.withAlpha (0.4f + 0.6f * level));
            g.fillRect (xOf (sourceAt (at)) - 1.0f, wave.getY(), 2.0f, wave.getHeight());
        }

        for (int i = 0; i < (int) slots.size(); ++i)
        {
            const auto& s = slots[(size_t) i];
            if (s.finger < 0 && ! s.held) continue;
            const float x = xOf (sourceAt (s.x)), y = wave.getBottom() - s.y * wave.getHeight();
            g.setColour (cloudColours[i].withAlpha (s.held && s.finger < 0 ? 0.6f : 0.95f));
            g.drawEllipse (x - 22.0f, y - 22.0f, 44.0f, 44.0f, s.held && s.finger < 0 ? 2.0f : 3.0f);
            if (s.held && s.finger < 0)
            {
                g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
                g.drawText ("HOLD", juce::Rectangle<float> (x - 22.0f, y + 22.0f, 44.0f, 14.0f), juce::Justification::centred);
            }
        }
    }

    juce::Rectangle<int> waveArea() const { return getLocalBounds().reduced (6, 0).withTrimmedTop (6).withTrimmedBottom (20); }

    double total() const { return source != nullptr ? (double) source->audio.getNumSamples() : 1.0; }
    double minView() const { return source != nullptr ? source->sampleRate * 0.02 : 1.0; }
    double clampView (double start) const { return juce::jlimit (0.0, juce::jmax (0.0, total() - viewLength), start); }

    float xOf (double sample) const
    {
        const auto w = waveArea();
        return (float) w.getX() + (float) ((sample - viewStart) / viewLength * w.getWidth());
    }

    double sampleAt (float x) const
    {
        const auto w = waveArea();
        return viewStart + (x - (float) w.getX()) / (float) w.getWidth() * viewLength;
    }

    void zoomBy (double factor, double anchor)
    {
        const double len = juce::jlimit (minView(), total(), viewLength * factor);
        const double rel = (anchor - viewStart) / viewLength;
        viewLength = len;
        viewStart = clampView (anchor - rel * len);
        repaint();
    }

    void fitSelection()
    {
        const double pad = (selEnd - selStart) * 0.04;
        viewLength = juce::jlimit (minView(), total(), selEnd - selStart + 2.0 * pad);
        viewStart = clampView (selStart - pad);
        repaint();
    }

    // Peaks of every block of BlockSize samples, so zoomed-out drawing is
    // cheap; zoomed right in, the samples themselves are read.
    static constexpr int BlockSize = 128;

    void buildPeaks()
    {
        peaks.clear();
        if (source == nullptr) return;
        const int n = source->audio.getNumSamples();
        peaks.reserve ((size_t) (n / BlockSize + 1));
        for (int a = 0; a < n; a += BlockSize)
        {
            auto r = source->audio.findMinMax (0, a, juce::jmin (BlockSize, n - a));
            peaks.push_back (r);
        }
    }

    juce::Range<float> rangeOf (double a, double e) const
    {
        const int n = source->audio.getNumSamples();
        int s0 = juce::jlimit (0, n, (int) a), s1 = juce::jlimit (0, n, (int) std::ceil (e));
        if (s1 <= s0) s1 = juce::jmin (n, s0 + 1);
        if (s1 <= s0) return {};
        if (s1 - s0 < BlockSize * 2)
            return source->audio.findMinMax (0, s0, s1 - s0);
        juce::Range<float> r = peaks[(size_t) (s0 / BlockSize)];
        for (int b = s0 / BlockSize + 1; b <= (s1 - 1) / BlockSize && b < (int) peaks.size(); ++b)
            r = r.getUnionWith (peaks[(size_t) b]);
        return r;
    }

    // The nearest place within a millisecond and a half where the left
    // channel crosses zero, so a trimmed end never clicks.
    double zeroCrossingNear (double at) const
    {
        const int n = source->audio.getNumSamples();
        const int centre = juce::jlimit (1, n - 1, (int) std::lround (at));
        const int reach = (int) (source->sampleRate * 0.0015);
        const float* d = source->audio.getReadPointer (0);
        for (int off = 0; off <= reach; ++off)
            for (int i : { centre - off, centre + off })
                if (i > 0 && i < n && (d[i - 1] <= 0.0f) != (d[i] <= 0.0f))
                    return (double) i;
        return at;
    }

    void paintOverlay (juce::Graphics& g)
    {
        if (overlay.isEmpty())
            return;
        auto r = getLocalBounds().reduced (10, 8).removeFromTop (40).withTrimmedRight (130);
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillRoundedRectangle (r.toFloat(), 6.0f);
        auto inner = r.reduced (8, 5);
        g.setColour (overlayAlarm ? juce::Colour (0xffFF6B5E) : MidiMorfColors::textLight.withAlpha (0.85f));
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawFittedText (overlay, inner.removeFromTop (18), juce::Justification::centredLeft, 1);
        auto bar = inner.removeFromBottom (8).toFloat();
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillRoundedRectangle (bar, 3.0f);
        const float fill = juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (meter, -60.0f) + 60.0f) / 60.0f);
        g.setColour (meter > 0.9f ? juce::Colour (0xffE0342A) : colour);
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * fill), 3.0f);
    }

    const juce::Colour handleColour { 0xffFFD23F };

    const Break* source = nullptr;
    std::vector<juce::Range<float>> peaks;
    double viewStart = 0.0, viewLength = 1.0, selStart = 0.0, selEnd = 1.0, beats = 0.0;
    juce::Colour colour { MidiMorfColors::accent };

    Grab grabbed = None;
    bool dragging = false;
    double panFrom = 0.0, pinchLength = 1.0, pinchAnchor = 0.0;
    float downX = 0.0f, pinchDistance = 1.0f;
    std::map<int, juce::Point<float>> touches;

    juce::String overlay;
    float meter = 0.0f;
    bool overlayAlarm = false;

    juce::TextButton zoomOut { "-" }, zoomIn { "+" }, zoomFit { "FIT" };

    GrainEngine* grains = nullptr;   // in play mode
    std::array<Slot, GrainEngine::NumTouch> slots {};
    bool hold = false, reversed = false;
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
            g.fillRoundedRectangle (ring.withSizeKeepingCentre (d * 0.36f, d * 0.36f), 3.0f);
        else
            g.fillEllipse (ring.reduced (d * 0.2f));
    }

private:
    bool recording = false;
};

//==============================================================================
// The sound page for a track's sampler, laid out like Ableton's Simpler:
//
//   • top:    how it plays (Slice / Keys / One-shot), one dropdown of the
//             built-in breaks (it shows the name of anything else loaded),
//             and IMPORT: a file (WAV, AIFF, MP3, M4A), the mic, or a note
//             rendered from a synth preset. The mic opens a record bar in
//             place of the edit row until DONE
//   • middle: the sample editor — trim it, and in Slice mode say how many
//             bars the trimmed part is, so a loop that isn't cut right can be
//             fixed here; REVERSE
//   • bottom: two pages of knobs. PLAY: warp (Slice), pitch, speed, grain,
//             gain, velocity, loop. SHAPE: the filter, fades and (Keys) the
//             envelope.
class SamplerPage : public juce::Component,
                    private juce::Timer
{
public:
    SamplerPage()
    {
        setLookAndFeel (&laf);

        for (int m = 0; m < 4; ++m)
        {
            auto* b = modeButtons.add (new juce::TextButton (SamplerSource::modeNames()[m]));
            b->setClickingTogglesState (true);
            b->setRadioGroupId (5150);
            b->onClick = [this, m] { if (sampler != nullptr) { sampler->setMode ((SamplerSource::Mode) m); refresh(); changed(); } };
            addAndMakeVisible (b);
        }

        // The one dropdown of what's loaded: the built-in breaks, or the name
        // of whatever came in through Import.
        breakCombo.setTextWhenNothingSelected ("Choose a break");
        breakCombo.addSectionHeading ("Breaks");
        breakCombo.addItemList (BreakLibrary::builtInNames(), 1);
        breakCombo.onChange = [this]
        {
            if (sampler != nullptr && breakCombo.getSelectedId() > 0
                && breakCombo.getSelectedId() - 1 != sampler->getBuiltInBreakIndex()
                && sampler->setBuiltInBreak (breakCombo.getSelectedId() - 1) != nullptr)
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
            if (sampler == nullptr) return;
            auto e = sampler->getEdit();
            e.start = start;
            e.end = end;
            applyEdit (e);
        };
        addAndMakeVisible (editor);

        reverseButton.setClickingTogglesState (true);
        reverseButton.onClick = [this]
        {
            if (sampler == nullptr) return;
            auto e = sampler->getEdit();
            e.reverse = reverseButton.getToggleState();
            applyEdit (e);
        };
        addAndMakeVisible (reverseButton);

        barsDown.onClick = [this] { stepBeats (-1); };
        barsUp.onClick   = [this] { stepBeats (1); };
        barsLabel.setJustificationType (juce::Justification::centred);
        barsLabel.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        for (auto* c : std::initializer_list<juce::Component*> { &barsDown, &barsUp, &barsLabel })
            addChildComponent (c);

        for (auto* b : { &playTab, &shapeTab })
        {
            b->setClickingTogglesState (true);
            b->setRadioGroupId (5151);
            b->onClick = [this] { layoutControls(); };
            addAndMakeVisible (b);
        }
        playTab.setToggleState (true, juce::dontSendNotification);

        // PLAY
        warpCombo.addItemList (SamplerSource::warpNames(), 1);
        warpCombo.onChange = [this] { setParam (SamplerSource::Warp, (float) (warpCombo.getSelectedId() - 1)); refreshEnables(); changed(); };
        warpLabel.setText ("WARP", juce::dontSendNotification);
        filterCombo.addItemList (SamplerSource::filterNames(), 1);
        filterCombo.onChange = [this] { setParam (SamplerSource::FilterType, (float) (filterCombo.getSelectedId() - 1)); refreshEnables(); changed(); };
        filterLabel.setText ("FILTER", juce::dontSendNotification);
        for (auto* l : { &warpLabel, &filterLabel })
        {
            l->setJustificationType (juce::Justification::centred);
            l->setFont (juce::FontOptions (11.0f, juce::Font::bold));
            l->setColour (juce::Label::textColourId, MidiMorfColors::textLight);
        }

        setUpKnob (transposeKnob, SamplerSource::Transpose, 1.0, [] (double v) { return (v > 0 ? "+" : "") + juce::String ((int) v) + " st"; });
        setUpKnob (detuneKnob, SamplerSource::Detune, 1.0, [] (double v) { return (v > 0 ? "+" : "") + juce::String ((int) v) + " ct"; });
        setUpKnob (speedKnob, SamplerSource::Speed, 0.01, [] (double v) { return juce::String (v, 2) + "x"; }, 1.0);
        setUpKnob (grainKnob, SamplerSource::GrainMs, 1.0, [] (double v) { return juce::String ((int) v) + " ms"; }, 40.0);
        setUpKnob (gainKnob, SamplerSource::Gain, 0.1, [] (double v) { return (v > 0 ? "+" : "") + juce::String (v, 1) + " dB"; });
        setUpKnob (velKnob, SamplerSource::VelSense, 0.01, [] (double v) { return juce::String (juce::roundToInt (v * 100)) + "%"; });
        setUpKnob (cutoffKnob, SamplerSource::Cutoff, 1.0, [] (double v) { return v >= 1000 ? juce::String (v / 1000.0, 1) + " k" : juce::String ((int) v) + " Hz"; }, 1000.0);
        setUpKnob (resKnob, SamplerSource::Resonance, 0.01, [] (double v) { return juce::String (juce::roundToInt (v * 100)) + "%"; });

        // GRAIN mode.
        auto percent = [] (double v) { return juce::String (juce::roundToInt (v * 100)) + "%"; };
        setUpKnob (densityKnob, SamplerSource::GDensity, 0.1, [] (double v) { return juce::String (v, v < 10 ? 1 : 0) + " /s"; }, 20.0);
        setUpKnob (sizeKnob, SamplerSource::GSize, 1.0, [] (double v) { return juce::String ((int) v) + " ms"; }, 80.0);
        setUpKnob (sprayKnob, SamplerSource::GSpray, 0.01, percent);
        setUpKnob (scanKnob, SamplerSource::GScan, 0.01, [] (double v) { return std::abs (v) < 0.005 ? juce::String ("still") : juce::String (v, 2) + "x"; });
        setUpKnob (loopLenKnob, SamplerSource::GLoop, 1.0, [] (double v) { return v >= 1000 ? juce::String (v / 1000.0, 2) + " s" : juce::String ((int) v) + " ms"; }, 300.0);
        setUpKnob (jitterKnob, SamplerSource::GJitter, 0.1, [] (double v) { return juce::String (v, 1) + " st"; });
        setUpKnob (spreadKnob, SamplerSource::GSpread, 0.01, percent);
        setUpKnob (windowKnob, SamplerSource::GWindow, 0.01, [] (double v) { return v < 0.02 ? juce::String ("smooth") : v > 0.98 ? juce::String ("hard") : juce::String (juce::roundToInt (v * 100)) + "%"; });
        setUpKnob (grainReverseKnob, SamplerSource::GReverse, 0.01, percent);
        setUpKnob (releaseKnob, SamplerSource::GRelease, 1.0, [] (double v) { return v >= 1000 ? juce::String (v / 1000.0, 2) + " s" : juce::String ((int) v) + " ms"; }, 400.0);

        yCombo.addItemList (SamplerSource::yAxisNames(), 1);
        yCombo.onChange = [this] { setParam (SamplerSource::GYAxis, (float) (yCombo.getSelectedId() - 1)); changed(); };
        yLabel.setText ("FINGER UP", juce::dontSendNotification);
        yLabel.setJustificationType (juce::Justification::centred);
        yLabel.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        yLabel.setColour (juce::Label::textColourId, MidiMorfColors::textLight);

        for (auto* b : { &holdButton, &freezeButton })
        {
            b->setClickingTogglesState (true);
            addChildComponent (b);
        }
        holdButton.onClick = [this]
        {
            setParam (SamplerSource::GHold, holdButton.getToggleState() ? 1.0f : 0.0f);
            editor.setHold (holdButton.getToggleState());
            changed();
        };
        freezeButton.onClick = [this] { setParam (SamplerSource::GFreeze, freezeButton.getToggleState() ? 1.0f : 0.0f); changed(); };

        // A finger on the waveform plays its cloud; notes play where the
        // last one went down.
        editor.onCloud = [this] (int cloud, bool down, float x, float y)
        {
            if (sampler == nullptr) return;
            sampler->grains().touch (cloud, down, x, y);
            if (down)
                sampler->setParam (SamplerSource::GPosition, x);
            else
                changed();
        };

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
        }

        loopButton.setClickingTogglesState (true);
        loopButton.onClick = [this] { setParam (SamplerSource::Loop, loopButton.getToggleState() ? 1.0f : 0.0f); changed(); };

        rootDown.onClick = [this] { nudgeRoot (-1); };
        rootUp.onClick   = [this] { nudgeRoot (1); };
        rootLabel.setJustificationType (juce::Justification::centred);
        rootLabel.setFont (juce::FontOptions (22.0f, juce::Font::bold));
        rootLabel.setColour (juce::Label::textColourId, MidiMorfColors::textLight);
        rootLabel.setColour (juce::Label::backgroundColourId, juce::Colours::black.withAlpha (0.35f));
        rootLabel.setColour (juce::Label::outlineColourId, MidiMorfColors::textLight.withAlpha (0.2f));
        rootCaption.setJustificationType (juce::Justification::centred);
        rootCaption.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        rootCaption.setColour (juce::Label::textColourId, MidiMorfColors::textLight);

        const char* names4[] = { "A", "D", "S", "R" };
        for (int i = 0; i < 4; ++i)
        {
            auto* s = envSliders.add (new juce::Slider (juce::Slider::LinearVertical, juce::Slider::TextBoxBelow));
            s->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 44, 16);
            s->setRange (i == 2 ? 0.0 : 0.001, i == 2 ? 1.0 : (i == 3 ? 8.0 : 5.0), 0.001);
            if (i != 2) s->setSkewFactorFromMidPoint (0.4);
            s->onValueChange = [this] { applyEnvelope(); };
            s->onDragEnd = [this] { changed(); };

            auto* l = envLabels.add (new juce::Label ({}, names4[i]));
            l->setJustificationType (juce::Justification::centred);
            l->setFont (juce::FontOptions (11.0f, juce::Font::bold));
        }

        for (auto* c : allControls())
            addChildComponent (c);
    }

    ~SamplerPage() override
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
        if (sampler->getBuiltInBreakIndex() < 0 && sampler->getBreak() != nullptr)
            breakCombo.setText (sampler->getSampleName(), juce::dontSendNotification);

        const auto& e = sampler->getEdit();
        reverseButton.setToggleState (e.reverse, juce::dontSendNotification);
        fadeInKnob.getSlider().setValue (e.fadeIn, juce::dontSendNotification);
        fadeOutKnob.getSlider().setValue (e.fadeOut, juce::dontSendNotification);

        for (auto& [knob, param] : paramKnobs)
            knob->getSlider().setValue (sampler->getParam (param), juce::dontSendNotification);
        warpCombo.setSelectedId (juce::roundToInt (sampler->getParam (SamplerSource::Warp)) + 1, juce::dontSendNotification);
        filterCombo.setSelectedId (juce::roundToInt (sampler->getParam (SamplerSource::FilterType)) + 1, juce::dontSendNotification);
        loopButton.setToggleState (sampler->getParam (SamplerSource::Loop) >= 0.5f, juce::dontSendNotification);

        rootLabel.setText (juce::MidiMessage::getMidiNoteName (sampler->getRootNote(), true, true, 4), juce::dontSendNotification);
        const auto env = sampler->getEnvelope();
        const float v[4] = { env.attack, env.decay, env.sustain, env.release };
        for (int i = 0; i < 4; ++i)
            envSliders[i]->setValue (v[i], juce::dontSendNotification);

        holdButton.setToggleState (sampler->getParam (SamplerSource::GHold) >= 0.5f, juce::dontSendNotification);
        freezeButton.setToggleState (sampler->getParam (SamplerSource::GFreeze) >= 0.5f, juce::dontSendNotification);
        yCombo.setSelectedId (juce::roundToInt (sampler->getParam (SamplerSource::GYAxis)) + 1, juce::dontSendNotification);
        editor.setPlayMode (mode == SamplerSource::Grain ? &sampler->grains() : nullptr);
        editor.setHold (holdButton.getToggleState());
        updateTimer();

        const auto* b = sampler->getBreak();
        barsLabel.setText (b != nullptr ? beatsText (b->beats) : juce::String(), juce::dontSendNotification);
        editor.setSample (sampler->getSource(), e, mode == SamplerSource::Slice && b != nullptr ? b->beats : 0.0, TrackColours::get (track));

        refreshEnables();
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
        Glassmorphism::paintPanel (g, getLocalBounds().toFloat(), samplerPagePalette);

        if (sampler == nullptr) return;

        // Name over the details, beside REVERSE.
        juce::String detail;
        if (const auto* b = sampler->getBreak())
            detail = sampler->getMode() == SamplerSource::Slice
                         ? juce::String (b->bpm(), 1) + " BPM, " + juce::String (b->lengthSeconds(), 2) + " s"
                         : sampler->getMode() == SamplerSource::Grain
                         ? juce::String (b->lengthSeconds(), 2) + " s. Touch the wave to play"
                         : juce::String (b->lengthSeconds(), 2) + " s";
        g.setColour (MidiMorfColors::textLight.withAlpha (0.8f));
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawFittedText (sampler->getSampleName(), infoArea.withTrimmedBottom (infoArea.getHeight() / 2), juce::Justification::bottomLeft, 1);
        g.setFont (juce::FontOptions (11.0f));
        g.setColour (MidiMorfColors::textLight.withAlpha (0.55f));
        g.drawFittedText (detail, infoArea.withTrimmedTop (infoArea.getHeight() / 2), juce::Justification::topLeft, 1);

        // Behind the knobs.
        g.setColour (juce::Colours::black.withAlpha (0.18f));
        g.fillRoundedRectangle (controlsArea.toFloat(), 8.0f);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (10, 8);

        // What plays and where it comes from.
        auto top = area.removeFromTop (34);
        for (auto* b : modeButtons)
            b->setBounds (top.removeFromLeft (70).reduced (2));
        top.removeFromLeft (6);
        importButton.setBounds (top.removeFromRight (110).reduced (2));
        top.removeFromRight (4);
        breakCombo.setBounds (top.reduced (2));
        area.removeFromTop (6);

        // Knobs at the bottom, the editor row above them, the editor fills
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
        auto bars = row.removeFromRight (150);
        holdButton.setBounds (bars.withWidth (bars.getWidth() / 2).reduced (2));
        freezeButton.setBounds (bars.withTrimmedLeft (bars.getWidth() / 2).reduced (2));
        barsDown.setBounds (bars.removeFromLeft (34).reduced (2));
        barsUp.setBounds (bars.removeFromRight (34).reduced (2));
        barsLabel.setBounds (bars);
        infoArea = row.reduced (4, 0);

        layoutControls();
    }

private:
    static inline const PagePalette samplerPagePalette {
        juce::Colour (0xff2A2014), juce::Colour (0xff120D07),
        juce::Colour (0xCC3A2C1A), juce::Colour (0xD8241A0E), juce::Colour (0xCC000000)
    };

    // Lengths the BARS stepper offers, in beats.
    static const std::vector<double>& beatSteps()
    {
        static const std::vector<double> s { 1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64 };
        return s;
    }

    static juce::String beatsText (double beats)
    {
        const int b = juce::roundToInt (beats);
        if (b % 4 == 0) return juce::String (b / 4) + (b == 4 ? " bar" : " bars");
        return juce::String (b) + (b == 1 ? " beat" : " beats");
    }

    std::vector<juce::Component*> allControls()
    {
        std::vector<juce::Component*> c { &warpCombo, &warpLabel, &filterCombo, &filterLabel, &loopButton,
                                          &rootDown, &rootUp, &rootLabel, &rootCaption, &fadeInKnob, &fadeOutKnob,
                                          &yCombo, &yLabel };
        for (auto& [knob, param] : paramKnobs) c.push_back (knob);
        for (auto* s : envSliders) c.push_back (s);
        for (auto* l : envLabels) c.push_back (l);
        return c;
    }

    // Shows this tab's controls for this mode and lays them out in a row.
    void layoutControls()
    {
        for (auto* c : allControls())
            c->setVisible (false);
        if (sampler == nullptr || controlsArea.isEmpty())
            return;

        const auto mode = sampler->getMode();
        const bool slice = mode == SamplerSource::Slice, keys = mode == SamplerSource::Keys, grain = mode == SamplerSource::Grain;
        const bool slicing = slice && sampler->getBreak() != nullptr;
        for (auto* c : std::initializer_list<juce::Component*> { &barsDown, &barsUp, &barsLabel })
            c->setVisible (slicing && ! micOpen);
        for (auto* c : std::initializer_list<juce::Component*> { &holdButton, &freezeButton })
            c->setVisible (grain && ! micOpen);

        // Each item: a component (or a pair stacked label-over-control) and
        // its share of the row.
        struct Item { juce::Component* c; juce::Component* label = nullptr; float weight = 1.0f; };
        std::vector<Item> items;

        if (grain && playTab.getToggleState())
        {
            for (auto* k : { &densityKnob, &sizeKnob, &sprayKnob, &scanKnob, &loopLenKnob, &transposeKnob, &jitterKnob })
                items.push_back ({ k });
            items.push_back ({ &yCombo, &yLabel, 1.3f });
            items.push_back ({ &gainKnob });
        }
        else if (grain)
        {
            items.push_back ({ &filterCombo, &filterLabel, 1.5f });
            for (auto* k : { &cutoffKnob, &resKnob, &spreadKnob, &windowKnob, &grainReverseKnob, &releaseKnob, &velKnob })
                items.push_back ({ k });
        }
        else if (playTab.getToggleState())
        {
            if (slice) items.push_back ({ &warpCombo, &warpLabel, 1.5f });
            if (keys)  items.push_back ({ &rootLabel, nullptr, 1.1f });
            items.push_back ({ &transposeKnob });
            items.push_back ({ &detuneKnob });
            if (! slice) items.push_back ({ &speedKnob });
            items.push_back ({ &grainKnob });
            if (! slice) items.push_back ({ &loopButton, nullptr, 0.9f });
            items.push_back ({ &gainKnob });
            items.push_back ({ &velKnob });
        }
        else
        {
            items.push_back ({ &filterCombo, &filterLabel, 1.5f });
            items.push_back ({ &cutoffKnob });
            items.push_back ({ &resKnob });
            items.push_back ({ &fadeInKnob });
            items.push_back ({ &fadeOutKnob });
            if (keys)
                for (int i = 0; i < 4; ++i)
                    items.push_back ({ envSliders[i], envLabels[i], 0.6f });
        }

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

            if (it.c == &rootLabel)
            {
                // ROOT, laid out like a knob: the note in a display, − and +
                // under it, and the name along the bottom with the others.
                auto col = cell.withSizeKeepingCentre (juce::jmin (cell.getWidth(), 96), cell.getHeight());
                rootCaption.setBounds (col.removeFromBottom (18));
                col.removeFromBottom (4);
                auto buttons = col.removeFromBottom (30);
                rootDown.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2).reduced (2, 1));
                rootUp.setBounds (buttons.reduced (2, 1));
                col.removeFromBottom (6);
                rootLabel.setBounds (col.withSizeKeepingCentre (col.getWidth() - 4, juce::jmin (col.getHeight(), 40)));
                for (auto* c : std::initializer_list<juce::Component*> { &rootDown, &rootUp, &rootLabel, &rootCaption }) c->setVisible (true);
                continue;
            }
            if (it.label != nullptr)
            {
                it.label->setBounds (cell.removeFromTop (18));
                it.label->setVisible (true);
                if (dynamic_cast<juce::ComboBox*> (it.c) != nullptr)
                    cell = cell.withSizeKeepingCentre (cell.getWidth(), 30).withY (cell.getY() + 6);
            }
            if (it.c == &loopButton)
                cell = cell.withSizeKeepingCentre (cell.getWidth(), 32);
            it.c->setBounds (cell);
            it.c->setVisible (true);
        }
        refreshEnables();
    }

    // Grain only matters when something is stretched; resonance only with a
    // filter.
    void refreshEnables()
    {
        if (sampler == nullptr) return;
        const bool slice = sampler->getMode() == SamplerSource::Slice;
        const bool grainy = slice ? juce::roundToInt (sampler->getParam (SamplerSource::Warp)) == BreakPlayer::Stretch
                                  : std::abs (sampler->getParam (SamplerSource::Speed) - 1.0f) > 0.001f;
        grainKnob.setEnabled (grainy);
        grainKnob.setAlpha (grainy ? 1.0f : 0.4f);
        const bool filtering = juce::roundToInt (sampler->getParam (SamplerSource::FilterType)) != BreakPlayer::FilterOff;
        for (auto* k : { &cutoffKnob, &resKnob })
        {
            k->setEnabled (filtering);
            k->setAlpha (filtering ? 1.0f : 0.4f);
        }
    }

    void setUpKnob (Knob& k, SamplerSource::Param p, double step, std::function<juce::String (double)> text, double skewMid = 0.0)
    {
        const auto& info = SamplerSource::paramInfo (p);
        auto& s = k.getSlider();
        s.setRange (info.min, info.max, step);
        if (skewMid > 0.0) s.setSkewFactorFromMidPoint (skewMid);
        s.setDoubleClickReturnValue (true, info.def);
        s.textFromValueFunction = text;
        s.updateText();
        s.onValueChange = [this, &s, p] { setParam (p, (float) s.getValue()); if (p == SamplerSource::Speed) refreshEnables(); };
        s.onDragEnd = [this] { changed(); };
        paramKnobs.push_back ({ &k, p });
    }

    void setParam (SamplerSource::Param p, float v) { if (sampler != nullptr) sampler->setParam (p, v); }

    void changed() { if (onChanged) onChanged(); }

    // An edit rebuilds the sample; the slicer follows it.
    void applyEdit (const SamplerSource::Edit& e)
    {
        if (sampler == nullptr || e == sampler->getEdit()) return;
        sampler->setEdit (e);
        loaded();
    }

    void applyFades()
    {
        if (sampler == nullptr) return;
        auto e = sampler->getEdit();
        e.fadeIn = (float) fadeInKnob.getSlider().getValue();
        e.fadeOut = (float) fadeOutKnob.getSlider().getValue();
        applyEdit (e);
    }

    // BARS − / +: the trimmed part's length, which sets its tempo.
    void stepBeats (int direction)
    {
        if (sampler == nullptr || sampler->getBreak() == nullptr) return;
        const double now = sampler->getBreak()->beats;
        const auto& steps = beatSteps();
        double next = now;
        if (direction > 0)
        {
            for (double s : steps) if (s > now + 0.01) { next = s; break; }
        }
        else
        {
            for (auto it = steps.rbegin(); it != steps.rend(); ++it) if (*it < now - 0.01) { next = *it; break; }
        }
        auto e = sampler->getEdit();
        e.beats = next;
        applyEdit (e);
    }

    bool isRecording() const { return recordTarget != nullptr; }

    // Import: a file, a recording from the mic, or a note rendered from one
    // of the synth's presets.
    void showImportMenu()
    {
        if (sampler == nullptr) return;

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
                                 sampler->isFromPreset() && sampler->getSampleName() == presets[(size_t) i].name);
            synths.addSubMenu (cat, sub);
        }
        menu.addSubMenu ("Render a synth preset", synths);

        juce::Component::SafePointer<SamplerPage> safe (this);
        menu.showMenuAsync (PopupStyle::attached (menu, importButton, 36), [safe] (int result)
        {
            if (safe == nullptr || safe->sampler == nullptr || result <= 0) return;
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
        // The importer loads into whichever sampler: give it this one.
        pendingImport = sampler;
        juce::Component::SafePointer<SamplerPage> safe (this);
        chooser = std::make_unique<juce::FileChooser> ("Import a sample", juce::File(), BreakImport::patterns);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [safe] (const juce::FileChooser& fc) { if (safe != nullptr) safe->imported (fc.getURLResult()); });
    }

    void renderPreset (const juce::String& name)
    {
        if (sampler->renderFromPreset (name) == nullptr) return;
        if (sampler->getMode() == SamplerSource::Slice)
            sampler->setMode (SamplerSource::Keys);   // a rendered note is for playing, not slicing
        loaded();
    }

    // Opens the mic (iOS asks the first time) and shows the record bar,
    // armed: nothing records until the record button is tapped.
    void openRecorder()
    {
        if (micOpen || recorder == nullptr || onMicNeeded == nullptr)
            return;

        importButton.setEnabled (false);
        juce::Component::SafePointer<SamplerPage> safe (this);
        onMicNeeded ([safe] (bool ok)
        {
            if (safe == nullptr) return;
            safe->importButton.setEnabled (true);
            if (! ok)
            {
                safe->alert ("No microphone", "Midi-Morf can't use the mic. Allow it in " + Platform::microphoneSettingsPath() + ".");
                return;
            }
            if (! safe->isShowing() || safe->sampler == nullptr)
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
        if (! micOpen || sampler == nullptr)
            return;
        recordTarget = sampler;
        recorder->start();
        recordButton.setRecording (true);
    }

    // Stop, and load the take into the sampler it was recorded for.
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
        if (target->getMode() == SamplerSource::Slice)
            target->setMode (SamplerSource::Keys);   // a recording is for playing, not slicing
        if (target == sampler)
            loaded();
        else
            changed();
    }

    // The timer runs for the mic's meter and to draw the grain clouds.
    void updateTimer()
    {
        const bool clouds = isVisible() && sampler != nullptr && sampler->getMode() == SamplerSource::Grain;
        if (micOpen || clouds) startTimerHz (30);
        else                   stopTimer();
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
    juce::TextButton importButton { "IMPORT" };
    juce::ComboBox breakCombo;
    RecordButton recordButton;
    juce::Label recordStatus;
    juce::TextButton recordDone { "DONE" };
    SampleEditor editor;

    juce::TextButton reverseButton { "REV" }, barsDown { "-" }, barsUp { "+" };
    juce::Label barsLabel;
    juce::TextButton playTab { "PLAY" }, shapeTab { "SHAPE" };

    juce::ComboBox warpCombo, filterCombo;
    juce::Label warpLabel, filterLabel;
    Knob transposeKnob { "TRANSPOSE", -24, 24, 0 }, detuneKnob { "DETUNE", -50, 50, 0 }, speedKnob { "SPEED", 0.25f, 4, 1 },
         grainKnob { "GRAIN", 5, 250, 40 }, gainKnob { "GAIN", -24, 12, 0 }, velKnob { "VEL", 0, 1, 1 },
         cutoffKnob { "CUTOFF", 20, 20000, 20000 }, resKnob { "RES", 0, 1, 0.2f },
         fadeInKnob { "FADE IN", 0, 2000, 0 }, fadeOutKnob { "FADE OUT", 0, 2000, 0 },
         densityKnob { "DENSITY", 1, 100, 30 }, sizeKnob { "SIZE", 5, 500, 80 }, sprayKnob { "SPRAY", 0, 1, 0.1f },
         scanKnob { "SCAN", -2, 2, 1 }, loopLenKnob { "LOOP", 10, 2000, 300 }, jitterKnob { "JITTER", 0, 12, 0 },
         spreadKnob { "SPREAD", 0, 1, 0.5f }, windowKnob { "WINDOW", 0, 1, 0 }, grainReverseKnob { "REVERSE", 0, 1, 0 },
         releaseKnob { "RELEASE", 5, 4000, 300 };
    juce::ComboBox yCombo;
    juce::Label yLabel;
    juce::TextButton holdButton { "HOLD" }, freezeButton { "FREEZE" };
    std::vector<std::pair<Knob*, SamplerSource::Param>> paramKnobs;
    juce::TextButton loopButton { "LOOP" };
    juce::TextButton rootDown { "-" }, rootUp { "+" };
    juce::Label rootLabel, rootCaption { {}, "ROOT" };
    juce::OwnedArray<juce::Slider> envSliders;
    juce::OwnedArray<juce::Label> envLabels;

    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<int> infoArea, controlsArea;
};
