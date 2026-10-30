#include "MainComponent.h"

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
    "Slice-Seq"
};

// Short names for the rail, same order.
static const juce::String controllerShortNames[] = {
    "FX", "Pad", "Strum", "Transport", "Dual", "Chord", "Rose", "Step", "Slice"
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
static constexpr int numPages         = MidiMorf::NUM_CONTROLLERS + 8;

// The four-at-once Perform view is for iPad; an iPhone screen is too small.
static bool isIPad()
{
   #if JUCE_IOS
    return [UIDevice currentDevice].userInterfaceIdiom == UIUserInterfaceIdiomPad;
   #else
    return false;
   #endif
}

static bool isSequencer (int mode)
{
    return mode == (int) MidiMorf::ControllerMode::StepSequencer
        || mode == (int) MidiMorf::ControllerMode::SliceSequencer
        || mode == drumGridPageIdx;
}

static LocalSequencers::Which whichSequencer (int mode)
{
    if (mode == drumGridPageIdx)
        return LocalSequencers::Drums;
    return mode == (int) MidiMorf::ControllerMode::StepSequencer ? LocalSequencers::Step : LocalSequencers::Slice;
}

// An instrument's name on the rail.
static juce::String instrumentShortName (int mode)
{
    if (mode == drumGridPageIdx)
        return "Drums";
    return juce::isPositiveAndBelow (mode, MidiMorf::NUM_CONTROLLERS) ? controllerShortNames[mode] : juce::String();
}

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

    // Morf FX plays the master bus's punch-in FX in Local mode.
    eventRouter.setPunchFx (&audioEngine.getPunchFx());

    SessionPage::Callbacks cb;
    cb.getInstrument = [this] (int t) { return instrumentOf[t]; };
    cb.setInstrument = [this] (int t, int mode) { setInstrument (t, mode); updateRail(); };
    cb.getSound      = [this] (int t) { return (int) audioEngine.getTrackSound (t); };
    cb.setSound      = [this] (int t, int s) { setSound (t, s); updateRail(); };
    cb.getVolume     = [this] (int t) { return audioEngine.getRack().getVolume (t); };
    cb.setVolume     = [this] (int t, float g) { audioEngine.getRack().setVolume (t, g); };
    cb.sequencerOf   = [this] (int t) { return sequencerOf (t); };
    cb.isRunning     = [this] (int t)
    {
        const int w = sequencerOf (t);
        return w >= 0 && audioEngine.getSequencers().getPlayhead ((LocalSequencers::Which) w) >= 0;
    };
    cb.launchClip    = [this] (int t, int s) { launchClip (t, s); };
    cb.newClip       = [this] (int t, int s) { newClip (t, s); };
    cb.editClip      = [this] (int t, int s) { editClip (t, s); };
    cb.duplicateClip = [this] (int t, int s) { duplicateClip (t, s); };
    cb.deleteClip    = [this] (int t, int s) { deleteClip (t, s); };
    cb.launchScene   = [this] (int s) { launchScene (s); };
    cb.stopTrack     = [this] (int t) { stopTrack (t); };
    cb.openPage      = [this] (int page) { pageContainer.setPage (page); };
    cb.openProjects  = [this] { showProjects(); };
    cb.projectName   = [this] { return projectName; };
    cb.changed       = [this] { saveState(); };
    homePage = std::make_unique<SessionPage> (session, cb, SessionPage::Pages { (int) MidiMorf::ControllerMode::Transport,
                                                                                (int) MidiMorf::ControllerMode::FXPunchIn,
                                                                                settingsPageIdx, aboutPageIdx, drumGridPageIdx });

    // Page container — controllers, then Sound, About, Settings, Home, Breaks.
    // The order is the page indices above.
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
    pageContainer.addPage (homePage.get());
    pageContainer.addPage (&samplerPage);
    pageContainer.addPage (&performView);
    pageContainer.addPage (&drumGridPage);
    pageContainer.addPage (&drumKitPage);

    // Perform borrows the tracks' instrument pages and hands them back.
    performView.onShow       = [this] { populatePerform(); };
    performView.onReturnPage = [this] (juce::Component* p) { pageContainer.reclaim (p); };
    performView.onExpand     = [this] (int t)
    {
        pageContainer.setPage (instrumentOf[t] >= 0 ? instrumentOf[t] : homePageIdx);
    };
    performView.onMute       = [this] (int t, bool m) { audioEngine.getRack().setMuted (t, m); saveState(); };

    // Step-Seq and Slice-Seq share some messages (steps, transport, ratchet)
    // that go to the page last shown. With several on screen, the one being
    // touched is the one they mean.
    performView.onTouched    = [this] (int t)
    {
        const int mode = instrumentOf[t];
        if (juce::isPositiveAndBelow (mode, MidiMorf::NUM_CONTROLLERS) && mode != focusedMode)
        {
            focusedMode = mode;
            eventRouter.sendScreenMode (static_cast<MidiMorf::ControllerMode> (mode));
        }
    };
    pageContainer.setListener (this);
    addAndMakeVisible (pageContainer);
    addAndMakeVisible (header);

    settingsPage.onModeChanged = [this] { fxPage.refreshFromRouter(); saveState(); };
    settingsPage.getAudioLoad  = [this] { return audioEngine.getCpuUsage(); };

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

    drumGridPage.onStateChanged = [this] { saveState(); };
    drumGridPage.onAudition = [this] (int note, bool down)
    {
        audition (audioEngine.getSequencers().getTrack (LocalSequencers::Drums), note, down);
    };
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
        if (b != nullptr && t == eventRouter.getTrackFor (MidiMorf::ControllerMode::SliceSequencer)
            && audioEngine.getTrackSampler (t).getMode() == SamplerSource::Slice)
            audioEngine.getSequencers().setBpm (b->bpm());
    };
    fxPage.onStateChanged = [this] { saveState(); };
    transportPage.onStateChanged = [this] { saveState(); };
    stepSequencerPage.setOnStateChanged ([this] { saveState(); });
    sliceSequencerPage.setOnStateChanged ([this] { saveState(); });

    // Network
    networkClient.setListener (this);
    networkClient.startSearching();

    // Accelerometer
    accelerometer.setListener (this);
    accelerometer.start();

    // Restore saved state (track setup, last page, scale settings, etc.)
    restoreState();

    // Keeps Play/Stop's light in step with the sequencers.
    startTimerHz (10);

    // Landscape initial size hint
    setSize (800, 400);
}

