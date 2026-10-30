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

static const juce::String synthPageName    = "Sound";
static const juce::String settingsPageName = "Settings";
static const juce::String aboutPageName    = "About";

// Page indices after the controllers.
static constexpr int synthPageIdx     = MidiMorf::NUM_CONTROLLERS;
static constexpr int aboutPageIdx     = MidiMorf::NUM_CONTROLLERS + 1;
static constexpr int settingsPageIdx  = MidiMorf::NUM_CONTROLLERS + 2;
static constexpr int numPages         = MidiMorf::NUM_CONTROLLERS + 3;

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
    pageContainer.addPage (&synthPage);
    pageContainer.addPage (&aboutPage);
    pageContainer.addPage (&settingsPage);
    pageContainer.setListener (this);
    addAndMakeVisible (pageContainer);

    // Settings page persists its mode change like everything else.
    settingsPage.onModeChanged = [this] { saveState(); };
    settingsPage.getAudioLoad  = [this] { return audioEngine.getCpuUsage(); };

    // Floating menu + synth shortcut (sit on top of the page, in the right gutter).
    menuButton.onClick = [this] { showNavigationMenu(); };
    addAndMakeVisible (menuButton);

    synthButton.onClick = [this] { openSound(); };
    addAndMakeVisible (synthButton);

    trackBadge.onClick = [this] { showTrackPicker(); };
    addAndMakeVisible (trackBadge);

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

    synthPage.onPatchChanged = [this] { saveState(); };
    transportPage.onStateChanged = [this] { saveState(); };
    stepSequencerPage.setOnStateChanged ([this] { saveState(); });
    sliceSequencerPage.setOnStateChanged ([this] { saveState(); });

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
    trackBadge.setBounds     (x, full.getY() + 36 +     (btnH + gap),    btnW, btnH);
    synthButton.setBounds    (x, full.getY() + 36 + 2 * (btnH + gap),    btnW, btnH);
    favoriteButton.setBounds (x, full.getY() + 36 + 3 * (btnH + gap),    btnW, btnH);
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
    audioEngine.setTilt (tiltX, tiltY);
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

    updateTrackBadge();

    // Persist current state whenever page changes
    saveState();
}

//==============================================================================
// Tracks

// -1 for pages that play nothing. The Sound page shows the track it edits.
int MainComponent::trackForPage (int pageIndex) const
{
    if (pageIndex >= 0 && pageIndex < MidiMorf::NUM_CONTROLLERS)
        return eventRouter.getTrackFor (static_cast<MidiMorf::ControllerMode> (pageIndex));
    if (pageIndex == synthPageIdx)
        return soundTrack;
    return -1;
}

void MainComponent::updateTrackBadge()
{
    const int t = trackForPage (pageContainer.getCurrentPage());
    trackBadge.setTrack (t, t >= 0 ? audioEngine.getTrackName (t) : juce::String());
}

// On a controller or sequencer page: which track it plays, plus that track's
// volume. On the Sound page: which track's sound to edit.
void MainComponent::showTrackPicker()
{
    const int page    = pageContainer.getCurrentPage();
    const int current = trackForPage (page);
    if (current < 0)
        return;

    const bool onSound = (page == synthPageIdx);
    juce::PopupMenu menu;
    menu.addSectionHeader (onSound ? juce::String ("Edit the sound of")
                                   : getPageDisplayName (page) + " plays");

    for (int t = 0; t < TrackRack::NumTracks; ++t)
    {
        const bool empty    = audioEngine.getRack().getSource (t) == nullptr;
        const bool editable = audioEngine.getWavetable (t) != nullptr;
        juce::String text = "T" + juce::String (t + 1) + "  " + audioEngine.getTrackName (t);
        if (empty)                      text << "  (drums coming)";
        else if (onSound && ! editable) text << "  (edit on Slice-Seq)";

        juce::PopupMenu::Item item (text);
        item.itemID    = t + 1;
        item.isTicked  = (t == current);
        item.isEnabled = ! empty;
        item.colour    = TrackColours::get (t);
        menu.addItem (item);
    }

    menu.addSeparator();
    menu.addCustomItem (100, std::make_unique<TrackVolumeMenuItem> (audioEngine.getRack(), current,
                                                                    [this] { saveState(); }),
                        nullptr, "Volume");

    menu.showMenuAsync (juce::PopupMenu::Options()
                            .withTargetComponent (&trackBadge)
                            .withStandardItemHeight (36),
        [this, page, onSound] (int result)
        {
            if (result >= 1 && result <= TrackRack::NumTracks)
            {
                const int t = result - 1;
                if (! onSound)
                    eventRouter.setTrackFor (static_cast<MidiMorf::ControllerMode> (page), t);
                else if (audioEngine.getWavetable (t) != nullptr)
                    editSound (t);
                else
                    pageContainer.setPage ((int) MidiMorf::ControllerMode::SliceSequencer);
            }
            updateTrackBadge();
            saveState();   // also catches a volume change
        });
}

