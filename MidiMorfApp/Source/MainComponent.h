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
#include "SynthSettingsPage.h"
#include "AboutPage.h"
#include "NetworkClient.h"
#include "AccelerometerManager.h"
#include "MidiMorfProtocol.h"
#include "MidiMorfColors.h"
#include "AudioEngine.h"
#include "EventRouter.h"

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
    EventRouter eventRouter { networkClient, audioEngine.getSynth() };
    AccelerometerManager accelerometer;

    PageContainer pageContainer;
    FXPunchInPage fxPage;
    MorfPadPage morfPadPage;
    OmnichordPage omnichordPage;
    TransportPage transportPage;
    DualMPadPage dualMPadPage;
    MorfChordPage morfChordPage;
    RoseChordPage roseChordPage;
    SynthSettingsPage synthSettingsPage { audioEngine.getSynth() };
    AboutPage aboutPage;

    juce::TextButton menuButton;
    juce::TextButton modeToggleButton;
    juce::Label modeLabel;
    juce::Label statusLabel;

    void updateModeToggleAppearance();

    juce::ApplicationProperties appProperties;

    void saveState();
    void restoreState();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
