#pragma once

#include <JuceHeader.h>
#include "MidiMorfProtocol.h"

class NetworkClient : private juce::Thread
{
public:
    struct Listener
    {
        virtual ~Listener() = default;
        virtual void connectedToPlugin (const juce::String& hostName) = 0;
        virtual void disconnectedFromPlugin() = 0;
        virtual void slotConfigReceived (int slotIndex, const juce::String& name) = 0;
    };

    NetworkClient();
    ~NetworkClient() override;

    void startSearching();
    void stopSearching();

    bool isConnected() const { return connected.load(); }

    void sendXYUpdate (float x, float y);
    void sendAxisUpdate (bool isXAxis, float value);
    void sendButtonDown (int slotIndex);
    void sendButtonUp (int slotIndex);
    void sendTiltUpdate (float tiltX, float tiltY);

    // Multi-controller messages
    void sendScreenMode (MidiMorf::ControllerMode mode);
    void sendMorfPadTouchDown (float x, float y);
    void sendMorfPadTouchMove (float x, float y);
    void sendMorfPadTouchUp();
    void sendMorfPadScaleSelect (uint8_t scaleIndex, uint8_t rootNote, uint8_t octaveRange, uint8_t baseOctave);
    void sendChordSelect (uint8_t rootNote, uint8_t quality);
    void sendChordRelease();
    void sendStrumGesture (float position, float velocity);
    void sendTransportButton (uint8_t buttonId);

    // Dual M-Pad Pad
    void sendDualMPadTouchDown (uint8_t padIndex, float x, float y);
    void sendDualMPadTouchMove (uint8_t padIndex, float x, float y);
    void sendDualMPadTouchUp (uint8_t padIndex);
    void sendDualMPadScaleSelect (uint8_t padIndex, uint8_t scaleIndex, uint8_t rootNote, uint8_t octaveRange, uint8_t baseOctave);

    // Morf-Chord
    void sendMorfChordSelect (uint8_t degreeIndex, float x, float y);
    void sendMorfChordMove (uint8_t degreeIndex, float x, float y);
    void sendMorfChordRelease();
    void sendMorfChordStrum (float position, float velocity);
    void sendMorfChordKeySelect (uint8_t rootNote, uint8_t scaleMode);

    // Rose-Chord (Orchid-inspired)
    void sendRoseChordNoteOn (uint8_t rootIdx, uint8_t typeBits, uint8_t modBits, int8_t voicingOffset);
    void sendRoseChordNoteOff();
    void sendRoseChordConfig (uint8_t typeBits, uint8_t modBits, int8_t voicingOffset);

    void setListener (Listener* l) { listener = l; }

private:
    void run() override;
    bool tryConnect();
    void handleConnection();
    void processIncoming (const uint8_t* data, int size);
    void queuePacket (const uint8_t* data, int size);
    void flushOutgoing();

    juce::StreamingSocket socket;
    std::atomic<bool> connected { false };
    Listener* listener = nullptr;

    // Lock-free outgoing message queue
    juce::AbstractFifo outgoingFifo { 4096 };
    uint8_t outgoingBuffer[4096];
    juce::CriticalSection outgoingLock;

    // JUCE network service discovery
    std::unique_ptr<juce::NetworkServiceDiscovery::AvailableServiceList> serviceList;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NetworkClient)
};
