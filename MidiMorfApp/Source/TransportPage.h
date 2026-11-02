#pragma once

#include <JuceHeader.h>
#include "EventRouter.h"
#include "MidiMorfColors.h"
#include "Glassmorphism.h"

// The global transport. The header strip sets the tempo; Play/Stop start and
// stop every sequencer together.
//
// Deep slate — utility hardware feel. Restrained enough that the candy-bright
// Play / Stop buttons stay the visual focus.
static const PagePalette transportPagePalette {
    juce::Colour (0xff1A1E26),       // substrate top — deep slate
    juce::Colour (0xff080A0D),       // substrate bottom — near-black
    juce::Colour (0xCC2A323C),       // acrylic top — translucent slate
    juce::Colour (0xD81A1E26),       // acrylic bottom — deeper translucent
    juce::Colour (0xCC000000)        // edge — translucent black
};

//==============================================================================
// Portrait-aspect content. Hosted by TransportPage which rotates it 90° CCW.
class TransportPortraitContent : public juce::Component
{
public:
    TransportPortraitContent (EventRouter& client)
        : router (client)
    {
    }

    // Lights Play while the phone's clock is running.
    void setLocalRunning (bool shouldShow)
    {
        if (localRunning != shouldShow)
        {
            localRunning = shouldShow;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds().reduced (8);

        // Two big transport buttons
        int bigGap = 10;
        int bigH = (area.getHeight() - bigGap) / 2;

        for (int i = 0; i < 2; ++i)
        {
            auto b = juce::Rectangle<float> (
                (float) area.getX(),
                (float) (area.getY() + i * (bigH + bigGap)),
                (float) area.getWidth(),
                (float) bigH);
            bigBounds[i] = b;

            bool isPressed = (pressedBig == i) || (i == 0 && localRunning);

            // Candy-bright Y2K colors
            juce::Colour baseCol, glowCol;
            switch (i)
            {
                case 0: baseCol = juce::Colour (0xFF00CC55); glowCol = juce::Colour (0xFF55FFaa); break;  // Play - vivid green
                case 1: baseCol = juce::Colour (0xFFFF8800); glowCol = juce::Colour (0xFFFFCC44); break;  // Stop - bright orange
                default: baseCol = MidiMorfColors::buttonFace; glowCol = MidiMorfColors::accent; break;
            }

            auto darkCol = baseCol.darker (0.5f);

            // Outer glow when pressed
            if (isPressed)
            {
                g.setColour (glowCol.withAlpha (0.35f));
                g.fillRoundedRectangle (b.expanded (6.0f), 18.0f);
            }

            // Dark base layer (visible through translucent top)
            g.setColour (darkCol);
            g.fillRoundedRectangle (b, 14.0f);

            // Main translucent body — gradient top-to-bottom
            {
                auto col = isPressed ? glowCol : baseCol;
                juce::ColourGradient grad (
                    col.withAlpha (0.85f), b.getX(), b.getY(),
                    col.withAlpha (0.35f), b.getX(), b.getBottom(), false);
                g.setGradientFill (grad);
                g.fillRoundedRectangle (b, 14.0f);
            }

            // Big glassy highlight — top half, strong white fade
            {
                auto highlight = b.withHeight (b.getHeight() * 0.45f);
                juce::ColourGradient sheen (
                    juce::Colours::white.withAlpha (isPressed ? 0.25f : 0.45f),
                    highlight.getX(), highlight.getY(),
                    juce::Colours::white.withAlpha (0.0f),
                    highlight.getX(), highlight.getBottom(), false);
                g.setGradientFill (sheen);
                g.fillRoundedRectangle (highlight.reduced (3.0f, 0.0f), 12.0f);
            }

            // White border — plastic rim
            g.setColour (juce::Colours::white.withAlpha (isPressed ? 0.35f : 0.2f));
            g.drawRoundedRectangle (b.reduced (1.0f), 14.0f, 1.5f);

            // Bottom inner shadow
            {
                auto shadowStrip = b.withTrimmedTop (b.getHeight() * 0.65f);
                juce::ColourGradient bottomShadow (
                    juce::Colours::black.withAlpha (0.0f), shadowStrip.getX(), shadowStrip.getY(),
                    juce::Colours::black.withAlpha (0.25f), shadowStrip.getX(), shadowStrip.getBottom(), false);
                g.setGradientFill (bottomShadow);
                g.fillRoundedRectangle (shadowStrip.reduced (2.0f, 0.0f), 10.0f);
            }

            // Icon — big and bold
            float cx = b.getCentreX();
            float cy = b.getCentreY() + 4.0f;  // nudge slightly below center (above gloss)
            float iconSize = juce::jmin (b.getWidth(), b.getHeight()) * 0.40f;

            switch (i)
            {
                case 0: // Play — triangle
                {
                    juce::Path tri;
                    tri.addTriangle (cx - iconSize * 0.55f, cy - iconSize * 0.6f,
                                     cx + iconSize * 0.55f, cy - iconSize * 0.6f,
                                     cx, cy + iconSize * 0.65f);
                    g.setColour (juce::Colours::black.withAlpha (0.25f));
                    g.fillPath (tri, juce::AffineTransform::translation (3.0f, 3.0f));
                    g.setColour (isPressed ? juce::Colours::white : juce::Colours::white.withAlpha (0.9f));
                    g.fillPath (tri);
                    break;
                }
                case 1: // Stop — rounded square
                {
                    float s = iconSize * 0.7f;
                    g.setColour (juce::Colours::black.withAlpha (0.25f));
                    g.fillRoundedRectangle (cx - s * 0.5f + 3.0f, cy - s * 0.5f + 3.0f, s, s, 4.0f);
                    g.setColour (isPressed ? juce::Colours::white : juce::Colours::white.withAlpha (0.9f));
                    g.fillRoundedRectangle (cx - s * 0.5f, cy - s * 0.5f, s, s, 4.0f);
                    break;
                }
            }
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        for (int i = 0; i < 2; ++i)
        {
            if (bigBounds[i].contains (e.position))
            {
                pressedBig = i;

                auto& seqs = router.getSequencers();
                if (i == 0)
                {
                    for (int t = 0; t < LocalSequencers::NumTracks; ++t)
                        seqs.setPlaying (t, true);   // tracks without a sequencer ignore it
                }
                else
                    seqs.stopAll();

                repaint();
                return;
            }
        }
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (pressedBig >= 0)
        {
            pressedBig = -1;
            repaint();
        }
    }

private:
    EventRouter& router;

    // Big transport: Play, Stop
    juce::Rectangle<float> bigBounds[2];
    int pressedBig = -1;
    bool localRunning = false;
};

//==============================================================================
// Outer page — landscape container. The header strip is plain landscape
// components; below it the portrait content is rotated 90° CCW, so Play
// and Stop sit side by side.
class TransportPage : public juce::Component, private juce::Timer
{
public:
    TransportPage (EventRouter& client)
        : seqs (client.getSequencers()), content (client)
    {
        setLookAndFeel (&glassLaf);

        bpmTitle.setText ("BPM", juce::dontSendNotification);
        bpmTitle.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.55f));
        bpmTitle.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
        bpmTitle.setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (bpmTitle);

        for (auto* b : { &slowerButton, &fasterButton })
        {
            b->setRepeatSpeed (400, 60);
            addAndMakeVisible (b);
        }
        slowerButton.onClick = [this] { setBpm (std::round (seqs.getBpm()) - 1.0); };
        fasterButton.onClick = [this] { setBpm (std::round (seqs.getBpm()) + 1.0); };
        tapButton.onClick    = [this] { tap(); };
        addAndMakeVisible (tapButton);

        bpmLabel.setJustificationType (juce::Justification::centred);
        bpmLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.95f));
        bpmLabel.setFont (juce::Font (juce::FontOptions (20.0f, juce::Font::bold)));
        bpmLabel.addMouseListener (this, false);
        addAndMakeVisible (bpmLabel);

        hintLabel.setText ("Play / Stop run every sequencer", juce::dontSendNotification);
        hintLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.45f));
        hintLabel.setFont (juce::Font (juce::FontOptions (12.0f)));
        hintLabel.setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (hintLabel);

        addAndMakeVisible (content);

        timerCallback();
        startTimerHz (15);
    }

    ~TransportPage() override { setLookAndFeel (nullptr); }

    // Tempo changed — worth saving.
    std::function<void()> onStateChanged;

    void paint (juce::Graphics& g) override
    {
        // Landscape-native panel paint — rotated content sits on top.
        Glassmorphism::paintPanel (g, getLocalBounds().toFloat(), transportPagePalette);
    }

    void resized() override
    {
        int landW = getWidth();
        int landH = getHeight();
        if (landW <= 0 || landH <= 0) return;

        auto header = getLocalBounds().removeFromTop (headerH).reduced (10, 8);
        bpmTitle.setBounds (header.removeFromLeft (36));
        header.removeFromLeft (6);
        slowerButton.setBounds (header.removeFromLeft (38));
        bpmLabel.setBounds (header.removeFromLeft (70));
        fasterButton.setBounds (header.removeFromLeft (38));
        header.removeFromLeft (6);
        tapButton.setBounds (header.removeFromLeft (56));
        header.removeFromLeft (10);
        hintLabel.setBounds (header);

        const int contentH = landH - headerH;
        content.setSize (contentH, landW);
        content.setTransform (
            juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi, 0.0f, 0.0f)
            .translated (0.0f, (float) landH));
    }

    // Drag the BPM readout up or down, one BPM per 4 px.
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.eventComponent == &bpmLabel)
            dragStartBpm = seqs.getBpm();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (e.eventComponent == &bpmLabel)
            setBpm (std::round (dragStartBpm - e.getDistanceFromDragStartY() / 4.0));
    }

