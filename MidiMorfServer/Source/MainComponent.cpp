#include "MainComponent.h"

static const juce::String modeNames[] = { "Morf FX", "Morf-Pad", "Strum-Chord", "Transport", "Dual M-Pad", "Morf-Chord" };
static const juce::String transportNames[] = { "Play", "Stop", "Record", "Rewind", "FF", "Loop" };

MainComponent::MainComponent()
    : xyPad (engine),
      ySlider (false, "Y", engine.getSlotConfigs()[0].ccXY_Y),
      xSlider (true,  "X", engine.getSlotConfigs()[0].ccXY_X)
{
    // Title
    titleLabel.setText ("MIDI-MORF", juce::dontSendNotification);
    titleLabel.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    titleLabel.setJustificationType (juce::Justification::centredLeft);
    titleLabel.setColour (juce::Label::textColourId, MidiMorfColors::textDark);
    addAndMakeVisible (titleLabel);

    // Mode label
    modeLabel.setText ("Morf FX", juce::dontSendNotification);
    modeLabel.setJustificationType (juce::Justification::centred);
    modeLabel.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    modeLabel.setColour (juce::Label::textColourId, MidiMorfColors::accent);
    addAndMakeVisible (modeLabel);

    // Settings button
    settingsButton.setButtonText ("Settings");
    settingsButton.setColour (juce::TextButton::buttonColourId, MidiMorfColors::buttonFace);
    settingsButton.setColour (juce::TextButton::textColourOffId, MidiMorfColors::textDark);
    settingsButton.onClick = [this] { showTiltSettingsPopup(); };
    addAndMakeVisible (settingsButton);

    // Status
    statusLabel.setText ("Searching...", juce::dontSendNotification);
    statusLabel.setJustificationType (juce::Justification::centredRight);
    statusLabel.setFont (juce::FontOptions (12.0f));
    statusLabel.setColour (juce::Label::textColourId, MidiMorfColors::statusAmber);
    addAndMakeVisible (statusLabel);

    // FX Punch-in controls
    xyPad.setListener (this);
    addAndMakeVisible (xyPad);

    ySlider.setListener (this);
    addAndMakeVisible (ySlider);

    xSlider.setListener (this);
    addAndMakeVisible (xSlider);

    auto& configs = engine.getSlotConfigs();
    for (int i = 0; i < MidiMorf::NUM_SLOTS; ++i)
    {
        auto* btn = new PluginFXButton (i, configs[(size_t) i]);
        btn->setListener (this);
        fxButtons.add (btn);
        addAndMakeVisible (btn);
    }

    // Transport buttons (created but hidden initially)
    for (int i = 0; i < 6; ++i)
    {
        auto* btn = new juce::TextButton (transportNames[i]);
        btn->setColour (juce::TextButton::buttonColourId, MidiMorfColors::buttonFace);
        btn->setColour (juce::TextButton::textColourOffId, MidiMorfColors::textDark);
        btn->setEnabled (false);  // Display only on server
        transportButtons.add (btn);
        addChildComponent (btn);  // Hidden initially
    }

    // Morf-Pad info labels
    morfPadScaleLabel.setText ("Scale: Major  Root: C  Oct: 2", juce::dontSendNotification);
    morfPadScaleLabel.setJustificationType (juce::Justification::centred);
    morfPadScaleLabel.setFont (juce::FontOptions (13.0f));
    morfPadScaleLabel.setColour (juce::Label::textColourId, MidiMorfColors::textDark);
    addChildComponent (morfPadScaleLabel);

    morfPadNoteLabel.setText ("Touch pad on phone to play", juce::dontSendNotification);
    morfPadNoteLabel.setJustificationType (juce::Justification::centred);
    morfPadNoteLabel.setFont (juce::FontOptions (11.0f));
    morfPadNoteLabel.setColour (juce::Label::textColourId, MidiMorfColors::textDark.withAlpha (0.6f));
    addChildComponent (morfPadNoteLabel);

    // Omnichord info labels
    omnichordChordLabel.setText ("No chord selected", juce::dontSendNotification);
    omnichordChordLabel.setJustificationType (juce::Justification::centred);
    omnichordChordLabel.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    omnichordChordLabel.setColour (juce::Label::textColourId, MidiMorfColors::textDark);
    addChildComponent (omnichordChordLabel);

    omnichordStrumLabel.setText ("Strum plate active on phone", juce::dontSendNotification);
    omnichordStrumLabel.setJustificationType (juce::Justification::centred);
    omnichordStrumLabel.setFont (juce::FontOptions (11.0f));
    omnichordStrumLabel.setColour (juce::Label::textColourId, MidiMorfColors::textDark.withAlpha (0.6f));
    addChildComponent (omnichordStrumLabel);

    // Strum-Chord channel config
    strumChordInfoLabel.setText ("Strum-Chord - chord grid + strum plate", juce::dontSendNotification);
    strumChordInfoLabel.setJustificationType (juce::Justification::centred);
    strumChordInfoLabel.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    strumChordInfoLabel.setColour (juce::Label::textColourId, MidiMorfColors::textDark);
    addChildComponent (strumChordInfoLabel);

    strumChordChordChLabel.setText ("Chord Ch 2", juce::dontSendNotification);
    strumChordChordChLabel.setJustificationType (juce::Justification::centred);
    strumChordChordChLabel.setFont (juce::FontOptions (13.0f));
    strumChordChordChLabel.setColour (juce::Label::textColourId, MidiMorfColors::accent);
    addChildComponent (strumChordChordChLabel);

    strumChordStrumChLabel.setText ("Strum Ch 2", juce::dontSendNotification);
    strumChordStrumChLabel.setJustificationType (juce::Justification::centred);
    strumChordStrumChLabel.setFont (juce::FontOptions (13.0f));
    strumChordStrumChLabel.setColour (juce::Label::textColourId, MidiMorfColors::accent);
    addChildComponent (strumChordStrumChLabel);

    // Helper for channel +/- buttons (used by Morf-Chord, Dual M-Pad, Strum-Chord)
    auto makeChBtn = [] (juce::TextButton& btn, const juce::String& text) {
        btn.setButtonText (text);
        btn.setColour (juce::TextButton::buttonColourId, MidiMorfColors::buttonFace);
        btn.setColour (juce::TextButton::textColourOffId, MidiMorfColors::textDark);
    };

    // Morf-Chord info/channel config
    morfChordInfoLabel.setText ("Morf-Chord - diatonic chord controller", juce::dontSendNotification);
    morfChordInfoLabel.setJustificationType (juce::Justification::centred);
    morfChordInfoLabel.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    morfChordInfoLabel.setColour (juce::Label::textColourId, MidiMorfColors::textDark);
    addChildComponent (morfChordInfoLabel);

    morfChordChordChLabel.setText ("Chord Ch 5", juce::dontSendNotification);
    morfChordChordChLabel.setJustificationType (juce::Justification::centred);
    morfChordChordChLabel.setFont (juce::FontOptions (13.0f));
    morfChordChordChLabel.setColour (juce::Label::textColourId, MidiMorfColors::accent);
    addChildComponent (morfChordChordChLabel);

    morfChordStrumChLabel.setText ("Strum Ch 6", juce::dontSendNotification);
    morfChordStrumChLabel.setJustificationType (juce::Justification::centred);
    morfChordStrumChLabel.setFont (juce::FontOptions (13.0f));
    morfChordStrumChLabel.setColour (juce::Label::textColourId, MidiMorfColors::accent);
    addChildComponent (morfChordStrumChLabel);

    makeChBtn (morfChordChordChDown, "-");
    makeChBtn (morfChordChordChUp, "+");
    makeChBtn (morfChordStrumChDown, "-");
    makeChBtn (morfChordStrumChUp, "+");

    morfChordChordChDown.onClick = [this] {
        auto& h = engine.getMorfChordHandler();
        int ch = h.getChordChannel();
        if (ch > 2) { h.setChordChannel (ch - 1); morfChordChordChLabel.setText ("Chord Ch " + juce::String (ch - 1), juce::dontSendNotification); }
    };
    morfChordChordChUp.onClick = [this] {
        auto& h = engine.getMorfChordHandler();
        int ch = h.getChordChannel();
        if (ch < 16) { h.setChordChannel (ch + 1); morfChordChordChLabel.setText ("Chord Ch " + juce::String (ch + 1), juce::dontSendNotification); }
    };
    morfChordStrumChDown.onClick = [this] {
        auto& h = engine.getMorfChordHandler();
        int ch = h.getStrumChannel();
        if (ch > 2) { h.setStrumChannel (ch - 1); morfChordStrumChLabel.setText ("Strum Ch " + juce::String (ch - 1), juce::dontSendNotification); }
    };
    morfChordStrumChUp.onClick = [this] {
        auto& h = engine.getMorfChordHandler();
        int ch = h.getStrumChannel();
        if (ch < 16) { h.setStrumChannel (ch + 1); morfChordStrumChLabel.setText ("Strum Ch " + juce::String (ch + 1), juce::dontSendNotification); }
    };

    addChildComponent (morfChordChordChDown);
    addChildComponent (morfChordChordChUp);
    addChildComponent (morfChordStrumChDown);
    addChildComponent (morfChordStrumChUp);

    // Strum-Chord channel buttons
    makeChBtn (strumChordChordChDown, "-");
    makeChBtn (strumChordChordChUp, "+");
    makeChBtn (strumChordStrumChDown, "-");
    makeChBtn (strumChordStrumChUp, "+");

    strumChordChordChDown.onClick = [this] {
        auto& h = engine.getOmnichordHandler();
        int ch = h.getChordChannel();
        if (ch > 2) { h.setChordChannel (ch - 1); strumChordChordChLabel.setText ("Chord Ch " + juce::String (ch - 1), juce::dontSendNotification); }
    };
    strumChordChordChUp.onClick = [this] {
        auto& h = engine.getOmnichordHandler();
        int ch = h.getChordChannel();
        if (ch < 16) { h.setChordChannel (ch + 1); strumChordChordChLabel.setText ("Chord Ch " + juce::String (ch + 1), juce::dontSendNotification); }
    };
    strumChordStrumChDown.onClick = [this] {
        auto& h = engine.getOmnichordHandler();
        int ch = h.getStrumChannel();
        if (ch > 2) { h.setStrumChannel (ch - 1); strumChordStrumChLabel.setText ("Strum Ch " + juce::String (ch - 1), juce::dontSendNotification); }
    };
    strumChordStrumChUp.onClick = [this] {
        auto& h = engine.getOmnichordHandler();
        int ch = h.getStrumChannel();
        if (ch < 16) { h.setStrumChannel (ch + 1); strumChordStrumChLabel.setText ("Strum Ch " + juce::String (ch + 1), juce::dontSendNotification); }
    };

    addChildComponent (strumChordChordChDown);
    addChildComponent (strumChordChordChUp);
    addChildComponent (strumChordStrumChDown);
    addChildComponent (strumChordStrumChUp);

    // Dual M-Pad channel config
    dualMPadLeftLabel.setText ("Pad L", juce::dontSendNotification);
    dualMPadLeftLabel.setJustificationType (juce::Justification::centred);
    dualMPadLeftLabel.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    dualMPadLeftLabel.setColour (juce::Label::textColourId, MidiMorfColors::textDark);
    addChildComponent (dualMPadLeftLabel);

    dualMPadRightLabel.setText ("Pad R", juce::dontSendNotification);
    dualMPadRightLabel.setJustificationType (juce::Justification::centred);
    dualMPadRightLabel.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    dualMPadRightLabel.setColour (juce::Label::textColourId, MidiMorfColors::textDark);
    addChildComponent (dualMPadRightLabel);

    dualMPadLeftChLabel.setText ("Ch 3", juce::dontSendNotification);
    dualMPadLeftChLabel.setJustificationType (juce::Justification::centred);
    dualMPadLeftChLabel.setFont (juce::FontOptions (13.0f));
    dualMPadLeftChLabel.setColour (juce::Label::textColourId, MidiMorfColors::accent);
    addChildComponent (dualMPadLeftChLabel);

    dualMPadRightChLabel.setText ("Ch 4", juce::dontSendNotification);
    dualMPadRightChLabel.setJustificationType (juce::Justification::centred);
    dualMPadRightChLabel.setFont (juce::FontOptions (13.0f));
    dualMPadRightChLabel.setColour (juce::Label::textColourId, MidiMorfColors::accent);
    addChildComponent (dualMPadRightChLabel);

    makeChBtn (dualMPadLeftChDown, "-");
    makeChBtn (dualMPadLeftChUp, "+");
    makeChBtn (dualMPadRightChDown, "-");
    makeChBtn (dualMPadRightChUp, "+");

    dualMPadLeftChDown.onClick = [this] {
        auto& h = engine.getDualMPadHandler (0);
        int ch = h.getChannel();
        if (ch > 2) { h.setChannel (ch - 1); dualMPadLeftChLabel.setText ("Ch " + juce::String (ch - 1), juce::dontSendNotification); }
    };
    dualMPadLeftChUp.onClick = [this] {
        auto& h = engine.getDualMPadHandler (0);
        int ch = h.getChannel();
        if (ch < 16) { h.setChannel (ch + 1); dualMPadLeftChLabel.setText ("Ch " + juce::String (ch + 1), juce::dontSendNotification); }
    };
    dualMPadRightChDown.onClick = [this] {
        auto& h = engine.getDualMPadHandler (1);
        int ch = h.getChannel();
        if (ch > 2) { h.setChannel (ch - 1); dualMPadRightChLabel.setText ("Ch " + juce::String (ch - 1), juce::dontSendNotification); }
    };
    dualMPadRightChUp.onClick = [this] {
        auto& h = engine.getDualMPadHandler (1);
        int ch = h.getChannel();
        if (ch < 16) { h.setChannel (ch + 1); dualMPadRightChLabel.setText ("Ch " + juce::String (ch + 1), juce::dontSendNotification); }
    };

    addChildComponent (dualMPadLeftChDown);
    addChildComponent (dualMPadLeftChUp);
    addChildComponent (dualMPadRightChDown);
    addChildComponent (dualMPadRightChUp);

    startTimerHz (4);
    engine.start();

    setSize (480, 500);
}

