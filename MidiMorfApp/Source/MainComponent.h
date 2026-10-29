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
#include "NetworkClient.h"
#include "AccelerometerManager.h"
#include "MidiMorfProtocol.h"
#include "MidiMorfColors.h"
#include "AudioEngine.h"
#include "EventRouter.h"
#include "PianoKeysButton.h"
#include "SynthShortcutButton.h"
#include "FavoriteButton.h"

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
    void showNavigationMenu();

    AudioEngine audioEngine;
    NetworkClient networkClient;
    EventRouter eventRouter { networkClient, audioEngine.getRack(), audioEngine.getSequencers() };
    AccelerometerManager accelerometer;

public:
    // Called from the app's suspended()/resumed() hooks so audio is released
    // when the app is backgrounded (otherwise iOS keeps the session open and
    // you get stuck-note / feedback artefacts).
    AudioEngine& getAudioEngine() noexcept { return audioEngine; }

private:

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

    PianoKeysButton menuButton;
    SynthShortcutButton synthButton;
    FavoriteButton favoriteButton;

    void showFavoritePicker();
    juce::String getPageDisplayName (int pageIndex) const;
    bool connected { false };
    juce::String connectedHost;

    juce::ApplicationProperties appProperties;

    void saveState();
    void restoreState();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

