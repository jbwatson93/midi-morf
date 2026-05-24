#pragma once

#include <JuceHeader.h>
#include "MidiMorfEngine.h"
#include "MidiMorfColors.h"

//==============================================================================
// Interactive 2D XY Pad
class PluginXYPad : public juce::Component, public juce::Timer
{
public:
    struct Listener
    {
        virtual ~Listener() = default;
        virtual void pluginXYPadDragged (float x, float y) = 0;
    };

    PluginXYPad (MidiMorfEngine& e) : engine (e)
    {
        startTimerHz (30);
    }

    void setListener (Listener* l) { listener = l; }

    void setLabel (const juce::String& text) { labelText = text; }

    void timerCallback() override { repaint(); }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();

        g.setColour (MidiMorfColors::padBackground);
        g.fillRoundedRectangle (bounds, 6.0f);

        g.setColour (MidiMorfColors::padBorder);
        g.drawRoundedRectangle (bounds.reduced (0.5f), 6.0f, 1.0f);

        float x = localTouching ? localX : engine.currentX.load();
        float y = localTouching ? localY : engine.currentY.load();

        float px = bounds.getX() + x * bounds.getWidth();
        float py = bounds.getY() + (1.0f - y) * bounds.getHeight();

        g.setColour (MidiMorfColors::crosshair);
        g.drawHorizontalLine ((int) py, bounds.getX(), bounds.getRight());
        g.drawVerticalLine ((int) px, bounds.getY(), bounds.getBottom());

        float dotSize = localTouching ? 14.0f : 10.0f;
        g.setColour (MidiMorfColors::dot);
        g.fillEllipse (px - dotSize * 0.5f, py - dotSize * 0.5f, dotSize, dotSize);

        g.setColour (MidiMorfColors::textLight.withAlpha (0.4f));
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (labelText, bounds.reduced (8.0f), juce::Justification::topLeft);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        localTouching = true;
        updateLocal (e.position);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        updateLocal (e.position);
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        localTouching = false;
        repaint();
    }

private:
    void updateLocal (juce::Point<float> pos)
    {
        auto bounds = getLocalBounds().toFloat();
        localX = juce::jlimit (0.0f, 1.0f, (pos.x - bounds.getX()) / bounds.getWidth());
        localY = juce::jlimit (0.0f, 1.0f, 1.0f - (pos.y - bounds.getY()) / bounds.getHeight());

        if (listener != nullptr)
            listener->pluginXYPadDragged (localX, localY);

        repaint();
    }

    MidiMorfEngine& engine;
    Listener* listener = nullptr;
    float localX = 0.5f, localY = 0.5f;
    bool localTouching = false;
    juce::String labelText { "XY PAD" };
};

//==============================================================================
// 1D Axis Slider
class PluginAxisSlider : public juce::Component
{
public:
    struct Listener
    {
        virtual ~Listener() = default;
        virtual void axisSliderDragged (bool isXAxis, float value) = 0;
    };

    PluginAxisSlider (bool horizontal, const juce::String& labelText, int ccNum)
        : isHorizontal (horizontal), label (labelText), ccNumber (ccNum) {}

    void setListener (Listener* l) { listener = l; }
    void setCCNumber (int cc) { ccNumber = cc; repaint(); }
    void setValue (float v) { currentValue = v; repaint(); }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();

        g.setColour (MidiMorfColors::padBackground);
        g.fillRoundedRectangle (bounds, 4.0f);

        g.setColour (MidiMorfColors::padBorder);
        g.drawRoundedRectangle (bounds.reduced (0.5f), 4.0f, 1.0f);

        g.setColour (MidiMorfColors::accent.withAlpha (0.6f));
        if (isHorizontal)
        {
            float barW = currentValue * bounds.getWidth();
            g.fillRoundedRectangle (bounds.getX(), bounds.getY(), barW, bounds.getHeight(), 4.0f);
        }
        else
        {
            float barH = currentValue * bounds.getHeight();
            g.fillRoundedRectangle (bounds.getX(), bounds.getBottom() - barH, bounds.getWidth(), barH, 4.0f);
        }

        g.setColour (MidiMorfColors::textLight.withAlpha (0.7f));
        g.setFont (juce::FontOptions (11.0f));
        auto textArea = bounds.reduced (4.0f);
        juce::String text = label + " - CC " + juce::String (ccNumber);