MainComponent::~MainComponent()
{
    stopTimer();
    engine.stop();
}

void MainComponent::timerCallback()
{
    // Connection status
    auto& server = engine.getNetworkServer();
    if (server.isClientConnected())
    {
        statusLabel.setText ("Connected", juce::dontSendNotification);
        statusLabel.setColour (juce::Label::textColourId, MidiMorfColors::statusGreen);
    }
    else
    {
        statusLabel.setText ("Searching...", juce::dontSendNotification);
        statusLabel.setColour (juce::Label::textColourId, MidiMorfColors::statusAmber);
    }

    // Mode change detection
    int mode = engine.currentMode.load();
    if (mode != displayedMode)
        updateModeDisplay (mode);

    // FX-specific updates
    if (displayedMode == 0)
    {
        auto& configs = engine.getSlotConfigs();
        ySlider.setCCNumber (configs[0].ccXY_Y);
        xSlider.setCCNumber (configs[0].ccXY_X);
        ySlider.setValue (engine.currentY.load());
        xSlider.setValue (engine.currentX.load());
    }
}

void MainComponent::updateModeDisplay (int mode)
{
    // Hide all mode-specific components
    xyPad.setVisible (false);
    ySlider.setVisible (false);
    xSlider.setVisible (false);
    for (auto* btn : fxButtons)
        btn->setVisible (false);
    for (auto* btn : transportButtons)
        btn->setVisible (false);
    morfPadScaleLabel.setVisible (false);
    morfPadNoteLabel.setVisible (false);
    omnichordChordLabel.setVisible (false);
    omnichordStrumLabel.setVisible (false);
    strumChordInfoLabel.setVisible (false);
    strumChordChordChLabel.setVisible (false);
    strumChordStrumChLabel.setVisible (false);
    strumChordChordChDown.setVisible (false);
    strumChordChordChUp.setVisible (false);
    strumChordStrumChDown.setVisible (false);
    strumChordStrumChUp.setVisible (false);
    dualMPadLeftLabel.setVisible (false);
    dualMPadRightLabel.setVisible (false);
    dualMPadLeftChLabel.setVisible (false);
    dualMPadRightChLabel.setVisible (false);
    dualMPadLeftChDown.setVisible (false);
    dualMPadLeftChUp.setVisible (false);
    dualMPadRightChDown.setVisible (false);
    dualMPadRightChUp.setVisible (false);
    morfChordInfoLabel.setVisible (false);
    morfChordChordChLabel.setVisible (false);
    morfChordStrumChLabel.setVisible (false);
    morfChordChordChDown.setVisible (false);
    morfChordChordChUp.setVisible (false);
    morfChordStrumChDown.setVisible (false);
    morfChordStrumChUp.setVisible (false);

    displayedMode = mode;
    modeLabel.setText (modeNames[juce::jlimit (0, 5, mode)], juce::dontSendNotification);

    // Show and layout the active mode
    switch (mode)
    {
        case 0: layoutFXPunchIn (contentArea); break;
        case 1: layoutMorfPad (contentArea); break;
        case 2: layoutOmnichord (contentArea); break;
        case 3: layoutTransport (contentArea); break;
        case 4: layoutDualMPad (contentArea); break;
        case 5: layoutMorfChord (contentArea); break;
    }
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (MidiMorfColors::background);
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced (10);

    auto topBar = area.removeFromTop (26);
    titleLabel.setBounds (topBar.removeFromLeft (120));
    modeLabel.setBounds (topBar.removeFromLeft (120));
    statusLabel.setBounds (topBar.removeFromRight (100));
    settingsButton.setBounds (topBar.removeFromRight (70));

    area.removeFromTop (6);
    contentArea = area;

    // Re-layout the current mode
    if (displayedMode >= 0)
        updateModeDisplay (displayedMode);
    else
        updateModeDisplay (0);
}

