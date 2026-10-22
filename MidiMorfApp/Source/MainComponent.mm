#include "MainComponent.h"

#if JUCE_IOS
 #import <UIKit/UIKit.h>
#endif

static const juce::String controllerNames[] = {
    "Morf FX",
    "Morf-Pad",
    "Strum-Chord",
    "Transport",
    "Dual M-Pad",
    "Morf-Chord",
    "Rose-Chord",
    "Step-Seq",
    "Slice-Seq"
};

// Guards the invariant that broke once already: NUM_CONTROLLERS is the index
// base for the synth/about/settings pages AND the bound on this array, so the
// two must not drift apart.
static_assert (juce::numElementsInArray (controllerNames) == MidiMorf::NUM_CONTROLLERS,
               "controllerNames must have exactly NUM_CONTROLLERS entries");

static const juce::String synthPageName    = "Synth";
static const juce::String wavetablePageName = "Wavetable";
static const juce::String settingsPageName = "Settings";
static const juce::String aboutPageName    = "About";

// Page indices after the controllers. Wavetable was added last so older saved
// indices still point at the same pages.
static constexpr int synthPageIdx     = MidiMorf::NUM_CONTROLLERS;
static constexpr int aboutPageIdx     = MidiMorf::NUM_CONTROLLERS + 1;
static constexpr int settingsPageIdx  = MidiMorf::NUM_CONTROLLERS + 2;
static constexpr int wavetablePageIdx = MidiMorf::NUM_CONTROLLERS + 3;
static constexpr int numPages         = MidiMorf::NUM_CONTROLLERS + 4;

MainComponent::MainComponent()
    : fxPage (eventRouter),
      morfPadPage (eventRouter),
      omnichordPage (eventRouter),
      transportPage (eventRouter),
      dualMPadPage (eventRouter),
      morfChordPage (eventRouter),
      roseChordPage (eventRouter),
      stepSequencerPage (eventRouter),
      sliceSequencerPage (eventRouter)
{
    // App properties for state persistence
    juce::PropertiesFile::Options options;
    options.applicationName = "MidiMorf";
    options.folderName = "MidiMorf";
    options.filenameSuffix = ".settings";
    options.osxLibrarySubFolder = "Application Support";
    appProperties.setStorageParameters (options);

    // Audio engine — defer init by a frame so the UI is fully mounted before
    // iOS negotiates the audio session. If the session can't be acquired (e.g.
    // another audio app holds it), the UI still shows and we silently retry on
    // the next foreground.
    juce::Timer::callAfterDelay (250, [this] { audioEngine.start(); });

    // Page container — controller pages, then Synth editor, then About.
    // Pages first so the floating menu button sits on top of them.
    pageContainer.addPage (&fxPage);
    pageContainer.addPage (&morfPadPage);
    pageContainer.addPage (&omnichordPage);
    pageContainer.addPage (&transportPage);
    pageContainer.addPage (&dualMPadPage);
    pageContainer.addPage (&morfChordPage);
    pageContainer.addPage (&roseChordPage);
    pageContainer.addPage (&stepSequencerPage);
    pageContainer.addPage (&sliceSequencerPage);
    pageContainer.addPage (&synthSettingsPage);
    pageContainer.addPage (&aboutPage);
    pageContainer.addPage (&settingsPage);
    pageContainer.addPage (&wavetablePage);
    pageContainer.setListener (this);
    addAndMakeVisible (pageContainer);

    // Settings page persists its mode change like everything else.
    settingsPage.onModeChanged = [this] { saveState(); };

    // Floating menu + synth shortcut (sit on top of the page, in the right gutter).
    menuButton.onClick = [this] { showNavigationMenu(); };
    addAndMakeVisible (menuButton);

    synthButton.onClick = [this]
    {
        // Opens the editor for whichever engine is currently playing.
        pageContainer.setPage (audioEngine.getActiveSynth().getEngine() == ActiveSynth::Wavetable
                                   ? wavetablePageIdx : synthPageIdx);
    };
    addAndMakeVisible (synthButton);

    // Favorite quick-jump button. Short tap navigates to the assigned page;
    // long-press opens a picker so the user can rebind it. Empty slot routes
    // a short tap to the picker as well, so the first interaction is useful.
    favoriteButton.onShortTap = [this]
    {
        int idx = favoriteButton.getAssignment();
        if (idx >= 0 && idx < pageContainer.getNumPages())
            pageContainer.setPage (idx);
    };
    favoriteButton.onLongPress = [this] { showFavoritePicker(); };
    addAndMakeVisible (favoriteButton);

    synthSettingsPage.onPatchChanged = [this] { saveState(); };
    wavetablePage.onPatchChanged = [this] { saveState(); };

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

    // Landscape initial size hint
    setSize (800, 400);
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
    // Wood-grain back panel — fills the whole window, but the page container
    // covers the centre so it's only visible in the safe-area gutters
    // (rounded corners + Dynamic Island side in landscape).
    auto wood = juce::ImageCache::getFromMemory (BinaryData::wood2_jpg,
                                                  BinaryData::wood2_jpgSize);
    if (! wood.isNull())
    {
        g.drawImage (wood, getLocalBounds().toFloat(),
                     juce::RectanglePlacement::fillDestination);
        // Slight darkening overlay so the texture reads as a "panel" rather
        // than a stark photo — keeps focus on the controls.
        g.setColour (juce::Colour (0x33000000));
        g.fillAll();
    }
    else
    {
        g.fillAll (MidiMorfColors::background);
    }
}

