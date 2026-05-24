#include "MainComponent.h"

static const juce::String controllerNames[] = {
    "Morf FX",
    "Morf-Pad",
    "Strum-Chord",
    "Transport",
    "Dual M-Pad",
    "Morf-Chord",
    "Rose-Chord"
};

static const juce::String synthPageName  = "Synth";
static const juce::String aboutPageName = "About";

MainComponent::MainComponent()
    : fxPage (eventRouter),
      morfPadPage (eventRouter),
      omnichordPage (eventRouter),
      transportPage (eventRouter),
      dualMPadPage (eventRouter),
      morfChordPage (eventRouter),
      roseChordPage (eventRouter)
{
    // App properties for state persistence
    juce::PropertiesFile::Options options;
    options.applicationName = "MidiMorf";
    options.folderName = "MidiMorf";
    options.filenameSuffix = ".settings";
    options.osxLibrarySubFolder = "Application Support";
    appProperties.setStorageParameters (options);

    // Audio engine — start first so the device is ready before any UI plays notes
    audioEngine.start();

    // Hamburger menu button
    menuButton.setButtonText ("=");
    menuButton.setColour (juce::TextButton::buttonColourId, MidiMorfColors::buttonFace);
    menuButton.setColour (juce::TextButton::textColourOffId, MidiMorfColors::textDark);
    menuButton.onClick = [this] { showNavigationMenu(); };
    addAndMakeVisible (menuButton);

    // Mode toggle (WiFi ↔ Local) — synth-capable controllers obey it
    modeToggleButton.onClick = [this]
    {
        auto next = (eventRouter.getMode() == EventRouter::Mode::WiFi)
                        ? EventRouter::Mode::Local
                        : EventRouter::Mode::WiFi;
        eventRouter.setMode (next);
        updateModeToggleAppearance();
        saveState();
    };
    addAndMakeVisible (modeToggleButton);
    updateModeToggleAppearance();

    // Mode label
    modeLabel.setText ("Morf FX", juce::dontSendNotification);
    modeLabel.setJustificationType (juce::Justification::centredLeft);
    modeLabel.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    modeLabel.setColour (juce::Label::textColourId, MidiMorfColors::textDark);
    addAndMakeVisible (modeLabel);

    // Status label
    statusLabel.setText ("Searching...", juce::dontSendNotification);
    statusLabel.setJustificationType (juce::Justification::centredRight);
    statusLabel.setColour (juce::Label::textColourId, MidiMorfColors::statusAmber);
    statusLabel.setFont (juce::FontOptions (11.0f));
    addAndMakeVisible (statusLabel);

    // Page container — controller pages, then Synth editor, then About
    pageContainer.addPage (&fxPage);
    pageContainer.addPage (&morfPadPage);
    pageContainer.addPage (&omnichordPage);
    pageContainer.addPage (&transportPage);
    pageContainer.addPage (&dualMPadPage);
    pageContainer.addPage (&morfChordPage);
    pageContainer.addPage (&roseChordPage);
    pageContainer.addPage (&synthSettingsPage);
    pageContainer.addPage (&aboutPage);
    pageContainer.setListener (this);
    addAndMakeVisible (pageContainer);

    synthSettingsPage.onPatchChanged = [this] { saveState(); };

    // Network
    networkClient.setListener (this);
    networkClient.startSearching();

    // Accelerometer
    accelerometer.setListener (this);
    accelerometer.start();

    // Restore saved state (last page, scale settings, etc.)
    restoreState();

    // Ping timer
    startTimerHz (2);

    setSize (400, 700);
}

MainComponent::~MainComponent()
{
    saveState();
    stopTimer();
    accelerometer.stop();
    networkClient.setListener (nullptr);
    networkClient.stopSearching();
    audioEngine.stop();
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (MidiMorfColors::background);
}

void MainComponent::resized()
{
    auto area = getLocalBounds();

    // Safe area padding for iOS notch/home indicator
    area.removeFromTop (50);
    area.removeFromBottom (34);
    area = area.reduced (8, 0);

    // Top bar: hamburger | mode label | mode toggle | status
    auto topBar = area.removeFromTop (40);
    menuButton.setBounds (topBar.removeFromLeft (50));
    topBar.removeFromLeft (8);
    statusLabel.setBounds (topBar.removeFromRight (90));
    topBar.removeFromRight (4);
    modeToggleButton.setBounds (topBar.removeFromRight (70));
    topBar.removeFromRight (6);
    modeLabel.setBounds (topBar);

    area.removeFromTop (4);

    // Page container fills the rest
    pageContainer.setBounds (area);
}