void MainComponent::layoutFXPunchIn (juce::Rectangle<int> area)
{
    xyPad.setVisible (true);
    ySlider.setVisible (true);
    xSlider.setVisible (true);
    for (auto* btn : fxButtons)
        btn->setVisible (true);

    int gridH = area.getHeight() / 3;
    auto buttonArea = area.removeFromBottom (gridH);
    int rowH = (gridH - 6) / 2;
    int colW = (buttonArea.getWidth() - 6) / 2;

    fxButtons[0]->setBounds (buttonArea.getX(),              buttonArea.getY(),              colW, rowH);
    fxButtons[1]->setBounds (buttonArea.getX() + colW + 6,   buttonArea.getY(),              colW, rowH);
    fxButtons[2]->setBounds (buttonArea.getX(),              buttonArea.getY() + rowH + 6,   colW, rowH);
    fxButtons[3]->setBounds (buttonArea.getX() + colW + 6,   buttonArea.getY() + rowH + 6,   colW, rowH);

    area.removeFromBottom (6);

    auto xSliderArea = area.removeFromBottom (36);
    area.removeFromBottom (6);

    auto ySliderArea = area.removeFromLeft (50);
    area.removeFromLeft (6);

    xyPad.setLabel ("XY PAD");
    xyPad.setBounds (area);
    ySlider.setBounds (ySliderArea);
    xSlider.setBounds (xSliderArea);
}