void MainComponent::resized()
{
    auto area = getLocalBounds();

    // Floating-button geometry needs to be in scope for both the page trim
    // (so we never let page content land under the buttons) and the button
    // layout below.
    const int btnW          = 56;
    const int floatingPadR  = 4;            // gap between button right edge and screen
    const int floatingGapLR = 4;            // gap between button left edge and page right
    const int rightGutter   = btnW + floatingPadR + floatingGapLR;   // 64

#if JUCE_IOS
    // iPad has tiny rounded-corner safe-area insets — page content would
    // slide right under the floating Menu / Synth / Favourite buttons (and
    // cover things like the right-edge strum bar on Strum-Chord and
    // Morf-Chord) unless we floor the trim at the button gutter. iPhone with
    // Dynamic Island already gives ~44pt of inset which is enough; iPhone
    // landscape is also tight on horizontal real estate so we don't want to
    // widen the wood frame there unnecessarily.
    const bool isIPad = ([UIDevice currentDevice].userInterfaceIdiom == UIUserInterfaceIdiomPad);

    if (auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
    {
        auto insets = display->safeAreaInsets;
        const int hMax      = juce::jmax (insets.getLeft(), insets.getRight());
        const int extraPad  = 8;
        const int safeTrim  = hMax + extraPad;
        const int trim      = isIPad ? juce::jmax (safeTrim, rightGutter)
                                     : safeTrim;
        area = area.withTrimmedLeft  (trim)
                   .withTrimmedRight (trim);
    }
    else if (isIPad)
    {
        area = area.withTrimmedLeft  (rightGutter)
                   .withTrimmedRight (rightGutter);
    }
#endif

    // PageContainer fills the safe area so page content never lands behind a
    // rounded corner or under the Dynamic Island.
    pageContainer.setBounds (area);

    // The menu button intentionally lives OUTSIDE the safe area, in the
    // rounded-corner gutter on the right edge — that's the dead space JB
    // wants reclaimed. It uses the full screen rect, not the safe-area one.
    auto full = getLocalBounds();
    const int btnH = 60;
    const int gap  = 6;
    int x = full.getRight() - btnW - 4;
    menuButton.setBounds     (x, full.getY() + 36,                       btnW, btnH);
    synthButton.setBounds    (x, full.getY() + 36 +     (btnH + gap),    btnW, btnH);
    favoriteButton.setBounds (x, full.getY() + 36 + 2 * (btnH + gap),    btnW, btnH);
}

void MainComponent::showNavigationMenu()
{
    juce::PopupMenu menu;

    // Controller pages
    for (int i = 0; i < MidiMorf::NUM_CONTROLLERS; ++i)
    {
        bool isCurrent = (i == pageContainer.getCurrentPage());
        menu.addItem (i + 1, controllerNames[i], true, isCurrent);
    }

    // Synth, Settings, About
    menu.addSeparator();
    const int current = pageContainer.getCurrentPage();
    menu.addItem (synthPageIdx     + 1, synthPageName,     true, current == synthPageIdx);
    menu.addItem (wavetablePageIdx + 1, wavetablePageName, true, current == wavetablePageIdx);
    menu.addItem (settingsPageIdx  + 1, settingsPageName,  true, current == settingsPageIdx);
    menu.addItem (aboutPageIdx     + 1, aboutPageName,     true, current == aboutPageIdx);

    menu.showMenuAsync (juce::PopupMenu::Options()
                            .withTargetComponent (&menuButton)
                            .withStandardItemHeight (36),
        [this] (int result)
        {
            if (result <= 0) return;
            pageContainer.setPage (result - 1);
        });
}

juce::String MainComponent::getPageDisplayName (int pageIndex) const
{
    if (pageIndex >= 0 && pageIndex < MidiMorf::NUM_CONTROLLERS)
        return controllerNames[pageIndex];
    if (pageIndex == synthPageIdx)     return synthPageName;
    if (pageIndex == aboutPageIdx)     return aboutPageName;
    if (pageIndex == settingsPageIdx)  return settingsPageName;
    if (pageIndex == wavetablePageIdx) return wavetablePageName;
    return {};
}

void MainComponent::showFavoritePicker()
{
    juce::PopupMenu menu;
    menu.addItem (juce::PopupMenu::Item ("Set favorite to...").setEnabled (false));
    menu.addSeparator();

    const int currentAssignment = favoriteButton.getAssignment();
    for (int i = 0; i < MidiMorf::NUM_CONTROLLERS; ++i)
        menu.addItem (i + 1, controllerNames[i], true, i == currentAssignment);

    menu.addSeparator();
    menu.addItem (synthPageIdx     + 1, synthPageName,     true, synthPageIdx     == currentAssignment);
    menu.addItem (wavetablePageIdx + 1, wavetablePageName, true, wavetablePageIdx == currentAssignment);
    menu.addItem (settingsPageIdx  + 1, settingsPageName,  true, settingsPageIdx  == currentAssignment);
    menu.addItem (aboutPageIdx     + 1, aboutPageName,     true, aboutPageIdx     == currentAssignment);

    menu.showMenuAsync (juce::PopupMenu::Options()
                            .withTargetComponent (&favoriteButton)
                            .withStandardItemHeight (36),
        [this] (int result)
        {
            if (result <= 0) return;
            const int idx = result - 1;
            favoriteButton.setAssignment (idx, getPageDisplayName (idx));
            saveState();
        });
}

// NetworkClient::Listener
void MainComponent::connectedToPlugin (const juce::String& hostName)
{
    juce::MessageManager::callAsync ([this, hostName]()
    {
        connected = true;
        connectedHost = hostName;
        settingsPage.setConnectionStatus (connected, connectedHost);
    });
}

void MainComponent::disconnectedFromPlugin()
{
    juce::MessageManager::callAsync ([this]()
    {
        connected = false;
        connectedHost.clear();
        settingsPage.setConnectionStatus (connected, connectedHost);
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
    // Local modulation: accel feeds the routing matrix's AccelX/AccelY sources.
    audioEngine.getActiveSynth().setAccelX (tiltX);
    audioEngine.getActiveSynth().setAccelY (tiltY);
}

// PageContainer::Listener
void MainComponent::pageChanged (int newPageIndex)
{
    if (newPageIndex < MidiMorf::NUM_CONTROLLERS)
    {
        auto mode = static_cast<MidiMorf::ControllerMode> (newPageIndex);
        eventRouter.sendScreenMode (mode);

        // The server keeps no per-page memory, so the sequencers have to
        // resend their whole state whenever their page comes back into focus.
        if (mode == MidiMorf::ControllerMode::StepSequencer)
            stepSequencerPage.pushFullState();
        else if (mode == MidiMorf::ControllerMode::SliceSequencer)
            sliceSequencerPage.pushFullState();
    }

    // Opening a synth editor makes that engine the one the controllers play.
    if (newPageIndex == synthPageIdx)
        audioEngine.getActiveSynth().setEngine (ActiveSynth::Classic);
    else if (newPageIndex == wavetablePageIdx)
        audioEngine.getActiveSynth().setEngine (ActiveSynth::Wavetable);

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
        props->setValue ("favorite_0", favoriteButton.getAssignment());
        morfPadPage.saveState (*props);
        dualMPadPage.saveState (*props);
        morfChordPage.saveState (*props);
        roseChordPage.saveState (*props);
        synthSettingsPage.saveState (*props);
        wavetablePage.saveState (*props);
        props->setValue ("synthEngine", audioEngine.getActiveSynth().getEngine());
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
        wavetablePage.restoreState (*props);
        audioEngine.getActiveSynth().setEngine (props->getIntValue ("synthEngine", ActiveSynth::Classic));

        // First-launch default: Local mode. New users hear the built-in
        // synth immediately when they tap a controller — no server setup
        // needed for a satisfying first-play. Existing users' saved mode
        // still wins because it's read from the same key.
        int savedMode = props->getIntValue ("mode", 1);
        eventRouter.setMode (savedMode == 1 ? EventRouter::Mode::Local : EventRouter::Mode::WiFi);
        settingsPage.refreshFromRouter();

        int favIdx = props->getIntValue ("favorite_0", -1);
        if (favIdx >= 0 && favIdx < numPages)
            favoriteButton.setAssignment (favIdx, getPageDisplayName (favIdx));

        // First-launch default: Morf-Chord (index 5). It's synth-capable so
        // Local mode plays immediately, and full chords make a stronger first
        // impression than single notes. Existing users' saved page still wins.
        int lastPage = props->getIntValue ("lastPage", 5);
        if (lastPage >= 0 && lastPage < numPages)
            pageContainer.setPage (lastPage);
    }
}

void MainComponent::seqPlayheadChanged (int step)
{
    // The server only streams the playhead for the page it thinks the phone
    // is on; applying it to both pages is harmless and keeps this simple.
    stepSequencerPage.setPlayhead (step);
    sliceSequencerPage.setPlayhead (step);
}
