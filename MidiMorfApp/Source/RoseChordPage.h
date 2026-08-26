#pragma once

#include <JuceHeader.h>
#include "EventRouter.h"
#include "MidiMorfColors.h"
#include "PianoKeySelector.h"
#include "RoseChordHandler.h"
#include "Glassmorphism.h"

// Deep rose / mauve — matches the page name (Rose-Chord) and lets the
// multi-coloured chord-quality + modifier buttons pop against a warm dark
// substrate.
static const PagePalette roseChordPagePalette {
    juce::Colour (0xff260D26),       // substrate top — deep rose-mauve
    juce::Colour (0xff100410),       // substrate bottom — near-black
    juce::Colour (0xCC4A1A4A),       // acrylic top — translucent rose
    juce::Colour (0xD8260D26),       // acrylic bottom — deeper translucent
    juce::Colour (0xCC000000)        // edge — translucent black
};

// Per-button substrate colours. The MultiColorGlassButtonLookAndFeel reads
// each button's juce::TextButton::buttonColourId and derives its glass
// gradients from that, so each button glows in its own hue.
static const juce::Colour roseChordTypeColors[4] = {
    juce::Colour (0xff3A5C68),       // DIM — cool slate teal
    juce::Colour (0xff4A4A82),       // MIN — muted indigo
    juce::Colour (0xff9C7028),       // MAJ — warm gold
    juce::Colour (0xff8C4A62)        // SUS — dusty pink
};

static const juce::Colour roseChordModColors[4] = {
    juce::Colour (0xff6A8C30),       // 6  — lime green
    juce::Colour (0xff8C3A30),       // m7 — rust red
    juce::Colour (0xff8C5238),       // M7 — rose gold
    juce::Colour (0xff3A5C8C)        // 9  — ocean blue
};