void MainComponent::layoutTransport (juce::Rectangle<int> area)
{
    for (auto* btn : transportButtons)
        btn->setVisible (true);

    auto centered = area.reduced (20, 40);
    int gap = 8;
    int rowH = (centered.getHeight() - 2 * gap) / 3;
    int colW = (centered.getWidth() - gap) / 2;

    for (int i = 0; i < transportButtons.size(); ++i)
    {
        int row = i / 2;
        int col = i % 2;
        int x = centered.getX() + col * (colW + gap);
        int y = centered.getY() + row * (rowH + gap);
        transportButtons[i]->setBounds (x, y, colW, rowH);
    }
}

void MainComponent::layoutMorfPad (juce::Rectangle<int> area)
{
    xyPad.setVisible (true);
    morfPadScaleLabel.setVisible (true);
    morfPadNoteLabel.setVisible (true);

    morfPadScaleLabel.setBounds (area.removeFromTop (24));
    area.removeFromTop (4);
    morfPadNoteLabel.setBounds (area.removeFromBottom (20));
    area.removeFromBottom (4);

    xyPad.setLabel ("MORF-PAD");
    xyPad.setBounds (area);
}

void MainComponent::layoutOmnichord (juce::Rectangle<int> area)
{
    omnichordChordLabel.setVisible (true);
    omnichordStrumLabel.setVisible (true);
    strumChordInfoLabel.setVisible (true);
    strumChordChordChLabel.setVisible (true);
    strumChordStrumChLabel.setVisible (true);
    strumChordChordChDown.setVisible (true);
    strumChordChordChUp.setVisible (true);
    strumChordStrumChDown.setVisible (true);
    strumChordStrumChUp.setVisible (true);

    // Update labels from current state
    strumChordChordChLabel.setText ("Chord Ch " + juce::String (engine.getOmnichordHandler().getChordChannel()), juce::dontSendNotification);
    strumChordStrumChLabel.setText ("Strum Ch " + juce::String (engine.getOmnichordHandler().getStrumChannel()), juce::dontSendNotification);

    auto centered = area.reduced (20, 0);

    // Info label at top
    strumChordInfoLabel.setBounds (centered.removeFromTop (24));
    centered.removeFromTop (8);

    // Chord display
    omnichordChordLabel.setBounds (centered.removeFromTop (30));
    centered.removeFromTop (4);
    omnichordStrumLabel.setBounds (centered.removeFromTop (20));
    centered.removeFromTop (12);

    // Chord channel row
    auto chordChRow = centered.removeFromTop (28);
    int btnW = 28;
    strumChordChordChDown.setBounds (chordChRow.removeFromLeft (btnW));
    strumChordChordChUp.setBounds (chordChRow.removeFromRight (btnW));
    strumChordChordChLabel.setBounds (chordChRow);

    centered.removeFromTop (4);

    // Strum channel row
    auto strumChRow = centered.removeFromTop (28);
    strumChordStrumChDown.setBounds (strumChRow.removeFromLeft (btnW));
    strumChordStrumChUp.setBounds (strumChRow.removeFromRight (btnW));
    strumChordStrumChLabel.setBounds (strumChRow);
}

