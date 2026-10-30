#pragma once

#include <JuceHeader.h>
#include "EventRouter.h"
#include "MidiMorfColors.h"
#include "PianoKeySelector.h"
#include "Glassmorphism.h"
#include <vector>
#include <algorithm>

// Deep emerald — organic / chord-progression vibe; pairs with Strum-Chord's
// indigo so the two chord-focused pages read as siblings but stay distinct.
static const PagePalette morfChordPagePalette {
    juce::Colour (0xff0F2418),       // substrate top — deep emerald
    juce::Colour (0xff050F08),       // substrate bottom — near-black with green undertone
    juce::Colour (0xCC1F4530),       // acrylic top — translucent emerald
    juce::Colour (0xD80F2418),       // acrylic bottom — deeper translucent
    juce::Colour (0xCC000000)        // edge — translucent black
};

//==============================================================================
// Vertical strum plate for landscape Morf-Chord - drag up/down to strum
class VerticalStrumPlate : public juce::Component
{
public:
    struct Listener
    {
        virtual ~Listener() = default;
        virtual void verticalStrumGesture (float position, float velocity) = 0;
    };

    void setListener (Listener* l) { listener = l; }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        const float radius = 8.0f;

        // 3D recessed-chamber glass treatment, matching the horizontal
        // StrumPlate on Strum-Chord.
        {
            juce::Graphics::ScopedSaveState save (g);
            juce::Path clip;
            clip.addRoundedRectangle (bounds, radius);
            g.reduceClipRegion (clip);

            g.setGradientFill (juce::ColourGradient (
                juce::Colour (0xff0A1810), bounds.getX(), bounds.getY(),
                juce::Colour (0xff020805), bounds.getX(), bounds.getBottom(), false));
            g.fillAll();

            auto sheen = bounds.withHeight (bounds.getHeight() * 0.35f);
            g.setGradientFill (juce::ColourGradient (
                juce::Colour (0x44FFFFFF), sheen.getX(), sheen.getY(),
                juce::Colour (0x00FFFFFF), sheen.getX(), sheen.getBottom(), false));
            g.fillRect (sheen);

            auto shadow = bounds.withTrimmedTop (bounds.getHeight() * 0.6f);
            g.setGradientFill (juce::ColourGradient (
                juce::Colour (0x00000000), shadow.getX(), shadow.getY(),
                juce::Colour (0x66000000), shadow.getX(), shadow.getBottom(), false));
            g.fillRect (shadow);
        }

        g.setColour (juce::Colour (0xAA000000));
        g.drawRoundedRectangle (bounds.reduced (1.0f), radius, 1.5f);

        // Horizontal string lines — slightly brighter for visibility on dark glass
        int numLines = 18;
        float lineSpacing = bounds.getHeight() / (numLines + 1);
        g.setColour (juce::Colour (0x44FFFFFF));
        for (int i = 1; i <= numLines; ++i)
        {
            float y = bounds.getY() + i * lineSpacing;
            g.drawHorizontalLine ((int) y, bounds.getX() + 6.0f, bounds.getRight() - 6.0f);
        }

        if (touching)
        {
            float py = bounds.getY() + lastPosition * bounds.getHeight();
            g.setColour (MidiMorfColors::accent.withAlpha (0.7f));
            g.fillRect (bounds.getX(), py - 2.0f, bounds.getWidth(), 4.0f);
        }

        g.setColour (MidiMorfColors::textLight.withAlpha (0.3f));
        g.setFont (juce::FontOptions (11.0f));
        g.drawText ("STRUM", bounds.reduced (6.0f), juce::Justification::topLeft);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        touching = true;
        lastY = e.position.y;
        lastTime = juce::Time::getMillisecondCounterHiRes();
        updateStrum (e.position);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (touching)
            updateStrum (e.position);
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        touching = false;
        repaint();
        if (listener != nullptr)
            listener->verticalStrumGesture (lastPosition, 0.0f);
    }

private:
    void updateStrum (juce::Point<float> pos)
    {
        auto bounds = getLocalBounds().toFloat();
        float position = juce::jlimit (0.0f, 1.0f, (pos.y - bounds.getY()) / bounds.getHeight());

        double now = juce::Time::getMillisecondCounterHiRes();
        double dt = now - lastTime;
        float dy = std::abs (pos.y - lastY);
        float speed = (dt > 0.0) ? (float) (dy / dt) : 0.0f;
        float velocity = juce::jlimit (0.0f, 1.0f, speed * 2.0f);

        lastY = pos.y;
        lastTime = now;
        lastPosition = position;

        repaint();

        if (listener != nullptr)
            listener->verticalStrumGesture (position, velocity);
    }

    Listener* listener = nullptr;
    bool touching = false;
    float lastY = 0.0f;
    double lastTime = 0.0;
    float lastPosition = 0.5f;
};