static const juce::Colour roseChordOctaveColor { 0xff4A2848 }; // dark mauve

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
// Button background that mirrors the page's 5-layer glassmorphism purple:
// deep substrate / translucent acrylic / top sheen / bottom shadow / edge glow.
// When pressed or toggled, the substrate + acrylic swap to a warm orange so a
// held momentary button reads instantly against the cool purple panel.
class GlassPurpleButtonLookAndFeel : public juce::LookAndFeel_V4
{
public:
    void drawButtonBackground (juce::Graphics& g, juce::Button& b,
                               const juce::Colour& /*bg*/,
                               bool shouldDrawButtonAsHighlighted,
                               bool shouldDrawButtonAsDown) override
    {
        auto bounds = b.getLocalBounds().toFloat();
        const float radius = 6.0f;
        const bool active = shouldDrawButtonAsDown || b.getToggleState();

        {
            juce::Graphics::ScopedSaveState save (g);
            juce::Path clip;
            clip.addRoundedRectangle (bounds, radius);
            g.reduceClipRegion (clip);

            // 1. Substrate — page-matching when idle, accent-darkened when held
            juce::ColourGradient base = active
                ? juce::ColourGradient (MidiMorfColors::accent.darker (0.4f), 0.0f, 0.0f,
                                         MidiMorfColors::accent.darker (0.7f), 0.0f, bounds.getHeight(), false)
                : juce::ColourGradient (MidiMorfColors::panelSubstrateTop,    0.0f, 0.0f,
                                         MidiMorfColors::panelSubstrateBottom, 0.0f, bounds.getHeight(), false);
            g.setGradientFill (base);
            g.fillAll();

            // 2. Translucent tinted acrylic — same logic
            juce::ColourGradient plastic = active
                ? juce::ColourGradient (MidiMorfColors::accent.withAlpha (0.8f),                 0.0f, 0.0f,
                                         MidiMorfColors::accent.darker (0.2f).withAlpha (0.85f), 0.0f, bounds.getHeight(), false)
                : juce::ColourGradient (MidiMorfColors::panelAcrylicTop,    0.0f, 0.0f,
                                         MidiMorfColors::panelAcrylicBottom, 0.0f, bounds.getHeight(), false);
            g.setGradientFill (plastic);
            g.fillAll();

            // 3. Top sheen
            auto sheen = bounds.withHeight (bounds.getHeight() * 0.45f);
            juce::ColourGradient sg (juce::Colour (0x66FFFFFF), sheen.getX(), sheen.getY(),
                                      juce::Colour (0x00FFFFFF), sheen.getX(), sheen.getBottom(), false);
            g.setGradientFill (sg);
            g.fillRect (sheen);

            // 4. Bottom shadow
            auto shadow = bounds.withTrimmedTop (bounds.getHeight() * 0.55f);
            juce::ColourGradient sh (juce::Colour (0x00000000), shadow.getX(), shadow.getY(),
                                      juce::Colour (0x55000000), shadow.getX(), shadow.getBottom(), false);
            g.setGradientFill (sh);
            g.fillRect (shadow);
        }

        // 5. Edge glow — brighter when hovered/active so a held button has a
        //    luminous rim in the accent colour.
        g.setColour (active                          ? MidiMorfColors::accent.brighter (0.2f).withAlpha (0.85f)
                   : shouldDrawButtonAsHighlighted   ? MidiMorfColors::panelEdgeGlow.brighter (0.3f)
                                                    : MidiMorfColors::panelEdgeGlow);
        g.drawRoundedRectangle (bounds.reduced (1.0f), radius, 1.5f);
    }
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
            b->setColour (juce::TextButton::buttonColourId,   roseChordTypeColors[i]);
            b->setColour (juce::TextButton::textColourOnId,   MidiMorfColors::textLight);
            b->setColour (juce::TextButton::textColourOffId,  MidiMorfColors::textLight);
            b->setLookAndFeel (&glassLnf);
            b->onPress   = [this, i] { currentTypes |=  (uint8_t) (1 << i); sendConfig(); };
            b->onRelease = [this, i] { currentTypes &= (uint8_t) ~(1 << i); sendConfig(); };
            typeButtons.add (b);
            addAndMakeVisible (b);
        }

        // Modifier buttons — momentary, stackable. Hold while playing a key.
        for (int i = 0; i < 4; ++i)
        {
            auto* b = new MomentaryButton (modifierNames[i]);
            b->setColour (juce::TextButton::buttonColourId,   roseChordModColors[i]);
            b->setColour (juce::TextButton::textColourOnId,   MidiMorfColors::textLight);
            b->setColour (juce::TextButton::textColourOffId,  MidiMorfColors::textLight);
            b->setLookAndFeel (&glassLnf);
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
            router.sendRoseChordNoteOn ((uint8_t) n, currentTypes, currentMods, voicingOffset, octaveOffset);
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

        // Octave −/+ pair with a readout. ±2 octaves.
        auto styleOctButton = [this] (juce::TextButton& b)
        {
            b.setColour (juce::TextButton::buttonColourId,  roseChordOctaveColor);
            b.setColour (juce::TextButton::textColourOffId, MidiMorfColors::textLight);
            b.setColour (juce::TextButton::textColourOnId,  MidiMorfColors::textLight);
            b.setLookAndFeel (&glassLnf);
        };
        octaveDownBtn.setButtonText ("-");
        styleOctButton (octaveDownBtn);
        octaveDownBtn.onClick = [this] { setOctave (octaveOffset - 1); };
        addAndMakeVisible (octaveDownBtn);

        octaveUpBtn.setButtonText ("+");
        styleOctButton (octaveUpBtn);
        octaveUpBtn.onClick = [this] { setOctave (octaveOffset + 1); };
        addAndMakeVisible (octaveUpBtn);

        octaveLabel.setJustificationType (juce::Justification::centred);
        octaveLabel.setColour (juce::Label::textColourId, MidiMorfColors::textLight);
        octaveLabel.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        addAndMakeVisible (octaveLabel);
        refreshOctaveLabel();
    }

    ~RoseChordLandscapeContent() override
    {
        // Clear LookAndFeel before glassLnf is destroyed. Belt and suspenders:
        // glassLnf is declared first so it already outlives these buttons,
        // but JUCE recommends nulling explicitly to silence the LnF assertion.
        for (auto* b : typeButtons)     b->setLookAndFeel (nullptr);
        for (auto* b : modifierButtons) b->setLookAndFeel (nullptr);
        octaveDownBtn.setLookAndFeel (nullptr);
        octaveUpBtn.setLookAndFeel   (nullptr);
    }

    void paint (juce::Graphics& g) override
    {
        // Transparent — let the PageContainer's dark panel background show
        // through so this page matches the rest of the controllers.
        juce::ignoreUnused (g);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (8);

        // Favour the keyboard: ~38% width for chord buttons + voicing + octave
        // on the left; remaining ~62% for the keyboard on the right.
        int leftW = (int) (area.getWidth() * 0.38f);
        auto leftCol = area.removeFromLeft (leftW);
        area.removeFromLeft (8);
        auto rightCol = area;

        keyboard.setBounds (rightCol);

        // Bottom: voicing scrub (left) + octave -/label/+ (right)
        auto voicingArea = leftCol.removeFromBottom (44);
        const int octBarW = 96;
        auto octArea = voicingArea.removeFromRight (octBarW);
        voicingArea.removeFromRight (6);
        voicing.setBounds (voicingArea);

        // Octave -, label, + (each ~32 wide)
        int third = octArea.getWidth() / 3;
        octaveDownBtn.setBounds (octArea.removeFromLeft (third).reduced (1));
        octaveUpBtn.setBounds   (octArea.removeFromRight (third).reduced (1));
        octaveLabel.setBounds   (octArea);

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
        // Voicing + octave persist. Type / modifier are momentary — at launch
        // no fingers are on the buttons, so they always start cleared.
        props.setValue ("roseChord_voicing", (int) voicingOffset);
        props.setValue ("roseChord_octave",  (int) octaveOffset);
    }

    void restoreState (juce::PropertiesFile& props)
    {
        voicingOffset = (int8_t) juce::jlimit (-7, 7,
            props.getIntValue ("roseChord_voicing", (int) voicingOffset));
        octaveOffset  = (int8_t) juce::jlimit (-2, 2,
            props.getIntValue ("roseChord_octave", (int) octaveOffset));
        currentTypes = 0;
        currentMods  = 0;
        voicing.setValue (voicingOffset);
        refreshOctaveLabel();
        sendConfig();
    }