void MainComponent::layoutDualMPad (juce::Rectangle<int> area)
{
    dualMPadLeftLabel.setVisible (true);
    dualMPadRightLabel.setVisible (true);
    dualMPadLeftChLabel.setVisible (true);
    dualMPadRightChLabel.setVisible (true);
    dualMPadLeftChDown.setVisible (true);
    dualMPadLeftChUp.setVisible (true);
    dualMPadRightChDown.setVisible (true);
    dualMPadRightChUp.setVisible (true);

    // Update channel labels to current state
    dualMPadLeftChLabel.setText ("Ch " + juce::String (engine.getDualMPadHandler (0).getChannel()), juce::dontSendNotification);
    dualMPadRightChLabel.setText ("Ch " + juce::String (engine.getDualMPadHandler (1).getChannel()), juce::dontSendNotification);

    auto centered = area.reduced (20, 40);
    int halfW = centered.getWidth() / 2;
    int gap = 30;

    // Left pad config
    auto leftArea = centered.removeFromLeft (halfW - gap / 2);
    dualMPadLeftLabel.setBounds (leftArea.removeFromTop (28));
    leftArea.removeFromTop (8);
    auto leftChRow = leftArea.removeFromTop (28);
    int chBtnW = 30;
    dualMPadLeftChDown.setBounds (leftChRow.getX() + leftChRow.getWidth() / 2 - chBtnW - 30, leftChRow.getY(), chBtnW, 28);
    dualMPadLeftChLabel.setBounds (leftChRow.getX() + leftChRow.getWidth() / 2 - 28, leftChRow.getY(), 56, 28);
    dualMPadLeftChUp.setBounds (leftChRow.getX() + leftChRow.getWidth() / 2 + 30, leftChRow.getY(), chBtnW, 28);

    // Right pad config
    centered.removeFromLeft (gap);
    auto rightArea = centered;
    dualMPadRightLabel.setBounds (rightArea.removeFromTop (28));
    rightArea.removeFromTop (8);
    auto rightChRow = rightArea.removeFromTop (28);
    dualMPadRightChDown.setBounds (rightChRow.getX() + rightChRow.getWidth() / 2 - chBtnW - 30, rightChRow.getY(), chBtnW, 28);
    dualMPadRightChLabel.setBounds (rightChRow.getX() + rightChRow.getWidth() / 2 - 28, rightChRow.getY(), 56, 28);
    dualMPadRightChUp.setBounds (rightChRow.getX() + rightChRow.getWidth() / 2 + 30, rightChRow.getY(), chBtnW, 28);
}