//==============================================================================
// Inner landscape content - 7 chord XY pads + key selector + strum plate
class MorfChordLandscapeContent : public juce::Component,
                                 public VerticalStrumPlate::Listener
{
public:
    MorfChordLandscapeContent (EventRouter& client)
        : networkClient (client)
    {
        // Key selector: piano keys for root note
        pianoKeys.onNoteSelected = [this] (int note) { rootNote = (uint8_t) note; sendKeySelect(); };
        addAndMakeVisible (pianoKeys);

        // Major/Minor toggle — glass treatment, light text in both states.
        modeButton.setButtonText ("Major");
        modeButton.setClickingTogglesState (true);
        modeButton.setColour (juce::TextButton::textColourOnId,  MidiMorfColors::textLight);
        modeButton.setColour (juce::TextButton::textColourOffId, MidiMorfColors::textLight);
        modeButton.setLookAndFeel (&glassLnf);
        modeButton.onClick = [this] {
            scaleMode = modeButton.getToggleState() ? 1 : 0;
            modeButton.setButtonText (scaleMode == 0 ? "Major" : "Minor");
            updateDegreeLabels();
            sendKeySelect();
        };
        addAndMakeVisible (modeButton);

        // Load LED font
        ledTypeface = juce::Typeface::createSystemTypefaceFor (
            BinaryData::DSEG14ModernMiniRegular_ttf,
            BinaryData::DSEG14ModernMiniRegular_ttfSize);

        updateDegreeLabels();

        // Strum plate
        strumPlate.setListener (this);
        addAndMakeVisible (strumPlate);
    }

    ~MorfChordLandscapeContent() override
    {
        modeButton.setLookAndFeel (nullptr);
    }

    void paint (juce::Graphics& g) override
    {
        // Draw 7 chord XY pads
        for (int i = 0; i < 7; ++i)
        {
            auto padRect = chordPadBounds[i].toFloat();

            // ROYGBIV rainbow: explicit hues for each degree
            static const float roygbiv[7] = {
                0.0f,    // Red
                0.083f,  // Orange
                0.155f,  // Yellow
                0.33f,   // Green
                0.58f,   // Blue
                0.67f,   // Indigo
                0.75f    // Violet
            };
            float hue = roygbiv[i];
            float sat = 0.65f;
            float baseLight = 0.45f;

            // When held, shift lightness based on voicing zone (XY position)
            // Center = neutral, corners shift lighter/darker
            float light = baseLight;
            if (i == heldDegree)
            {
                // Map touchY: bottom (0) = darker, top (1) = lighter
                light = 0.25f + touchY * 0.45f;  // range 0.25 to 0.70
            }

            auto padColour = juce::Colour::fromHSL (hue, sat, light, 1.0f);

            // Glassmorphism chord pad: substrate gradient in the hue, top
            // sheen, bottom shadow. When held, substrate brightens so the
            // active chord glows.
            const float radius = 6.0f;
            const bool isActive = (i == heldDegree);
            {
                juce::Graphics::ScopedSaveState save (g);
                juce::Path clip;
                clip.addRoundedRectangle (padRect, radius);
                g.reduceClipRegion (clip);

                const auto subTop = isActive ? padColour.brighter (0.55f) : padColour;
                const auto subBot = isActive ? padColour                   : padColour.darker (0.35f);

                g.setGradientFill (juce::ColourGradient (
                    subTop, padRect.getX(), padRect.getY(),
                    subBot, padRect.getX(), padRect.getBottom(), false));
                g.fillAll();

                auto sheen = padRect.withHeight (padRect.getHeight() * 0.42f);
                g.setGradientFill (juce::ColourGradient (
                    juce::Colour (0x55FFFFFF), sheen.getX(), sheen.getY(),
                    juce::Colour (0x00FFFFFF), sheen.getX(), sheen.getBottom(), false));
                g.fillRect (sheen);

                auto shadow = padRect.withTrimmedTop (padRect.getHeight() * 0.6f);
                g.setGradientFill (juce::ColourGradient (
                    juce::Colour (0x00000000), shadow.getX(), shadow.getY(),
                    juce::Colour (0x55000000), shadow.getX(), shadow.getBottom(), false));
                g.fillRect (shadow);
            }

            // Border — brighter rim when active
            g.setColour (isActive ? padColour.brighter (0.7f) : padColour.darker (0.45f));
            g.drawRoundedRectangle (padRect.reduced (1.0f), radius, isActive ? 2.0f : 1.5f);

            // Zone grid lines (faint)
            g.setColour (juce::Colours::white.withAlpha (0.15f));
            float thirdW = padRect.getWidth() / 3.0f;
            float thirdH = padRect.getHeight() / 3.0f;
            g.drawVerticalLine ((int) (padRect.getX() + thirdW), padRect.getY() + 4, padRect.getBottom() - 4);
            g.drawVerticalLine ((int) (padRect.getX() + thirdW * 2), padRect.getY() + 4, padRect.getBottom() - 4);
            g.drawHorizontalLine ((int) (padRect.getY() + thirdH), padRect.getX() + 4, padRect.getRight() - 4);
            g.drawHorizontalLine ((int) (padRect.getY() + thirdH * 2), padRect.getX() + 4, padRect.getRight() - 4);

            // Degree label
            g.setColour (light > 0.5f ? juce::Colour (0xFF1A1A2E) : juce::Colours::white);
            g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
            g.drawText (degreeLabels[i], padRect, juce::Justification::centred);

            // Touch dot
            if (i == heldDegree)
            {
                float px = padRect.getX() + touchX * padRect.getWidth();
                float py = padRect.getY() + (1.0f - touchY) * padRect.getHeight();
                float dotSize = 16.0f;
                g.setColour (juce::Colours::white.withAlpha (0.8f));
                g.fillEllipse (px - dotSize * 0.5f, py - dotSize * 0.5f, dotSize, dotSize);
            }
        }

        // LED voicing display - orange LCD background with DSEG7 font, rotated CCW
        {
            auto ledRect = ledDisplayBounds.toFloat();

            // Orange LCD background (accent color)
            g.setColour (MidiMorfColors::accent);
            g.fillRoundedRectangle (ledRect, 4.0f);

            // Black border
            g.setColour (juce::Colour (0xFF000000));
            g.drawRoundedRectangle (ledRect.reduced (1.0f), 4.0f, 3.5f);

            // Inner shadow (dark gradient around edges)
            float shadowSize = 8.0f;
            auto inner = ledRect.reduced (2.0f);
            // Top shadow
            g.setGradientFill (juce::ColourGradient (
                juce::Colour (0xAA000000), inner.getX(), inner.getY(),
                juce::Colour (0x00000000), inner.getX(), inner.getY() + shadowSize, false));
            g.fillRect (inner.getX(), inner.getY(), inner.getWidth(), shadowSize);
            // Bottom shadow
            g.setGradientFill (juce::ColourGradient (
                juce::Colour (0xAA000000), inner.getX(), inner.getBottom(),
                juce::Colour (0x00000000), inner.getX(), inner.getBottom() - shadowSize, false));
            g.fillRect (inner.getX(), inner.getBottom() - shadowSize, inner.getWidth(), shadowSize);
            // Left shadow
            g.setGradientFill (juce::ColourGradient (
                juce::Colour (0xAA000000), inner.getX(), inner.getY(),
                juce::Colour (0x00000000), inner.getX() + shadowSize, inner.getY(), false));
            g.fillRect (inner.getX(), inner.getY(), shadowSize, inner.getHeight());
            // Right shadow
            g.setGradientFill (juce::ColourGradient (
                juce::Colour (0xAA000000), inner.getRight(), inner.getY(),
                juce::Colour (0x00000000), inner.getRight() - shadowSize, inner.getY(), false));
            g.fillRect (inner.getRight() - shadowSize, inner.getY(), shadowSize, inner.getHeight());

            // Text rotated 90° CCW for landscape reading
            g.saveState();
            float cx = ledRect.getCentreX();
            float cy = ledRect.getCentreY();
            g.addTransform (juce::AffineTransform::rotation (
                -juce::MathConstants<float>::halfPi, cx, cy));

            g.setColour (juce::Colour (0xFF2A1A00));  // dark brown text
            g.setFont (juce::Font (ledTypeface).withHeight (32.0f));

            float textW = ledRect.getHeight();
            float textH = ledRect.getWidth();
            g.drawText (voicingText, cx - textW * 0.5f, cy - textH * 0.5f,
                        textW, textH, juce::Justification::centred);

            g.restoreState();
        }
    }

    void resized() override
    {
        auto area = getLocalBounds();

        // Left: keyboard column with Major/Minor button
        int keyColW = 60;
        auto leftCol = area.removeFromLeft (keyColW);
        area.removeFromLeft (4);

        // Major/Minor button at end of keyboard strip
        int btnH = 28;
        auto btnArea = leftCol.removeFromTop (btnH);
        modeButton.setBounds (btnArea);

        // Piano keys fill the rest of the column
        pianoKeys.setSize (leftCol.getHeight(), keyColW);
        pianoKeys.setTransform (
            juce::AffineTransform::rotation (
                -juce::MathConstants<float>::halfPi, 0.0f, 0.0f)
            .translated ((float) leftCol.getX(), (float) leftCol.getBottom()));

        // Right: vertical strum plate
        int strumW = 65;
        auto rightCol = area.removeFromRight (strumW);
        area.removeFromRight (4);
        strumPlate.setBounds (rightCol);

        // Middle: 4-col x 2-row chord grid
        int gap = 4;
        int cols = 4;
        int rows = 2;
        int cellW = (area.getWidth() - gap * (cols - 1)) / cols;
        int cellH = (area.getHeight() - gap) / rows;

        static const int topDegrees[3]    = { 4, 5, 6 };       // V, vi, vii
        static const int bottomDegrees[4] = { 0, 1, 2, 3 };    // I, ii, iii, IV

        int topY = area.getY();
        int botY = area.getY() + cellH + gap;

        // Top row: V, vi, vii centered
        int topRowW = cellW * 3 + gap * 2;
        int topRowX = area.getX() + (area.getWidth() - topRowW) / 2;
        for (int c = 0; c < 3; ++c)
            chordPadBounds[topDegrees[c]] = juce::Rectangle<int> (topRowX + c * (cellW + gap), topY, cellW, cellH);

        // LED display: left of V, same height
        int ledW = topRowX - gap - area.getX();
        ledDisplayBounds = juce::Rectangle<int> (area.getX(), topY, ledW, cellH);

        // Bottom row: I, ii, iii, IV
        for (int c = 0; c < 4; ++c)
            chordPadBounds[bottomDegrees[c]] = juce::Rectangle<int> (area.getX() + c * (cellW + gap), botY, cellW, cellH);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        int idx = getChordPadIndex (e.position);
        if (idx < 0)
            return;

        int srcIdx = e.source.getIndex();

        TouchInfo ti;
        ti.sourceIndex = srcIdx;
        ti.degree = idx;
        ti.x = 0.5f;
        ti.y = 0.5f;
        activeTouches.push_back (ti);

        // Release previous chord before selecting new one
        if (activeTouches.size() > 1)
            networkClient.sendMorfChordRelease();

        heldDegree = idx;
        updateTouchXY (idx, e.position);
        networkClient.sendMorfChordSelect ((uint8_t) idx, touchX, touchY);
        updateVoicingLabel();
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        int srcIdx = e.source.getIndex();

        // Only track the active (most recent) finger
        if (activeTouches.empty() || activeTouches.back().sourceIndex != srcIdx)
            return;

        if (heldDegree < 0)
            return;

        int idx = getChordPadIndex (e.position);
        if (idx >= 0 && idx != heldDegree)
        {
            // Slide to new chord - hard snap
            heldDegree = idx;
            activeTouches.back().degree = idx;
            updateTouchXY (idx, e.position);
            networkClient.sendMorfChordSelect ((uint8_t) idx, touchX, touchY);
            updateVoicingLabel();
            repaint();
        }
        else if (idx == heldDegree)
        {
            float oldX = touchX;
            float oldY = touchY;
            updateTouchXY (idx, e.position);

            if (std::abs (touchX - oldX) > 0.01f || std::abs (touchY - oldY) > 0.01f)
            {
                networkClient.sendMorfChordMove ((uint8_t) idx, touchX, touchY);
                updateVoicingLabel();
            }
            repaint();
        }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        int srcIdx = e.source.getIndex();
        bool wasActive = (! activeTouches.empty() && activeTouches.back().sourceIndex == srcIdx);

        activeTouches.erase (
            std::remove_if (activeTouches.begin(), activeTouches.end(),
                [srcIdx] (const TouchInfo& t) { return t.sourceIndex == srcIdx; }),
            activeTouches.end());

        if (wasActive)
        {
            networkClient.sendMorfChordRelease();

            if (! activeTouches.empty())
            {
                // Fall back to previous finger's chord
                auto& prev = activeTouches.back();
                heldDegree = prev.degree;
                touchX = prev.x;
                touchY = prev.y;
                networkClient.sendMorfChordSelect ((uint8_t) prev.degree, touchX, touchY);
                updateVoicingLabel();
            }
            else
            {
                heldDegree = -1;
                voicingText = "";
            }
            repaint();
        }
    }

    // VerticalStrumPlate::Listener
    void verticalStrumGesture (float position, float velocity) override
    {
        networkClient.sendMorfChordStrum (position, velocity);
    }

    void saveState (juce::PropertySet& props)
    {
        props.setValue ("morfChord_rootNote", (int) rootNote);
        props.setValue ("morfChord_scaleMode", (int) scaleMode);
    }

    void restoreState (const juce::PropertySet& props)
    {
        rootNote = (uint8_t) props.getIntValue ("morfChord_rootNote", 0);
        scaleMode = (uint8_t) props.getIntValue ("morfChord_scaleMode", 0);

        pianoKeys.setSelectedNote (rootNote);
        modeButton.setButtonText (scaleMode == 0 ? "Major" : "Minor");
        updateDegreeLabels();
        sendKeySelect();
        repaint();
    }

private:
    int getChordPadIndex (juce::Point<float> pos)
    {
        for (int i = 0; i < 7; ++i)
        {
            if (chordPadBounds[i].toFloat().contains (pos))
                return i;
        }
        return -1;
    }

    struct TouchInfo
    {
        int sourceIndex;
        int degree;
        float x, y;
    };

    void updateTouchXY (int padIdx, juce::Point<float> pos)
    {
        auto bounds = chordPadBounds[padIdx].toFloat();
        touchX = juce::jlimit (0.0f, 1.0f, (pos.x - bounds.getX()) / bounds.getWidth());
        touchY = juce::jlimit (0.0f, 1.0f, 1.0f - (pos.y - bounds.getY()) / bounds.getHeight());

        // Keep touch stack in sync
        if (! activeTouches.empty())
        {
            activeTouches.back().x = touchX;
            activeTouches.back().y = touchY;
            activeTouches.back().degree = padIdx;
        }
    }

    void updateVoicingLabel()
    {
        // Show which zone the XY position maps to
        static const juce::String zoneNames[3][3] = {
            { "aug",   "maj/min", "dim"  },   // top row    (y > 0.67)
            { "dom7",  "triad",   "6/sus2" }, // middle row
            { "9th",   "sus4",    "Maj7" }    // bottom row (y < 0.33)
        };

        int col = (touchX < 0.33f) ? 2 : (touchX > 0.67f) ? 0 : 1;
        int row = (touchY > 0.67f) ? 0 : (touchY < 0.33f) ? 2 : 1;
        voicingText = zoneNames[row][col].toUpperCase();
        repaint (ledDisplayBounds);
    }

    void updateDegreeLabels()
    {
        // Major: I ii iii IV V vi vii
        // Minor: i ii III iv v VI VII
        static const juce::String majorLabels[7] = { "I", "ii", "iii", "IV", "V", "vi", "vii" };
        static const juce::String minorLabels[7] = { "i", "ii", "III", "iv", "v", "VI", "VII" };

        const juce::String* labels = (scaleMode == 0) ? majorLabels : minorLabels;
        for (int i = 0; i < 7; ++i)
            degreeLabels[i] = labels[i];
    }

    void sendKeySelect()
    {
        networkClient.sendMorfChordKeySelect (rootNote, scaleMode);
    }

    EventRouter& networkClient;
    GlassButtonLookAndFeel glassLnf { morfChordPagePalette, MidiMorfColors::accent, 6.0f };
    PianoKeySelector pianoKeys;
    juce::TextButton modeButton;
    VerticalStrumPlate strumPlate;

    juce::Typeface::Ptr ledTypeface;
    juce::String voicingText;
    juce::Rectangle<int> ledDisplayBounds;
    juce::Rectangle<int> chordPadBounds[7];
    juce::String degreeLabels[7];

    uint8_t rootNote = 0;
    uint8_t scaleMode = 0;  // 0=Major, 1=Minor
    int heldDegree = -1;
    float touchX = 0.5f;
    float touchY = 0.5f;
    std::vector<TouchInfo> activeTouches;
};

//==============================================================================
// Outer page - fits in portrait PageContainer, rotates content for landscape use
class MorfChordPage : public juce::Component
{
public:
    MorfChordPage (EventRouter& client)
        : content (client)
    {
        addAndMakeVisible (content);
    }

    void paint (juce::Graphics& g) override
    {
        Glassmorphism::paintPanel (g, getLocalBounds().toFloat(), morfChordPagePalette);
    }

    void saveState (juce::PropertySet& props) { content.saveState (props); }
    void restoreState (const juce::PropertySet& props) { content.restoreState (props); }

    void resized() override
    {
        // App is now native landscape — no rotation needed.
        content.setBounds (getLocalBounds().reduced (12));
    }

private:
    MorfChordLandscapeContent content;
};
