#include "MainComponent.h"
#include <set>

#if JUCE_IOS
 #import <UIKit/UIKit.h>
 #import <AVFoundation/AVFoundation.h>
#endif

// Asks for the microphone (iOS shows its prompt the first time) and calls
// back on the message thread with the answer.
static void requestMicrophone (std::function<void (bool)> done)
{
   #if JUCE_IOS
    JUCE_BEGIN_IGNORE_DEPRECATION_WARNINGS
    [[AVAudioSession sharedInstance] requestRecordPermission: ^(BOOL granted)
    {
        juce::MessageManager::callAsync ([done, granted] { done (granted); });
    }];
    JUCE_END_IGNORE_DEPRECATION_WARNINGS
   #else
    juce::MessageManager::callAsync ([done] { done (true); });
   #endif
}

static juce::File projectFile();   // Documents/Projects/Current.midimorf, below

static const juce::String controllerNames[] = {
    "Morf FX",
    "Morf-Pad",
    "Strum-Chord",
    "Transport",
    "Dual M-Pad",
    "Morf-Chord",
    "Rose-Chord",
    "Step-Seq",
    "Breaker"   // the slice sequencer
};

// Short names for the rail, same order.
static const juce::String controllerShortNames[] = {
    "FX", "Pad", "Strum", "Transport", "Dual", "Chord", "Rose", "Step", "Break"
};

// Guards the invariant that broke once already: NUM_CONTROLLERS is the index
// base for the pages after the controllers AND the bound on these arrays, so
// they must not drift apart.
static_assert (juce::numElementsInArray (controllerNames) == MidiMorf::NUM_CONTROLLERS,
               "controllerNames must have exactly NUM_CONTROLLERS entries");
static_assert (juce::numElementsInArray (controllerShortNames) == MidiMorf::NUM_CONTROLLERS,
               "controllerShortNames must have exactly NUM_CONTROLLERS entries");

// Page indices after the controllers, in the order they're added.
static constexpr int synthPageIdx     = MidiMorf::NUM_CONTROLLERS;
static constexpr int aboutPageIdx     = MidiMorf::NUM_CONTROLLERS + 1;
static constexpr int settingsPageIdx  = MidiMorf::NUM_CONTROLLERS + 2;
static constexpr int homePageIdx      = MidiMorf::NUM_CONTROLLERS + 3;
static constexpr int samplerPageIdx   = MidiMorf::NUM_CONTROLLERS + 4;
static constexpr int performPageIdx   = MidiMorf::NUM_CONTROLLERS + 5;
static constexpr int drumGridPageIdx  = MidiMorf::NUM_CONTROLLERS + 6;   // an instrument, but the phone's own
static constexpr int drumKitPageIdx   = MidiMorf::NUM_CONTROLLERS + 7;
static constexpr int noteEditorPageIdx = MidiMorf::NUM_CONTROLLERS + 8;
// Then every track's own instrument pages: 4 of each kind, kind-major.
// Kinds 0-2 are the sequencers (as LocalSequencers::Kind), 3-7 the live
// controllers.
static constexpr int NumKinds         = 9;   // ... and 8, the Remix Lane
static constexpr int seqPageBase      = MidiMorf::NUM_CONTROLLERS + 9;
static constexpr int numPages         = seqPageBase + NumKinds * TrackRack::NumTracks;

static int seqPageIdx (int kind, int track) { return seqPageBase + kind * TrackRack::NumTracks + track; }
static bool isSeqPage (int page)            { return page >= seqPageBase && page < numPages; }   // any track's instrument page
static int pageKind (int page)              { return (page - seqPageBase) / TrackRack::NumTracks; }
static int pageTrack (int page)             { return (page - seqPageBase) % TrackRack::NumTracks; }

// The live controllers' kinds and their ControllerModes, in kind order.
static const MidiMorf::ControllerMode controllerKinds[5] = {
    MidiMorf::ControllerMode::MorfPad, MidiMorf::ControllerMode::Omnichord, MidiMorf::ControllerMode::DualMPad,
    MidiMorf::ControllerMode::MorfChord, MidiMorf::ControllerMode::RoseChord };

// The four-at-once Perform view is for iPad; an iPhone screen is too small.
static bool isIPad()
{
   #if JUCE_IOS
    return [UIDevice currentDevice].userInterfaceIdiom == UIUserInterfaceIdiomPad;
   #else
    return false;
   #endif
}

// The Remix Lane's instrument id: not a page index (its pages are per track).
static constexpr int RemixId = 200;

static bool isSequencer (int mode)
{
    if (mode == RemixId)
        return true;
    return mode == (int) MidiMorf::ControllerMode::StepSequencer
        || mode == (int) MidiMorf::ControllerMode::SliceSequencer
        || mode == drumGridPageIdx;
}

// The sequencer kind an instrument id is (LocalSequencers::Kind as int).
static int whichSequencer (int mode)
{
    if (mode == RemixId)
        return (int) LocalSequencers::Kind::Remix;
    if (mode == drumGridPageIdx)
        return (int) LocalSequencers::Kind::Drums;
    return mode == (int) MidiMorf::ControllerMode::StepSequencer ? (int) LocalSequencers::Kind::Step : (int) LocalSequencers::Kind::Slice;
}

static constexpr int KindStep = (int) LocalSequencers::Kind::Step, KindSlice = (int) LocalSequencers::Kind::Slice,
                     KindDrums = (int) LocalSequencers::Kind::Drums;

// An instrument id (a ControllerMode, or the Drum Grid's page) as a page kind.
static int kindOf (int mode)
{
    if (mode == RemixId)
        return 8;
    if (isSequencer (mode))
        return whichSequencer (mode);
    for (int k = 0; k < 5; ++k)
        if (mode == (int) controllerKinds[k])
            return 3 + k;
    return -1;
}

// An instrument's name on the rail.
static juce::String instrumentShortName (int mode)
{
    if (mode == RemixId)
        return "Remix";
    if (mode == drumGridPageIdx)
        return "Drums";
    return juce::isPositiveAndBelow (mode, MidiMorf::NUM_CONTROLLERS) ? controllerShortNames[mode] : juce::String();
}

