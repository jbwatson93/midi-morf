#pragma once

#include <JuceHeader.h>
#include "WavetableEngine.h"
#include "SynthWidgets.h"
#include "MidiMorfColors.h"

//==============================================================================
// The synth page's modulation UI, Serum-style but for thumbs:
//
//   ModContext      — which source is being assigned, the colour of each
//                     source, and the bridge between knobs and the matrix.
//   ModSourceStrip  — the row of sources along the bottom of every tab. Tap
//                     one, then drag any knob with a ring to set how much it
//                     moves it; tap it again (or another) to finish.
//   LfoShapeEditor  — draw an LFO: tap to add a point, drag points, drag a
//                     segment's middle up/down to bend it, double-tap a point
//                     to delete it; PENCIL paints steps on the grid.
//   EnvDisplay      — an ADSR drawn as its shape.
namespace SynthModUI
{
    inline juce::Colour sourceColour (int source)
    {
        using E = WavetableEngine;
        switch (source)
        {
            case E::ModSrcLFO:      return juce::Colour (0xff4FC3F7);
            case E::ModSrcLFO2:     return juce::Colour (0xff81C784);
            case E::ModSrcLFO3:     return juce::Colour (0xffFFD54F);
            case E::ModSrcLFO4:     return juce::Colour (0xffBA68C8);
            case E::ModSrcEnv1:     return juce::Colour (0xffFF8A65);
            case E::ModSrcEnv2:     return juce::Colour (0xffF06292);
            case E::ModSrcEnv3:     return juce::Colour (0xffE57373);
            case E::ModSrcVelocity: return juce::Colour (0xffAED581);
            case E::ModSrcNote:     return juce::Colour (0xff4DB6AC);
            case E::ModSrcRandom:   return juce::Colour (0xff90A4AE);
            case E::ModSrcModWheel: return juce::Colour (0xffFFB74D);
            case E::ModSrcAccelX:   return juce::Colour (0xff7986CB);
            case E::ModSrcAccelY:   return juce::Colour (0xff9575CD);
            case E::ModSrcMacro1:   return juce::Colour (0xffF48FB1);
            case E::ModSrcMacro2:   return juce::Colour (0xffCE93D8);
            case E::ModSrcMacro3:   return juce::Colour (0xff80DEEA);
            case E::ModSrcMacro4:   return juce::Colour (0xffC5E1A5);
            default:                return juce::Colours::grey;
        }
    }

    // Sources that swing either side of zero.
    inline bool isBipolar (int source)
    {
        using E = WavetableEngine;
        return (source >= E::ModSrcLFO && source <= E::ModSrcLFO4) || source == E::ModSrcAccelX
            || source == E::ModSrcAccelY || source == E::ModSrcNote || source == E::ModSrcRandom;
    }

    //==========================================================================
    class ModContext : public ModAssignHost, private juce::Timer
    {
    public:
        ModContext (WavetableEngine& e, std::function<void()> changed)
            : engine (e), onChange (std::move (changed))
        {
            startTimerHz (20);
        }

        void attach (Knob& k, int dest)
        {
            k.setModDest (this, dest);
            knobs.push_back (&k);
        }

        void setAssigning (int source)
        {
            assigning = source;
            for (auto* k : knobs) k->refreshModState();
            if (onAssigningChanged) onAssigningChanged();
        }

        // Called when the matrix changes outside the knobs (MATRIX tab, preset).
        void matrixChanged()
        {
            for (auto* k : knobs) k->repaint();
            if (onRoutesChanged) onRoutesChanged();
        }

        std::function<void()> onAssigningChanged, onRoutesChanged;

        //======================================================================
        int assigningSource() const override { return assigning; }
        juce::Colour assigningColour() const override { return sourceColour (assigning); }

        void nudgeRoute (int dest, float delta) override
        {
            if (assigning < 0 || dest <= 0) return;
            int slot = engine.findRouting (assigning, dest);
            if (slot < 0)
            {
                slot = engine.freeRoutingSlot();
                if (slot < 0) return;   // all 16 in use
                engine.clearRouting (slot);
                engine.setRoutingSource (slot, assigning);
                engine.setRoutingDest (slot, dest);
            }
            engine.setRoutingEnabled (slot, true);
            engine.setRoutingAmount (slot, engine.getRoutingAmount (slot) + delta);
            changed();
        }

