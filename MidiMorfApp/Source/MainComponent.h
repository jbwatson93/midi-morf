#pragma once

#include <JuceHeader.h>
#include "PageContainer.h"
#include "FXPunchInPage.h"
#include "MorfPadPage.h"
#include "OmnichordPage.h"
#include "TransportPage.h"
#include "DualMPadPage.h"
#include "MorfChordPage.h"
#include "RoseChordPage.h"
#include "StepSequencerPage.h"
#include "SliceSequencerPage.h"
#include "WavetableSynthPage.h"
#include "AboutPage.h"
#include "SettingsPage.h"
#include "SessionPage.h"
#include "ProjectBrowser.h"
#include "NoteEditorPage.h"
#include "Walkthrough.h"
#include "SamplerPage.h"
#include "DrumGridPage.h"
#include "DrumKitPage.h"
#include "PerformView.h"
#include "NetworkClient.h"
#include "AccelerometerManager.h"
#include "MidiMorfProtocol.h"
#include "MidiMorfColors.h"
#include "AudioEngine.h"
#include "EventRouter.h"
#include "TrackRail.h"

// The app is organised around four tracks. Each track is one instrument (a
// controller or sequencer page) playing one sound (a synth, the breaks, or
// nothing). The wood rail is Home, the four tracks and Play/Stop: tapping a
// track shows its instrument, tapping it again its sound. Home sets up the
// tracks and holds the pages that belong to none (Transport, Morf FX,
// Settings, About).
class MainComponent : public juce::Component,
                      public NetworkClient::Listener,
                      public AccelerometerManager::Listener,
                      public PageContainer::Listener,
                      public juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // NetworkClient::Listener
    void connectedToPlugin (const juce::String& hostName) override;
    void disconnectedFromPlugin() override;
    void slotConfigReceived (int slotIndex, const juce::String& name) override;
    void seqPlayheadChanged (int step) override;

    // AccelerometerManager::Listener
    void tiltUpdated (float tiltX, float tiltY) override;

    // PageContainer::Listener
    void pageChanged (int newPageIndex) override;

    // Timer
    void timerCallback() override;

private:
    AudioEngine audioEngine;
    NetworkClient networkClient;
    EventRouter eventRouter { networkClient, audioEngine.getRack(), audioEngine.getSequencers() };
    AccelerometerManager accelerometer;

public:
    // Called from the app's suspended()/resumed() hooks so audio is released
    // when the app is backgrounded (otherwise iOS keeps the session open and
    // you get stuck-note / feedback artefacts).
    AudioEngine& getAudioEngine() noexcept { return audioEngine; }

    // Writes the project and settings now rather than after the usual
    // half-second pause — for suspend and shutdown.
    void saveNow();

