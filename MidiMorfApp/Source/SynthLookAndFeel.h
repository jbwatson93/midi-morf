#pragma once

#include <JuceHeader.h>
#include "MidiMorfColors.h"

//==============================================================================
// SynthLookAndFeel — dark studio-rack vibe. Applied to the synth settings
// page tree so all knobs / combos / tab buttons share a cohesive aesthetic.
class SynthLookAndFeel : public juce::LookAndFeel_V4
{
public:
    SynthLookAndFeel()
    {
        // ComboBox palette (used as the base; drawComboBox paints over)
        setColour (juce::ComboBox::backgroundColourId,  juce::Colour (0xff1F1F23));
        setColour (juce::ComboBox::textColourId,        MidiMorfColors::textLight);
        setColour (juce::ComboBox::outlineColourId,     juce::Colour (0xff3A3A40));
        setColour (juce::ComboBox::arrowColourId,       MidiMorfColors::accent);
        setColour (juce::ComboBox::buttonColourId,      juce::Colour (0xff1F1F23));

        // Popup menu (dropdown list)
        setColour (juce::PopupMenu::backgroundColourId,         juce::Colour (0xff232328));
        setColour (juce::PopupMenu::textColourId,               MidiMorfColors::textLight);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, MidiMorfColors::accent);
        setColour (juce::PopupMenu::highlightedTextColourId,    juce::Colours::white);