void MainComponent::layoutMorfChord (juce::Rectangle<int> area)
{
    morfChordInfoLabel.setVisible (true);
    morfChordChordChLabel.setVisible (true);
    morfChordStrumChLabel.setVisible (true);
    morfChordChordChDown.setVisible (true);
    morfChordChordChUp.setVisible (true);
    morfChordStrumChDown.setVisible (true);
    morfChordStrumChUp.setVisible (true);

    // Update channel labels
    morfChordChordChLabel.setText ("Chord Ch " + juce::String (engine.getMorfChordHandler().getChordChannel()), juce::dontSendNotification);
    morfChordStrumChLabel.setText ("Strum Ch " + juce::String (engine.getMorfChordHandler().getStrumChannel()), juce::dontSendNotification);

    auto centered = area.reduced (20, 40);

    morfChordInfoLabel.setBounds (centered.removeFromTop (28));
    centered.removeFromTop (20);

    int chBtnW = 30;

    // Chord channel row
    auto chordRow = centered.removeFromTop (28);
    int cx = chordRow.getCentreX();
    morfChordChordChDown.setBounds (cx - chBtnW - 50, chordRow.getY(), chBtnW, 28);
    morfChordChordChLabel.setBounds (cx - 46, chordRow.getY(), 92, 28);
    morfChordChordChUp.setBounds (cx + 50, chordRow.getY(), chBtnW, 28);

    centered.removeFromTop (12);

    // Strum channel row
    auto strumRow = centered.removeFromTop (28);
    morfChordStrumChDown.setBounds (cx - chBtnW - 50, strumRow.getY(), chBtnW, 28);
    morfChordStrumChLabel.setBounds (cx - 46, strumRow.getY(), 92, 28);
    morfChordStrumChUp.setBounds (cx + 50, strumRow.getY(), chBtnW, 28);
}

