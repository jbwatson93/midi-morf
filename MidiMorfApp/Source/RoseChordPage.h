#pragma once

#include <JuceHeader.h>
#include "EventRouter.h"
#include "MidiMorfColors.h"
#include "PianoKeySelector.h"
#include "RoseChordHandler.h"

//==============================================================================
// Button that fires onPress when held and onRelease when released or when the
// finger drags off. Used for the Orchid-style momentary chord buttons.
class MomentaryButton : public juce::TextButton
{
public:
    using juce::TextButton::TextButton;
    std::function<void()> onPress;
    std::function<void()> onRelease;

protected:
    void buttonStateChanged() override
    {
        juce::TextButton::buttonStateChanged();
        bool nowDown = (getState() == juce::Button::buttonDown);
        if (nowDown != wasDown)
        {
            wasDown = nowDown;
            if (nowDown)
            {
                if (onPress) onPress();
            }
            else
            {
                if (onRelease) onRelease();
            }
        }
    }

private:
    bool wasDown = false;
};

//==============================================================================
// Horizontal scrub bar that drives an integer voicing offset in [-N..+N].
// Drag left = negative (drop tones an octave), drag right = positive (raise).
class VoicingScrubBar : public juce::Component
{
public:
    std::function<void (int)> onValueChanged;

    void setRange (int minVal, int maxVal)
    {
        minValue = minVal;
        maxValue = maxVal;
        currentValue = juce::jlimit (minValue, maxValue, currentValue);
        repaint();
    }

    void setValue (int v, juce::NotificationType notify = juce::dontSendNotification)
    {
        int clamped = juce::jlimit (minValue, maxValue, v);
        if (clamped == currentValue) return;
        currentValue = clamped;
        repaint();
        if (notify == juce::sendNotification && onValueChanged) onValueChanged (currentValue);
    }

    int getValue() const noexcept { return currentValue; }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();

        g.setColour (MidiMorfColors::padBackground);
        g.fillRoundedRectangle (bounds, 6.0f);

        g.setColour (MidiMorfColors::padBorder);
        g.drawRoundedRectangle (bounds.reduced (0.5f), 6.0f, 1.0f);

        // Label
        g.setColour (MidiMorfColors::textLight);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText ("VOICING", bounds.removeFromLeft (70.0f).reduced (8.0f, 0.0f),
                    juce::Justification::centredLeft);

        // Track + thumb
        auto track = bounds.reduced (12.0f, 14.0f);
        g.setColour (MidiMorfColors::padBorder.darker());
        g.fillRoundedRectangle (track.withHeight (4.0f).withCentre (
                                    { track.getCentreX(), track.getCentreY() }),
                                2.0f);

        // Detent marks
        int range = maxValue - minValue;
        for (int i = 0; i <= range; ++i)
        {
            float t = (float) i / (float) range;
            float x = track.getX() + t * track.getWidth();
            bool isCenter = (i == -minValue);
            g.setColour (isCenter ? MidiMorfColors::accent : MidiMorfColors::padBorder);
            float h = isCenter ? 12.0f : 6.0f;
            g.fillRect (juce::Rectangle<float> (x - 1.0f, track.getCentreY() - h * 0.5f, 2.0f, h));
        }

        // Thumb
        float tNow = (float) (currentValue - minValue) / (float) range;
        float thumbX = track.getX() + tNow * track.getWidth();
        g.setColour (MidiMorfColors::accent);
        g.fillEllipse (thumbX - 10.0f, track.getCentreY() - 10.0f, 20.0f, 20.0f);
        g.setColour (juce::Colour (0xff222222));
        g.drawEllipse (thumbX - 10.0f, track.getCentreY() - 10.0f, 20.0f, 20.0f, 1.5f);

        // Numeric readout on the right
        auto readoutArea = juce::Rectangle<float> (getWidth() - 56.0f, 0.0f, 50.0f, (float) getHeight());
        g.setColour (MidiMorfColors::textLight);
        g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        juce::String label = (currentValue > 0 ? "+" : "") + juce::String (currentValue);
        g.drawText (label, readoutArea, juce::Justification::centred);
    }

    void mouseDown (const juce::MouseEvent& e) override   { updateFromX ((float) e.x); }
    void mouseDrag (const juce::MouseEvent& e) override   { updateFromX ((float) e.x); }
    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        setValue (0, juce::sendNotification);  // recenter
    }

private:
    void updateFromX (float x)
    {
        auto bounds = getLocalBounds().toFloat();
        bounds.removeFromLeft (70.0f);
        auto track = bounds.reduced (12.0f, 14.0f);
        float t = juce::jlimit (0.0f, 1.0f, (x - track.getX()) / track.getWidth());
        int v = minValue + (int) std::round (t * (maxValue - minValue));
        setValue (v, juce::sendNotification);
    }

    int minValue = -7;
    int maxValue = 7;
    int currentValue = 0;
};

