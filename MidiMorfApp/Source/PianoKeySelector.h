#pragma once

#include <JuceHeader.h>
#include "MidiMorfColors.h"

//==============================================================================
// One-octave piano keyboard. Two modes:
//   • Selection mode (default) — tap a key to select a root note. Highlights
//     selectedNote persistently. Used by Morf-Pad / Dual M-Pad / Morf-Chord.
//   • Momentary mode — press = noteOn, release = noteOff, drag = legato.
//     Highlights the currently-pressed key only. Used by Rose-Chord.
// Callbacks (any may be left null):
//   onNoteSelected(note) — fired on every key press in either mode
//   onNoteDown(note)     — fired on press (mostly relevant in momentary mode)
//   onNoteUp(note)       — fired on release / when finger leaves the key
class PianoKeySelector : public juce::Component
{
public:
    std::function<void (int)> onNoteSelected;
    std::function<void (int)> onNoteDown;
    std::function<void (int)> onNoteUp;

    void setMomentary (bool m) { momentary = m; }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        float whiteKeyW = bounds.getWidth() / 7.0f;
        float h = bounds.getHeight();

        int highlight = momentary ? activeNote : selectedNote;

        // White keys
        for (int i = 0; i < 7; ++i)
        {
            auto keyRect = juce::Rectangle<float> (bounds.getX() + i * whiteKeyW, bounds.getY(), whiteKeyW, h);
            int note = whiteNotes[i];

            g.setColour (note == highlight ? MidiMorfColors::accent : juce::Colour (0xffEEEEEE));
            g.fillRoundedRectangle (keyRect.reduced (0.5f), 3.0f);
            g.setColour (juce::Colour (0xff555555));
            g.drawRoundedRectangle (keyRect.reduced (0.5f), 3.0f, 1.0f);
        }

        // Black keys
        float blackW = whiteKeyW * 0.6f;
        float blackH = h * 0.58f;
        for (int i = 0; i < 5; ++i)
        {
            float xPos = bounds.getX() + blackKeyOffsets[i] * whiteKeyW - blackW * 0.5f;
            auto keyRect = juce::Rectangle<float> (xPos, bounds.getY(), blackW, blackH);
            int note = blackNotes[i];

            g.setColour (note == highlight ? MidiMorfColors::accent : juce::Colour (0xff1A1A1A));
            g.fillRoundedRectangle (keyRect, 2.0f);

            if (note == highlight)
            {
                g.setColour (MidiMorfColors::accent.darker (0.3f));
                g.drawRoundedRectangle (keyRect, 2.0f, 1.0f);
            }
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        int note = hitTest (e.position);
        if (note < 0)
            return;

        selectedNote = note;
        activeNote = note;
        repaint();

        if (onNoteDown)     onNoteDown (note);
        if (onNoteSelected) onNoteSelected (note);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! momentary)
            return;

        int note = hitTest (e.position);
        if (note < 0 || note == activeNote)
            return;

        int previous = activeNote;
        activeNote = note;
        selectedNote = note;
        repaint();

        if (onNoteUp)       onNoteUp (previous);
        if (onNoteDown)     onNoteDown (note);
        if (onNoteSelected) onNoteSelected (note);
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (! momentary)
            return;

        if (activeNote >= 0)
        {
            int released = activeNote;
            activeNote = -1;
            repaint();
            if (onNoteUp) onNoteUp (released);
        }
    }

    int getSelectedNote() const { return selectedNote; }

    void setSelectedNote (int note)
    {
        selectedNote = note;
        repaint();
    }

private:
    int selectedNote = 0;
    int activeNote = -1;
    bool momentary = false;

    static constexpr int whiteNotes[7] = { 0, 2, 4, 5, 7, 9, 11 };
    static constexpr int blackNotes[5] = { 1, 3, 6, 8, 10 };
    static constexpr float blackKeyOffsets[5] = { 1.0f, 2.0f, 4.0f, 5.0f, 6.0f };

    int hitTest (juce::Point<float> pos)
    {
        auto bounds = getLocalBounds().toFloat();
        float whiteKeyW = bounds.getWidth() / 7.0f;
        float blackW = whiteKeyW * 0.6f;
        float blackH = bounds.getHeight() * 0.58f;

        for (int i = 0; i < 5; ++i)
        {
            float xPos = bounds.getX() + blackKeyOffsets[i] * whiteKeyW - blackW * 0.5f;
            auto keyRect = juce::Rectangle<float> (xPos, bounds.getY(), blackW, blackH);
            if (keyRect.contains (pos))
                return blackNotes[i];
        }

        for (int i = 0; i < 7; ++i)
        {
            auto keyRect = juce::Rectangle<float> (bounds.getX() + i * whiteKeyW, bounds.getY(), whiteKeyW, bounds.getHeight());
            if (keyRect.contains (pos))
                return whiteNotes[i];
        }

        return -1;
    }
};