        void removeRoute (int dest) override
        {
            const int slot = engine.findRouting (assigning, dest);
            if (slot < 0) return;
            engine.clearRouting (slot);
            changed();
        }

        std::vector<Ring> ringsFor (int dest) const override
        {
            std::vector<Ring> rings;
            for (int i = 0; i < WavetableEngine::NumRoutingSlots; ++i)
            {
                const auto& r = engine.getRouting (i);
                if (r.dest == dest && r.isActive())
                    rings.push_back ({ sourceColour (r.source), r.amount, isBipolar (r.source) });
            }
            return rings;
        }

        float liveModFor (int dest) const override { return engine.getLiveMod (dest); }

    private:
        void changed()
        {
            if (onChange) onChange();
            if (onRoutesChanged) onRoutesChanged();
        }

        // Keeps the live dots moving on knobs that are modulated.
        void timerCallback() override
        {
            for (auto* k : knobs)
                if (k->isShowing() && k->hasModulation())
                    k->repaint();
        }

        WavetableEngine& engine;
        std::function<void()> onChange;
        std::vector<Knob*> knobs;
        int assigning = -1;
    };

    //==========================================================================
    class ModSourceStrip : public juce::Component, private juce::Timer
    {
    public:
        ModSourceStrip (WavetableEngine& e, ModContext& c) : engine (e), ctx (c)
        {
            using E = WavetableEngine;
            sources = { E::ModSrcEnv1, E::ModSrcEnv2, E::ModSrcEnv3, E::ModSrcLFO, E::ModSrcLFO2, E::ModSrcLFO3, E::ModSrcLFO4,
                        E::ModSrcVelocity, E::ModSrcNote, E::ModSrcRandom, E::ModSrcModWheel, E::ModSrcAccelX, E::ModSrcAccelY,
                        E::ModSrcMacro1, E::ModSrcMacro2, E::ModSrcMacro3, E::ModSrcMacro4 };
            startTimerHz (20);
        }

        void paint (juce::Graphics& g) override
        {
            const auto names = WavetableEngine::sourceShortNames();
            for (int i = 0; i < (int) sources.size(); ++i)
            {
                const int s = sources[(size_t) i];
                auto r = chipBounds (i).toFloat().reduced (1.5f);
                const auto colour = sourceColour (s);
                const bool active = ctx.assigningSource() == s;

                g.setColour (active ? colour : juce::Colour (0xff232328));
                g.fillRoundedRectangle (r, 4.0f);
                g.setColour (colour.withAlpha (active ? 1.0f : 0.7f));
                g.drawRoundedRectangle (r, 4.0f, 1.0f);

                // How hard this source is being used (routes) as a dot.
                if (! active && usage (s) > 0)
                {
                    g.setColour (colour);
                    g.fillEllipse (r.getRight() - 7.0f, r.getY() + 3.0f, 4.0f, 4.0f);
                }

                // LFOs show where they are.
                if (s >= WavetableEngine::ModSrcLFO && s <= WavetableEngine::ModSrcLFO4)
                {
                    const float v = engine.getLiveLfo (s - WavetableEngine::ModSrcLFO);
                    auto bar = r.reduced (4.0f, 0.0f).removeFromBottom (3.0f).translated (0.0f, -2.0f);
                    g.setColour ((active ? juce::Colours::black : colour).withAlpha (0.6f));
                    const float mid = bar.getCentreX();
                    const float x = mid + v * bar.getWidth() * 0.5f;
                    g.fillRect (juce::Rectangle<float>::leftTopRightBottom (juce::jmin (mid, x), bar.getY(), juce::jmax (mid, x), bar.getBottom()));
                }

                g.setColour (active ? juce::Colours::black : MidiMorfColors::textLight);
                g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
                g.drawFittedText (names[s], r.toNearestInt().reduced (2, 0), juce::Justification::centred, 1, 0.7f);
            }
        }