MainComponent::MainComponent()
    : fxPage (eventRouter),
      transportPage (eventRouter)
{
    juce::LookAndFeel::setDefaultLookAndFeel (&appLookAndFeel);

    for (int t = 0; t < TrackRack::NumTracks; ++t)
    {
        auto& router = *(trackRouters[(size_t) t] = std::make_unique<EventRouter> (networkClient, audioEngine.getRack(), audioEngine.getSequencers()));
        router.setFixedTrack (t);
        router.followSettingsOf (eventRouter);
        morfPadPages[(size_t) t]   = std::make_unique<MorfPadPage> (router);
        omnichordPages[(size_t) t] = std::make_unique<OmnichordPage> (router);
        dualMPadPages[(size_t) t]  = std::make_unique<DualMPadPage> (router);
        morfChordPages[(size_t) t] = std::make_unique<MorfChordPage> (router);
        roseChordPages[(size_t) t] = std::make_unique<RoseChordPage> (router);
        remixPages[(size_t) t]     = std::make_unique<RemixPage> (router);
        remixPages[(size_t) t]->setTrack (t);

        stepPages[(size_t) t] = std::make_unique<StepSequencerPage> (router);
        stepPages[(size_t) t]->setTrack (t);
        slicePages[(size_t) t] = std::make_unique<SliceSequencerPage> (router);
        slicePages[(size_t) t]->setTrack (t);
        drumPages[(size_t) t] = std::make_unique<DrumGridPage> (audioEngine.getSequencers());
        drumPages[(size_t) t]->setTrack (t);
    }

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

    // Morf FX plays the master bus's punch-in FX in Local mode.
    eventRouter.setPunchFx (&audioEngine.getPunchFx());

    SessionPage::Callbacks cb;
    cb.getInstrument = [this] (int t) { return instrumentOf[t]; };
    cb.setInstrument = [this] (int t, int mode) { setInstrument (t, mode); seedClips(); updateRail(); };
    cb.getSound      = [this] (int t) { return (int) audioEngine.getTrackSound (t); };
    cb.setSound      = [this] (int t, int s) { setSound (t, s); updateRail(); };
    cb.getVolume     = [this] (int t) { return audioEngine.getRack().getVolume (t); };
    cb.setVolume     = [this] (int t, float g) { audioEngine.getRack().setVolume (t, g); };
    cb.sequencerOf   = [this] (int t) { return sequencerOf (t); };
    cb.isRunning     = [this] (int t)
    {
        const int w = sequencerOf (t);
        if (w == SessionPage::NoClips)
            return audioEngine.getSequencers().getPlayhead (t) >= 0;
        if (w < 0)
            return audioEngine.getSequencers().isNoteClipPlaying (t);
        return audioEngine.getSequencers().getPlayhead (t) >= 0;
    };
    cb.launchClip    = [this] (int t, int s) { launchClip (t, s); };
    cb.newClip       = [this] (int t, int s) { newClip (t, s); };
    cb.editClip      = [this] (int t, int s) { editClip (t, s); };
    cb.duplicateClip = [this] (int t, int s) { duplicateClip (t, s); };
    cb.deleteClip    = [this] (int t, int s) { deleteClip (t, s); };
    cb.launchScene   = [this] (int s) { launchScene (s); };
    cb.stopTrack     = [this] (int t) { stopTrack (t); };
    cb.recordingScene = [this] (int t) { return t == recordingTrack ? recordingScene : -1; };
    cb.isCountingIn  = [this] { return audioEngine.getSequencers().getRecState() <= LocalSequencers::RecCountIn && recordingTrack >= 0; };
    cb.recordClip    = [this] (int t, int s) { recordClip (t, s); };
    cb.stopRecording = [this] { stopRecordingClip(); };
    cb.quantizeClip  = [this] (int t, int s) { quantizeClip (t, s); };
    cb.undoPass      = [this] (int t) { undoLastPass (t); };
    cb.canUndo       = [this] (int t, int s) { return undo[(size_t) t].has_value() && undo[(size_t) t]->scene == s; };
    cb.getRecBars    = [this] { return recBars; };
    cb.setRecBars    = [this] (int b) { recBars = b; saveState(); };
    cb.getRecQuantize = [this] { return recQuantize; };
    cb.setRecQuantize = [this] (bool q) { recQuantize = q; saveState(); };
    cb.getMetronome  = [this] { return recMetronome; };
    cb.setMetronome  = [this] (bool m) { recMetronome = m; audioEngine.getSequencers().setMetronome (m); saveState(); };
    cb.getLaunchQuantize = [this] { return audioEngine.getSequencers().getLaunchQuantize(); };
    cb.setLaunchQuantize = [this] (int q) { audioEngine.getSequencers().setLaunchQuantize (q); };
    cb.soundName     = [this] (int t) { return audioEngine.getSoundName (t); };
    cb.openSound     = [this] (int t) { showSound (t); };
    cb.getMix = [this] (int t, int param) -> float
    {
        auto& rack = audioEngine.getRack();
        auto& ins = rack.getInsert (t);
        switch (param)
        {
            case SessionPage::Pan:        return rack.getPan (t);
            case SessionPage::SendDelay:  return rack.getSend (t, TrackRack::DelayBus);
            case SessionPage::SendReverb: return rack.getSend (t, TrackRack::ReverbBus);
            case SessionPage::Filter:     return ins.filter.load();
            case SessionPage::Drive:      return ins.drive.load();
            case SessionPage::Crush:      return ins.crush.load();
            default:                      return 0.0f;
        }
    };
    cb.setMix = [this] (int t, int param, float v)
    {
        auto& rack = audioEngine.getRack();
        auto& ins = rack.getInsert (t);
        switch (param)
        {
            case SessionPage::Pan:        rack.setPan (t, v); break;
            case SessionPage::SendDelay:  rack.setSend (t, TrackRack::DelayBus, v); break;
            case SessionPage::SendReverb: rack.setSend (t, TrackRack::ReverbBus, v); break;
            case SessionPage::Filter:     ins.filter = juce::jlimit (-1.0f, 1.0f, v); break;
            case SessionPage::Drive:      ins.drive = juce::jlimit (0.0f, 1.0f, v); break;
            case SessionPage::Crush:      ins.crush = juce::jlimit (0.0f, 1.0f, v); break;
            default: break;
        }
    };
    cb.getSolo   = [this] (int t) { return audioEngine.getRack().isSolo (t); };
    cb.setSolo   = [this] (int t, bool on) { audioEngine.getRack().setSolo (t, on); };
    cb.getMaster = [this] (int param) -> float
    {
        auto& rack = audioEngine.getRack();
        auto& buses = rack.getBuses();
        switch (param)
        {
            case SessionPage::MasterVolume:  return rack.getMasterVolume();
            case SessionPage::DelayTime:     return (float) buses.delayTime.load();
            case SessionPage::DelayFeedback: return buses.delayFeedback.load();
            case SessionPage::ReverbSize:    return buses.reverbSize.load();
            default:                         return 0.0f;
        }
    };
    cb.setMaster = [this] (int param, float v)
    {
        auto& rack = audioEngine.getRack();
        auto& buses = rack.getBuses();
        switch (param)
        {
            case SessionPage::MasterVolume:  rack.setMasterVolume (v); break;
            case SessionPage::DelayTime:     buses.delayTime = juce::jlimit (0, 3, juce::roundToInt (v)); break;
            case SessionPage::DelayFeedback: buses.delayFeedback = juce::jlimit (0.0f, 0.9f, v); break;
            case SessionPage::ReverbSize:    buses.reverbSize = juce::jlimit (0.0f, 1.0f, v); break;
            default: break;
        }
    };
    cb.getMuted      = [this] (int t) { return audioEngine.getRack().isMuted (t); };
    cb.setMuted      = [this] (int t, bool m) { audioEngine.getRack().setMuted (t, m); };
    cb.openPage      = [this] (int page) { pageContainer.setPage (page); };
    cb.openProjects  = [this] { showProjects(); };
    cb.projectName   = [this] { return projectName; };
    cb.changed       = [this] { saveState(); };
    homePage = std::make_unique<SessionPage> (session, cb, SessionPage::Pages { (int) MidiMorf::ControllerMode::Transport,
                                                                                (int) MidiMorf::ControllerMode::FXPunchIn,
                                                                                settingsPageIdx, aboutPageIdx, drumGridPageIdx, RemixId });

    // Page container — controllers, then Sound, About, Settings, Home, Breaks.
    // The order is the page indices above.
    pageContainer.addPage (&fxPage);
    pageContainer.addPage (&morfPadPlaceholder);
    pageContainer.addPage (&omnichordPlaceholder);
    pageContainer.addPage (&transportPage);
    pageContainer.addPage (&dualMPadPlaceholder);
    pageContainer.addPage (&morfChordPlaceholder);
    pageContainer.addPage (&roseChordPlaceholder);
    pageContainer.addPage (&stepPlaceholder);
    pageContainer.addPage (&slicePlaceholder);
    pageContainer.addPage (&synthPage);
    pageContainer.addPage (&aboutPage);
    pageContainer.addPage (&settingsPage);
    pageContainer.addPage (homePage.get());
    pageContainer.addPage (&samplerPage);
    pageContainer.addPage (&performView);
    pageContainer.addPage (&drumPlaceholder);
    pageContainer.addPage (&drumKitPage);
    pageContainer.addPage (&noteEditorPage);
    for (int t = 0; t < TrackRack::NumTracks; ++t) pageContainer.addPage (stepPages[(size_t) t].get());
    for (int t = 0; t < TrackRack::NumTracks; ++t) pageContainer.addPage (slicePages[(size_t) t].get());
    for (int t = 0; t < TrackRack::NumTracks; ++t) pageContainer.addPage (drumPages[(size_t) t].get());
    for (int t = 0; t < TrackRack::NumTracks; ++t) pageContainer.addPage (morfPadPages[(size_t) t].get());
    for (int t = 0; t < TrackRack::NumTracks; ++t) pageContainer.addPage (omnichordPages[(size_t) t].get());
    for (int t = 0; t < TrackRack::NumTracks; ++t) pageContainer.addPage (dualMPadPages[(size_t) t].get());
    for (int t = 0; t < TrackRack::NumTracks; ++t) pageContainer.addPage (morfChordPages[(size_t) t].get());
    for (int t = 0; t < TrackRack::NumTracks; ++t) pageContainer.addPage (roseChordPages[(size_t) t].get());
    for (int t = 0; t < TrackRack::NumTracks; ++t) pageContainer.addPage (remixPages[(size_t) t].get());

    // Perform borrows the tracks' instrument pages and hands them back.
    performView.onShow       = [this] { populatePerform(); };
    performView.onReturnPage = [this] (juce::Component* p) { pageContainer.reclaim (p); };
    performView.onExpand     = [this] (int t)
    {
        pageContainer.setPage (instrumentOf[t] >= 0 ? pageOf (t) : homePageIdx);
    };
    performView.onMute       = [this] (int t, bool m) { audioEngine.getRack().setMuted (t, m); saveState(); };

    // Step-Seq and Slice-Seq share some messages (steps, transport, ratchet)
    // that go to the page last shown. With several on screen, the one being
    // touched is the one they mean.
    // Each quarter's page has its own track's router, so touches need no
    // routing; the server is told which page is in use (WiFi).
    performView.onTouched    = [this] (int t)
    {
        const int mode = instrumentOf[t];
        if (juce::isPositiveAndBelow (mode, MidiMorf::NUM_CONTROLLERS))
            trackRouters[(size_t) t]->sendScreenMode (static_cast<MidiMorf::ControllerMode> (mode));
    };
    pageContainer.setListener (this);
    addAndMakeVisible (pageContainer);
    addAndMakeVisible (header);

    settingsPage.onModeChanged = [this] { fxPage.refreshFromRouter(); saveState(); };
    settingsPage.getAudioLoad  = [this] { return audioEngine.getCpuUsage(); };
    settingsPage.onTour        = [this] { startWalkthrough(); };

    addChildComponent (walkthrough);
    walkthrough.onFinished = [this]
    {
        if (auto* props = appProperties.getUserSettings())
        {
            props->setValue ("tour_done", true);
            props->saveIfNeeded();
        }
    };

    // The rail: Home, the four tracks, Play/Stop. It sits in the right gutter,
    // outside the safe area, on top of the wood.
    homeButton.onClick = [this] { pageContainer.setPage (homePageIdx); };
    addAndMakeVisible (homeButton);

    performButton.onClick = [this] { pageContainer.setPage (performPageIdx); };
    if (isIPad())
        addAndMakeVisible (performButton);

    for (int t = 0; t < TrackRack::NumTracks; ++t)
    {
        auto* b = trackButtons.add (new RailTrackButton (t));
        b->onClick = [this, t] { trackTapped (t); };
        addAndMakeVisible (b);
    }

    playButton.onClick = [this] { togglePlay(); };
    addAndMakeVisible (playButton);
    recordButton.onClick = [this] { railRecord(); };
    addChildComponent (recordButton);

    synthPage.onPatchChanged = [this] { saveState(); };
    synthPage.onPresetChanged = [this] (const juce::String& name)
    {
        audioEngine.setSoundName (soundTrack, name);
        homePage->refresh();
        updateRail();
        saveState();
    };
    samplerPage.onChanged = [this] { homePage->refresh(); updateRail(); saveState(); };
    samplerPage.setRecorder (audioEngine.getRecorder());

    noteEditorPage.onBack = [this] { pageContainer.setPage (homePageIdx); };
    noteEditorPage.getPlayPosition = [this]
    {
        const int t = noteEditorPage.getTrack();
        return session.getPlaying (t) == editingScene ? audioEngine.getSequencers().getNoteClipPosition (t) : -1.0;
    };
    noteEditorPage.onChanged = [this] (const NoteClip& c)
    {
        const int t = noteEditorPage.getTrack();
        if (! session.hasClip (t, editingScene) || ! session.clip (t, editingScene)->isNotes())
            return;
        session.clip (t, editingScene)->notes = c;
        if (session.getPlaying (t) == editingScene)
            audioEngine.getSequencers().updateNoteClip (t, c);
        saveState();
    };

    for (int t = 0; t < TrackRack::NumTracks; ++t)
    {
        drumPage (t).onStateChanged = [this] { saveState(); };
        drumPage (t).onAudition = [this, t] (int note, bool down) { audition (t, note, down); };
        stepPage (t).setOnStateChanged ([this] { saveState(); });
        slicePage (t).setOnStateChanged ([this] { saveState(); });
    }
    drumKitPage.onChanged   = [this] { saveState(); };
    drumKitPage.onKitLoaded = [this] { homePage->refresh(); updateRail(); };
    drumKitPage.onAudition  = [this] (int note, bool down) { audition (drumKitPage.getTrack(), note, down); };
    samplerPage.onMicNeeded = [this] (std::function<void (bool)> done)
    {
        requestMicrophone ([this, done] (bool granted) { done (granted && audioEngine.setMicOn (true)); });
    };
    samplerPage.onMicDone = [this] { audioEngine.setMicOn (false); };

    // A break loaded into the slicer's sampler sets the tempo to the loop's
    // own, as picking it on Slice-Seq does.
    samplerPage.onLoaded = [this] (int t, const Break* b)
    {
        if (b != nullptr && sequencerOf (t) == KindSlice
            && audioEngine.getTrackSampler (t).getMode() == SamplerSource::Slice)
            audioEngine.getSequencers().setBpm (b->bpm());
    };
    fxPage.onStateChanged = [this] { saveState(); };
    transportPage.onStateChanged = [this] { saveState(); };

    // Network
    networkClient.setListener (this);
    networkClient.startSearching();

    // Accelerometer
    accelerometer.setListener (this);
    accelerometer.start();

    // A brand-new install (no project, no settings) gets the walkthrough.
    auto* settings = appProperties.getUserSettings();
    const bool firstLaunch = ! projectFile().existsAsFile()
                             && (settings == nullptr || ! settings->containsKey ("lastPage"));

    // Restore saved state (track setup, last page, scale settings, etc.)
    restoreState();

    if (firstLaunch && (settings == nullptr || ! settings->getBoolValue ("tour_done", false)))
        juce::Timer::callAfterDelay (900, [safe = juce::Component::SafePointer<MainComponent> (this)]
        {
            if (safe != nullptr) safe->startWalkthrough();
        });

    // Keeps Play/Stop's light in step with the sequencers.
    startTimerHz (10);

    // Landscape initial size hint
    setSize (800, 400);
}

