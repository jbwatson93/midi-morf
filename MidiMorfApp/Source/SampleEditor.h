#pragma once

#include <JuceHeader.h>
#include "MidiMorfColors.h"
#include "SampleSource.h"
#include "GrainEngine.h"
#include <map>

//==============================================================================
// The sample editor: the whole loaded sample, with the part that plays
// between two yellow handles (the same look as Step-Seq's range bracket).
// Drag a handle to trim; ends snap to the nearest zero crossing so they never
// click. Drag the waveform to scroll, pinch (or − / + / FIT) to zoom. In the
// Sampler's Slice mode the beat grid sits inside the trimmed part, so it's
// easy to see when the bars line up. The Grain page plays it instead.
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
    void setSample (const Break* src, const SampleSource::Edit& edit, double gridBeats, juce::Colour c)
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
    // Play mode (the Grain page): the waveform is played, not edited. Each finger is a cloud
    // (GrainEngine); `x` is 0..1 through the trimmed sample as it plays (so
    // right to left when reversed), `y` 0..1 up the waveform. With HOLD a
    // lifted finger's cloud keeps playing; tapping it stops it, dragging it
    // moves it.
    std::function<void (int cloud, bool down, float x, float y)> onCloud;

    // nullptr leaves play mode. The clouds' slots start empty either way:
    // held clouds of another track stay with it.
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
