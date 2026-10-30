#pragma once

#include <JuceHeader.h>
#include "MidiMorfColors.h"
#include "WavetablePresets.h"

//==============================================================================
// The preset strip on the Sound page: ◀  name  ▶  SAVE. A star after the
// name means the sound has been changed since the preset was loaded.
class PresetBar : public juce::Component
{
public:
    PresetBar()
    {
        for (auto* b : { &prev, &next, &name, &save })
            addAndMakeVisible (b);
        prev.onClick = [this] { if (onStep) onStep (-1); };
        next.onClick = [this] { if (onStep) onStep (1); };
        name.onClick = [this] { if (onBrowse) onBrowse(); };
        save.onClick = [this] { if (onSave) onSave(); };
    }

    std::function<void (int direction)> onStep;
    std::function<void()> onBrowse, onSave;

    void set (const juce::String& presetName, bool modified, juce::Colour colour)
    {
        name.setButtonText (presetName + (modified ? "  *" : ""));
        name.setColour (juce::TextButton::textColourOffId, colour.brighter (0.3f));
        name.setColour (juce::TextButton::textColourOnId, colour.brighter (0.3f));
    }

    void resized() override
    {
        auto r = getLocalBounds();
        save.setBounds (r.removeFromRight (56).reduced (3));
        prev.setBounds (r.removeFromLeft (34).reduced (3));
        next.setBounds (r.removeFromRight (34).reduced (3));
        name.setBounds (r.reduced (3));
    }

private:
    juce::TextButton prev { "<" }, next { ">" }, name, save { "SAVE" };
};

//==============================================================================
// Full-page preset browser over the Sound page: a column of categories, the
// presets in the chosen one as a grid (yours after the factory ones), Init,
// and Close. Picking a preset loads it and closes.
class PresetBrowser : public juce::Component
{
public:
    PresetBrowser()
    {
        title.setText ("PRESETS", juce::dontSendNotification);
        title.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        title.setColour (juce::Label::textColourId, MidiMorfColors::textLight);
        addAndMakeVisible (title);

        initButton.onClick  = [this] { pick (WavetablePresets::init()); };
        closeButton.onClick = [this] { setVisible (false); };
        addAndMakeVisible (initButton);
        addAndMakeVisible (closeButton);

        for (auto& cat : WavetablePresets::categories())
        {
            auto* b = categoryButtons.add (new juce::TextButton (cat));
            b->setClickingTogglesState (true);
            b->setRadioGroupId (4242);
            b->onClick = [this, cat] { showCategory (cat); };
            addAndMakeVisible (b);
        }
    }

    std::function<void (const WavetablePreset&)> onPick;

    // Opens on the category of the current preset.
    void show (const juce::String& currentName, juce::Colour colour)
    {
        accent = colour;
        current = currentName;
        presets = WavetablePresets::all();

        juce::String category = WavetablePresets::categories()[0];
        for (auto& p : presets)
            if (p.name == currentName && p.category.isNotEmpty())
                category = p.category;

        showCategory (category);
        setVisible (true);
        toFront (true);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xf0121316));
        g.setColour (accent.withAlpha (0.6f));
        g.drawRect (getLocalBounds(), 1);

        if (presetButtons.isEmpty())
        {
            g.setColour (MidiMorfColors::textLight.withAlpha (0.5f));
            g.setFont (juce::FontOptions (13.0f));
            g.drawText ("No presets here yet. SAVE adds yours.", gridArea, juce::Justification::centred);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (10);
        auto top = area.removeFromTop (36);
        closeButton.setBounds (top.removeFromRight (80).reduced (2));
        initButton.setBounds (top.removeFromRight (80).reduced (2));
        title.setBounds (top);
        area.removeFromTop (6);

        auto left = area.removeFromLeft (100);
        const int catH = juce::jmin (40, (left.getHeight() - 5 * 6) / juce::jmax (1, categoryButtons.size()));
        for (auto* b : categoryButtons)
        {
            b->setBounds (left.removeFromTop (catH));
            left.removeFromTop (6);
        }

        area.removeFromLeft (10);
        gridArea = area;
        layoutGrid();
    }

private:
    void showCategory (const juce::String& category)
    {
        for (auto* b : categoryButtons)
            b->setToggleState (b->getButtonText() == category, juce::dontSendNotification);

        presetButtons.clear();
        shown.clear();
        for (auto& p : presets)
        {
            if (p.category != category)
                continue;
            shown.push_back (p);
            auto* b = presetButtons.add (new juce::TextButton (p.isFactory() ? p.name : p.name + "  (yours)"));
            const auto preset = p;
            b->onClick = [this, preset] { pick (preset); };
            if (p.name == current)
                b->setColour (juce::TextButton::textColourOffId, accent.brighter (0.3f));
            addAndMakeVisible (b);
        }
        layoutGrid();
        repaint();
    }

    void layoutGrid()
    {
        const int cols = 3, gap = 6;
        const int cellW = (gridArea.getWidth() - (cols - 1) * gap) / cols;
        const int cellH = 40;
        for (int i = 0; i < presetButtons.size(); ++i)
            presetButtons[i]->setBounds (gridArea.getX() + (i % cols) * (cellW + gap),
                                         gridArea.getY() + (i / cols) * (cellH + gap), cellW, cellH);
    }

    void pick (const WavetablePreset& p)
    {
        setVisible (false);
        if (onPick) onPick (p);
    }

    juce::Label title;
    juce::TextButton initButton { "Init" }, closeButton { "Close" };
    juce::OwnedArray<juce::TextButton> categoryButtons, presetButtons;
    std::vector<WavetablePreset> presets, shown;
    juce::String current;
    juce::Colour accent { MidiMorfColors::accent };
    juce::Rectangle<int> gridArea;
};