MainComponent::~MainComponent()
{
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    saveNow();
    stopTimer();
    accelerometer.stop();
    networkClient.setListener (nullptr);
    networkClient.stopSearching();
    audioEngine.stop();
    pageContainer.setListener (nullptr);
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
    walkthrough.setBounds (area);

    // Rail geometry needs to be in scope for both the page trim (so we never
    // let page content land under the rail) and the rail layout below.
    const int btnW          = 56;
    const int floatingPadR  = 4;            // gap between button right edge and screen
    const int floatingGapLR = 4;            // gap between button left edge and page right
    const int rightGutter   = btnW + floatingPadR + floatingGapLR;   // 64

#if JUCE_IOS
    // iPad has tiny rounded-corner safe-area insets — page content would
    // slide right under the rail (and cover things like the right-edge strum
    // bar on Strum-Chord and Morf-Chord) unless we floor the trim at the rail
    // gutter. iPhone with Dynamic Island already gives ~44pt of inset which is
    // enough; iPhone landscape is also tight on horizontal real estate so we
    // don't want to widen the wood frame there unnecessarily.
    const bool onIPad = isIPad();

    if (auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
    {
        auto insets = display->safeAreaInsets;
        const int hMax      = juce::jmax (insets.getLeft(), insets.getRight());
        const int extraPad  = 8;
        const int safeTrim  = hMax + extraPad;
        const int trim      = onIPad ? juce::jmax (safeTrim, rightGutter)
                                     : safeTrim;
        area = area.withTrimmedLeft  (trim)
                   .withTrimmedRight (trim)
                   .withTrimmedTop   (insets.getTop());   // iPad keeps its status bar in landscape
    }
    else if (onIPad)
    {
        area = area.withTrimmedLeft  (rightGutter)
                   .withTrimmedRight (rightGutter);
    }
#else
    juce::ignoreUnused (rightGutter);
#endif

    // The header strip says which track the screen belongs to; the page
    // fills the rest of the safe area.
    header.setBounds (area.removeFromTop (22));
    pageContainer.setBounds (area);

    // The rail lives OUTSIDE the safe area, in the rounded-corner gutter on
    // the right edge — dead space reclaimed. Home on top, Play/Stop at the
    // bottom, the four tracks sharing what's between. 30 pt from the top and
    // bottom so the screen's rounded corners (~60 pt radius on recent
    // iPhones) don't clip Home and Play.
    auto rail = getLocalBounds().reduced (0, 30);
    rail = rail.withX (rail.getRight() - btnW - floatingPadR).withWidth (btnW);
    const int gap = 6;
    homeButton.setBounds (rail.removeFromTop (44));
    if (performButton.isVisible())
    {
        rail.removeFromTop (gap);
        performButton.setBounds (rail.removeFromTop (44));
    }
    playButton.setBounds (rail.removeFromBottom (44));
    if (recordButton.isVisible())
    {
        rail.removeFromBottom (gap);
        recordButton.setBounds (rail.removeFromBottom (44));
    }
    rail.removeFromTop (gap);
    rail.removeFromBottom (gap);
    const int trackH = (rail.getHeight() - gap * (TrackRack::NumTracks - 1)) / TrackRack::NumTracks;
    for (auto* b : trackButtons)
    {
        b->setBounds (rail.removeFromTop (trackH));
        rail.removeFromTop (gap);
    }
}

juce::String MainComponent::getPageDisplayName (int pageIndex) const
{
    if (pageIndex >= 0 && pageIndex < MidiMorf::NUM_CONTROLLERS)
        return controllerNames[pageIndex];
    if (pageIndex == synthPageIdx)     return "Sound";
    if (pageIndex == aboutPageIdx)     return "About";
    if (pageIndex == settingsPageIdx)  return "Settings";
    if (pageIndex == homePageIdx)      return "Session";
    if (pageIndex == samplerPageIdx)   return "Sampler";
    if (pageIndex == performPageIdx)   return "Perform";
    if (pageIndex == drumGridPageIdx)  return "Drum Grid";
    if (isSeqPage (pageIndex))
    {
        const int kind = pageKind (pageIndex);
        static const char* names[3] = { "Step-Seq", "Breaker", "Drum Grid" };
        if (kind == 8) return "Remix Lane";
        return kind < 3 ? juce::String (names[kind]) : controllerNames[(int) controllerKinds[kind - 3]];
    }
    if (pageIndex == drumKitPageIdx)   return "Drums";
    if (pageIndex == noteEditorPageIdx) return "Editor";
    return {};
}

//==============================================================================
// Tracks

// An instrument plays exactly one track: giving it to this track takes it
// from any other. A sequencer that loses its track stops.
void MainComponent::setInstrument (int track, int mode)
{
    if (! juce::isPositiveAndBelow (track, TrackRack::NumTracks))
        return;

    // Only pages that play notes are instruments; anything else (e.g. a
    // damaged settings file) means no instrument.
    using CM = MidiMorf::ControllerMode;
    const bool isInstrument = mode == (int) CM::MorfPad || mode == (int) CM::Omnichord || mode == (int) CM::DualMPad
                           || mode == (int) CM::MorfChord || mode == (int) CM::RoseChord || isSequencer (mode);   // incl. Drum Grid
    if (! isInstrument)
        mode = -1;

    // Every track has its own copy of every instrument, so nothing moves.
    instrumentOf[track] = mode;

    auto& seqs = audioEngine.getSequencers();
    if (isSequencer (mode))
    {
        const int kind = whichSequencer (mode);
        seqs.setKind (track, (LocalSequencers::Kind) kind);

        // The engine starts from what the track's page shows.
        if (mode == RemixId)
        {
            if (audioEngine.getTrackSound (track) == AudioEngine::Sound::Sampler)
                audioEngine.getTrackSampler (track).setMode (SamplerSource::Slice);   // it plays the sample in slices
            remixPages[(size_t) track]->updateLength();
        }
        else if (kind == KindDrums)
        {
            seqs.queuePattern (track, drumPage (track).getPattern());
        }
        else
        {
            seqs.setActiveTrack (track);
            if (kind == KindStep) stepPage (track).pushFullState();
            else                  slicePage (track).pushFullState();
        }
    }
    else
    {
        seqs.setKind (track, LocalSequencers::Kind::None);
    }
}

void MainComponent::setSound (int track, int sound)
{
    audioEngine.setTrackSound (track, static_cast<AudioEngine::Sound> (juce::jlimit (0, 3, sound)));

    // A sound page can't keep editing a sound its track no longer plays.
    const int page = pageContainer.getCurrentPage();
    if ((page == synthPageIdx && audioEngine.getWavetable (soundTrack) == nullptr)
        || (page == drumKitPageIdx && audioEngine.getTrackSound (drumKitPage.getTrack()) != AudioEngine::Sound::Drums))
        pageContainer.setPage (homePageIdx);
}

// -1 for pages that belong to no track.
int MainComponent::trackOfPage (int pageIndex) const
{
    if (pageIndex == synthPageIdx)
        return soundTrack;

    if (pageIndex == samplerPageIdx)
        return samplerPage.getTrack();

    if (pageIndex == drumKitPageIdx)
        return drumKitPage.getTrack();

    if (pageIndex == noteEditorPageIdx)
        return noteEditorPage.getTrack();

    if (isSeqPage (pageIndex))
    {
        const int t = pageTrack (pageIndex);
        return pageOf (t) == pageIndex ? t : -1;
    }
    return -1;
}

int MainComponent::pageOf (int track) const
{
    const int inst = juce::isPositiveAndBelow (track, TrackRack::NumTracks) ? instrumentOf[track] : -1;
    const int kind = kindOf (inst);
    return kind >= 0 ? seqPageIdx (kind, track) : -1;
}

bool MainComponent::pageIsSound (int pageIndex) const
{
    return pageIndex == synthPageIdx || pageIndex == samplerPageIdx || pageIndex == drumKitPageIdx;
}

// First tap: the track's instrument. Tapping the lit track again flips
// between its instrument and its sound. A track with no instrument goes
// straight to its sound; an empty track opens Home to set it up.
void MainComponent::trackTapped (int track)
{
    const int page = pageContainer.getCurrentPage();
    const bool onThisTrack   = trackOfPage (page) == track;
    const bool hasInstrument = instrumentOf[track] >= 0;
    const bool hasSound      = audioEngine.getTrackSound (track) != AudioEngine::Sound::Empty;

    if (onThisTrack && ! pageIsSound (page) && hasSound)
        showSound (track);
    else if (hasInstrument)
        showInstrument (track);
    else if (hasSound)
        showSound (track);
    else
        pageContainer.setPage (homePageIdx);

    updateRail();
}

void MainComponent::showInstrument (int track)
{
    if (instrumentOf[track] >= 0)
        pageContainer.setPage (pageOf (track));
}

void MainComponent::showSound (int track)
{
    switch (audioEngine.getTrackSound (track))
    {
        case AudioEngine::Sound::Synth:
            soundTrack = track;
            synthPage.edit (audioEngine.getTrackSynth (track), audioEngine.getSoundName (track), TrackColours::get (track));
            pageContainer.setPage (synthPageIdx);
            break;

        case AudioEngine::Sound::Sampler:
            samplerPage.edit (audioEngine.getTrackSampler (track), track);
            pageContainer.setPage (samplerPageIdx);
            break;

        case AudioEngine::Sound::Drums:
            drumKitPage.edit (audioEngine.getTrackDrums (track), track);
            pageContainer.setPage (drumKitPageIdx);
            break;

        case AudioEngine::Sound::Empty:
            break;
    }
    updateRail();
}

//==============================================================================
// Session clips
//
// A clip is a sequencer track's pattern. The page shows and edits the
// playing clip; its edits are copied back into the clip before anything
// else happens to it (a launch, a duplicate, a save). On the phone's clock a
// launch waits for the next bar and LocalSequencers swaps it in exactly; on
// DAW sync the page sends it straight away.

int MainComponent::sequencerOf (int track) const
{
    const int inst = juce::isPositiveAndBelow (track, TrackRack::NumTracks) ? instrumentOf[track] : -1;
    if (inst == RemixId)
        return SessionPage::NoClips;   // played live, no clips
    return isSequencer (inst) ? whichSequencer (inst) : -1;
}

// The pattern on a track's sequencer page.
SeqPattern MainComponent::pagePattern (int track)
{
    switch (sequencerOf (track))
    {
        case KindStep:  return stepPage (track).getPattern();
        case KindSlice: return slicePage (track).getPattern();
        default:        return drumPage (track).getPattern();
    }
}

void MainComponent::showOnPage (int track, const SeqPattern& p)
{
    switch (sequencerOf (track))
    {
        case KindStep:  stepPage (track).showPattern (p); break;
        case KindSlice: slicePage (track).showPattern (p); break;
        case KindDrums: drumPage (track).showPattern (p); break;
        default: break;
    }
}

void MainComponent::captureClip (int track)
{
    const int w = sequencerOf (track), s = session.getPlaying (track);
    if (w >= 0 && session.hasClip (track, s) && session.clip (track, s)->sequencer == w)
        session.clip (track, s)->pattern = pagePattern (track);
}

void MainComponent::launchClip (int track, int scene)
{
    const int w = sequencerOf (track);

    // A note clip on a controller track: its own player.
    if (w < 0)
    {
        if (! session.hasClip (track, scene) || ! session.clip (track, scene)->isNotes())
            return;
        if (track == recordingTrack)
            stopRecordingClip();
        auto& seqs = audioEngine.getSequencers();
        const bool wasPlaying = seqs.isNoteClipPlaying (track);
        seqs.queueNoteClip (track, session.clip (track, scene)->notes);
        if (wasPlaying) session.setQueued (track, scene);
        else            session.setPlaying (track, scene);
        saveState();
        return;
    }

    if (w < 0 || ! session.hasClip (track, scene) || session.clip (track, scene)->sequencer != w)
        return;

    captureClip (track);
    const auto& pattern = session.clip (track, scene)->pattern;
    auto& seqs = audioEngine.getSequencers();
    if (w == KindDrums || eventRouter.isLocalSync())
    {
        const bool wasRunning = seqs.getPlayhead (track) >= 0;
        seqs.queuePattern (track, pattern);
        if (wasRunning)
        {
            session.setQueued (track, scene);   // takes over on the bar; pollLaunches moves it to playing
        }
        else
        {
            showOnPage (track, pattern);
            session.setPlaying (track, scene);
            shownLaunches[track] = seqs.getLaunchCount (track);
        }
        seqs.setPlaying (track, true);
    }
    else
    {
        seqs.setActiveTrack (track);
        if (w == KindStep) stepPage (track).loadPattern (pattern);
        else               slicePage (track).loadPattern (pattern);
        session.setPlaying (track, scene);
    }
    saveState();
}

// A new clip starts as a copy of what the track is playing, so a variation is
// one tap away.
void MainComponent::newClip (int track, int scene)
{
    const int w = sequencerOf (track);
    if (w < 0)
    {
        recordClip (track, scene);   // an empty slot on a controller track records
        return;
    }
    captureClip (track);
    session.setClip (track, scene, { session.freshName (track), pagePattern (track), w });
    launchClip (track, scene);
    homePage->refresh();
}

void MainComponent::editClip (int track, int scene)
{
    if (session.hasClip (track, scene) && session.clip (track, scene)->isNotes())
    {
        showNoteEditor (track, scene);
        return;
    }
    if (session.getPlaying (track) != scene)
        launchClip (track, scene);
    showInstrument (track);
}

// Into the next empty slot to the right.
void MainComponent::duplicateClip (int track, int scene)
{
    if (! session.hasClip (track, scene)) return;
    if (session.getPlaying (track) == scene)
        captureClip (track);
    for (int s = scene + 1; s < SessionModel::NumScenes; ++s)
        if (! session.hasClip (track, s))
        {
            auto copy = *session.clip (track, scene);
            copy.name = session.freshName (track, copy.isNotes() ? "Take" : "Pattern");
            session.setClip (track, s, copy);
            saveState();
            return;
        }
}

void MainComponent::deleteClip (int track, int scene)
{
    if (track == recordingTrack && scene == recordingScene)
        stopRecordingClip();
    if (undo[(size_t) track] && undo[(size_t) track]->scene == scene)
        undo[(size_t) track].reset();
    if (session.getPlaying (track) == scene)
        stopTrack (track);
    session.removeClip (track, scene);
    saveState();
}

// Every track's clip in this scene; tracks with nothing there stop.
void MainComponent::launchScene (int scene)
{
    for (int t = 0; t < TrackRack::NumTracks; ++t)
    {
        const int w = sequencerOf (t);
        if (w == SessionPage::NoClips)
            continue;   // a Remix Lane plays on through scene changes
        const int kind = w >= 0 ? w : SessionModel::NotesClip;
        if (session.hasClip (t, scene) && session.clip (t, scene)->sequencer == kind)
            launchClip (t, scene);
        else
            stopTrack (t);
    }
}

void MainComponent::stopTrack (int track)
{
    const int w = sequencerOf (track);
    if (w == SessionPage::NoClips)
    {
        audioEngine.getSequencers().queueStop (track);
        return;
    }
    if (w < 0)
    {
        if (track == recordingTrack)
            stopRecordingClip();
        session.setQueued (track, -1);
        audioEngine.getSequencers().queueNoteStop (track);
        return;
    }
    session.setQueued (track, -1);
    audioEngine.getSequencers().queueStop (track);
}

// Timer: a queued clip that has taken over becomes the playing one, and its
// page shows it.
void MainComponent::pollLaunches()
{
    auto& seqs = audioEngine.getSequencers();
    for (int t = 0; t < TrackRack::NumTracks; ++t)
    {
        if (sequencerOf (t) < 0)
            continue;
        const int count = seqs.getLaunchCount (t);
        if (count == shownLaunches[t])
            continue;
        shownLaunches[t] = count;
        showOnPage (t, seqs.getPattern (t));
        if (session.getQueued (t) >= 0)
            session.setPlaying (t, session.getQueued (t));
    }

    // Note clips: a queued one has taken over once nothing is pending.
    for (int t = 0; t < TrackRack::NumTracks; ++t)
        if (sequencerOf (t) < 0 && session.getQueued (t) >= 0 && ! seqs.isNoteLaunchPending (t))
            session.setPlaying (t, session.getQueued (t));

    collectPasses();
}

//==============================================================================
// Recording note clips
//
// Recording runs in passes of the clip's length: each finished pass is merged
// into the clip (and the clip plays underneath, so the next pass overdubs).
// Undo takes back the last pass. With the clock stopped there's a one-bar
// count-in.

void MainComponent::recordClip (int track, int scene)
{
    if (sequencerOf (track) >= 0)
        return;
    if (recordingTrack >= 0)
        stopRecordingClip();

    if (! session.hasClip (track, scene))
    {
        NoteClip empty;
        empty.lengthBeats = recBars * 4.0;
        session.setClip (track, scene, { session.freshName (track, "Take"), {}, SessionModel::NotesClip, empty });
    }
    auto& clip = *session.clip (track, scene);
    if (! clip.isNotes())
        return;

    auto& seqs = audioEngine.getSequencers();
    session.setPlaying (track, scene);
    session.setQueued (track, -1);
    recordingTrack = track;
    recordingScene = scene;
    seenPasses = seqs.getPassCount();
    seqs.startRecording (track, clip.notes, recMetronome);
    homePage->refresh();
    updateRail();
}

void MainComponent::showNoteEditor (int track, int scene)
{
    if (! session.hasClip (track, scene) || ! session.clip (track, scene)->isNotes())
        return;
    editingScene = scene;
    const auto& c = *session.clip (track, scene);
    noteEditorPage.edit (c.notes, c.name, track, audioEngine.getTrackSound (track) == AudioEngine::Sound::Drums);
    pageContainer.setPage (noteEditorPageIdx);
    updateRail();
}

void MainComponent::stopRecordingClip()
{
    if (recordingTrack < 0)
        return;
    audioEngine.getSequencers().stopRecording();
    collectPasses();
    recordingTrack = recordingScene = -1;
    updateRail();
    saveState();
}

void MainComponent::collectPasses()
{
    auto& seqs = audioEngine.getSequencers();
    if (recordingTrack < 0)
        return;

    if (seqs.getPassCount() != seenPasses)
    {
        seenPasses = seqs.getPassCount();
        const auto pass = seqs.takePass();
        if (pass.numNotes > 0 && session.hasClip (recordingTrack, recordingScene))
        {
            auto& clip = session.clip (recordingTrack, recordingScene)->notes;
            undo[(size_t) recordingTrack] = Undo { recordingScene, clip };
            for (int i = 0; i < pass.numNotes; ++i)
                clip.add (pass.notes[(size_t) i]);
            if (recQuantize)
                clip.quantize (0.25);
            clip.sort();
            seqs.updateNoteClip (recordingTrack, clip);
            if (pageContainer.getCurrentPage() == noteEditorPageIdx && noteEditorPage.getTrack() == recordingTrack && editingScene == recordingScene)
                showNoteEditor (recordingTrack, recordingScene);   // show the new pass
            saveState();
        }
    }

    // Stopped from elsewhere (the rail's ■ stops everything).
    if (seqs.getRecordingTrack() < 0)
    {
        recordingTrack = recordingScene = -1;
        updateRail();
    }
}

void MainComponent::undoLastPass (int track)
{
    auto& u = undo[(size_t) track];
    if (! u || ! session.hasClip (track, u->scene))
        return;
    session.clip (track, u->scene)->notes = u->notes;
    if (session.getPlaying (track) == u->scene)
        audioEngine.getSequencers().updateNoteClip (track, u->notes);
    u.reset();
    saveState();
}

void MainComponent::quantizeClip (int track, int scene)
{
    if (! session.hasClip (track, scene) || ! session.clip (track, scene)->isNotes())
        return;
    auto& clip = session.clip (track, scene)->notes;
    undo[(size_t) track] = Undo { scene, clip };
    clip.quantize (0.25);
    if (session.getPlaying (track) == scene)
        audioEngine.getSequencers().updateNoteClip (track, clip);
    saveState();
}

// On a controller page: stop if recording; otherwise overdub the track's
// playing note clip, or a new take in its first empty slot.
void MainComponent::railRecord()
{
    if (recordingTrack >= 0)
    {
        stopRecordingClip();
        return;
    }
    const int track = trackOfPage (pageContainer.getCurrentPage());
    if (track < 0 || sequencerOf (track) != -1)
        return;

    const int playing = session.getPlaying (track);
    if (session.hasClip (track, playing) && session.clip (track, playing)->isNotes())
    {
        recordClip (track, playing);
        return;
    }
    for (int s = 0; s < SessionModel::NumScenes; ++s)
        if (! session.hasClip (track, s))
        {
            recordClip (track, s);
            return;
        }
}

// The first time a sequencer track has no clips: its current pattern
// becomes Pattern 1 in scene 1.
void MainComponent::seedClips()
{
    for (int t = 0; t < TrackRack::NumTracks; ++t)
    {
        const int w = sequencerOf (t);
        if (w < 0) continue;
        bool any = false;
        for (int s = 0; s < SessionModel::NumScenes; ++s)
            any |= session.hasClip (t, s);
        if (any) continue;
        session.setClip (t, 0, { "Pattern 1", pagePattern (t), w });
        session.setPlaying (t, 0);
    }
}

// After a project loads: each sequencer gets its track's playing clip.
void MainComponent::applySessionToSequencers()
{
    auto& seqs = audioEngine.getSequencers();
    for (int t = 0; t < TrackRack::NumTracks; ++t)
    {
        const int w = sequencerOf (t), s = session.getPlaying (t);
        if (w < 0 || ! session.hasClip (t, s) || session.clip (t, s)->sequencer != w)
            continue;
        const auto& p = session.clip (t, s)->pattern;
        seqs.queuePattern (t, p);   // stopped: applies at once
        seqs.setActiveTrack (t);    // the page's loadPattern sends to its own track
        switch (w)
        {
            case KindStep:  stepPage (t).loadPattern (p); break;
            case KindSlice: slicePage (t).loadPattern (p); break;
            default:        drumPage (t).showPattern (p); break;
        }
        shownLaunches[t] = seqs.getLaunchCount (t);
    }
}

//==============================================================================
// Projects

void MainComponent::showProjects()
{
    if (projectBrowser == nullptr)
    {
        projectBrowser = std::make_unique<ProjectBrowser>();
        projectBrowser->currentName = [this] { return projectName; };
        projectBrowser->onOpen      = [this] (const juce::File& f) { openProject (f); };
        projectBrowser->onSaveAs    = [this] (const juce::String& n) { saveProjectAs (n); };
        addChildComponent (*projectBrowser);
    }
    projectBrowser->setBounds (pageContainer.getBounds());
    projectBrowser->show();
}

void MainComponent::saveProjectAs (const juce::String& name)
{
    projectName = name;
    saveNow();   // writes <name>.midimorf beside the working copy
    homePage->refresh();
}

// The project New starts from: T1 Morf-Chord on a synth, T2 Step-Seq on Acid
// Bass, T3 Slice-Seq on the Amen, T4 Drum Grid on the 808 kit, each
// sequencer with one clip.
//
// The walkthrough's (tour) starts with every track empty, for the player to
// set up: the synths on Init, track 2's sampler already holding the Amen.
static juce::ValueTree starterProject (int drumGridMode, bool tour = false)
{
    using CM = MidiMorf::ControllerMode;
    const int inst[TrackRack::NumTracks]  = { (int) CM::MorfChord, (int) CM::StepSequencer, (int) CM::SliceSequencer, drumGridMode };
    const int sound[TrackRack::NumTracks] = { 1, 1, 2, 3 };

    juce::ValueTree project ("MidiMorfProject");
    project.setProperty ("version", 1, nullptr);
    project.setProperty ("name", "Untitled", nullptr);

    for (int t = 0; t < TrackRack::NumTracks; ++t)
    {
        juce::ValueTree track ("Track");
        track.setProperty ("index", t, nullptr);
        track.setProperty ("instrument", tour ? -1 : inst[t], nullptr);
        track.setProperty ("sound", tour ? 0 : sound[t], nullptr);
        track.setProperty ("volume", 1.0, nullptr);
        const juce::String presetName = t == 1 && ! tour ? "Acid Bass" : "";
        const auto* preset = presetName.isNotEmpty() ? WavetablePresets::findFactory (presetName) : nullptr;
        track.setProperty ("soundName", preset != nullptr ? presetName : juce::String ("Init"), nullptr);
        track.appendChild ((preset != nullptr ? preset->state : WavetablePresets::init().state).createCopy(), nullptr);
        juce::ValueTree sampler (SamplerSource::stateType);
        if (t == (tour ? 1 : 2))
            sampler.setProperty ("break", BreakLibrary::builtInNames()[0], nullptr);
        track.appendChild (sampler, nullptr);
        juce::ValueTree drums (DrumSource::stateType);
        drums.setProperty ("kit", Drums::factoryKits().front().name, nullptr);
        track.appendChild (drums, nullptr);
        project.appendChild (track, nullptr);
    }

    SessionModel s;
    SeqPattern step, slice, beat;
    static const int degrees[16] = { 0, 2, 4, 2, 7, 4, 2, 4, 0, 2, 4, 7, 9, 7, 4, 2 };
    for (int i = 0; i < 16; ++i)
    {
        step.steps[(size_t) i]  = { (uint8_t) degrees[i], (uint8_t) (i % 4 == 0 ? 112 : 80), 50, 64, false };
        slice.steps[(size_t) i] = { (uint8_t) i, 100, 95, 64, false };
    }
    const auto& boomBap = BreakLibrary::styles()[1];
    const char* lanes[3] = { boomBap.kick, boomBap.snare, boomBap.hat };
    const int laneOf[3] = { Drums::Kick, Drums::Snare, Drums::ClosedHat };
    for (int l = 0; l < 3; ++l)
        for (int i = 0; i < 16 && i < (int) std::strlen (lanes[l]); ++i)
            beat.drums[(size_t) laneOf[l]][(size_t) i] = (uint8_t) juce::roundToInt (BreakLibrary::detail::velocityFor (lanes[l][i]) * 127.0f);
    if (! tour)   // the tour's tracks get their clips as the player sets them up
    {
        s.setClip (1, 0, { "Pattern 1", step, KindStep });
        s.setClip (2, 0, { "Pattern 1", slice, KindSlice });
        s.setClip (3, 0, { "Pattern 1", beat, KindDrums });
        for (int t = 1; t < 4; ++t)
            s.setPlaying (t, 0);
    }
    project.appendChild (s.toValueTree(), nullptr);
    return project;
}

// Swaps the working copy for another project (or the starter) and reloads
// everything from it. The one being left is saved first.
void MainComponent::openProject (const juce::File& file)
{
    stopRecordingClip();
    for (auto& u : undo) u.reset();
    saveNow();
    audioEngine.allNotesOff();

    if (file.existsAsFile())
    {
        if (file.copyFileTo (projectFile()))
            loadProjectTree ({});
        return;
    }
    loadProjectTree (starterProject (drumGridPageIdx));
}

// Makes `project` the working copy (or, if invalid, reloads the working copy
// as it is) and reloads everything from it.
void MainComponent::loadProjectTree (const juce::ValueTree& project)
{
    audioEngine.allNotesOff();
    if (project.isValid())
    {
        auto xml = project.createXml();
        if (xml == nullptr || ! xml->writeTo (projectFile()))
            return;
    }

    restoreState();
    pageContainer.setPage (homePageIdx);
    homePage->refresh();
    updateRail();
}

//==============================================================================
// The walkthrough: the player makes their own beat on a fresh project, one
// step at a time. Each step points at the real control and moves on when
// they've done it. An untitled project they were working on is kept as
// "Before the tour".
void MainComponent::startWalkthrough()
{
    stopRecordingClip();
    saveNow();
    if (projectName == "Untitled" && session.hasClip (1, 0))
        projectFile().copyFileTo (ProjectBrowser::folder().getChildFile ("Before the tour.midimorf"));
    loadProjectTree (starterProject (drumGridPageIdx, true));

    auto& seqs = audioEngine.getSequencers();
    seqs.setBpm (140.0);   // the Amen's own tempo

    // The step page starts empty (all rests, all on the root) and the
    // slicer in order, so the first glitch is obvious.
    SeqPattern rests, inOrder;
    for (auto& st : rests.steps) st = { 0, 100, 50, 64, true };
    for (int i = 0; i < SeqPattern::MaxSteps; ++i) inOrder.steps[(size_t) i] = { (uint8_t) i, 100, 95, 64, false };
    stepPage (0).showPattern (rests);     // track 1's Step-Seq, sent when the player picks it
    slicePage (1).showPattern (inOrder);  // track 2's Breaker, likewise

    auto& breaker = slicePage (1).getContent();
    auto onPage    = [this] (int page) { return pageContainer.getCurrentPage() == page; };
    auto railTrack = [this] (int t) { return trackButtons[t]->getBounds(); };
    auto inPage    = [this] (juce::Component& c, juce::Rectangle<int> r) { return getLocalArea (&c, r); };
    auto onSession = [this] (juce::Rectangle<int> r) { return getLocalArea (homePage.get(), r); };
    auto stepsOn   = [this]
    {
        const auto p = stepPage (0).getPattern();
        int on = 0;
        for (int i = 0; i < p.length; ++i) on += p.steps[(size_t) i].rest ? 0 : 1;
        return on;
    };
    auto notesUsed = [this]
    {
        const auto p = stepPage (0).getPattern();
        std::set<int> degrees;
        for (int i = 0; i < p.length; ++i)
            if (! p.steps[(size_t) i].rest) degrees.insert (p.steps[(size_t) i].index);
        return (int) degrees.size();
    };
    auto slicesMoved = [this, &breaker]
    {
        const auto p = slicePage (1).getPattern();
        int moved = 0;
        for (int i = 0; i < breaker.numStepsShown(); ++i) moved += p.steps[(size_t) i].index != i || p.steps[(size_t) i].rest;
        return moved;
    };
    auto baseline = std::make_shared<std::array<int, 3>>();   // ribbon and looper counts when their step starts
    const int stepPageIdx  = seqPageIdx (KindStep, 0);
    const int slicePageIdx = seqPageIdx (KindSlice, 1);

    std::vector<Walkthrough::Step> steps = {
        { "Let's make something: a bassline you write yourself, then the Amen break, chopped up live. About two minutes.",
          nullptr, nullptr, "Let's go" },
        { "Every track has something that plays it. Pick Step-Seq for track 1.",
          [=] { return onSession (homePage->instrumentBounds (0)); }, [this] { return instrumentOf[0] == (int) MidiMorf::ControllerMode::StepSequencer; } },
        { "And a sound for it to play: pick Synth.",
          [=] { return onSession (homePage->soundBounds (0)); }, [this] { return audioEngine.getTrackSound (0) == AudioEngine::Sound::Synth; } },
        { "Tap 1 to open its Step-Seq.",
          [=] { return railTrack (0); }, [=] { return onPage (stepPageIdx); } },
        { "Tap 1 again to get to its sound.",
          [=] { return railTrack (0); }, [=] { return onPage (synthPageIdx); } },
        { "Tap the name to browse the synth sounds and pick one. Try the Bass ones.",
          [this] { return getLocalArea (&synthPage, synthPage.presetBarBounds()); },
          [this] { return audioEngine.getSoundName (0) != "Init"; } },
        { "Tap 1 to go back to the steps.",
          [=] { return railTrack (0); }, [=] { return onPage (stepPageIdx); } },
        { "Every step is off. Tap a few to switch them on.",
          [=] { return inPage (stepPage (0), stepPage (0).stepsBounds()); }, [=] { return stepsOn() >= 4; } },
        { "Now drag the sliders up and down to change each step's note.",
          [=] { return inPage (stepPage (0), stepPage (0).stepsBounds()); }, [=] { return notesUsed() >= 3; } },
        { "Press play.",
          [this] { return playButton.getBounds(); }, [&seqs] { return seqs.isAnyPlaying(); } },
        { "That's your bassline. Now the drums. Tap the house.",
          [this] { return homeButton.getBounds(); }, [=] { return onPage (homePageIdx); } },
        { "For track 2, pick Breaker: it plays a drum break in slices.",
          [=] { return onSession (homePage->instrumentBounds (1)); }, [this] { return instrumentOf[1] == (int) MidiMorf::ControllerMode::SliceSequencer; } },
        { "And for its sound, pick Sampler. It already holds the Amen break.",
          [=] { return onSession (homePage->soundBounds (1)); }, [this] { return audioEngine.getTrackSound (1) == AudioEngine::Sound::Sampler; } },
        { "Tap 2 to open Breaker.",
          [=] { return railTrack (1); }, [=] { return onPage (slicePageIdx); },
          {}, [&seqs] { seqs.setPlaying (1, true); } },
        { "It's playing the Amen in order, one slice per step. Tap and drag in the grid to change which slice plays where.",
          [=, &breaker] { return inPage (breaker, breaker.gridBounds()); }, [=] { return slicesMoved() >= 3; } },
        { "Now drag along the right-hand strip on top: it stutters each step, faster as you go.",
          [=, &breaker] { return inPage (breaker, breaker.ribbonBounds (1)); }, [=, &breaker] { return breaker.ribbonMoves[1] > (*baseline)[1]; },
          {}, [=, &breaker] { (*baseline)[1] = breaker.ribbonMoves[1]; } },
        { "The left strip bends the pitch. Try it.",
          [=, &breaker] { return inPage (breaker, breaker.ribbonBounds (0)); }, [=, &breaker] { return breaker.ribbonMoves[0] > (*baseline)[0]; },
          {}, [=, &breaker] { (*baseline)[0] = breaker.ribbonMoves[0]; } },
        { "Hold a square on the looper for a roll. Turn on RND first and every roll lands somewhere different.",
          [=, &breaker] { return inPage (breaker, breaker.looperBounds()); }, [=, &breaker] { return breaker.looperHolds > (*baseline)[2]; },
          {}, [=, &breaker] { (*baseline)[2] = breaker.looperHolds; } },
        { "Now go crazy. When you've got something, save it from Session: Project, then Save As. You can replay this tour from Settings.",
          nullptr, nullptr, "Done" },
    };
    walkthrough.setBounds (getLocalBounds());
    walkthrough.start (std::move (steps));
}

// A pad or lane name held: straight to the track's sound, outside any
// sequencer.
void MainComponent::audition (int track, int note, bool down)
{
    if (down) audioEngine.getRack().postNoteOn (track, 10, note, 110);
    else      audioEngine.getRack().postNoteOff (track, 10, note);
}

juce::String MainComponent::headerText (int pageIndex) const
{
    const int t = trackOfPage (pageIndex);
    if (t < 0)
        return getPageDisplayName (pageIndex);

    const juce::String dot   = juce::String::fromUTF8 ("  \xc2\xb7  ");
    const juce::String arrow = juce::String::fromUTF8 ("  \xe2\x86\x92  ");
    const juce::String track = "T" + juce::String (t + 1) + dot;

    if (pageIsSound (pageIndex))
        return track + audioEngine.getSoundName (t) + " sound";

    const auto sound = audioEngine.getTrackSound (t) == AudioEngine::Sound::Empty ? juce::String ("nothing")
                                                                                   : audioEngine.getSoundName (t);
    return track + getPageDisplayName (pageIndex) + arrow + sound;
}

// Before samplers, the slicer's break was saved beside the tempo, as
// seq_break / seq_breakFile. It moves into the sampler on that track.
void MainComponent::migrateBreak (int track, juce::PropertySet& pages)
{
    auto& s = audioEngine.getTrackSampler (track);
    s.setMode (SamplerSource::Slice);

    const auto path = pages.getValue ("seq_breakFile");
    juce::String error;
    if (path.isNotEmpty() && juce::File (path).existsAsFile() && s.importBreak (juce::File (path), error) != nullptr)
        return;
    s.setBuiltInBreak (juce::jmax (0, BreakLibrary::indexForName (pages.getValue ("seq_break"))));
}

// Each quarter gets its track's instrument page (or nothing) and a header
// in the same words as the top strip.
void MainComponent::populatePerform()
{
    for (int t = 0; t < TrackRack::NumTracks; ++t)
    {
        const int inst = pageOf (t);
        const juce::String text = inst >= 0 ? headerText (inst)
                                            : "T" + juce::String (t + 1) + juce::String::fromUTF8 ("  \xc2\xb7  ") + audioEngine.getSoundName (t);
        performView.setQuarter (t, inst >= 0 ? pageContainer.getPage (inst) : nullptr, text,
                                audioEngine.getRack().isMuted (t));
    }
}

void MainComponent::updateRail()
{
    const int page = pageContainer.getCurrentPage();
    const int current = trackOfPage (page);

    for (int t = 0; t < TrackRack::NumTracks; ++t)
    {
        const bool lit      = (t == current);
        const bool hasSound = audioEngine.getTrackSound (t) != AudioEngine::Sound::Empty;
        const int  inst     = instrumentOf[t];

        juce::String caption = inst >= 0 ? instrumentShortName (inst)
                             : hasSound  ? juce::String ("Sound")
                                         : juce::String::fromUTF8 ("\xe2\x80\x94");
        if (lit && pageIsSound (page))
            caption = "Sound";

        trackButtons[t]->setState (caption, lit, inst < 0 && ! hasSound);
    }

    homeButton.setLit (page == homePageIdx);
    performButton.setLit (page == performPageIdx);

    // ● on a controller track's own page (or its drum pads), to record what
    // you play there.
    const bool canRecord = current >= 0 && (! pageIsSound (page) || page == drumKitPageIdx)
                           && page != noteEditorPageIdx && sequencerOf (current) == -1;
    recordButton.setLit (recordingTrack >= 0);
    if (recordButton.isVisible() != (canRecord || recordingTrack >= 0))
    {
        recordButton.setVisible (canRecord || recordingTrack >= 0);
        resized();
    }
    header.set (current, headerText (page));
}

// Local sync: Play starts every sequencer that's some track's instrument;
// Stop stops them all. DAW sync: Play/Stop go to the DAW's transport.
void MainComponent::togglePlay()
{
    auto& seqs = audioEngine.getSequencers();

    if (! eventRouter.isLocalSync())
    {
        dawPlaying = ! dawPlaying;
        eventRouter.sendTransportButton ((uint8_t) (dawPlaying ? 0 : 1));   // 0 Play, 1 Stop
        playButton.setLit (dawPlaying);
        return;
    }

    if (seqs.isAnyPlaying())
    {
        seqs.stopAll();
        return;
    }

    for (int t = 0; t < TrackRack::NumTracks; ++t)
    {
        if (isSequencer (instrumentOf[t]))
            seqs.setPlaying (t, true);
        else if (const int s = session.getPlaying (t); session.hasClip (t, s) && session.clip (t, s)->isNotes())
            seqs.queueNoteClip (t, session.clip (t, s)->notes);
    }
}

//==============================================================================
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

    }

    // A track's sequencer page: its messages go to that track's engine, and
    // the server (which keeps no per-page memory) gets its whole state.
    if (isSeqPage (newPageIndex))
    {
        const int t = pageTrack (newPageIndex), kind = pageKind (newPageIndex);
        auto& router = *trackRouters[(size_t) t];
        audioEngine.getSequencers().setActiveTrack (t);
        if (kind == KindStep)
        {
            router.sendScreenMode (MidiMorf::ControllerMode::StepSequencer);
            stepPage (t).pushFullState();
        }
        else if (kind == KindSlice)
        {
            router.sendScreenMode (MidiMorf::ControllerMode::SliceSequencer);
            slicePage (t).pushFullState();
        }
        else if (kind >= 3)
        {
            router.sendScreenMode (controllerKinds[kind - 3]);
        }
    }

    updateRail();

    // Persist current state whenever page changes
    saveState();
}