void MainComponent::showNavigationMenu()
{
    juce::PopupMenu menu;

    for (int i = 0; i < MidiMorf::NUM_CONTROLLERS; ++i)
    {
        bool isCurrent = (i == pageContainer.getCurrentPage());
        menu.addItem (i + 1, controllerNames[i], true, isCurrent);
    }

    menu.addSeparator();
    int synthIdx = MidiMorf::NUM_CONTROLLERS;
    int aboutIdx = MidiMorf::NUM_CONTROLLERS + 1;
    menu.addItem (synthIdx + 1, synthPageName, true, pageContainer.getCurrentPage() == synthIdx);
    menu.addItem (aboutIdx + 1, aboutPageName, true, pageContainer.getCurrentPage() == aboutIdx);

    menu.showMenuAsync (juce::PopupMenu::Options()
                            .withTargetComponent (&menuButton)
                            .withStandardItemHeight (44),
        [this] (int result)
        {
            if (result > 0)
                pageContainer.setPage (result - 1);
        });
}

// NetworkClient::Listener
void MainComponent::connectedToPlugin (const juce::String& hostName)
{
    juce::MessageManager::callAsync ([this, hostName]()
    {
        statusLabel.setText ("Connected", juce::dontSendNotification);
        statusLabel.setColour (juce::Label::textColourId, MidiMorfColors::statusGreen);
    });
}

void MainComponent::disconnectedFromPlugin()
{
    juce::MessageManager::callAsync ([this]()
    {
        statusLabel.setText ("Searching...", juce::dontSendNotification);
        statusLabel.setColour (juce::Label::textColourId, MidiMorfColors::statusAmber);
    });
}

void MainComponent::slotConfigReceived (int slotIndex, const juce::String& name)
{
    juce::MessageManager::callAsync ([this, slotIndex, name]()
    {
        fxPage.setButtonLabel (slotIndex, name);
    });
}

// AccelerometerManager::Listener
void MainComponent::tiltUpdated (float tiltX, float tiltY)
{
    eventRouter.sendTiltUpdate (tiltX, tiltY);
}

// PageContainer::Listener
void MainComponent::pageChanged (int newPageIndex)
{
    if (newPageIndex < MidiMorf::NUM_CONTROLLERS)
    {
        auto mode = static_cast<MidiMorf::ControllerMode> (newPageIndex);
        eventRouter.sendScreenMode (mode);
        modeLabel.setText (controllerNames[newPageIndex], juce::dontSendNotification);
    }
    else if (newPageIndex == MidiMorf::NUM_CONTROLLERS)
    {
        modeLabel.setText (synthPageName, juce::dontSendNotification);
    }
    else
    {
        modeLabel.setText (aboutPageName, juce::dontSendNotification);
    }

    // Persist current state whenever page changes
    saveState();
}

// Timer
void MainComponent::timerCallback()
{
    // Keep-alive — handled by network layer
}

// State persistence
void MainComponent::saveState()
{
    if (auto* props = appProperties.getUserSettings())
    {
        props->setValue ("lastPage", pageContainer.getCurrentPage());
        props->setValue ("mode", eventRouter.getMode() == EventRouter::Mode::Local ? 1 : 0);
        morfPadPage.saveState (*props);
        dualMPadPage.saveState (*props);
        morfChordPage.saveState (*props);
        roseChordPage.saveState (*props);
        synthSettingsPage.saveState (*props);
        props->saveIfNeeded();
    }
}

void MainComponent::restoreState()
{
    if (auto* props = appProperties.getUserSettings())
    {
        morfPadPage.restoreState (*props);
        dualMPadPage.restoreState (*props);
        morfChordPage.restoreState (*props);
        roseChordPage.restoreState (*props);
        synthSettingsPage.restoreState (*props);

        int savedMode = props->getIntValue ("mode", 0);
        eventRouter.setMode (savedMode == 1 ? EventRouter::Mode::Local : EventRouter::Mode::WiFi);
        updateModeToggleAppearance();

        int lastPage = props->getIntValue ("lastPage", 0);
        if (lastPage >= 0 && lastPage < MidiMorf::NUM_CONTROLLERS + 2)  // controllers + synth + about
            pageContainer.setPage (lastPage);
    }
}

void MainComponent::updateModeToggleAppearance()
{
    bool local = (eventRouter.getMode() == EventRouter::Mode::Local);
    modeToggleButton.setButtonText (local ? "Local" : "WiFi");
    modeToggleButton.setColour (juce::TextButton::buttonColourId,
                                 local ? MidiMorfColors::accent : MidiMorfColors::buttonFace);
    modeToggleButton.setColour (juce::TextButton::textColourOffId,
                                 local ? MidiMorfColors::textLight : MidiMorfColors::textDark);
}
