#pragma once

#include <JuceHeader.h>
#include "PopupStyle.h"

//==============================================================================
// Per-page glassmorphism colour set. Each instrument page declares one of
// these, then passes it to Glassmorphism::paintPanel for its background and
// to Glassmorphism::paintButton for any glass-style controls. Sharing one
// palette between page + buttons keeps each instrument visually cohesive.
struct PagePalette
{
    juce::Colour substrateTop;       // opaque — bottom of the gradient stack
    juce::Colour substrateBottom;
    juce::Colour acrylicTop;         // alpha baked in (0xCC..0xD8 range)
    juce::Colour acrylicBottom;
    juce::Colour edgeGlow;           // border colour, alpha baked in
};

namespace Glassmorphism
{
    //==========================================================================
    // Paints the 5-layer glass stack as a page background — full-bleed, no
    // rounded corners. Use from a page's paint() override.
    inline void paintPanel (juce::Graphics& g,
                            juce::Rectangle<float> bounds,
                            const PagePalette& p)
    {
        // 1. Substrate
        g.setGradientFill (juce::ColourGradient (
            p.substrateTop,    bounds.getX(), bounds.getY(),
            p.substrateBottom, bounds.getX(), bounds.getBottom(), false));
        g.fillRect (bounds);

        // 2. Translucent acrylic
        g.setGradientFill (juce::ColourGradient (
            p.acrylicTop,    bounds.getX(), bounds.getY(),
            p.acrylicBottom, bounds.getX(), bounds.getBottom(), false));
        g.fillRect (bounds);

        // 3. Top sheen
        auto sheen = bounds.withHeight (bounds.getHeight() * 0.22f);
        g.setGradientFill (juce::ColourGradient (
            juce::Colour (0x55FFFFFF), sheen.getX(), sheen.getY(),
            juce::Colour (0x00FFFFFF), sheen.getX(), sheen.getBottom(), false));
        g.fillRect (sheen);

        // 4. Bottom shadow
        auto shadow = bounds.withTrimmedTop (bounds.getHeight() * 0.65f);
        g.setGradientFill (juce::ColourGradient (
            juce::Colour (0x00000000), shadow.getX(), shadow.getY(),
            juce::Colour (0x44000000), shadow.getX(), shadow.getBottom(), false));
        g.fillRect (shadow);

        // 5. Edge glow
        g.setColour (p.edgeGlow);
        g.drawRect (bounds.reduced (1.0f), 1.5f);
    }

    //==========================================================================
    // Paints the same stack into a rounded-rect button-sized area. When
    // `active` is true (pressed / toggled), substrate + acrylic swap to
    // accent-tinted variants so the press reads instantly. When
    // `highlighted` is true (hover / last-triggered indicator), the edge
    // brightens.
    inline void paintButton (juce::Graphics& g,
                             juce::Rectangle<float> bounds,
                             const PagePalette& palette,
                             juce::Colour accent,
                             bool active,
                             bool highlighted,
                             float cornerRadius = 6.0f)
    {
        {
            juce::Graphics::ScopedSaveState save (g);
            juce::Path clip;
            clip.addRoundedRectangle (bounds, cornerRadius);
            g.reduceClipRegion (clip);

            // 1. Substrate
            const auto subTop = active ? accent.darker (0.4f) : palette.substrateTop;
            const auto subBot = active ? accent.darker (0.7f) : palette.substrateBottom;
            g.setGradientFill (juce::ColourGradient (
                subTop, bounds.getX(), bounds.getY(),
                subBot, bounds.getX(), bounds.getBottom(), false));
            g.fillAll();

            // 2. Acrylic
            const auto acrTop = active ? accent.withAlpha (0.8f)                        : palette.acrylicTop;
            const auto acrBot = active ? accent.darker (0.2f).withAlpha (0.85f)         : palette.acrylicBottom;
            g.setGradientFill (juce::ColourGradient (
                acrTop, bounds.getX(), bounds.getY(),
                acrBot, bounds.getX(), bounds.getBottom(), false));
            g.fillAll();

            // 3. Sheen
            auto sheen = bounds.withHeight (bounds.getHeight() * 0.45f);
            g.setGradientFill (juce::ColourGradient (
                juce::Colour (0x66FFFFFF), sheen.getX(), sheen.getY(),
                juce::Colour (0x00FFFFFF), sheen.getX(), sheen.getBottom(), false));
            g.fillRect (sheen);

            // 4. Shadow
            auto shadow = bounds.withTrimmedTop (bounds.getHeight() * 0.55f);
            g.setGradientFill (juce::ColourGradient (
                juce::Colour (0x00000000), shadow.getX(), shadow.getY(),
                juce::Colour (0x55000000), shadow.getX(), shadow.getBottom(), false));
            g.fillRect (shadow);
        }

        // 5. Edge glow
        const auto edge = active      ? accent.brighter (0.2f).withAlpha (0.85f)
                        : highlighted ? palette.edgeGlow.brighter (0.3f)
                                      : palette.edgeGlow;
        g.setColour (edge);
        g.drawRoundedRectangle (bounds.reduced (1.0f), cornerRadius, 1.5f);
    }
}