        void mouseUp (const juce::MouseEvent& e) override
        {
            for (int i = 0; i < (int) sources.size(); ++i)
                if (chipBounds (i).contains (e.getPosition()))
                {
                    const int s = sources[(size_t) i];
                    ctx.setAssigning (ctx.assigningSource() == s ? -1 : s);
                    repaint();
                    return;
                }
        }

    private:
        juce::Rectangle<int> chipBounds (int i) const
        {
            const float w = (float) getWidth() / (float) sources.size();
            return { juce::roundToInt (w * (float) i), 0, juce::roundToInt (w * (float) (i + 1)) - juce::roundToInt (w * (float) i), getHeight() };
        }

        int usage (int source) const
        {
            int n = 0;
            for (int i = 0; i < WavetableEngine::NumRoutingSlots; ++i)
                if (engine.getRouting (i).source == source && engine.getRouting (i).isActive()) ++n;
            return n;
        }

        void timerCallback() override { if (isShowing()) repaint(); }

        WavetableEngine& engine;
        ModContext& ctx;
        std::vector<int> sources;
    };

    //==========================================================================
    class LfoShapeEditor : public juce::Component, private juce::Timer
    {
    public:
        LfoShapeEditor (WavetableEngine& e) : engine (e) { startTimerHz (30); }

        void setLfo (int index) { lfo = index; repaint(); }
        void setGrid (int divisions) { grid = juce::jmax (1, divisions); repaint(); }
        void setPencil (bool on) { pencil = on; }
        int  getGrid() const { return grid; }

        std::function<void()> onShapeChanged;

