#pragma once

#include <JuceHeader.h>

//==============================================================================
// How every dropdown opens: all of it at once. A long list spreads into
// side-by-side columns instead of a scrolling strip, which is what a phone
// held sideways would otherwise get. The look-and-feels use this for their
// combo boxes, and MainComponent installs AppLookAndFeel as the default for
// everything that has no look-and-feel of its own.
namespace PopupStyle
{
    inline juce::PopupMenu::Options forComboBox (juce::ComboBox& box, juce::Label&)
    {
        return juce::PopupMenu::Options().withTargetComponent (&box)
                                         .withInitiallySelectedItem (box.getSelectedId())
                                         .withMinimumWidth (box.getWidth())
                                         .withMaximumNumColumns (6)
                                         .withStandardItemHeight (28);
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