        if (isHorizontal)
            g.drawText (text, textArea, juce::Justification::centred);
        else
            g.drawFittedText (text, textArea.toNearestInt(), juce::Justification::centredTop, 2);
    }

    void mouseDown (const juce::MouseEvent& e) override { updateFromMouse (e.position); }
    void mouseDrag (const juce::MouseEvent& e) override { updateFromMouse (e.position); }

private:
    void updateFromMouse (juce::Point<float> pos)
    {
        auto bounds = getLocalBounds().toFloat();
        if (isHorizontal)
            currentValue = juce::jlimit (0.0f, 1.0f, (pos.x - bounds.getX()) / bounds.getWidth());
        else
            currentValue = juce::jlimit (0.0f, 1.0f, 1.0f - (pos.y - bounds.getY()) / bounds.getHeight());

        if (listener != nullptr)
            listener->axisSliderDragged (isHorizontal, currentValue);

        repaint();
    }

    bool isHorizontal;
    juce::String label;
    int ccNumber;
    float currentValue = 0.0f;
    Listener* listener = nullptr;
};

//==============================================================================
// FX Button — left-click = press/release, right-click = config popup
class PluginFXButton : public juce::Component
{
public:
    struct Listener
    {
        virtual ~Listener() = default;
        virtual void pluginFXButtonDown (int slotIndex) = 0;
        virtual void pluginFXButtonUp (int slotIndex) = 0;
    };

    PluginFXButton (int index, SlotConfig& config)
        : slotIndex (index), cfg (config)
    {
    }

    void setListener (Listener* l) { listener = l; }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().reduced (2.0f);

        if (held)
        {
            g.setColour (MidiMorfColors::buttonFaceActive);
            g.fillRoundedRectangle (bounds, 6.0f);
            g.setColour (MidiMorfColors::textDark);
        }
        else
        {
            g.setColour (MidiMorfColors::buttonFace);
            g.fillRoundedRectangle (bounds, 6.0f);
            g.setColour (MidiMorfColors::buttonBorder);
            g.drawRoundedRectangle (bounds.reduced (0.5f), 6.0f, 1.0f);
            g.setColour (MidiMorfColors::textDark);
        }

        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText (cfg.name, bounds, juce::Justification::centred);

        g.setFont (juce::FontOptions (9.0f));
        g.setColour (MidiMorfColors::textDark.withAlpha (0.5f));
        auto modeText = cfg.noteMode ? ("N" + juce::String (cfg.midiNote)) : ("CC" + juce::String (cfg.ccButton));
        g.drawText (modeText, bounds.reduced (3.0f), juce::Justification::topRight);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            showConfigPopup();
            return;
        }

        held = true;
        repaint();
        if (listener != nullptr)
            listener->pluginFXButtonDown (slotIndex);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
            return;

        if (held)
        {
            held = false;
            repaint();
            if (listener != nullptr)
                listener->pluginFXButtonUp (slotIndex);
        }
    }

private:
    void showConfigPopup()
    {
        auto popup = std::make_unique<juce::Component>();
        popup->setSize (220, 140);

        auto* nameLabel = new juce::Label ({}, "Name:");
        nameLabel->setBounds (10, 10, 50, 24);
        popup->addAndMakeVisible (nameLabel);

        auto* nameEditor = new juce::TextEditor();
        nameEditor->setBounds (65, 10, 145, 24);
        nameEditor->setText (cfg.name);
        nameEditor->onTextChange = [this, nameEditor] { cfg.name = nameEditor->getText(); repaint(); };
        popup->addAndMakeVisible (nameEditor);

        auto* modeToggle = new juce::ToggleButton ("Note Mode");
        modeToggle->setBounds (10, 42, 200, 24);
        modeToggle->setToggleState (cfg.noteMode, juce::dontSendNotification);
        popup->addAndMakeVisible (modeToggle);

        auto* noteLabel = new juce::Label ({}, "Note #:");
        noteLabel->setBounds (10, 74, 55, 24);
        popup->addAndMakeVisible (noteLabel);

        auto* noteEditor = new juce::TextEditor();
        noteEditor->setBounds (65, 74, 50, 24);
        noteEditor->setInputRestrictions (3, "0123456789");
        noteEditor->setText (juce::String (cfg.midiNote));
        noteEditor->onTextChange = [this, noteEditor] { cfg.midiNote = noteEditor->getText().getIntValue(); repaint(); };
        popup->addAndMakeVisible (noteEditor);

        auto* ccLabel = new juce::Label ({}, "CC #:");
        ccLabel->setBounds (125, 74, 40, 24);
        popup->addAndMakeVisible (ccLabel);

        auto* ccEditor = new juce::TextEditor();
        ccEditor->setBounds (165, 74, 45, 24);
        ccEditor->setInputRestrictions (3, "0123456789");
        ccEditor->setText (juce::String (cfg.ccButton));
        ccEditor->onTextChange = [this, ccEditor] { cfg.ccButton = ccEditor->getText().getIntValue(); repaint(); };
        popup->addAndMakeVisible (ccEditor);

        modeToggle->onClick = [this, modeToggle] { cfg.noteMode = modeToggle->getToggleState(); repaint(); };

        auto* channelLabel = new juce::Label ({}, "Channel:");
        channelLabel->setBounds (10, 106, 55, 24);
        popup->addAndMakeVisible (channelLabel);

        auto* channelEditor = new juce::TextEditor();
        channelEditor->setBounds (65, 106, 50, 24);
        channelEditor->setInputRestrictions (2, "0123456789");
        channelEditor->setText (juce::String (cfg.midiChannel));
        channelEditor->onTextChange = [this, channelEditor] { cfg.midiChannel = juce::jlimit (1, 16, channelEditor->getText().getIntValue()); repaint(); };
        popup->addAndMakeVisible (channelEditor);

        juce::CallOutBox::launchAsynchronously (std::move (popup), getScreenBounds(), nullptr);
    }

    int slotIndex;
    SlotConfig& cfg;
    bool held = false;
    Listener* listener = nullptr;
};