        void paint (juce::Graphics& g) override
        {
            auto bounds = getLocalBounds().toFloat().reduced (1.5f);
            g.setColour (juce::Colour (0xff141417));
            g.fillRoundedRectangle (bounds, 5.0f);
            plot = bounds.reduced (10.0f, 10.0f);

            g.setColour (MidiMorfColors::textLight.withAlpha (0.07f));
            for (int i = 1; i < grid; ++i)
                g.drawVerticalLine ((int) (plot.getX() + plot.getWidth() * (float) i / (float) grid), plot.getY(), plot.getBottom());
            for (int i = 1; i < 4; ++i)
                g.drawHorizontalLine ((int) (plot.getY() + plot.getHeight() * (float) i / 4.0f), plot.getX(), plot.getRight());
            g.setColour (MidiMorfColors::textLight.withAlpha (0.16f));
            g.drawHorizontalLine ((int) plot.getCentreY(), plot.getX(), plot.getRight());

            const auto colour = sourceColour (WavetableEngine::ModSrcLFO + lfo);
            const auto& shape = engine.getLfo (lfo).getShape();

            juce::Path curve;
            const int steps = juce::jmax (64, (int) plot.getWidth());
            for (int i = 0; i <= steps; ++i)
            {
                const float x = (float) i / (float) steps;
                const auto p = toScreen (x, SynthMod::valueAt (shape, x));
                if (i == 0) curve.startNewSubPath (p); else curve.lineTo (p);
            }
            juce::Path fill (curve);
            fill.lineTo (plot.getRight(), plot.getCentreY());
            fill.lineTo (plot.getX(), plot.getCentreY());
            fill.closeSubPath();
            g.setColour (colour.withAlpha (0.15f));
            g.fillPath (fill);
            g.setColour (colour);
            g.strokePath (curve, juce::PathStrokeType (2.0f));

            // Points, and each segment's bend handle.
            for (size_t i = 0; i < shape.size(); ++i)
            {
                const auto p = toScreen (shape[i].x, shape[i].y);
                g.setColour (i == (size_t) dragPoint ? juce::Colours::white : colour.brighter (0.4f));
                g.fillEllipse (p.x - 5.0f, p.y - 5.0f, 10.0f, 10.0f);
                if (i + 1 < shape.size() && shape[i + 1].x - shape[i].x > 0.02f)
                {
                    const auto h = handle (shape, i);
                    g.setColour (colour.withAlpha (0.6f));
                    g.drawEllipse (h.x - 4.0f, h.y - 4.0f, 8.0f, 8.0f, 1.2f);
                }
            }

            // Playhead.
            const float ph = engine.getLiveLfoPhase (lfo);
            if (ph >= 0.0f)
            {
                const float x = plot.getX() + plot.getWidth() * ph;
                g.setColour (juce::Colours::white.withAlpha (0.5f));
                g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
            }
            const float live = engine.getLiveLfo (lfo);
            g.setColour (juce::Colours::white);
            g.fillEllipse (plot.getRight() + 2.0f, toScreen (0.0f, live / juce::jmax (0.01f, engine.getLfo (lfo).depth)).y - 3.0f, 6.0f, 6.0f);
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            auto shape = engine.getLfo (lfo).getShape();
            dragPoint = dragHandle = -1;

            if (pencil) { paintStep (e.position); return; }

            for (size_t i = 0; i < shape.size(); ++i)
                if (toScreen (shape[i].x, shape[i].y).getDistanceFrom (e.position) < 14.0f) { dragPoint = (int) i; return; }
            for (size_t i = 0; i + 1 < shape.size(); ++i)
                if (shape[i + 1].x - shape[i].x > 0.02f && handle (shape, i).getDistanceFrom (e.position) < 14.0f)
                {
                    dragHandle = (int) i;
                    handleStartCurve = shape[i].curve;
                    handleStartY = e.position.y;
                    return;
                }

            // A new point where the finger is.
            const auto v = fromScreen (e.position);
            shape.push_back ({ snapX (v.x), v.y, 0.0f });
            SynthMod::tidy (shape);
            for (size_t i = 0; i < shape.size(); ++i)
                if (std::abs (shape[i].x - snapX (v.x)) < 1.0e-4f && std::abs (shape[i].y - v.y) < 1.0e-4f) dragPoint = (int) i;
            commit (shape);
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (pencil) { paintStep (e.position); return; }

            auto shape = engine.getLfo (lfo).getShape();
            if (dragPoint >= 0 && dragPoint < (int) shape.size())
            {
                const auto v = fromScreen (e.position);
                auto& p = shape[(size_t) dragPoint];
                const bool end = dragPoint == 0 || dragPoint == (int) shape.size() - 1;
                if (! end)
                {
                    const float lo = shape[(size_t) dragPoint - 1].x, hi = shape[(size_t) dragPoint + 1].x;
                    p.x = juce::jlimit (lo, hi, snapX (v.x));
                }
                p.y = v.y;
                commit (shape, false);
            }
            else if (dragHandle >= 0 && dragHandle + 1 < (int) shape.size())
            {
                // Up bows the segment up, whichever way it runs.
                auto& a = shape[(size_t) dragHandle];
                const float dir = shape[(size_t) dragHandle + 1].y >= a.y ? 1.0f : -1.0f;
                a.curve = juce::jlimit (-1.0f, 1.0f, handleStartCurve - dir * (e.position.y - handleStartY) / 80.0f);
                commit (shape, false);
            }
        }

        void mouseUp (const juce::MouseEvent&) override
        {
            dragPoint = dragHandle = -1;
            lastPencilCell = -1;
            if (onShapeChanged) onShapeChanged();
            repaint();
        }

        void mouseDoubleClick (const juce::MouseEvent& e) override
        {
            auto shape = engine.getLfo (lfo).getShape();
            for (size_t i = 1; i + 1 < shape.size(); ++i)
                if (toScreen (shape[i].x, shape[i].y).getDistanceFrom (e.position) < 14.0f)
                {
                    shape.erase (shape.begin() + (long) i);
                    commit (shape);
                    return;
                }
        }

    private:
        juce::Point<float> toScreen (float x, float y) const
        {
            return { plot.getX() + plot.getWidth() * x, plot.getCentreY() - plot.getHeight() * 0.5f * juce::jlimit (-1.0f, 1.0f, y) };
        }

