#pragma once

#include <JuceHeader.h>
#include "MidiMorfColors.h"
#include <vector>
#include <algorithm>

class ChordGrid : public juce::Component
{
public:
    struct Listener
    {
        virtual ~Listener() = default;
        virtual void chordSelected (uint8_t rootNote, uint8_t quality) = 0;
        virtual void chordReleased() = 0;
    };

    ChordGrid() {}

    void setListener (Listener* l) { listener = l; }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds();

        // 4 columns (dim, 7th, Min, Maj) x 12 rows (circle of fifths)
        float cellH = bounds.getHeight() / (float) numChords;
        float cellW = bounds.getWidth() / 4.0f;
        float margin = 3.0f;
        float gridWidth = cellW * 4.0f;
        float offsetX = (bounds.getWidth() - gridWidth) * 0.5f;

        for (int row = 0; row < numChords; ++row)
        {
            for (int col = 0; col < 4; ++col)
            {
                float slotX = bounds.getX() + offsetX + col * cellW;
                float slotY = bounds.getY() + row * cellH;
                auto cellBounds = juce::Rectangle<float> (
                    slotX + margin,
                    slotY + margin,
                    cellW - margin * 2.0f,
                    cellH - margin * 2.0f);

                bool isActive = (row == activeRoot && col == activeQuality && chordHeld);

                // Rainbow hue across rows, darker for dim → lighter for Maj
                // Columns: 0=dim, 1=7th, 2=min, 3=Maj
                float hue = (float) row / (float) numChords;
                float sat = 0.65f;
                float light = 0.30f + (col / 3.0f) * 0.35f;  // dim=0.30, 7=0.42, min=0.53, Maj=0.65

                auto cellColour = juce::Colour::fromHSL (hue, sat, light, 1.0f);

                if (isActive)
                {
                    g.setColour (cellColour.brighter (0.4f));
                    g.fillRoundedRectangle (cellBounds, 4.0f);
                    g.setColour (juce::Colours::white);
                }
                else
                {
                    g.setColour (cellColour);
                    g.fillRoundedRectangle (cellBounds, 4.0f);
                    g.setColour (cellColour.darker (0.3f));
                    g.drawRoundedRectangle (cellBounds, 4.0f, 1.5f);
                    g.setColour (light > 0.5f ? juce::Colour (0xFF1A1A2E) : juce::Colours::white);
                }

                // Draw rotated text (90 degrees for sideways phone)
                juce::String text = chordNames[row] + qualNames[col];
                float cx = cellBounds.getCentreX();
                float cy = cellBounds.getCentreY();

                juce::Graphics::ScopedSaveState saveState (g);
                g.addTransform (juce::AffineTransform::rotation (
                    juce::MathConstants<float>::halfPi, cx, cy));
                g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
                g.drawText (text, cellBounds, juce::Justification::centred, false);
            }
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        int srcIdx = e.source.getIndex();

        // Resolve which chord cell this touch lands on
        auto bounds = getLocalBounds();
        float cellH = bounds.getHeight() / (float) numChords;
        float cellW = bounds.getWidth() / 4.0f;
        float gridWidth = cellW * 4.0f;
        float offsetX = (bounds.getWidth() - gridWidth) * 0.5f;
        float localX = e.position.x - bounds.getX() - offsetX;
        if (localX < 0 || localX > gridWidth)
            return;

        int col = juce::jlimit (0, 3, (int) (localX / cellW));
        int row = juce::jlimit (0, numChords - 1, (int) ((e.position.y - bounds.getY()) / cellH));

        TouchInfo ti;
        ti.sourceIndex = srcIdx;
        ti.row = row;
        ti.col = col;
        activeTouches.push_back (ti);

        // New finger becomes active — release old chord, select new
        if (activeTouches.size() > 1 && listener != nullptr)
            listener->chordReleased();

        activeRoot = row;
        activeQuality = col;
        chordHeld = true;
        repaint();

        if (listener != nullptr)
            listener->chordSelected (fifthsToChromatic[row], colToQuality[col]);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        int srcIdx = e.source.getIndex();

        // Only process drag for the active (most recent) finger
        if (activeTouches.empty() || activeTouches.back().sourceIndex != srcIdx)
            return;

        updateChord (e.position, false);
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
            // Release the chord that was playing
            if (listener != nullptr)
                listener->chordReleased();

            if (! activeTouches.empty())
            {
                // Fall back to previous finger's chord
                auto& prev = activeTouches.back();
                activeRoot = prev.row;
                activeQuality = prev.col;
                chordHeld = true;

                if (listener != nullptr)
                    listener->chordSelected (fifthsToChromatic[prev.row], colToQuality[prev.col]);
            }
            else
            {
                chordHeld = false;
            }
            repaint();
        }
    }

private:
    void updateChord (juce::Point<float> pos, bool isNewTouch)
    {
        auto bounds = getLocalBounds();
        float cellH = bounds.getHeight() / (float) numChords;
        float cellW = bounds.getWidth() / 4.0f;
        float gridWidth = cellW * 4.0f;
        float offsetX = (bounds.getWidth() - gridWidth) * 0.5f;
        float localX = pos.x - bounds.getX() - offsetX;

        // Only respond within the button area
        if (localX < 0 || localX > gridWidth)
            return;

        int col = juce::jlimit (0, 3, (int) (localX / cellW));
        int row = juce::jlimit (0, numChords - 1, (int) ((pos.y - bounds.getY()) / cellH));

        if (isNewTouch || row != activeRoot || col != activeQuality)
        {
            activeRoot = row;
            activeQuality = col;
            chordHeld = true;
            repaint();

            if (listener != nullptr)
                listener->chordSelected (fifthsToChromatic[row], colToQuality[col]);
        }
    }

    static constexpr int numChords = 12;

    // Circle of fifths order: Db Ab Eb Bb F C G D A E B F#
    const uint8_t fifthsToChromatic[12] = { 1, 8, 3, 10, 5, 0, 7, 2, 9, 4, 11, 6 };
    const juce::String chordNames[12] = { "Db", "Ab", "Eb", "Bb", "F", "C", "G", "D", "A", "E", "B", "F#" };

    // Columns: dim, 7th, Min, Maj
    const juce::String qualNames[4] = { "dim", "7", "m", "" };
    const uint8_t colToQuality[4] = { 3, 2, 1, 0 };

    struct TouchInfo
    {
        int sourceIndex;
        int row, col;
    };

    Listener* listener = nullptr;
    int activeRoot = -1;
    int activeQuality = -1;
    bool chordHeld = false;
    std::vector<TouchInfo> activeTouches;
};