//==============================================================================
// Main Component
class MainComponent : public juce::Component,
                      public juce::Timer,
                      public PluginXYPad::Listener,
                      public PluginAxisSlider::Listener,
                      public PluginFXButton::Listener
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

    // PluginXYPad::Listener
    void pluginXYPadDragged (float x, float y) override;

    // PluginAxisSlider::Listener
    void axisSliderDragged (bool isXAxis, float value) override;

    // PluginFXButton::Listener
    void pluginFXButtonDown (int slotIndex) override;
    void pluginFXButtonUp (int slotIndex) override;

private:
    void updateModeDisplay (int mode);
    void layoutFXPunchIn (juce::Rectangle<int> area);
    void layoutTransport (juce::Rectangle<int> area);
    void layoutMorfPad (juce::Rectangle<int> area);
    void layoutOmnichord (juce::Rectangle<int> area);
    void layoutDualMPad (juce::Rectangle<int> area);
    void layoutMorfChord (juce::Rectangle<int> area);

    MidiMorfEngine engine;
    int displayedMode = -1;  // Track which mode is currently laid out

    void showTiltSettingsPopup();

    // Header
    juce::Label titleLabel;
    juce::Label statusLabel;
    juce::Label modeLabel;
    juce::TextButton settingsButton;

    // FX Punch-in components
    PluginXYPad xyPad;
    PluginAxisSlider ySlider;
    PluginAxisSlider xSlider;
    juce::OwnedArray<PluginFXButton> fxButtons;

    // Transport components
    juce::OwnedArray<juce::TextButton> transportButtons;

    // Morf-Pad components (mirror display)
    juce::Label morfPadScaleLabel;
    juce::Label morfPadNoteLabel;

    // Strum-Chord components
    juce::Label omnichordChordLabel;
    juce::Label omnichordStrumLabel;
    juce::Label strumChordInfoLabel;
    juce::Label strumChordChordChLabel;
    juce::Label strumChordStrumChLabel;
    juce::TextButton strumChordChordChDown, strumChordChordChUp;
    juce::TextButton strumChordStrumChDown, strumChordStrumChUp;

    // Morf-Chord components
    juce::Label morfChordInfoLabel;
    juce::Label morfChordChordChLabel;
    juce::Label morfChordStrumChLabel;
    juce::TextButton morfChordChordChDown, morfChordChordChUp;
    juce::TextButton morfChordStrumChDown, morfChordStrumChUp;

    // Dual M-Pad components
    juce::Label dualMPadLeftLabel;
    juce::Label dualMPadRightLabel;
    juce::Label dualMPadLeftChLabel;
    juce::Label dualMPadRightChLabel;
    juce::TextButton dualMPadLeftChDown, dualMPadLeftChUp;
    juce::TextButton dualMPadRightChDown, dualMPadRightChUp;

    // Content area for layout
    juce::Rectangle<int> contentArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