        juce::Point<float> fromScreen (juce::Point<float> p) const
        {
            return { juce::jlimit (0.0f, 1.0f, (p.x - plot.getX()) / plot.getWidth()),
                     juce::jlimit (-1.0f, 1.0f, (plot.getCentreY() - p.y) / (plot.getHeight() * 0.5f)) };
        }

        juce::Point<float> handle (const SynthMod::Shape& s, size_t i) const
        {
            const float x = (s[i].x + s[i + 1].x) * 0.5f;
            return toScreen (x, SynthMod::valueAt (s, x));
        }

        float snapX (float x) const
        {
            const float step = 1.0f / (float) grid;
            const float snapped = std::round (x / step) * step;
            return std::abs (snapped - x) < step * 0.2f ? snapped : x;   // snap when close
        }

        // PENCIL: the grid cell under the finger becomes a flat step.
        void paintStep (juce::Point<float> pos)
        {
            const auto v = fromScreen (pos);
            const int cell = juce::jlimit (0, grid - 1, (int) (v.x * (float) grid));
            const float x0 = (float) cell / (float) grid, x1 = (float) (cell + 1) / (float) grid;
            const float y = std::round (v.y * 8.0f) / 8.0f;
            if (cell == lastPencilCell && std::abs (y - lastPencilY) < 1.0e-4f) return;
            lastPencilCell = cell; lastPencilY = y;

            auto shape = engine.getLfo (lfo).getShape();
            const float before = SynthMod::valueAt (shape, x0 - 1.0e-4f), after = SynthMod::valueAt (shape, x1 + 1.0e-4f);
            SynthMod::Shape next;
            for (auto& p : shape)
                if (p.x < x0 - 1.0e-5f || p.x > x1 + 1.0e-5f) next.push_back (p);
            if (cell > 0) next.push_back ({ x0, before });
            next.push_back ({ x0, y });
            next.push_back ({ x1, y });
            if (cell < grid - 1) next.push_back ({ x1, after });
            commit (next);
        }

        void commit (SynthMod::Shape shape, bool notify = true)
        {
            engine.getLfo (lfo).setShape (std::move (shape));
            repaint();
            if (notify && onShapeChanged) onShapeChanged();
        }

        void timerCallback() override { if (isShowing()) repaint(); }

        WavetableEngine& engine;
        juce::Rectangle<float> plot;
        int lfo = 0, grid = 8;
        bool pencil = false;
        int dragPoint = -1, dragHandle = -1;
        float handleStartCurve = 0.0f, handleStartY = 0.0f;
        int lastPencilCell = -1;
        float lastPencilY = 0.0f;
    };

    //==========================================================================
    class EnvDisplay : public juce::Component
    {
    public:
        void set (float a, float d, float s, float r, juce::Colour c)
        {
            attack = a; decay = d; sustain = s; release = r; colour = c;
            repaint();
        }

        void paint (juce::Graphics& g) override
        {
            auto bounds = getLocalBounds().toFloat().reduced (1.5f);
            g.setColour (juce::Colour (0xff141417));
            g.fillRoundedRectangle (bounds, 5.0f);
            auto plot = bounds.reduced (10.0f, 8.0f);

            // Each stage gets width by its time, with the sustain a fixed hold.
            const float hold = 0.5f;
            const float total = attack + decay + hold + release;
            auto x = [&] (float t) { return plot.getX() + plot.getWidth() * t / total; };
            auto y = [&] (float v) { return plot.getBottom() - plot.getHeight() * v; };

            juce::Path p;
            p.startNewSubPath (x (0), y (0));
            p.lineTo (x (attack), y (1));
            p.lineTo (x (attack + decay), y (sustain));
            p.lineTo (x (attack + decay + hold), y (sustain));
            p.lineTo (x (total), y (0));
            juce::Path fill (p);
            fill.closeSubPath();
            g.setColour (colour.withAlpha (0.18f));
            g.fillPath (fill);
            g.setColour (colour);
            g.strokePath (p, juce::PathStrokeType (2.0f));
        }

    private:
        float attack = 0.01f, decay = 0.3f, sustain = 0.7f, release = 0.5f;
        juce::Colour colour { juce::Colours::orange };
    };
}