void MainComponent::pluginXYPadDragged (float x, float y)
{
    engine.localXYUpdated (x, y);
}

void MainComponent::axisSliderDragged (bool isXAxis, float value)
{
    engine.localAxisUpdated (isXAxis, value);
}

void MainComponent::pluginFXButtonDown (int slotIndex)
{
    engine.localButtonDown (slotIndex);
}

void MainComponent::pluginFXButtonUp (int slotIndex)
{
    engine.localButtonUp (slotIndex);
}

void MainComponent::showTiltSettingsPopup()
{
    auto& tilt = engine.getGlobalTiltConfig();

    auto popup = std::make_unique<juce::Component>();
    popup->setSize (240, 210);

    int y = 10;

    auto* tiltXLabel = new juce::Label ({}, "Tilt CC X:");
    tiltXLabel->setBounds (10, y, 80, 24);
    popup->addAndMakeVisible (tiltXLabel);

    auto* tiltXEditor = new juce::TextEditor();
    tiltXEditor->setBounds (95, y, 50, 24);
    tiltXEditor->setInputRestrictions (3, "0123456789");
    tiltXEditor->setText (juce::String (tilt.ccTiltX));
    tiltXEditor->onTextChange = [&tilt, tiltXEditor] { tilt.ccTiltX = juce::jlimit (0, 127, tiltXEditor->getText().getIntValue()); };
    popup->addAndMakeVisible (tiltXEditor);

    auto* assignX = new juce::TextButton ("Assign X");
    assignX->setBounds (155, y, 75, 24);
    assignX->onClick = [this] { engine.sendTiltAssignBurst (true); };
    popup->addAndMakeVisible (assignX);

    y += 32;

    auto* tiltYLabel = new juce::Label ({}, "Tilt CC Y:");
    tiltYLabel->setBounds (10, y, 80, 24);
    popup->addAndMakeVisible (tiltYLabel);

    auto* tiltYEditor = new juce::TextEditor();
    tiltYEditor->setBounds (95, y, 50, 24);
    tiltYEditor->setInputRestrictions (3, "0123456789");
    tiltYEditor->setText (juce::String (tilt.ccTiltY));
    tiltYEditor->onTextChange = [&tilt, tiltYEditor] { tilt.ccTiltY = juce::jlimit (0, 127, tiltYEditor->getText().getIntValue()); };
    popup->addAndMakeVisible (tiltYEditor);

    auto* assignY = new juce::TextButton ("Assign Y");
    assignY->setBounds (155, y, 75, 24);
    assignY->onClick = [this] { engine.sendTiltAssignBurst (false); };
    popup->addAndMakeVisible (assignY);

    y += 32;

    auto* channelLabel = new juce::Label ({}, "Channel:");
    channelLabel->setBounds (10, y, 80, 24);
    popup->addAndMakeVisible (channelLabel);

    auto* channelEditor = new juce::TextEditor();
    channelEditor->setBounds (95, y, 50, 24);
    channelEditor->setInputRestrictions (2, "0123456789");
    channelEditor->setText (juce::String (tilt.midiChannel));
    channelEditor->onTextChange = [&tilt, channelEditor] { tilt.midiChannel = juce::jlimit (1, 16, channelEditor->getText().getIntValue()); };
    popup->addAndMakeVisible (channelEditor);

    y += 32;

    auto* alwaysOnToggle = new juce::ToggleButton ("Always On");
    alwaysOnToggle->setBounds (10, y, 200, 24);
    alwaysOnToggle->setToggleState (tilt.alwaysOn, juce::dontSendNotification);
    alwaysOnToggle->onClick = [&tilt, alwaysOnToggle] { tilt.alwaysOn = alwaysOnToggle->getToggleState(); };
    popup->addAndMakeVisible (alwaysOnToggle);

    juce::CallOutBox::launchAsynchronously (std::move (popup), settingsButton.getScreenBounds(), nullptr);
}