// The Sound button: the sound of whatever the current page plays. A synth
// track opens the Sound page on that synth; the break player's sound is
// picked on Slice-Seq; pages that play nothing reopen the last edited sound.
void MainComponent::openSound()
{
    const int page = pageContainer.getCurrentPage();
    int t = trackForPage (page);
    if (t < 0 || page == synthPageIdx)
        t = soundTrack;

    if (audioEngine.getWavetable (t) != nullptr)
    {
        editSound (t);
        pageContainer.setPage (synthPageIdx);
    }
    else if (audioEngine.getRack().getSource (t) != nullptr)
    {
        pageContainer.setPage ((int) MidiMorf::ControllerMode::SliceSequencer);
    }
}

void MainComponent::editSound (int track)
{
    if (auto* synth = audioEngine.getWavetable (track))
    {
        soundTrack = track;
        synthPage.edit (*synth, "T" + juce::String (track + 1) + " " + audioEngine.getTrackName (track).toUpperCase(),
                        TrackColours::get (track));
        updateTrackBadge();
    }
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
        synthPage.saveState (*props);
        audioEngine.getSequencers().saveState (*props);

        // Tracks — until the project file (Phase 1 step 6) takes these over.
        if (auto xml = audioEngine.getBassSynth().getState().createXml())
            props->setValue ("t2_state", xml.get());
        for (int m = 0; m < MidiMorf::NUM_CONTROLLERS; ++m)
        {
            const int t = eventRouter.getTrackFor (static_cast<MidiMorf::ControllerMode> (m));
            if (t >= 0)
                props->setValue ("track_page_" + juce::String (m), t);
        }
        for (int t = 0; t < TrackRack::NumTracks; ++t)
            props->setValue ("track_vol_" + juce::String (t), audioEngine.getRack().getVolume (t));
        props->setValue ("sound_track", soundTrack);
        props->setValue ("seq_sync", eventRouter.isLocalSync() ? 0 : 1);
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
        synthPage.restoreState (*props);
        audioEngine.getSequencers().restoreState (*props);

        if (auto xml = props->getXmlValue ("t2_state"))
            audioEngine.getBassSynth().setState (juce::ValueTree::fromXml (*xml));
        for (int m = 0; m < MidiMorf::NUM_CONTROLLERS; ++m)
        {
            const juce::String key = "track_page_" + juce::String (m);
            if (props->containsKey (key))
                eventRouter.setTrackFor (static_cast<MidiMorf::ControllerMode> (m), props->getIntValue (key));
        }
        for (int t = 0; t < TrackRack::NumTracks; ++t)
            audioEngine.getRack().setVolume (t, (float) props->getDoubleValue ("track_vol_" + juce::String (t), 1.0));
        editSound (props->getIntValue ("sound_track", 0));
        eventRouter.setSyncSource (props->getIntValue ("seq_sync", 0) == 1 ? EventRouter::SyncSource::DAW
                                                                           : EventRouter::SyncSource::Local);

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