// Timer
void MainComponent::timerCallback()
{
    if (eventRouter.isLocalSync())
    {
        auto& seqs = audioEngine.getSequencers();
        playButton.setLit (seqs.isAnyPlaying());
    }
    pollLaunches();

    // Write the project once things have been still for half a second, so
    // dragging a knob doesn't rewrite the file on every tick.
    if (saveRequested && juce::Time::getMillisecondCounter() - saveRequestedAt > 500)
        saveNow();
}

//==============================================================================
// State persistence
//
// The project — the music: each track's instrument, sound, volume and synth
// patch, the controller pages' settings, tempo and break — lives in one file,
// Documents/Projects/Current.midimorf. The settings file keeps only app
// preferences: WiFi/Local, sync source, last page, the edited sound.

static const juce::StringArray appSettingKeys { "lastPage", "mode", "seq_sync", "sound_track", "tour_done" };

static juce::File projectFile()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
               .getChildFile ("Projects").getChildFile ("Current.midimorf");
}

void MainComponent::saveState()
{
    saveRequested = true;
    saveRequestedAt = juce::Time::getMillisecondCounter();
}

void MainComponent::saveNow()
{
    saveRequested = false;

    // The project. Page settings go through a PropertySet, as they always
    // have, and ride along as one child.
    juce::PropertySet pageState;
    // Each track's controller pages keep their own settings (scales, keys,
    // voicings), one set per track.
    for (int t = 0; t < TrackRack::NumTracks; ++t)
    {
        juce::PropertySet ps;
        morfPadPages[(size_t) t]->saveState (ps);
        dualMPadPages[(size_t) t]->saveState (ps);
        morfChordPages[(size_t) t]->saveState (ps);
        roseChordPages[(size_t) t]->saveState (ps);
        if (auto xml = ps.createXml ("TrackPages"))
            pageState.setValue ("track_pages_" + juce::String (t), xml.get());
    }
    fxPage.saveState (pageState);
    audioEngine.getSequencers().saveState (pageState);
    for (int t = 0; t < TrackRack::NumTracks; ++t)
        drumPage (t).saveState (pageState);
    pageState.setValue ("rec_bars", recBars);
    pageState.setValue ("rec_quantize", recQuantize);
    pageState.setValue ("rec_metronome", recMetronome);

    for (int t = 0; t < TrackRack::NumTracks; ++t)
        captureClip (t);

    juce::ValueTree project ("MidiMorfProject");
    project.setProperty ("version", 1, nullptr);
    project.setProperty ("name", projectName, nullptr);
    {
        auto& rack = audioEngine.getRack();
        auto& buses = rack.getBuses();
        project.setProperty ("masterVolume", rack.getMasterVolume(), nullptr);
        project.setProperty ("delayTime", buses.delayTime.load(), nullptr);
        project.setProperty ("delayFeedback", buses.delayFeedback.load(), nullptr);
        project.setProperty ("reverbSize", buses.reverbSize.load(), nullptr);
    }
    for (int t = 0; t < TrackRack::NumTracks; ++t)
    {
        juce::ValueTree track ("Track");
        track.setProperty ("index", t, nullptr);
        track.setProperty ("instrument", instrumentOf[t], nullptr);
        track.setProperty ("sound", (int) audioEngine.getTrackSound (t), nullptr);
        track.setProperty ("volume", audioEngine.getRack().getVolume (t), nullptr);
        track.setProperty ("soundName", audioEngine.getSynthName (t), nullptr);
        track.setProperty ("muted", audioEngine.getRack().isMuted (t), nullptr);
        track.setProperty ("pan", audioEngine.getRack().getPan (t), nullptr);
        {
            auto& rack = audioEngine.getRack();
            auto& ins = rack.getInsert (t);
            track.setProperty ("solo", rack.isSolo (t), nullptr);
            track.setProperty ("sendDelay", rack.getSend (t, TrackRack::DelayBus), nullptr);
            track.setProperty ("sendReverb", rack.getSend (t, TrackRack::ReverbBus), nullptr);
            track.setProperty ("filter", ins.filter.load(), nullptr);
            track.setProperty ("drive", ins.drive.load(), nullptr);
            track.setProperty ("crush", ins.crush.load(), nullptr);
        }
        track.appendChild (audioEngine.getTrackSynth (t).getState(), nullptr);     // both kept, whichever
        track.appendChild (audioEngine.getTrackSampler (t).getState(), nullptr);   // the track plays
        track.appendChild (audioEngine.getTrackDrums (t).getState(), nullptr);
        project.appendChild (track, nullptr);
    }
    if (auto xml = pageState.createXml ("PageState"))
        project.appendChild (juce::ValueTree::fromXml (*xml), nullptr);
    project.appendChild (session.toValueTree(), nullptr);

    // Written beside the old file and swapped in, so a crash mid-write
    // can't leave half a project.
    bool written = false;
    if (auto xml = project.createXml())
    {
        const auto file = projectFile();
        file.getParentDirectory().createDirectory();
        juce::TemporaryFile temp (file);
        written = xml->writeTo (temp.getFile()) && temp.overwriteTargetFileWithTemporary();

        // A named project keeps its own copy too, so opening another never
        // loses it.
        if (written && projectName != "Untitled")
            file.copyFileTo (file.getSiblingFile (projectName).withFileExtension ("midimorf"));
    }

    if (auto* props = appProperties.getUserSettings())
    {
        props->setValue ("lastPage", pageContainer.getCurrentPage());
        props->setValue ("mode", eventRouter.getMode() == EventRouter::Mode::Local ? 1 : 0);
        props->setValue ("seq_sync", eventRouter.isLocalSync() ? 0 : 1);
        props->setValue ("sound_track", soundTrack);

        // Once the project is safely on disk, everything else in the settings
        // file is from before it existed.
        if (written)
        {
            const auto keys = props->getAllProperties().getAllKeys();   // copy: removing while iterating
            for (auto& key : keys)
                if (! appSettingKeys.contains (key))
                    props->removeValue (key);
        }
        props->saveIfNeeded();
    }
}

