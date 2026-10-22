#pragma once

#include <JuceHeader.h>
#include "MidiMorfProtocol.h"

class NetworkServer : private juce::Thread
{
public:
    struct Listener
    {
        virtual ~Listener() = default;

        // FX Punch-in (existing)
        virtual void xyUpdated (float x, float y) = 0;
        virtual void axisUpdated (bool isXAxis, float value) = 0;
        virtual void buttonDown (int slotIndex) = 0;
        virtual void buttonUp (int slotIndex) = 0;
        virtual void tiltUpdated (float tiltX, float tiltY) = 0;
        virtual void clientConnected (const juce::String& address) = 0;
        virtual void clientDisconnected() = 0;

        // Multi-controller (default empty for backward compatibility)
        virtual void screenModeChanged (MidiMorf::ControllerMode /*mode*/) {}
        virtual void morfPadTouchDown (float /*x*/, float /*y*/) {}
        virtual void morfPadTouchMove (float /*x*/, float /*y*/) {}
        virtual void morfPadTouchUp() {}
        virtual void morfPadScaleSelected (uint8_t /*scaleIndex*/, uint8_t /*rootNote*/, uint8_t /*octaveRange*/, uint8_t /*baseOctave*/) {}
        virtual void chordSelected (uint8_t /*rootNote*/, uint8_t /*quality*/) {}
        virtual void chordReleased() {}
        virtual void strumGesture (float /*position*/, float /*velocity*/) {}
        virtual void transportButtonPressed (uint8_t /*buttonId*/) {}
        virtual void dualMPadTouchDown (uint8_t /*padIndex*/, float /*x*/, float /*y*/) {}
        virtual void dualMPadTouchMove (uint8_t /*padIndex*/, float /*x*/, float /*y*/) {}
        virtual void dualMPadTouchUp (uint8_t /*padIndex*/) {}
        virtual void dualMPadScaleSelected (uint8_t /*padIndex*/, uint8_t /*scaleIndex*/, uint8_t /*rootNote*/, uint8_t /*octaveRange*/, uint8_t /*baseOctave*/) {}
        virtual void morfChordSelected (uint8_t /*degreeIndex*/, float /*x*/, float /*y*/) {}
        virtual void morfChordMoved (uint8_t /*degreeIndex*/, float /*x*/, float /*y*/) {}
        virtual void morfChordReleased() {}
        virtual void morfChordStrum (float /*position*/, float /*velocity*/) {}
        virtual void morfChordKeySelected (uint8_t /*rootNote*/, uint8_t /*scaleMode*/) {}

        // Rose-Chord (Orchid-inspired)
        virtual void roseChordNoteOn (uint8_t /*rootIdx*/, uint8_t /*typeBits*/, uint8_t /*modBits*/, int8_t /*voicingOffset*/, int8_t /*octaveOffset*/) {}
        virtual void roseChordNoteOff() {}
        virtual void roseChordConfig (uint8_t /*typeBits*/, uint8_t /*modBits*/, int8_t /*voicingOffset*/, int8_t /*octaveOffset*/) {}

        // Creative Sequencer Pack
        virtual void packEntitlement (uint8_t /*packId*/, bool /*valid*/) {}
        virtual void seqStepSet (uint8_t /*step*/, uint8_t /*degree*/, uint8_t /*velocity*/, uint8_t /*gate*/,
                                 uint8_t /*mod*/, uint8_t /*flags*/) {}
        virtual void seqConfig (uint8_t /*patternLength*/, uint8_t /*rate*/, uint8_t /*direction*/, uint8_t /*swing*/,
                                uint8_t /*scaleIndex*/, uint8_t /*rootNote*/, uint8_t /*baseOctave*/,
                                uint8_t /*octaveRange*/) {}
        virtual void seqTransport (uint8_t /*command*/) {}
        virtual void seqRatchet (uint8_t /*division*/, bool /*active*/) {}

        // Slice sequencer
        virtual void seqSliceConfig (uint8_t /*bars*/, uint8_t /*division*/, uint8_t /*sliceCount*/,
                                     uint8_t /*rootNote*/, uint8_t /*direction*/, uint8_t /*swing*/) {}
        virtual void seqLooper (uint8_t /*division*/, bool /*active*/, bool /*randomOn*/) {}
        virtual void seqRibbon (uint8_t /*ribbonIndex*/, int /*value14bit*/) {}
        virtual void seqRibbonAssign (uint8_t /*ribbonIndex*/, uint8_t /*target*/, uint8_t /*ccNumber*/) {}
    };

    NetworkServer();
    ~NetworkServer() override;

    bool start();
    void stop();

    bool isClientConnected() const;
    juce::String getClientAddress() const;

    void sendSlotConfig (int slotIndex, const juce::String& name);
    void sendSeqPlayhead (uint8_t currentStep);

    void setListener (Listener* l);

private:
    void run() override;
    void handleClientConnection (juce::StreamingSocket& client);
    void processPacket (const uint8_t* data, int size);

    juce::StreamingSocket serverSocket;
    std::unique_ptr<juce::StreamingSocket> clientSocket;
    std::atomic<bool> clientIsConnected { false };
    juce::String clientAddress;
    juce::CriticalSection clientLock;

    // Listener is protected by its own lock to prevent use-after-free on shutdown
    Listener* listener = nullptr;
    juce::CriticalSection listenerLock;

    // Bonjour advertisement
    std::unique_ptr<juce::NetworkServiceDiscovery::Advertiser> advertiser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NetworkServer)
};
