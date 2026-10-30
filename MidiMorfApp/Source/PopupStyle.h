#pragma once

#include <JuceHeader.h>

//==============================================================================
// How every menu opens: all of it, at once. JUCE places a menu next to what
// opened it, using only the room on that side, so on a phone held sideways a
// dropdown near the top or bottom opened as a two-item strip you had to
// scroll. Here every menu hangs from the top of the screen, lined up with
// what opened it, with the whole height to use; a list longer than the
// screen spreads into side-by-side columns.
//
// The look-and-feels use forComboBox for their dropdowns; MainComponent
// installs AppLookAndFeel as the default for everything that has no
// look-and-feel of its own; menus opened directly use around().
namespace PopupStyle
{
    // `screenArea` is what opened the menu, in screen coordinates.
    inline juce::PopupMenu::Options around (juce::Rectangle<int> screenArea)
    {
        juce::Rectangle<int> screen = screenArea;
        if (const auto* d = juce::Desktop::getInstance().getDisplays().getDisplayForRect (screenArea))
            screen = d->userArea;

        // A thin strip along the top edge, over the opener: the menu drops
        // from there with the full height below it.
        const juce::Rectangle<int> anchor (screenArea.getX(), screen.getY(), juce::jmax (1, screenArea.getWidth()), 1);

        return juce::PopupMenu::Options().withTargetScreenArea (anchor)
                                         .withMaximumNumColumns (6)
                                         .withStandardItemHeight (28);
    }

    inline juce::PopupMenu::Options around (juce::Component& opener)
    {
        return around (opener.getScreenBounds());
    }

    inline juce::PopupMenu::Options forComboBox (juce::ComboBox& box, juce::Label&)
    {
        return around (box).withInitiallySelectedItem (box.getSelectedId())
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
