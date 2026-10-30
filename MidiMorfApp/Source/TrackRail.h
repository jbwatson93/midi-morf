#pragma once

#include <JuceHeader.h>
#include "MidiMorfColors.h"
#include "TrackRack.h"

//==============================================================================
// One colour per track, used wherever a track is shown (the rail, the header,
// Home's rows, later the Session rows and mixer strips).
namespace TrackColours
{
    inline juce::Colour get (int track)
    {
        static const juce::Colour c[TrackRack::NumTracks] = {
            juce::Colour (0xffE0564A),   // T1 coral red
            juce::Colour (0xffA06CD5),   // T2 violet
            juce::Colour (0xffE0A030),   // T3 amber
            juce::Colour (0xff3FA7C9),   // T4 teal
        };
        return juce::isPositiveAndBelow (track, TrackRack::NumTracks) ? c[track] : juce::Colours::grey;
    }
}

//==============================================================================
// Shared card look for everything on the wood rail.
namespace RailCard
{
    inline void paint (juce::Graphics& g, juce::Rectangle<float> bounds, juce::Colour edge, bool lit, bool down)
    {
        g.setColour (lit ? edge.darker (1.6f).withAlpha (0.95f) : juce::Colour (0xff2A2A2E).withAlpha (0.85f));
        g.fillRoundedRectangle (bounds, 8.0f);
        g.setColour (edge.withAlpha (lit ? 1.0f : 0.55f));
        g.drawRoundedRectangle (bounds.reduced (0.5f), 8.0f, lit ? 2.2f : 1.0f);

        if (down)
        {
            g.setColour (edge.withAlpha (0.25f));
            g.fillRoundedRectangle (bounds, 8.0f);
        }
    }
}

//==============================================================================
// One track on the rail: its number in the track colour over what it is
// showing — the instrument's short name, or "Sound". Lit while the screen
// belongs to this track.
class RailTrackButton : public juce::Button
{
public:
    explicit RailTrackButton (int t) : juce::Button ("Track" + juce::String (t + 1)), track (t) {}

    void setState (const juce::String& label, bool isLit, bool isEmpty)
    {
        if (label == caption && isLit == lit && isEmpty == empty)
            return;
        caption = label; lit = isLit; empty = isEmpty;
        repaint();
    }

protected:
    void paintButton (juce::Graphics& g, bool, bool isDown) override
    {
        auto bounds = getLocalBounds().toFloat().reduced (1.5f);
        const auto colour = TrackColours::get (track);
        RailCard::paint (g, bounds, empty ? juce::Colours::grey : colour, lit, isDown);

        const float labelH = juce::jmin (16.0f, bounds.getHeight() * 0.32f);
        auto labelArea = bounds.removeFromBottom (labelH + 2.0f);

        g.setColour (empty ? juce::Colours::grey.withAlpha (0.6f) : (lit ? colour.brighter (0.4f) : colour));
        g.setFont (juce::FontOptions (juce::jmin (24.0f, bounds.getHeight() * 0.62f), juce::Font::bold));
        g.drawText (juce::String (track + 1), bounds, juce::Justification::centred);

        g.setColour (MidiMorfColors::textLight.withAlpha (empty ? 0.45f : 1.0f));
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawFittedText (caption, labelArea.toNearestInt(), juce::Justification::centredTop, 1, 0.8f);
    }

private:
    const int track;
    juce::String caption;
    bool lit = false, empty = false;
};

//==============================================================================
// Home (house glyph) and Play/Stop on the rail.
class RailIconButton : public juce::Button
{
public:
    enum class Kind { Home, PlayStop };

    explicit RailIconButton (Kind k) : juce::Button (k == Kind::Home ? "Home" : "PlayStop"), kind (k) {}

    // Home: lit on the Home screen. Play/Stop: lit while playing (shows stop).
    void setLit (bool shouldBeLit)
    {
        if (lit != shouldBeLit) { lit = shouldBeLit; repaint(); }
    }

protected:
    void paintButton (juce::Graphics& g, bool, bool isDown) override
    {
        auto bounds = getLocalBounds().toFloat().reduced (1.5f);
        const auto edge = kind == Kind::Home ? MidiMorfColors::textLight : MidiMorfColors::statusGreen;
        RailCard::paint (g, bounds, edge, lit, isDown);

        auto glyph = bounds.withSizeKeepingCentre (bounds.getHeight() * 0.5f, bounds.getHeight() * 0.5f);
        juce::Path p;

        if (kind == Kind::Home)
        {
            const float x = glyph.getX(), y = glyph.getY(), w = glyph.getWidth(), h = glyph.getHeight();
            p.startNewSubPath (x, y + h * 0.45f);
            p.lineTo (x + w * 0.5f, y);
            p.lineTo (x + w, y + h * 0.45f);
            p.addRectangle (x + w * 0.18f, y + h * 0.45f, w * 0.64f, h * 0.55f);
            g.setColour (MidiMorfColors::textLight);
            g.strokePath (p, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            return;
        }

        g.setColour (lit ? MidiMorfColors::textLight : MidiMorfColors::statusGreen);
        if (lit)
            g.fillRoundedRectangle (glyph.reduced (glyph.getWidth() * 0.12f), 2.0f);
        else
        {
            p.addTriangle (glyph.getX() + glyph.getWidth() * 0.18f, glyph.getY(),
                           glyph.getX() + glyph.getWidth() * 0.18f, glyph.getBottom(),
                           glyph.getRight(), glyph.getCentreY());
            g.fillPath (p);
        }
    }

private:
    const Kind kind;
    bool lit = false;
};

//==============================================================================
// The strip above every page: which track this screen belongs to and what's
// playing it — "T2 · Step-Seq → Bass", "T2 · Bass sound", or just a page name.
class TrackHeader : public juce::Component
{
public:
    void set (int trackIndex, const juce::String& text)
    {
        if (trackIndex == track && text == label)
            return;
        track = trackIndex; label = text;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        const auto colour = track >= 0 ? TrackColours::get (track) : MidiMorfColors::textLight.withAlpha (0.6f);

        g.setColour (juce::Colour (0xff141518).withAlpha (0.92f));
        g.fillRect (r);
        g.setColour (colour);
        g.fillRect (r.removeFromLeft (4.0f));

        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText (label, r.reduced (8.0f, 0.0f), juce::Justification::centredLeft);
    }

private:
    int track = -1;
    juce::String label;
};