private:
    //==========================================================================
    // Tracks
    void setInstrument (int track, int mode);   // mode: a ControllerMode, or -1
    void setSound (int track, int sound);       // AudioEngine::Sound as int
    void trackTapped (int track);
    void showInstrument (int track);
    void showSound (int track);
    int  trackOfPage (int pageIndex) const;     // -1 if the page belongs to no track
    bool pageIsSound (int pageIndex) const;
    void updateRail();
    void togglePlay();
    juce::String headerText (int pageIndex) const;
    juce::String getPageDisplayName (int pageIndex) const;

    int instrumentOf[TrackRack::NumTracks] { -1, -1, -1, -1 };
    int soundTrack = 0;     // whose synth the Sound page is editing
    bool dawPlaying = false;
    int  focusedMode = -1;   // the page shared sequencer messages go to, in Perform
    void populatePerform();
    void migrateBreak (int track, juce::PropertySet& pages);
    void audition (int track, int note, bool down);

    //==========================================================================
    // Session clips
    int  sequencerOf (int track) const;                 // LocalSequencers::Kind as int, or -1
    int  pageOf (int track) const;                      // the track's instrument page, or -1
    StepSequencerPage&  stepPage (int t)  { return *stepPages[(size_t) t]; }
    SliceSequencerPage& slicePage (int t) { return *slicePages[(size_t) t]; }
    DrumGridPage&       drumPage (int t)  { return *drumPages[(size_t) t]; }
    SeqPattern pagePattern (int which);
    void showOnPage (int which, const SeqPattern& p);
    void captureClip (int track);                       // the page's edits into the playing clip
    void launchClip (int track, int scene);
    void newClip (int track, int scene);
    void editClip (int track, int scene);
    void duplicateClip (int track, int scene);
    void deleteClip (int track, int scene);
    void launchScene (int scene);
    void stopTrack (int track);
    void pollLaunches();
    void seedClips();                                   // a first clip per sequencer track

    // Recording note clips (controller tracks)
    void recordClip (int track, int scene);             // a new take into an empty slot, or overdub
    void stopRecordingClip();
    void collectPasses();                               // timer: finished passes into the clip
    void undoLastPass (int track);
    void quantizeClip (int track, int scene);
    void railRecord();                                  // the rail's ● on a controller page
    void showNoteEditor (int track, int scene);
    int  recordingTrack = -1, recordingScene = -1, seenPasses = 0;
    int  recBars = 2;
    bool recQuantize = false, recMetronome = true;
    struct Undo { int scene; NoteClip notes; };
    std::array<std::optional<Undo>, TrackRack::NumTracks> undo;
    int  shownLaunches[TrackRack::NumTracks] { 0, 0, 0, 0 };

    //==========================================================================
    // Projects: the working one autosaves to Current.midimorf, and a named
    // one also to <name>.midimorf beside it.
    void showProjects();
    void openProject (const juce::File& file);   // a named project file, or {} for a new one
    void saveProjectAs (const juce::String& name);
    void loadProjectTree (const juce::ValueTree& project);
    void startWalkthrough();
    Walkthrough walkthrough;
    void applySessionToSequencers();
    juce::String projectName { "Untitled" };
    std::unique_ptr<ProjectBrowser> projectBrowser;

    //==========================================================================
    PageContainer pageContainer;
    FXPunchInPage fxPage;
    MorfPadPage morfPadPage;
    OmnichordPage omnichordPage;
    TransportPage transportPage;
    DualMPadPage dualMPadPage;
    MorfChordPage morfChordPage;
    RoseChordPage roseChordPage;
    // Every track has its own sequencer pages; the one-of-each slots they
    // replaced stay as placeholders so the page numbers don't move.
    std::array<std::unique_ptr<StepSequencerPage>, TrackRack::NumTracks> stepPages;
    std::array<std::unique_ptr<SliceSequencerPage>, TrackRack::NumTracks> slicePages;
    std::array<std::unique_ptr<DrumGridPage>, TrackRack::NumTracks> drumPages;
    juce::Component stepPlaceholder, slicePlaceholder, drumPlaceholder;
    WavetableSynthPage synthPage { audioEngine.getSynth() };
    AboutPage aboutPage;
    SettingsPage settingsPage { eventRouter };
    SessionModel session;
    std::unique_ptr<SessionPage> homePage;   // the Session page; Home before it
    SamplerPage samplerPage;
    PerformView performView;   // iPad only
    DrumKitPage drumKitPage;
    NoteEditorPage noteEditorPage;
    int editingScene = -1;   // the clip the note editor has open

    TrackHeader header;
    RailIconButton homeButton { RailIconButton::Kind::Home };
    RailIconButton performButton { RailIconButton::Kind::Perform };
    RailIconButton playButton { RailIconButton::Kind::PlayStop };
    RailIconButton recordButton { RailIconButton::Kind::Record };
    juce::OwnedArray<RailTrackButton> trackButtons;

    bool connected { false };
    juce::String connectedHost;

    juce::ApplicationProperties appProperties;

    void saveState();      // asks for a save; the timer writes it once things settle
    void restoreState();
    bool saveRequested = false;
    juce::uint32 saveRequestedAt = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
