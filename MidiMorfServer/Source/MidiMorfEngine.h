#pragma once

#include <JuceHeader.h>
#include "SlotConfig.h"
#include "VirtualMidiPort.h"
#include "NetworkServer.h"

class TransportHandler;
#include "MorfPadHandler.h"
#include "OmnichordHandler.h"
#include "MorfChordHandler.h"
#include "RoseChordHandler.h"

class MidiMorfEngine : public NetworkServer::Listener
{
public:
    MidiMorfEngine();
    ~MidiMorfEngine();

    void start();
    void stop();

    std::array<SlotConfig, MidiMorf::NUM_SLOTS>& getSlotConfigs() { return slotConfigs; }
    GlobalTiltConfig& getGlobalTiltConfig() { return globalTiltConfig; }
    NetworkServer& getNetworkServer() { return networkServer; }
    MorfPadHandler& getDualMPadHandler (int index) { return *dualMPadHandlers[juce::jlimit (0, 1, index)]; }
    MorfChordHandler& getMorfChordHandler() { return *morfChordHandler; }
    OmnichordHandler& getOmnichordHandler() { return *omnichordHandler; }
    RoseChordHandler& getRoseChordHandler() { return *roseChordHandler; }

    void sendTiltAssignBurst (bool isXAxis);

    // Current XY/tilt state for the UI mirror display
    std::atomic<float> currentX { 0.0f };
    std::atomic<float> currentY { 0.0f };
    std::atomic<float> currentTiltX { 0.0f };
    std::atomic<float> currentTiltY { 0.0f };
    std::atomic<int> activeSlotMask { 0 };

    // Current controller mode
    std::atomic<int> currentMode { 0 };  // MidiMorf::ControllerMode as int for atomics

    // NetworkServer::Listener — FX Punch-in
    void xyUpdated (float x, float y) override;
    void axisUpdated (bool isXAxis, float value) override;
    void buttonDown (int slotIndex) override;
    void buttonUp (int slotIndex) override;
    void tiltUpdated (float tiltX, float tiltY) override;
    void clientConnected (const juce::String& address) override;
    void clientDisconnected() override;

    // NetworkServer::Listener — Multi-controller
    void screenModeChanged (MidiMorf::ControllerMode mode) override;
    void morfPadTouchDown (float x, float y) override;
    void morfPadTouchMove (float x, float y) override;
    void morfPadTouchUp() override;
    void morfPadScaleSelected (uint8_t scaleIndex, uint8_t rootNote, uint8_t octaveRange, uint8_t baseOctave) override;
    void chordSelected (uint8_t rootNote, uint8_t quality) override;
    void chordReleased() override;
    void strumGesture (float position, float velocity) override;
    void transportButtonPressed (uint8_t buttonId) override;
    void dualMPadTouchDown (uint8_t padIndex, float x, float y) override;
    void dualMPadTouchMove (uint8_t padIndex, float x, float y) override;
    void dualMPadTouchUp (uint8_t padIndex) override;
    void dualMPadScaleSelected (uint8_t padIndex, uint8_t scaleIndex, uint8_t rootNote, uint8_t octaveRange, uint8_t baseOctave) override;
    void morfChordSelected (uint8_t degreeIndex, float x, float y) override;
    void morfChordMoved (uint8_t degreeIndex, float x, float y) override;
    void morfChordReleased() override;
    void morfChordStrum (float position, float velocity) override;
    void morfChordKeySelected (uint8_t rootNote, uint8_t scaleMode) override;
    void roseChordNoteOn (uint8_t rootIdx, uint8_t typeBits, uint8_t modBits, int8_t voicingOffset) override;
    void roseChordNoteOff() override;
    void roseChordConfig (uint8_t typeBits, uint8_t modBits, int8_t voicingOffset) override;

    // Local UI interaction
    void localXYUpdated (float x, float y);
    void localAxisUpdated (bool isXAxis, float value);
    void localButtonDown (int slotIndex);
    void localButtonUp (int slotIndex);

    void saveState();
    void loadState();

private:
    void sendCC (int channel, int ccNumber, int value);
    void sendNoteOn (int channel, int note, int velocity);
    void sendNoteOff (int channel, int note);

    juce::File getConfigFile() const;

    std::array<SlotConfig, MidiMorf::NUM_SLOTS> slotConfigs = createDefaultSlotConfigs();
    GlobalTiltConfig globalTiltConfig;
    int lastActiveSlot = 0;  // Last-pressed FX slot — XY always sends to this slot's CCs
    VirtualMidiPort virtualMidiPort;
    NetworkServer networkServer;

    std::unique_ptr<TransportHandler> transportHandler;
    std::unique_ptr<MorfPadHandler> morfPadHandler;
    std::unique_ptr<MorfPadHandler> dualMPadHandlers[2];
    std::unique_ptr<OmnichordHandler> omnichordHandler;
    std::unique_ptr<MorfChordHandler> morfChordHandler;
    std::unique_ptr<RoseChordHandler> roseChordHandler;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiMorfEngine)
};