//==============================================================================
// Drop-in LookAndFeel that paints any juce::Button with the glass treatment
// using a specific PagePalette + accent colour. Declare one as a member of
// your page (before the buttons) and call setLookAndFeel on each button.
class GlassButtonLookAndFeel : public juce::LookAndFeel_V4
{
public:
    // Dropdowns open fully (PopupStyle).
    juce::PopupMenu::Options getOptionsForComboBoxPopupMenu (juce::ComboBox& box, juce::Label& label) override
    {
        return PopupStyle::forComboBox (box, label);
    }

    GlassButtonLookAndFeel (const PagePalette& p, juce::Colour accentColour,
                            float cornerRadius = 6.0f)
        : palette (p), accent (accentColour), radius (cornerRadius) {}

    void drawButtonBackground (juce::Graphics& g, juce::Button& b,
                               const juce::Colour& /*bg*/,
                               bool shouldDrawButtonAsHighlighted,
                               bool shouldDrawButtonAsDown) override
    {
        auto bounds = b.getLocalBounds().toFloat();
        const bool active = shouldDrawButtonAsDown || b.getToggleState();
        Glassmorphism::paintButton (g, bounds, palette, accent, active,
                                    shouldDrawButtonAsHighlighted, radius);
    }

private:
    const PagePalette& palette;
    juce::Colour accent;
    float radius;
};

//==============================================================================
// Variant that derives the glass substrate from each button's own
// `buttonColourId` instead of a shared PagePalette — so a row of buttons
// styled with setColour(juce::TextButton::buttonColourId, ...) renders as a
// row of differently-coloured glass chips. Used on Rose-Chord where each
// chord-quality button has its own hue.
class MultiColorGlassButtonLookAndFeel : public juce::LookAndFeel_V4
{
public:
    // Dropdowns open fully (PopupStyle).
    juce::PopupMenu::Options getOptionsForComboBoxPopupMenu (juce::ComboBox& box, juce::Label& label) override
    {
        return PopupStyle::forComboBox (box, label);
    }

    MultiColorGlassButtonLookAndFeel (juce::Colour accentColour,
                                      float cornerRadius = 6.0f)
        : accent (accentColour), radius (cornerRadius) {}

    void drawButtonBackground (juce::Graphics& g, juce::Button& b,
                               const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted,
                               bool shouldDrawButtonAsDown) override
    {
        auto bounds = b.getLocalBounds().toFloat();
        const bool active = shouldDrawButtonAsDown || b.getToggleState();

        // Synthesise a palette on the fly from the button's colour.
        PagePalette p {
            backgroundColour.brighter (0.20f),                              // substrate top
            backgroundColour.darker   (0.45f),                              // substrate bottom
            backgroundColour.withAlpha (0.85f),                             // acrylic top
            backgroundColour.darker (0.20f).withAlpha (0.85f),              // acrylic bottom
            backgroundColour.brighter (0.45f).withAlpha (0.65f)             // edge glow
        };

        Glassmorphism::paintButton (g, bounds, p, accent, active,
                                    shouldDrawButtonAsHighlighted, radius);
    }

private:
    juce::Colour accent;
    float radius;
};