void MainComponent::restoreState()
{
    auto* settings = appProperties.getUserSettings();
    if (settings == nullptr)
        return;

    // The project, if there is one. Without it (first launch of this
    // version) everything comes from the settings file, as it used to.
    juce::ValueTree project;
    if (auto xml = juce::XmlDocument::parse (projectFile()))
        project = juce::ValueTree::fromXml (*xml);
    if (! project.hasType ("MidiMorfProject"))
    {
        project = {};

        // A file that's there but unreadable is set aside, not overwritten
        // by the defaults this launch will save.
        if (projectFile().existsAsFile())
            projectFile().moveFileTo (projectFile().withFileExtension ("corrupt"));
    }

    juce::PropertySet projectPages;
    if (project.isValid())
        if (auto xml = project.getChildWithName ("PageState").createXml())
            projectPages.restoreFromXml (*xml);
    juce::PropertySet& pages = project.isValid() ? projectPages : static_cast<juce::PropertySet&> (*settings);

    // A project from before per-track pages had one set: every track's copy
    // starts from it.
    for (int t = 0; t < TrackRack::NumTracks; ++t)
    {
        juce::PropertySet own;
        const auto xml = pages.getXmlValue ("track_pages_" + juce::String (t));
        if (xml != nullptr)
            own.restoreFromXml (*xml);
        juce::PropertySet& ps = xml != nullptr ? own : pages;
        morfPadPages[(size_t) t]->restoreState (ps);
        dualMPadPages[(size_t) t]->restoreState (ps);
        morfChordPages[(size_t) t]->restoreState (ps);
        roseChordPages[(size_t) t]->restoreState (ps);
    }
    fxPage.restoreState (pages);
    audioEngine.getSequencers().restoreState (pages);
    for (int t = 0; t < TrackRack::NumTracks; ++t)
        drumPage (t).restoreState (pages);
    recBars      = juce::jlimit (1, 8, pages.getIntValue ("rec_bars", 2));
    recQuantize  = pages.getBoolValue ("rec_quantize", false);
    recMetronome = pages.getBoolValue ("rec_metronome", true);
    eventRouter.setSyncSource (settings->getIntValue ("seq_sync", 0) == 1 ? EventRouter::SyncSource::DAW
                                                                          : EventRouter::SyncSource::Local);

    // First-launch default: Local mode. New users hear the built-in
    // synth immediately when they tap a controller — no server setup
    // needed for a satisfying first-play. Existing users' saved mode
    // still wins because it's read from the same key.
    eventRouter.setMode (settings->getIntValue ("mode", 1) == 1 ? EventRouter::Mode::Local : EventRouter::Mode::WiFi);
    settingsPage.refreshFromRouter();
    fxPage.refreshFromRouter();

    // Tracks. Defaults: T1 Morf-Chord on a synth, T2 Step-Seq on the bass
    // synth, T3 Slice-Seq on the breaks, T4 Drum Grid on the drum synth.
    using CM = MidiMorf::ControllerMode;
    const int defaultInst[TrackRack::NumTracks]  = { (int) CM::MorfChord, (int) CM::StepSequencer, (int) CM::SliceSequencer, drumGridPageIdx };
    const int defaultSound[TrackRack::NumTracks] = { 1, 1, 2, 3 };

    if (project.isValid())
    {
        for (int t = 0; t < TrackRack::NumTracks; ++t)
        {
            const auto track = project.getChildWithProperty ("index", t);
            setSound (t, track.getProperty ("sound", defaultSound[t]));
            if (auto synth = track.getChildWithName (WavetableEngine::stateType); synth.isValid())
                audioEngine.getTrackSynth (t).setState (synth);
            if (auto sampler = track.getChildWithName (SamplerSource::stateType); sampler.isValid())
                audioEngine.getTrackSampler (t).setState (sampler);
            else if (audioEngine.getTrackSound (t) == AudioEngine::Sound::Sampler)
                migrateBreak (t, pages);   // a project from before samplers
            if (auto kit = track.getChildWithName (DrumSource::stateType); kit.isValid())
                audioEngine.getTrackDrums (t).setState (kit);
            setInstrument (t, track.getProperty ("instrument", defaultInst[t]));
            audioEngine.getRack().setVolume (t, (float) (double) track.getProperty ("volume", 1.0));
            audioEngine.setSoundName (t, track["soundName"].toString());
            audioEngine.getRack().setMuted (t, track.getProperty ("muted", false));
            audioEngine.getRack().setPan (t, (float) (double) track.getProperty ("pan", 0.0));
            {
                auto& rack = audioEngine.getRack();
                auto& ins = rack.getInsert (t);
                rack.setSolo (t, track.getProperty ("solo", false));
                rack.setSend (t, TrackRack::DelayBus, (float) (double) track.getProperty ("sendDelay", 0.0));
                rack.setSend (t, TrackRack::ReverbBus, (float) (double) track.getProperty ("sendReverb", 0.0));
                ins.filter = (float) (double) track.getProperty ("filter", 0.0);
                ins.drive  = (float) (double) track.getProperty ("drive", 0.0);
                ins.crush  = (float) (double) track.getProperty ("crush", 0.0);
            }
        }
    }
    else
    {
        // From the settings file: T1's patch under its original key (or the
        // older formats WavetableSynthPage still reads), the rest per track.
        synthPage.restoreState (*settings);

        for (int t = 0; t < TrackRack::NumTracks; ++t)
        {
            setSound (t, settings->getIntValue ("trk_sound_" + juce::String (t), defaultSound[t]));
            if (audioEngine.getTrackSound (t) == AudioEngine::Sound::Sampler)
                migrateBreak (t, *settings);
        }

        for (int t = 1; t < TrackRack::NumTracks; ++t)
        {
            auto xml = settings->getXmlValue ("trk_synth_" + juce::String (t));
            if (xml == nullptr && t == 1)
                xml = settings->getXmlValue ("t2_state");   // the bass, as saved before tracks had their own keys
            if (xml != nullptr)
                audioEngine.getTrackSynth (t).setState (juce::ValueTree::fromXml (*xml));
        }

        for (int t = 0; t < TrackRack::NumTracks; ++t)
        {
            const juce::String n (t);
            setInstrument (t, settings->getIntValue ("trk_inst_" + n, defaultInst[t]));
            audioEngine.getRack().setVolume (t, (float) settings->getDoubleValue ("trk_vol_" + n, 1.0));
        }
    }

    // The Session: its clips, or a first clip per sequencer track from what
    // the pages hold (a project from before Session).
    projectName = project.isValid() ? project.getProperty ("name", "Untitled").toString() : juce::String ("Untitled");
    {
        auto& rack = audioEngine.getRack();
        auto& buses = rack.getBuses();
        rack.setMasterVolume ((float) (double) project.getProperty ("masterVolume", 1.0));
        buses.delayTime     = juce::jlimit (0, 3, (int) project.getProperty ("delayTime", 1));
        buses.delayFeedback = (float) (double) project.getProperty ("delayFeedback", 0.45);
        buses.reverbSize    = (float) (double) project.getProperty ("reverbSize", 0.6);
    }
    session.fromValueTree (project.getChildWithName ("Session"));
    seedClips();
    applySessionToSequencers();

    soundTrack = juce::jlimit (0, TrackRack::NumTracks - 1, settings->getIntValue ("sound_track", 0));
    if (audioEngine.getWavetable (soundTrack) != nullptr)
        synthPage.edit (audioEngine.getTrackSynth (soundTrack), audioEngine.getSoundName (soundTrack), TrackColours::get (soundTrack));

    // The Sampler page starts on the first track playing a sampler.
    for (int t = 0; t < TrackRack::NumTracks; ++t)
        if (audioEngine.getTrackSound (t) == AudioEngine::Sound::Sampler)
        {
            samplerPage.edit (audioEngine.getTrackSampler (t), t);
            break;
        }

    // The Drums page likewise, on the first track playing drums.
    for (int t = 0; t < TrackRack::NumTracks; ++t)
        if (audioEngine.getTrackSound (t) == AudioEngine::Sound::Drums)
        {
            drumKitPage.edit (audioEngine.getTrackDrums (t), t);
            break;
        }

    // Last page, if it still belongs somewhere; otherwise T1's
    // instrument (Morf-Chord on first launch), or Home.
    int lastPage = settings->getIntValue ("lastPage", -1);
    const bool reachable = lastPage >= 0 && lastPage < numPages
                           && ((lastPage >= MidiMorf::NUM_CONTROLLERS && lastPage < seqPageBase && lastPage != drumGridPageIdx && lastPage != noteEditorPageIdx)
                               || lastPage == (int) CM::Transport || lastPage == (int) CM::FXPunchIn
                               || trackOfPage (lastPage) >= 0);
    if (! reachable)
        lastPage = instrumentOf[0] >= 0 ? pageOf (0) : homePageIdx;
    if (lastPage == performPageIdx && ! isIPad())
        lastPage = homePageIdx;

    pageContainer.setPage (lastPage);
    updateRail();   // setPage does nothing if lastPage is already showing

    // Writes the project straight away, which also moves an older install's
    // settings into it.
    saveNow();
}

void MainComponent::seqPlayheadChanged (int step)
{
    // The server only streams the playhead for the page it thinks the phone
    // is on; applying it to both pages is harmless and keeps this simple.
    const int t = audioEngine.getSequencers().getActiveTrack();
    stepPage (t).setPlayhead (step);
    slicePage (t).setPlayhead (step);
}