        // Slider text box (we keep it transparent — the Knob component handles it)
        setColour (juce::Slider::textBoxTextColourId,       MidiMorfColors::textLight);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxOutlineColourId,    juce::Colours::transparentBlack);

        // Labels default
        setColour (juce::Label::textColourId, MidiMorfColors::textLight);
    }

    //==========================================================================
    // Rotary knobs — gradient face + metallic rim + accent value arc + LED dot
    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h,
                           float sliderPos, float rotaryStart, float rotaryEnd,
                           juce::Slider& slider) override
    {
        auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (6.0f);
        auto centre  = bounds.getCentre();
        auto outerR  = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        auto faceR   = outerR * 0.76f;
        auto lineW   = juce::jmin (3.5f, outerR * 0.17f);
        auto arcR    = outerR - lineW * 0.6f;
        auto toAngle = rotaryStart + sliderPos * (rotaryEnd - rotaryStart);

        // Track (background arc)
        {
            juce::Path track;
            track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f,
                                  rotaryStart, rotaryEnd, true);
            g.setColour (juce::Colour (0xff2A2A30));
            g.strokePath (track, juce::PathStrokeType (lineW,
                                                       juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));
        }

        // Value arc (accent)
        if (slider.isEnabled())
        {
            juce::Path value;
            value.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f,
                                  rotaryStart, toAngle, true);
            g.setColour (MidiMorfColors::accent);
            g.strokePath (value, juce::PathStrokeType (lineW,
                                                       juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));
        }

        // Knob body — top-light gradient (gives the rim a metallic feel)
        {
            juce::ColourGradient body (juce::Colour (0xff3C3C44),
                                       centre.x, centre.y - faceR,
                                       juce::Colour (0xff17171B),
                                       centre.x, centre.y + faceR,
                                       false);
            g.setGradientFill (body);
            g.fillEllipse (centre.x - faceR, centre.y - faceR, faceR * 2, faceR * 2);
        }

        // Glassy top highlight
        {
            auto hlR = faceR * 0.7f;
            juce::ColourGradient sheen (juce::Colour (0x55FFFFFF),
                                        centre.x, centre.y - faceR * 0.7f,
                                        juce::Colour (0x00FFFFFF),
                                        centre.x, centre.y - faceR * 0.1f,
                                        false);
            g.setGradientFill (sheen);
            g.fillEllipse (centre.x - hlR, centre.y - faceR * 0.95f, hlR * 2, hlR * 1.1f);
        }

        // Outer rim
        g.setColour (juce::Colour (0xff555560));
        g.drawEllipse (centre.x - faceR, centre.y - faceR, faceR * 2, faceR * 2, 0.8f);

        // Pointer line + LED dot at the value position
        const float ang = toAngle - juce::MathConstants<float>::halfPi;
        const float startR = faceR * 0.30f;
        const float endR   = faceR * 0.92f;
        juce::Point<float> p0 (centre.x + std::cos (ang) * startR,
                               centre.y + std::sin (ang) * startR);
        juce::Point<float> p1 (centre.x + std::cos (ang) * endR,
                               centre.y + std::sin (ang) * endR);
        g.setColour (MidiMorfColors::accent);
        g.drawLine ({ p0, p1 }, juce::jmax (1.5f, lineW * 0.55f));

        // LED dot at the tip — slight glow if slider is being dragged.
        const float dotR = juce::jmax (2.2f, lineW * 0.7f);
        if (slider.isMouseButtonDown())
        {
            g.setColour (MidiMorfColors::accent.withAlpha (0.35f));
            g.fillEllipse (p1.x - dotR * 2.2f, p1.y - dotR * 2.2f, dotR * 4.4f, dotR * 4.4f);
        }
        g.setColour (MidiMorfColors::accent);
        g.fillEllipse (p1.x - dotR, p1.y - dotR, dotR * 2.0f, dotR * 2.0f);
    }

    //==========================================================================
    // Vertical fader — Juno-style: dark slot, accent fill below the thumb,
    // bright orange cap with a top highlight.
    void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPos, float /*minSliderPos*/, float /*maxSliderPos*/,
                           const juce::Slider::SliderStyle style,
                           juce::Slider& slider) override
    {
        if (style != juce::Slider::LinearVertical)
        {
            juce::LookAndFeel_V4::drawLinearSlider (g, x, y, width, height,
                                                    sliderPos, sliderPos, sliderPos,
                                                    style, slider);
            return;
        }

        auto bounds = juce::Rectangle<float> ((float) x, (float) y,
                                              (float) width, (float) height);
        const float trackX = bounds.getCentreX();
        const float trackW = 5.0f;

        const float trackTop    = bounds.getY() + 10.0f;
        const float trackBottom = bounds.getBottom() - 10.0f;
        auto trackRect = juce::Rectangle<float> (trackX - trackW * 0.5f, trackTop,
                                                 trackW, trackBottom - trackTop);

        // Slot (recessed dark)
        g.setColour (juce::Colour (0xff18181C));
        g.fillRoundedRectangle (trackRect, 2.0f);
        g.setColour (juce::Colour (0xff3A3A40));
        g.drawRoundedRectangle (trackRect, 2.0f, 0.8f);

        // Active fill below the thumb
        if (sliderPos < trackBottom)
        {
            auto activeRect = juce::Rectangle<float> (trackX - trackW * 0.5f, sliderPos,
                                                       trackW, trackBottom - sliderPos);
            g.setColour (MidiMorfColors::accent);
            g.fillRoundedRectangle (activeRect, 2.0f);
        }

        // Cap — Juno-style bright bar with gradient body + top sheen
        const float capW = 28.0f;
        const float capH = 14.0f;
        auto capRect = juce::Rectangle<float> (trackX - capW * 0.5f,
                                               sliderPos - capH * 0.5f,
                                               capW, capH);

        // Drop shadow
        g.setColour (juce::Colour (0x55000000));
        g.fillRoundedRectangle (capRect.translated (0.0f, 1.5f), 3.0f);

        // Body gradient
        juce::ColourGradient capGrad (MidiMorfColors::accent.brighter (0.30f),
                                      capRect.getX(), capRect.getY(),
                                      MidiMorfColors::accent.darker (0.25f),
                                      capRect.getX(), capRect.getBottom(),
                                      false);
        g.setGradientFill (capGrad);
        g.fillRoundedRectangle (capRect, 3.0f);

        // Top sheen
        auto sheen = capRect.withHeight (capRect.getHeight() * 0.40f);
        g.setColour (juce::Colour (0x55FFFFFF));
        g.fillRoundedRectangle (sheen.reduced (1.5f, 1.0f), 2.0f);

        // Border + centerline
        g.setColour (MidiMorfColors::accent.darker (0.5f));
        g.drawRoundedRectangle (capRect, 3.0f, 1.0f);
        g.setColour (juce::Colour (0x99000000));
        g.drawHorizontalLine ((int) capRect.getCentreY(),
                              capRect.getX() + 5.0f, capRect.getRight() - 5.0f);
    }

    //==========================================================================
    // ComboBox — dark rounded chip with accent chevron
    void drawComboBox (juce::Graphics& g, int width, int height, bool /*isButtonDown*/,
                       int /*buttonX*/, int /*buttonY*/, int /*buttonW*/, int /*buttonH*/,
                       juce::ComboBox& /*box*/) override
    {
        auto bounds = juce::Rectangle<int> (0, 0, width, height).toFloat().reduced (0.5f);

        juce::ColourGradient grad (juce::Colour (0xff2B2B32), 0.0f, 0.0f,
                                   juce::Colour (0xff1A1A1E), 0.0f, (float) height, false);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (bounds, 4.0f);

        g.setColour (juce::Colour (0xff3A3A40));
        g.drawRoundedRectangle (bounds, 4.0f, 1.0f);

        // Chevron arrow (down) on the right
        auto cx = (float) width - 12.0f;
        auto cy = (float) height * 0.5f;
        juce::Path arrow;
        arrow.startNewSubPath (cx - 4.0f, cy - 2.0f);
        arrow.lineTo (cx,        cy + 3.0f);
        arrow.lineTo (cx + 4.0f, cy - 2.0f);
        g.setColour (MidiMorfColors::accent);
        g.strokePath (arrow, juce::PathStrokeType (1.6f,
                                                    juce::PathStrokeType::curved,
                                                    juce::PathStrokeType::rounded));
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override
    {
        return juce::FontOptions (12.0f);
    }

    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds (8, 0, box.getWidth() - 22, box.getHeight());
        label.setFont (getComboBoxFont (box));
        label.setColour (juce::Label::textColourId, MidiMorfColors::textLight);
    }

    //==========================================================================
    // Tab buttons — active = accent fill with white sheen; inactive = dark slab
    void drawButtonBackground (juce::Graphics& g, juce::Button& button,
                               const juce::Colour& /*backgroundColour*/,
                               bool isMouseOverButton, bool isButtonDown) override
    {
        auto bounds = button.getLocalBounds().toFloat().reduced (1.0f);
        bool on = button.getToggleState();

        if (on)
        {
            // Active tab — accent fill + soft white sheen on top
            g.setColour (MidiMorfColors::accent);
            g.fillRoundedRectangle (bounds, 4.0f);

            auto sheen = bounds.withHeight (bounds.getHeight() * 0.45f);
            juce::ColourGradient sg (juce::Colour (0x66FFFFFF), sheen.getX(), sheen.getY(),
                                     juce::Colour (0x00FFFFFF), sheen.getX(), sheen.getBottom(),
                                     false);
            g.setGradientFill (sg);
            g.fillRoundedRectangle (sheen.reduced (2.0f, 0.0f), 3.0f);

            g.setColour (juce::Colours::white.withAlpha (0.35f));
            g.drawRoundedRectangle (bounds, 4.0f, 1.0f);
        }
        else
        {
            juce::ColourGradient grad (juce::Colour (0xff2B2B32), 0.0f, bounds.getY(),
                                       juce::Colour (0xff1A1A1E), 0.0f, bounds.getBottom(),
                                       false);
            g.setGradientFill (grad);
            g.fillRoundedRectangle (bounds, 4.0f);

            g.setColour (juce::Colour (0xff3A3A40));
            g.drawRoundedRectangle (bounds, 4.0f, 1.0f);

            if (isMouseOverButton || isButtonDown)
            {
                g.setColour (juce::Colours::white.withAlpha (isButtonDown ? 0.10f : 0.06f));
                g.fillRoundedRectangle (bounds, 4.0f);
            }
        }
    }

    juce::Font getTextButtonFont (juce::TextButton&, int /*buttonHeight*/) override
    {
        return juce::FontOptions (12.0f, juce::Font::bold);
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& button,
                         bool /*isOver*/, bool /*isDown*/) override
    {
        bool on = button.getToggleState();
        g.setColour (on ? juce::Colours::white
                        : juce::Colour (0xff9A9AA0));
        g.setFont (getTextButtonFont (button, button.getHeight()));
        g.drawFittedText (button.getButtonText(),
                          button.getLocalBounds(),
                          juce::Justification::centred, 1);
    }
};
