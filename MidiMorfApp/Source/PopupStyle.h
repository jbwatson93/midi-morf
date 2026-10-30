#pragma once

#include <JuceHeader.h>

//==============================================================================
// How every menu opens: attached to what opened it, and all of it at once.
//
// JUCE puts a menu below (or above) its opener using only the room on that
// side, and shrinks it into a scrolling strip when it doesn't fit — on a
// phone held sideways that left a dropdown as a two-item strip. Here the
// menu's full height is worked out first, then:
//   • it opens straight down from the opener if it fits there,
//   • else straight up if it fits there,
//   • else it slides only as far as it must to fit on screen, still lined up
//     with the opener — and a list taller than the screen spreads into
//     side-by-side columns.
//
// The look-and-feels use forComboBox for their dropdowns; MainComponent
// installs AppLookAndFeel as the default for everything that has no
// look-and-feel of its own; menus opened directly use attached().
namespace PopupStyle
{
    static constexpr int ItemHeight = 28;

    // The menu's height as JUCE will lay it out in one column: rows, half
    // rows for separators, and the border.
    inline int heightOf (const juce::PopupMenu& menu, int itemHeight)
    {
        int h = 2 * 2;
        for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
            h += it.getItem().isSeparator ? itemHeight / 2 : itemHeight;
        return h;
    }

    // `opener` is what opened the menu, in screen coordinates.
    inline juce::PopupMenu::Options attached (const juce::PopupMenu& menu, juce::Rectangle<int> opener, int itemHeight = ItemHeight)
    {
        auto options = juce::PopupMenu::Options().withMaximumNumColumns (6).withStandardItemHeight (itemHeight);

        juce::Rectangle<int> screen = opener;
        if (const auto* d = juce::Desktop::getInstance().getDisplays().getDisplayForRect (opener))
            screen = d->userArea;

        const int h = heightOf (menu, itemHeight);
        constexpr int slack = 32;   // JUCE wants a little room past the menu

        if (h < screen.getBottom() - opener.getBottom() - slack)
            return options.withTargetScreenArea (opener);   // straight down

        if (h < opener.getY() - screen.getY() - slack)
            return options.withTargetScreenArea (opener).withPreferredPopupDirection (juce::PopupMenu::Options::PopupDirection::upwards);

        // Neither side has room: start as near the opener as fits (the top
        // of the screen for a list that needs columns).
        const int top = juce::jmax (screen.getY() - 1, juce::jmin (opener.getY(), screen.getBottom() - h - slack));
        return options.withTargetScreenArea ({ opener.getX(), top, opener.getWidth(), 1 });
    }

    inline juce::PopupMenu::Options attached (const juce::PopupMenu& menu, juce::Component& opener, int itemHeight = ItemHeight)
    {
        return attached (menu, opener.getScreenBounds(), itemHeight);
    }

    inline juce::PopupMenu::Options forComboBox (juce::ComboBox& box, juce::Label&)
    {
        return attached (*box.getRootMenu(), box).withInitiallySelectedItem (box.getSelectedId())
                                                 .withMinimumWidth (box.getWidth());
    }
}

class AppLookAndFeel : public juce::LookAndFeel_V4
{
public:
    juce::PopupMenu::Options getOptionsForComboBoxPopupMenu (juce::ComboBox& box, juce::Label& label) override
    {
        return PopupStyle::forComboBox (box, label);
    }
};