private:
    static constexpr int headerH = 52;

    void timerCallback() override
    {
        bpmLabel.setText (juce::String (juce::roundToInt (seqs.getBpm())), juce::dontSendNotification);
        content.setLocalRunning (seqs.isAnyPlaying());
    }

    void setBpm (double b)
    {
        b = juce::jlimit (40.0, 240.0, b);
        if (b == seqs.getBpm())
            return;
        seqs.setBpm (b);
        bpmLabel.setText (juce::String (juce::roundToInt (b)), juce::dontSendNotification);
        if (onStateChanged) onStateChanged();
    }

    // Average of the last few taps; a 2 s gap starts a fresh count.
    void tap()
    {
        const double now = juce::Time::getMillisecondCounterHiRes();
        if (! taps.empty() && now - taps.back() > 2000.0)
            taps.clear();
        taps.push_back (now);
        if (taps.size() > 5)
            taps.erase (taps.begin());
        if (taps.size() >= 2)
            setBpm (std::round (60000.0 * (double) (taps.size() - 1) / (taps.back() - taps.front())));
    }

    LocalSequencers& seqs;
    GlassButtonLookAndFeel glassLaf { transportPagePalette, MidiMorfColors::accent };

    juce::Label bpmTitle, bpmLabel, hintLabel;
    juce::TextButton slowerButton { "-" }, fasterButton { "+" }, tapButton { "TAP" };

    TransportPortraitContent content;

    std::vector<double> taps;
    double dragStartBpm = 120.0;
};
