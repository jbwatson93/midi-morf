#pragma once

#include <JuceHeader.h>
#include "MidiMorfColors.h"

//==============================================================================
// The first-run walkthrough: a coaching layer over the real app. It dims the
// screen, leaves a lit hole around the thing to use, and shows a bubble with
// what to do. Touches go straight through to the app (only the bubble takes
// them), and each step moves on by itself once the player has done it.
//
// MainComponent supplies the steps: where to point (in this component's
// coordinates, which are MainComponent's), and how to tell it's done.
class Walkthrough : public juce::Component,
                    private juce::Timer
{
public:
    struct Step
    {
        juce::String text;
        std::function<juce::Rectangle<int>()> target;   // empty rect or none: no hole, bubble centred
        std::function<bool()> done;                      // none: the bubble's button moves on
        juce::String button;                             // e.g. "Next", "Let's go"
        std::function<void()> onEnter;
        bool atTop = false;                              // a slim bubble along the top, out of the way
    };

    Walkthrough()
    {
        setInterceptsMouseClicks (true, true);
        skipButton.onClick = [this] { finish(); };
        actionButton.onClick = [this] { advance(); };
        addAndMakeVisible (skipButton);
        addAndMakeVisible (actionButton);
        skipButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2A2C33));
        actionButton.setColour (juce::TextButton::buttonColourId, MidiMorfColors::accent);
    }

    std::function<void()> onFinished;   // finished or skipped

    void start (std::vector<Step> s)
    {
        steps = std::move (s);
        index = -1;
        setVisible (true);
        toFront (false);
        advance();
        startTimerHz (20);
    }

    bool isRunning() const { return isVisible() && index >= 0; }

    // Only the bubble takes touches; everything else reaches the app.
    bool hitTest (int x, int y) override { return bubble.contains (x, y); }

    void paint (juce::Graphics& g) override
    {
        if (! juce::isPositiveAndBelow (index, (int) steps.size()))
            return;

        // The dim, with a hole where the player should look.
        juce::Path dim;
        dim.addRectangle (getLocalBounds().toFloat());
        if (! hole.isEmpty())
        {
            dim.addRoundedRectangle (hole.toFloat(), 10.0f);
            dim.setUsingNonZeroWinding (false);
        }
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillPath (dim);

        if (! hole.isEmpty())
        {
            const float pulse = 0.5f + 0.5f * std::sin ((float) juce::Time::getMillisecondCounter() * 0.006f);
            g.setColour (MidiMorfColors::accent.withAlpha (0.55f + 0.45f * pulse));
            g.drawRoundedRectangle (hole.toFloat().expanded (2.0f * pulse), 10.0f, 2.5f);
        }

        // The bubble.
        auto b = bubble.toFloat();
        g.setColour (juce::Colour (0xf0181A20));
        g.fillRoundedRectangle (b, 12.0f);
        g.setColour (MidiMorfColors::accent.withAlpha (0.8f));
        g.drawRoundedRectangle (b.reduced (0.75f), 12.0f, 1.5f);

        auto text = bubble.reduced (14, 10);
        g.setColour (MidiMorfColors::textLight.withAlpha (0.5f));
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (juce::String (index + 1) + " / " + juce::String ((int) steps.size()), text.removeFromTop (14), juce::Justification::centredLeft);
        g.setColour (juce::Colours::white);
        g.setFont (juce::FontOptions (15.0f));
        g.drawFittedText (steps[(size_t) index].text, text.withTrimmedBottom (34), juce::Justification::topLeft, 5);
    }

    void resized() override { layout(); }

private:
    void advance()
    {
        ++index;
        doneSince = 0;
        if (index >= (int) steps.size())
        {
            finish();
            return;
        }
        if (steps[(size_t) index].onEnter)
            steps[(size_t) index].onEnter();
        layout();
        repaint();
    }

    void finish()
    {
        stopTimer();
        index = -1;
        setVisible (false);
        if (onFinished) onFinished();
    }

    void layout()
    {
        if (! juce::isPositiveAndBelow (index, (int) steps.size()))
            return;
        const auto& s = steps[(size_t) index];
        hole = s.target ? s.target().expanded (6) : juce::Rectangle<int>();

        // Beside the hole: below it if there's room, else above, else to the
        // side; centred when there's nothing to point at.
        const int w = juce::jmin (330, getWidth() - 40), h = 138;
        auto area = getLocalBounds().reduced (16);
        juce::Rectangle<int> r (w, h);
        if (s.atTop)
            r = juce::Rectangle<int> (juce::jmin (520, getWidth() - 140), 96).withCentre ({ getWidth() / 2 - 30, 8 + 48 });
        else if (hole.isEmpty())
            r.setCentre (area.getCentre());
        else if (hole.getBottom() + 12 + h <= area.getBottom())
            r.setPosition (hole.getCentreX() - w / 2, hole.getBottom() + 12);
        else if (hole.getY() - 12 - h >= area.getY())
            r.setPosition (hole.getCentreX() - w / 2, hole.getY() - 12 - h);
        else if (hole.getX() - 12 - w >= area.getX())
            r.setPosition (hole.getX() - 12 - w, hole.getCentreY() - h / 2);
        else
            r.setPosition (hole.getRight() + 12, hole.getCentreY() - h / 2);
        bubble = r.constrainedWithin (area);

        auto buttons = bubble.reduced (12, 10).removeFromBottom (30);
        skipButton.setButtonText (index == (int) steps.size() - 1 ? "Close" : "Skip tour");
        skipButton.setVisible (index < (int) steps.size() - 1 || s.button.isEmpty());
        skipButton.setBounds (buttons.removeFromLeft (92));
        actionButton.setVisible (s.button.isNotEmpty());
        actionButton.setButtonText (s.button);
        actionButton.setBounds (buttons.removeFromRight (104));
    }

    void timerCallback() override
    {
        if (! juce::isPositiveAndBelow (index, (int) steps.size()))
            return;

        // Targets move (pages change, layouts settle): keep the hole on them.
        const auto& s = steps[(size_t) index];
        const auto newHole = s.target ? s.target().expanded (6) : juce::Rectangle<int>();
        if (newHole != hole)
            layout();

        // Done: a short beat to see it worked, then on.
        if (s.done && s.done())
        {
            if (doneSince == 0)
                doneSince = juce::Time::getMillisecondCounter();
            else if (juce::Time::getMillisecondCounter() - doneSince > 600)
                advance();
        }
        else
        {
            doneSince = 0;
        }
        repaint();
    }

    std::vector<Step> steps;
    int index = -1;
    juce::uint32 doneSince = 0;
    juce::Rectangle<int> hole, bubble;
    juce::TextButton skipButton { "Skip tour" }, actionButton;
};