//==============================================================================
// Landscape content for Rose-Chord: chord-type/modifier buttons on the left,
// keyboard on the right, voicing scrub bar at the bottom.
class RoseChordLandscapeContent : public juce::Component
{
public:
    RoseChordLandscapeContent (EventRouter& r) : router (r)
    {
        const juce::String typeNames[4]     = { "DIM", "MIN", "MAJ", "SUS" };
        const juce::String modifierNames[4] = { "6",   "m7",  "M7",  "9"   };

        // Chord type buttons — momentary; hold to apply that quality to the
        // chord built by the next keypress. Multiple types can be held at once.
        for (int i = 0; i < 4; ++i)
        {
            auto* b = new MomentaryButton (typeNames[i]);
            b->setColour (juce::TextButton::buttonColourId,   MidiMorfColors::buttonFace);
            b->setColour (juce::TextButton::buttonOnColourId, MidiMorfColors::accent);
            b->setColour (juce::TextButton::textColourOnId,   MidiMorfColors::textLight);
            b->setColour (juce::TextButton::textColourOffId,  MidiMorfColors::textDark);
            b->onPress   = [this, i] { currentTypes |=  (uint8_t) (1 << i); sendConfig(); };
            b->onRelease = [this, i] { currentTypes &= (uint8_t) ~(1 << i); sendConfig(); };
            typeButtons.add (b);
            addAndMakeVisible (b);
        }

        // Modifier buttons — momentary, stackable. Hold while playing a key.
        for (int i = 0; i < 4; ++i)
        {
            auto* b = new MomentaryButton (modifierNames[i]);
            b->setColour (juce::TextButton::buttonColourId,   MidiMorfColors::buttonFace);
            b->setColour (juce::TextButton::buttonOnColourId, MidiMorfColors::accent);
            b->setColour (juce::TextButton::textColourOnId,   MidiMorfColors::textLight);
            b->setColour (juce::TextButton::textColourOffId,  MidiMorfColors::textDark);
            b->onPress   = [this, i] { currentMods |=  (uint8_t) (1 << i); sendConfig(); };
            b->onRelease = [this, i] { currentMods &= (uint8_t) ~(1 << i); sendConfig(); };
            modifierButtons.add (b);
            addAndMakeVisible (b);
        }

        // Keyboard in momentary mode — press = note on, release = note off.
        // If no chord buttons are held at press time, the handler plays a
        // single note. If any are held, it builds the chord.
        keyboard.setMomentary (true);
        keyboard.onNoteDown = [this] (int n)
        {
            router.sendRoseChordNoteOn ((uint8_t) n, currentTypes, currentMods, voicingOffset);
        };
        keyboard.onNoteUp = [this] (int /*n*/)
        {
            router.sendRoseChordNoteOff();
        };
        addAndMakeVisible (keyboard);

        // Voicing scrub
        voicing.setRange (-7, 7);
        voicing.setValue (voicingOffset);
        voicing.onValueChanged = [this] (int v)
        {
            voicingOffset = (int8_t) v;
            sendConfig();
        };
        addAndMakeVisible (voicing);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (MidiMorfColors::background);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (8);

        // Favour the keyboard: ~38% of width for chord buttons + voicing on the
        // left, remaining ~62% for the keyboard on the right (full height).
        int leftW = (int) (area.getWidth() * 0.38f);
        auto leftCol = area.removeFromLeft (leftW);
        area.removeFromLeft (8);
        auto rightCol = area;

        // Right col: keyboard fills the full height
        keyboard.setBounds (rightCol);

        // Left col: voicing slider hugs the bottom, chord buttons stack above it
        auto voicingArea = leftCol.removeFromBottom (44);
        voicing.setBounds (voicingArea);
        leftCol.removeFromBottom (6);

        // Two rows of 4 buttons sharing the remaining left-col height
        int rowH = leftCol.getHeight() / 2;
        auto typeRow = leftCol.removeFromTop (rowH - 3);
        leftCol.removeFromTop (6);
        auto modRow  = leftCol.removeFromTop (rowH - 3);

        int colW = typeRow.getWidth() / 4;
        for (int i = 0; i < 4; ++i)
        {
            auto cell = typeRow.removeFromLeft (i == 3 ? typeRow.getWidth() : colW);
            typeButtons[i]->setBounds (cell.reduced (2));
        }

        int modColW = modRow.getWidth() / 4;
        for (int i = 0; i < 4; ++i)
        {
            auto cell = modRow.removeFromLeft (i == 3 ? modRow.getWidth() : modColW);
            modifierButtons[i]->setBounds (cell.reduced (2));
        }
    }

    void saveState (juce::PropertiesFile& props)
    {
        // Only voicing persists. Type / modifier state is momentary — at launch
        // no fingers are on the buttons, so we always start with nothing held.
        props.setValue ("roseChord_voicing", (int) voicingOffset);
    }

    void restoreState (juce::PropertiesFile& props)
    {
        voicingOffset = (int8_t) juce::jlimit (-7, 7,
            props.getIntValue ("roseChord_voicing", (int) voicingOffset));
        currentTypes = 0;
        currentMods  = 0;
        voicing.setValue (voicingOffset);
        sendConfig();
    }

private:
    void sendConfig()
    {
        router.sendRoseChordConfig (currentTypes, currentMods, voicingOffset);
    }

    EventRouter& router;
    juce::OwnedArray<MomentaryButton> typeButtons;
    juce::OwnedArray<MomentaryButton> modifierButtons;
    PianoKeySelector keyboard;
    VoicingScrubBar voicing;

    uint8_t currentTypes = 0;  // momentary bitmask of held type buttons
    uint8_t currentMods  = 0;  // momentary bitmask of held modifier buttons
    int8_t  voicingOffset = 0;
};

//==============================================================================
// Outer page — fits in portrait container, rotates landscape content 90° CW.
class RoseChordPage : public juce::Component
{
public:
    RoseChordPage (EventRouter& router) : content (router)
    {
        addAndMakeVisible (content);
    }

    void saveState (juce::PropertiesFile& props)    { content.saveState (props); }
    void restoreState (juce::PropertiesFile& props) { content.restoreState (props); }

    void resized() override
    {
        int pw = getWidth();
        int ph = getHeight();

        content.setSize (ph, pw);
        content.setTransform (
            juce::AffineTransform::rotation (
                juce::MathConstants<float>::halfPi, 0.0f, 0.0f)
            .translated ((float) pw, 0.0f));
    }

private:
    RoseChordLandscapeContent content;
};
