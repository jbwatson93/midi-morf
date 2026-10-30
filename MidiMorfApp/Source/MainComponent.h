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
#include "HomePage.h"
#include "BreaksPage.h"
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

    //==========================================================================
    PageContainer pageContainer;
    FXPunchInPage fxPage;
    MorfPadPage morfPadPage;
    OmnichordPage omnichordPage;
    TransportPage transportPage;
    DualMPadPage dualMPadPage;
    MorfChordPage morfChordPage;
    RoseChordPage roseChordPage;
    StepSequencerPage stepSequencerPage;
    SliceSequencerPage sliceSequencerPage;
    WavetableSynthPage synthPage { audioEngine.getSynth() };
    AboutPage aboutPage;
    SettingsPage settingsPage { eventRouter };
    std::unique_ptr<HomePage> homePage;
    BreaksPage breaksPage { audioEngine.getSequencers() };
    PerformView performView;   // iPad only

    TrackHeader header;
    RailIconButton homeButton { RailIconButton::Kind::Home };
    RailIconButton performButton { RailIconButton::Kind::Perform };
    RailIconButton playButton { RailIconButton::Kind::PlayStop };
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
