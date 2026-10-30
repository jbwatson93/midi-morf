#pragma once

#include <JuceHeader.h>
#include "Glassmorphism.h"
#include "BreakImport.h"
#include "TrackRail.h"

//==============================================================================
// The sound page for the break player: every built-in break as a button,
// the currently loaded one lit, plus Import .wav. The slice grid on Slice-Seq
// follows whatever is picked here (its strip watches the loaded break).
class BreaksPage : public juce::Component
{
public:
    explicit BreaksPage (LocalSequencers& s) : seqs (s)
    {
        const auto names = BreakLibrary::builtInNames();
        for (int i = 0; i < names.size(); ++i)
        {
            auto* b = breakButtons.add (new juce::TextButton (names[i]));
            b->setLookAndFeel (&glassLnf);
            b->setClickingTogglesState (false);
            b->onClick = [this, i]
            {
                seqs.setBuiltInBreak (i);
                refresh();
                if (onChanged) onChanged();
            };
            addAndMakeVisible (b);
        }

        importButton.setLookAndFeel (&glassLnf);
        importButton.onClick = [this]
        {
            juce::Component::SafePointer<BreaksPage> safe (this);
            BreakImport::launch (chooser, seqs, [safe]
            {
                if (safe == nullptr) return;
                safe->refresh();
                if (safe->onChanged) safe->onChanged();
            });
        };
        addAndMakeVisible (importButton);
    }

    ~BreaksPage() override
    {
        for (auto* b : breakButtons)
            b->setLookAndFeel (nullptr);
        importButton.setLookAndFeel (nullptr);
    }

    std::function<void()> onChanged;   // persist

    void setTrack (int t) { track = t; refresh(); }

    void refresh()
    {
        const int current = seqs.getBuiltInBreakIndex();
        for (int i = 0; i < breakButtons.size(); ++i)
        {
            auto* b = breakButtons[i];
            const bool on = (i == current);
            b->setToggleState (on, juce::dontSendNotification);
            b->setColour (juce::TextButton::textColourOffId, on ? TrackColours::get (track) : MidiMorfColors::textLight);
        }

        const auto* br = seqs.getBreak();
        importButton.setButtonText (current < 0 && br != nullptr ? "Imported: " + br->name : juce::String ("Import .wav..."));
        repaint();
    }

    void visibilityChanged() override { if (isVisible()) refresh(); }

    void paint (juce::Graphics& g) override
    {
        Glassmorphism::paintPanel (g, getLocalBounds().toFloat(), breaksPagePalette);

        if (const auto* br = seqs.getBreak())
        {
            g.setColour (MidiMorfColors::textLight.withAlpha (0.7f));
            g.setFont (juce::FontOptions (13.0f));
            g.drawText (br->name + "   " + juce::String (br->bars()) + (br->bars() == 1 ? " bar   " : " bars   ")
                            + juce::String (juce::roundToInt (br->bpm())) + " BPM",
                        infoArea, juce::Justification::centredLeft);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (12, 10);
        infoArea = area.removeFromTop (22);
        area.removeFromTop (6);

        auto bottom = area.removeFromBottom (44);
        importButton.setBounds (bottom.withWidth (juce::jmin (320, bottom.getWidth())));
        area.removeFromBottom (8);

        const int cols = 4;
        const int n = breakButtons.size();
        const int rowsN = (n + cols - 1) / cols;
        const int gap = 8;
        const int cellW = (area.getWidth() - (cols - 1) * gap) / cols;
        const int cellH = juce::jmin (56, (area.getHeight() - (rowsN - 1) * gap) / juce::jmax (1, rowsN));
        for (int i = 0; i < n; ++i)
            breakButtons[i]->setBounds (area.getX() + (i % cols) * (cellW + gap),
                                        area.getY() + (i / cols) * (cellH + gap), cellW, cellH);
    }

private:
    static inline const PagePalette breaksPagePalette {
        juce::Colour (0xff2A2014), juce::Colour (0xff120D07),
        juce::Colour (0xCC3A2C1A), juce::Colour (0xD8241A0E), juce::Colour (0xCC000000)
    };

    LocalSequencers& seqs;
    int track = 2;
    GlassButtonLookAndFeel glassLnf { breaksPagePalette, MidiMorfColors::accent, 8.0f };
    juce::OwnedArray<juce::TextButton> breakButtons;
    juce::TextButton importButton { "Import .wav..." };
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<int> infoArea;
};