MainComponent::~MainComponent()
{
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
    if (pageIndex == drumKitPageIdx)   return "Drums";
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

    for (int t = 0; t < TrackRack::NumTracks; ++t)
        if (t != track && mode >= 0 && instrumentOf[t] == mode)
            instrumentOf[t] = -1;

    const int old = instrumentOf[track];
    if (old >= 0 && old != mode && isSequencer (old))
        audioEngine.getSequencers().setPlaying (whichSequencer (old), false);

    instrumentOf[track] = mode;
    if (mode == drumGridPageIdx)
    {
        audioEngine.getSequencers().setTrack (LocalSequencers::Drums, track);
        drumGridPage.setTrack (track);
    }
    else if (mode >= 0)
    {
        eventRouter.setTrackFor (static_cast<MidiMorf::ControllerMode> (mode), track);
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

    for (int t = 0; t < TrackRack::NumTracks; ++t)
        if (instrumentOf[t] == pageIndex)
            return t;
    return -1;
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
        pageContainer.setPage (instrumentOf[track]);
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
    return isSequencer (inst) ? (int) whichSequencer (inst) : -1;
}

SeqPattern MainComponent::pagePattern (int which)
{
    switch (which)
    {
        case LocalSequencers::Step:  return stepSequencerPage.getPattern();
        case LocalSequencers::Slice: return sliceSequencerPage.getPattern();
        default:                     return drumGridPage.getPattern();
    }
}

void MainComponent::showOnPage (int which, const SeqPattern& p)
{
    switch (which)
    {
        case LocalSequencers::Step:  stepSequencerPage.showPattern (p); break;
        case LocalSequencers::Slice: sliceSequencerPage.showPattern (p); break;
        default:                     drumGridPage.showPattern (p); break;
    }
}

void MainComponent::captureClip (int track)
{
    const int w = sequencerOf (track), s = session.getPlaying (track);
    if (w >= 0 && session.hasClip (track, s) && session.clip (track, s)->sequencer == w)
        session.clip (track, s)->pattern = pagePattern (w);
}

void MainComponent::launchClip (int track, int scene)
{
    const int w = sequencerOf (track);
    if (w < 0 || ! session.hasClip (track, scene) || session.clip (track, scene)->sequencer != w)
        return;

    captureClip (track);
    const auto& pattern = session.clip (track, scene)->pattern;
    auto& seqs = audioEngine.getSequencers();
    const auto which = (LocalSequencers::Which) w;

    if (which == LocalSequencers::Drums || eventRouter.isLocalSync())
    {
        const bool wasRunning = seqs.getPlayhead (which) >= 0;
        seqs.queuePattern (which, pattern);
        if (wasRunning)
        {
            session.setQueued (track, scene);   // takes over on the bar; pollLaunches moves it to playing
        }
        else
        {
            showOnPage (w, pattern);
            session.setPlaying (track, scene);
            shownLaunches[w] = seqs.getLaunchCount (which);
        }
        seqs.setPlaying (which, true);
    }
    else
    {
        switch (which)
        {
            case LocalSequencers::Step:  stepSequencerPage.loadPattern (pattern); break;
            case LocalSequencers::Slice: sliceSequencerPage.loadPattern (pattern); break;
            default: break;
        }
        session.setPlaying (track, scene);
    }
    saveState();
}

// A new clip starts as a copy of what the track is playing, so a variation is
// one tap away.
void MainComponent::newClip (int track, int scene)
{
    const int w = sequencerOf (track);
    if (w < 0) return;
    captureClip (track);
    session.setClip (track, scene, { session.freshName (track), pagePattern (w), w });
    launchClip (track, scene);
    homePage->refresh();
}

void MainComponent::editClip (int track, int scene)
{
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
            copy.name = session.freshName (track);
            session.setClip (track, s, copy);
            saveState();
            return;
        }
}