private:
    void sendConfig()
    {
        router.sendRoseChordConfig (currentTypes, currentMods, voicingOffset, octaveOffset);
    }

    void setOctave (int newValue)
    {
        int clamped = juce::jlimit (-2, 2, newValue);
        if (clamped == octaveOffset) return;
        octaveOffset = (int8_t) clamped;
        refreshOctaveLabel();
        sendConfig();
    }

    void refreshOctaveLabel()
    {
        juce::String text;
        if (octaveOffset == 0)      text = "0";
        else if (octaveOffset > 0)  text = "+" + juce::String ((int) octaveOffset);
        else                         text = juce::String ((int) octaveOffset);
        octaveLabel.setText ("OCT " + text, juce::dontSendNotification);
    }

    EventRouter& router;
    MultiColorGlassButtonLookAndFeel glassLnf { MidiMorfColors::accent, 6.0f }; // must outlive the buttons
    juce::OwnedArray<MomentaryButton> typeButtons;
    juce::OwnedArray<MomentaryButton> modifierButtons;
    PianoKeySelector keyboard;
    VoicingScrubBar voicing;

    juce::TextButton octaveDownBtn, octaveUpBtn;
    juce::Label octaveLabel;

    uint8_t currentTypes = 0;  // momentary bitmask of held type buttons
    uint8_t currentMods  = 0;  // momentary bitmask of held modifier buttons
    int8_t  voicingOffset = 0;
    int8_t  octaveOffset  = 0; // -2..+2
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

    void paint (juce::Graphics& g) override
    {
        Glassmorphism::paintPanel (g, getLocalBounds().toFloat(), roseChordPagePalette);
    }

    void saveState (juce::PropertiesFile& props)    { content.saveState (props); }
    void restoreState (juce::PropertiesFile& props) { content.restoreState (props); }

    void resized() override
    {
        content.setBounds (getLocalBounds().reduced (12));
    }

private:
    RoseChordLandscapeContent content;
};