void MainComponent::deleteClip (int track, int scene)
{
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
        if (w < 0) continue;
        if (session.hasClip (t, scene) && session.clip (t, scene)->sequencer == w)
            launchClip (t, scene);
        else
            stopTrack (t);
    }
}

void MainComponent::stopTrack (int track)
{
    const int w = sequencerOf (track);
    if (w < 0) return;
    session.setQueued (track, -1);
    audioEngine.getSequencers().queueStop ((LocalSequencers::Which) w);
}

// Timer: a queued clip that has taken over becomes the playing one, and its
// page shows it.
void MainComponent::pollLaunches()
{
    auto& seqs = audioEngine.getSequencers();
    for (int w = 0; w < LocalSequencers::NumSequencers; ++w)
    {
        const int count = seqs.getLaunchCount ((LocalSequencers::Which) w);
        if (count == shownLaunches[w])
            continue;
        shownLaunches[w] = count;
        showOnPage (w, seqs.getPattern ((LocalSequencers::Which) w));
        for (int t = 0; t < TrackRack::NumTracks; ++t)
            if (sequencerOf (t) == w && session.getQueued (t) >= 0)
                session.setPlaying (t, session.getQueued (t));
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
        session.setClip (t, 0, { "Pattern 1", pagePattern (w), w });
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
        seqs.queuePattern ((LocalSequencers::Which) w, p);   // stopped: applies at once
        switch (w)
        {
            case LocalSequencers::Step:  stepSequencerPage.loadPattern (p); break;
            case LocalSequencers::Slice: sliceSequencerPage.loadPattern (p); break;
            default:                     drumGridPage.showPattern (p); break;
        }
        shownLaunches[w] = seqs.getLaunchCount ((LocalSequencers::Which) w);
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
static juce::ValueTree starterProject (int drumGridMode)
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
        track.setProperty ("instrument", inst[t], nullptr);
        track.setProperty ("sound", sound[t], nullptr);
        track.setProperty ("volume", 1.0, nullptr);
        const auto* preset = t == 1 ? WavetablePresets::findFactory ("Acid Bass") : nullptr;
        track.setProperty ("soundName", preset != nullptr ? "Acid Bass" : "Init", nullptr);
        track.appendChild ((preset != nullptr ? preset->state : WavetablePresets::init().state).createCopy(), nullptr);
        juce::ValueTree sampler (SamplerSource::stateType);
        if (t == 2)
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
    s.setClip (1, 0, { "Pattern 1", step, LocalSequencers::Step });
    s.setClip (2, 0, { "Pattern 1", slice, LocalSequencers::Slice });
    s.setClip (3, 0, { "Pattern 1", beat, LocalSequencers::Drums });
    for (int t = 1; t < 4; ++t)
        s.setPlaying (t, 0);
    project.appendChild (s.toValueTree(), nullptr);
    return project;
}

// Swaps the working copy for another project (or the starter) and reloads
// everything from it. The one being left is saved first.
void MainComponent::openProject (const juce::File& file)
{
    saveNow();
    audioEngine.allNotesOff();

    const auto current = projectFile();
    bool ok = false;
    if (file.existsAsFile())
    {
        ok = file.copyFileTo (current);
    }
    else if (auto xml = starterProject (drumGridPageIdx).createXml())
    {
        ok = xml->writeTo (current);
    }
    if (! ok)
        return;

    restoreState();
    pageContainer.setPage (homePageIdx);
    homePage->refresh();
    updateRail();
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
    focusedMode = -1;
    for (int t = 0; t < TrackRack::NumTracks; ++t)
    {
        const int inst = instrumentOf[t];
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
        if (isSequencer (instrumentOf[t]))
            seqs.setPlaying (whichSequencer (instrumentOf[t]), true);
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

        // The server keeps no per-page memory, so the sequencers have to
        // resend their whole state whenever their page comes back into focus.
        if (mode == MidiMorf::ControllerMode::StepSequencer)
            stepSequencerPage.pushFullState();
        else if (mode == MidiMorf::ControllerMode::SliceSequencer)
            sliceSequencerPage.pushFullState();
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

static const juce::StringArray appSettingKeys { "lastPage", "mode", "seq_sync", "sound_track" };

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
    morfPadPage.saveState (pageState);
    dualMPadPage.saveState (pageState);
    morfChordPage.saveState (pageState);
    roseChordPage.saveState (pageState);
    fxPage.saveState (pageState);
    audioEngine.getSequencers().saveState (pageState);
    drumGridPage.saveState (pageState);

    for (int t = 0; t < TrackRack::NumTracks; ++t)
        captureClip (t);

    juce::ValueTree project ("MidiMorfProject");
    project.setProperty ("version", 1, nullptr);
    project.setProperty ("name", projectName, nullptr);
    for (int t = 0; t < TrackRack::NumTracks; ++t)
    {
        juce::ValueTree track ("Track");
        track.setProperty ("index", t, nullptr);
        track.setProperty ("instrument", instrumentOf[t], nullptr);
        track.setProperty ("sound", (int) audioEngine.getTrackSound (t), nullptr);
        track.setProperty ("volume", audioEngine.getRack().getVolume (t), nullptr);
        track.setProperty ("soundName", audioEngine.getSynthName (t), nullptr);
        track.setProperty ("muted", audioEngine.getRack().isMuted (t), nullptr);
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

    morfPadPage.restoreState (pages);
    dualMPadPage.restoreState (pages);
    morfChordPage.restoreState (pages);
    roseChordPage.restoreState (pages);
    fxPage.restoreState (pages);
    audioEngine.getSequencers().restoreState (pages);
    drumGridPage.restoreState (pages);
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
                           && ((lastPage >= MidiMorf::NUM_CONTROLLERS && lastPage != drumGridPageIdx)
                               || lastPage == (int) CM::Transport || lastPage == (int) CM::FXPunchIn
                               || trackOfPage (lastPage) >= 0);
    if (! reachable)
        lastPage = instrumentOf[0] >= 0 ? instrumentOf[0] : homePageIdx;
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
    stepSequencerPage.setPlayhead (step);
    sliceSequencerPage.setPlayhead (step);
}
