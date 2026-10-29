#include "NetworkServer.h"

NetworkServer::NetworkServer()
    : Thread ("MidiMorf Network Server")
{
}

NetworkServer::~NetworkServer()
{
    stop();
}

void NetworkServer::setListener (Listener* l)
{
    juce::ScopedLock lock (listenerLock);
    listener = l;
}

bool NetworkServer::start()
{
    // Don't start if already running
    if (isThreadRunning())
        return true;

    if (! serverSocket.createListener (MidiMorf::DEFAULT_PORT, "0.0.0.0"))
    {
        DBG ("NetworkServer: failed to bind to port " << MidiMorf::DEFAULT_PORT);
        return false;
    }

    DBG ("NetworkServer: listening on port " << MidiMorf::DEFAULT_PORT);

    advertiser = std::make_unique<juce::NetworkServiceDiscovery::Advertiser> (
        "_midimorf._tcp",
        "Midi-Morf Bridge",
        MidiMorf::DISCOVERY_PORT,
        MidiMorf::DEFAULT_PORT);

    DBG ("NetworkServer: Bonjour service advertised");

    startThread (juce::Thread::Priority::normal);
    return true;
}

void NetworkServer::stop()
{
    // Clear listener first so no callbacks fire during shutdown
    {
        juce::ScopedLock lock (listenerLock);
        listener = nullptr;
    }

    advertiser.reset();

    signalThreadShouldExit();

    // Close sockets to unblock any waiting reads/accepts
    {
        juce::ScopedLock lock (clientLock);
        if (clientSocket != nullptr)
            clientSocket->close();
    }
    serverSocket.close();

    stopThread (3000);
    clientIsConnected.store (false);
}

bool NetworkServer::isClientConnected() const
{
    return clientIsConnected.load();
}

juce::String NetworkServer::getClientAddress() const
{
    juce::ScopedLock lock (clientLock);
    return clientAddress;
}

void NetworkServer::sendSlotConfig (int slotIndex, const juce::String& name)
{
    juce::ScopedLock lock (clientLock);

    if (clientSocket == nullptr || ! clientSocket->isConnected())
        return;

    auto nameUtf8 = name.toUTF8();
    int nameLen = (int) name.getNumBytesAsUTF8() + 1;

    std::vector<uint8_t> packet;
    packet.push_back (static_cast<uint8_t> (MidiMorf::PluginMessage::SlotConfig));
    packet.push_back (static_cast<uint8_t> (slotIndex));
    packet.insert (packet.end(), nameUtf8.getAddress(), nameUtf8.getAddress() + nameLen);

    clientSocket->write (packet.data(), (int) packet.size());
}

void NetworkServer::sendSeqPlayhead (uint8_t currentStep)
{
    juce::ScopedLock lock (clientLock);

    if (clientSocket == nullptr || ! clientSocket->isConnected())
        return;

    const uint8_t packet[MidiMorf::HEADER_SIZE + MidiMorf::SEQ_PLAYHEAD_PAYLOAD_SIZE] =
    {
        static_cast<uint8_t> (MidiMorf::PluginMessage::SeqPlayhead),
        currentStep
    };

    clientSocket->write (packet, (int) sizeof (packet));
}

void NetworkServer::run()
{
    while (! threadShouldExit())
    {
        auto* incoming = serverSocket.waitForNextConnection();

        if (incoming == nullptr || threadShouldExit())
        {
            delete incoming;
            continue;
        }

        {
            juce::ScopedLock lock (clientLock);
            clientSocket.reset (incoming);
            clientAddress = clientSocket->getHostName();
            clientIsConnected.store (true);
        }

        DBG ("NetworkServer: client connected from " << clientAddress);

        {
            juce::ScopedLock lock (listenerLock);
            if (listener != nullptr)
                listener->clientConnected (clientAddress);
        }

        // Handle this connection until it drops or we're told to exit
        handleClientConnection (*clientSocket);

        // Client disconnected — clean up
        {
            juce::ScopedLock lock (clientLock);
            clientSocket.reset();
            clientAddress.clear();
            clientIsConnected.store (false);
        }

        DBG ("NetworkServer: client disconnected");

        {
            juce::ScopedLock lock (listenerLock);
            if (listener != nullptr)
                listener->clientDisconnected();
        }
    }
}

void NetworkServer::handleClientConnection (juce::StreamingSocket& client)
{
    uint8_t buffer[1024];
    int buffered = 0;

    while (! threadShouldExit() && client.isConnected())
    {
        if (! client.waitUntilReady (true, 500))
            continue;

        int bytesRead = client.read (buffer + buffered, (int) sizeof (buffer) - buffered, false);

        if (bytesRead <= 0)
            break;

        buffered += bytesRead;

        // Parse all complete packets in the buffer
        int offset = 0;
        while (offset < buffered)
        {
            int remaining = buffered - offset;
            if (remaining < MidiMorf::HEADER_SIZE)
                break;

            auto msgType = static_cast<MidiMorf::PhoneMessage> (buffer[offset]);
            int payloadSize = 0;

            switch (msgType)
            {
                case MidiMorf::PhoneMessage::XYUpdate:        payloadSize = MidiMorf::XY_PAYLOAD_SIZE; break;
                case MidiMorf::PhoneMessage::ButtonDown:      payloadSize = MidiMorf::BUTTON_PAYLOAD_SIZE; break;
                case MidiMorf::PhoneMessage::ButtonUp:        payloadSize = MidiMorf::BUTTON_PAYLOAD_SIZE; break;
                case MidiMorf::PhoneMessage::TiltUpdate:      payloadSize = MidiMorf::TILT_PAYLOAD_SIZE; break;
                case MidiMorf::PhoneMessage::Ping:            payloadSize = MidiMorf::PING_PAYLOAD_SIZE; break;
                case MidiMorf::PhoneMessage::AxisUpdate:      payloadSize = MidiMorf::AXIS_PAYLOAD_SIZE; break;
                case MidiMorf::PhoneMessage::ScreenMode:      payloadSize = MidiMorf::SCREEN_MODE_PAYLOAD_SIZE; break;
                case MidiMorf::PhoneMessage::MorfPadTouchDown:  payloadSize = MidiMorf::MORFPAD_TOUCH_PAYLOAD_SIZE; break;
                case MidiMorf::PhoneMessage::MorfPadTouchMove:  payloadSize = MidiMorf::MORFPAD_TOUCH_PAYLOAD_SIZE; break;
                case MidiMorf::PhoneMessage::MorfPadTouchUp:    payloadSize = MidiMorf::MORFPAD_TOUCHUP_PAYLOAD_SIZE; break;
                case MidiMorf::PhoneMessage::MorfPadScaleSelect: payloadSize = MidiMorf::MORFPAD_SCALE_PAYLOAD_SIZE; break;
                case MidiMorf::PhoneMessage::ChordSelect:     payloadSize = MidiMorf::CHORD_SELECT_PAYLOAD_SIZE; break;
                case MidiMorf::PhoneMessage::ChordRelease:    payloadSize = MidiMorf::CHORD_RELEASE_PAYLOAD_SIZE; break;
                case MidiMorf::PhoneMessage::StrumGesture:    payloadSize = MidiMorf::STRUM_PAYLOAD_SIZE; break;
                case MidiMorf::PhoneMessage::TransportButton: payloadSize = MidiMorf::TRANSPORT_PAYLOAD_SIZE; break;
                case MidiMorf::PhoneMessage::DualMPadTouchDown:  payloadSize = MidiMorf::DUAL_MPAD_TOUCH_PAYLOAD_SIZE; break;
                case MidiMorf::PhoneMessage::DualMPadTouchMove:  payloadSize = MidiMorf::DUAL_MPAD_TOUCH_PAYLOAD_SIZE; break;
                case MidiMorf::PhoneMessage::DualMPadTouchUp:    payloadSize = MidiMorf::DUAL_MPAD_TOUCHUP_PAYLOAD_SIZE; break;
                case MidiMorf::PhoneMessage::DualMPadScaleSelect: payloadSize = MidiMorf::DUAL_MPAD_SCALE_PAYLOAD_SIZE; break;
                case MidiMorf::PhoneMessage::MorfChordSelect:      payloadSize = MidiMorf::MORFCHORD_SELECT_PAYLOAD_SIZE; break;
                case MidiMorf::PhoneMessage::MorfChordRelease:     payloadSize = MidiMorf::MORFCHORD_RELEASE_PAYLOAD_SIZE; break;
                case MidiMorf::PhoneMessage::MorfChordStrum:       payloadSize = MidiMorf::MORFCHORD_STRUM_PAYLOAD_SIZE; break;
                case MidiMorf::PhoneMessage::MorfChordKeySelect:   payloadSize = MidiMorf::MORFCHORD_KEY_SELECT_PAYLOAD_SIZE; break;
                case MidiMorf::PhoneMessage::MorfChordMove:        payloadSize = MidiMorf::MORFCHORD_MOVE_PAYLOAD_SIZE; break;
                case MidiMorf::PhoneMessage::RoseChordNoteOn:      payloadSize = MidiMorf::ROSECHORD_NOTEON_PAYLOAD_SIZE; break;
                case MidiMorf::PhoneMessage::RoseChordNoteOff:     payloadSize = MidiMorf::ROSECHORD_NOTEOFF_PAYLOAD_SIZE; break;
                case MidiMorf::PhoneMessage::RoseChordConfig:      payloadSize = MidiMorf::ROSECHORD_CONFIG_PAYLOAD_SIZE; break;
                case MidiMorf::PhoneMessage::PackEntitlement:      payloadSize = MidiMorf::PACK_ENTITLEMENT_PAYLOAD_SIZE; break;
                case MidiMorf::PhoneMessage::SeqStepSet:           payloadSize = MidiMorf::SEQ_STEP_SET_PAYLOAD_SIZE; break;
                case MidiMorf::PhoneMessage::SeqConfig:            payloadSize = MidiMorf::SEQ_CONFIG_PAYLOAD_SIZE; break;
                case MidiMorf::PhoneMessage::SeqTransport:         payloadSize = MidiMorf::SEQ_TRANSPORT_PAYLOAD_SIZE; break;
                case MidiMorf::PhoneMessage::SeqRatchet:           payloadSize = MidiMorf::SEQ_RATCHET_PAYLOAD_SIZE; break;
                case MidiMorf::PhoneMessage::SeqSliceConfig:       payloadSize = MidiMorf::SEQ_SLICE_CONFIG_PAYLOAD_SIZE; break;
                case MidiMorf::PhoneMessage::SeqLooper:            payloadSize = MidiMorf::SEQ_LOOPER_PAYLOAD_SIZE; break;
                case MidiMorf::PhoneMessage::SeqRibbon:            payloadSize = MidiMorf::SEQ_RIBBON_PAYLOAD_SIZE; break;
                case MidiMorf::PhoneMessage::SeqRibbonAssign:      payloadSize = MidiMorf::SEQ_RIBBON_ASSIGN_PAYLOAD_SIZE; break;
                default:
                    DBG ("NetworkServer: unknown message type " << (int) buffer[offset] << " — resync, discarding " << remaining << " bytes");
                    offset = buffered;
                    continue;
            }

            int totalPacketSize = MidiMorf::HEADER_SIZE + payloadSize;
            if (remaining < totalPacketSize)
                break;  // Incomplete packet — wait for more data

            processPacket (buffer + offset, totalPacketSize);
            offset += totalPacketSize;
        }

        // Move any unconsumed bytes (partial packet) to the front of the buffer
        if (offset > 0 && offset < buffered)
        {
            std::memmove (buffer, buffer + offset, (size_t) (buffered - offset));
            buffered -= offset;
        }
        else if (offset >= buffered)
        {
            buffered = 0;
        }
    }
}

void NetworkServer::processPacket (const uint8_t* data, int size)
{
    if (size < MidiMorf::HEADER_SIZE)
        return;

    auto msgType = static_cast<MidiMorf::PhoneMessage> (data[0]);
    const uint8_t* payload = data + MidiMorf::HEADER_SIZE;

    // Lock listener for the duration of the callback
    juce::ScopedLock lock (listenerLock);
    if (listener == nullptr)
        return;

    switch (msgType)
    {
        case MidiMorf::PhoneMessage::XYUpdate:
        {
            float x = MidiMorf::readBigEndianFloat (payload);
            float y = MidiMorf::readBigEndianFloat (payload + 4);
            listener->xyUpdated (x, y);
            break;
        }

        case MidiMorf::PhoneMessage::AxisUpdate:
        {
            bool isXAxis = payload[0] != 0;
            float value = MidiMorf::readBigEndianFloat (payload + 1);
            listener->axisUpdated (isXAxis, value);
            break;
        }

        case MidiMorf::PhoneMessage::ButtonDown:
        {
            int slotIndex = payload[0];
            if (slotIndex < MidiMorf::NUM_SLOTS)
                listener->buttonDown (slotIndex);
            break;
        }

        case MidiMorf::PhoneMessage::ButtonUp:
        {
            int slotIndex = payload[0];
            if (slotIndex < MidiMorf::NUM_SLOTS)
                listener->buttonUp (slotIndex);
            break;
        }

        case MidiMorf::PhoneMessage::TiltUpdate:
        {
            float tiltX = MidiMorf::readBigEndianFloat (payload);
            float tiltY = MidiMorf::readBigEndianFloat (payload + 4);
            listener->tiltUpdated (tiltX, tiltY);
            break;
        }

        case MidiMorf::PhoneMessage::Ping:
        {
            juce::ScopedLock cLock (clientLock);
            if (clientSocket != nullptr && clientSocket->isConnected())
            {
                uint8_t pong = static_cast<uint8_t> (MidiMorf::PluginMessage::Pong);
                clientSocket->write (&pong, 1);
            }
            break;
        }

        case MidiMorf::PhoneMessage::ScreenMode:
        {
            auto mode = static_cast<MidiMorf::ControllerMode> (payload[0]);
            listener->screenModeChanged (mode);
            break;
        }

        case MidiMorf::PhoneMessage::MorfPadTouchDown:
        {
            float x = MidiMorf::readBigEndianFloat (payload);
            float y = MidiMorf::readBigEndianFloat (payload + 4);
            listener->morfPadTouchDown (x, y);
            break;
        }

        case MidiMorf::PhoneMessage::MorfPadTouchMove:
        {
            float x = MidiMorf::readBigEndianFloat (payload);
            float y = MidiMorf::readBigEndianFloat (payload + 4);
            listener->morfPadTouchMove (x, y);
            break;
        }

        case MidiMorf::PhoneMessage::MorfPadTouchUp:
        {
            listener->morfPadTouchUp();
            break;
        }

        case MidiMorf::PhoneMessage::MorfPadScaleSelect:
        {
            listener->morfPadScaleSelected (payload[0], payload[1], payload[2], payload[3]);
            break;
        }

        case MidiMorf::PhoneMessage::ChordSelect:
        {
            listener->chordSelected (payload[0], payload[1]);
            break;
        }

        case MidiMorf::PhoneMessage::ChordRelease:
        {
            listener->chordReleased();
            break;
        }

        case MidiMorf::PhoneMessage::StrumGesture:
        {
            float position = MidiMorf::readBigEndianFloat (payload);
            float velocity = MidiMorf::readBigEndianFloat (payload + 4);
            listener->strumGesture (position, velocity);
            break;
        }

        case MidiMorf::PhoneMessage::TransportButton:
        {
            listener->transportButtonPressed (payload[0]);
            break;
        }

        case MidiMorf::PhoneMessage::DualMPadTouchDown:
        {
            uint8_t padIndex = payload[0];
            float x = MidiMorf::readBigEndianFloat (payload + 1);
            float y = MidiMorf::readBigEndianFloat (payload + 5);
            listener->dualMPadTouchDown (padIndex, x, y);
            break;
        }

        case MidiMorf::PhoneMessage::DualMPadTouchMove:
        {
            uint8_t padIndex = payload[0];
            float x = MidiMorf::readBigEndianFloat (payload + 1);
            float y = MidiMorf::readBigEndianFloat (payload + 5);
            listener->dualMPadTouchMove (padIndex, x, y);
            break;
        }

        case MidiMorf::PhoneMessage::DualMPadTouchUp:
        {
            listener->dualMPadTouchUp (payload[0]);
            break;
        }

        case MidiMorf::PhoneMessage::DualMPadScaleSelect:
        {
            listener->dualMPadScaleSelected (payload[0], payload[1], payload[2], payload[3], payload[4]);
            break;
        }

        case MidiMorf::PhoneMessage::MorfChordSelect:
        {
            float x = MidiMorf::readBigEndianFloat (payload + 1);
            float y = MidiMorf::readBigEndianFloat (payload + 5);
            listener->morfChordSelected (payload[0], x, y);
            break;
        }

        case MidiMorf::PhoneMessage::MorfChordMove:
        {
            float x = MidiMorf::readBigEndianFloat (payload + 1);
            float y = MidiMorf::readBigEndianFloat (payload + 5);
            listener->morfChordMoved (payload[0], x, y);
            break;
        }

        case MidiMorf::PhoneMessage::MorfChordRelease:
        {
            listener->morfChordReleased();
            break;
        }

        case MidiMorf::PhoneMessage::MorfChordStrum:
        {
            float position = MidiMorf::readBigEndianFloat (payload);
            float velocity = MidiMorf::readBigEndianFloat (payload + 4);
            listener->morfChordStrum (position, velocity);
            break;
        }

        case MidiMorf::PhoneMessage::MorfChordKeySelect:
        {
            listener->morfChordKeySelected (payload[0], payload[1]);
            break;
        }

        case MidiMorf::PhoneMessage::RoseChordNoteOn:
        {
            int8_t voicing = static_cast<int8_t> (payload[3]);
            int8_t octave  = static_cast<int8_t> (payload[4]);
            listener->roseChordNoteOn (payload[0], payload[1], payload[2], voicing, octave);
            break;
        }

        case MidiMorf::PhoneMessage::RoseChordNoteOff:
        {
            listener->roseChordNoteOff();
            break;
        }

        case MidiMorf::PhoneMessage::RoseChordConfig:
        {
            int8_t voicing = static_cast<int8_t> (payload[2]);
            int8_t octave  = static_cast<int8_t> (payload[3]);
            listener->roseChordConfig (payload[0], payload[1], voicing, octave);
            break;
        }

        case MidiMorf::PhoneMessage::PackEntitlement:
        {
            listener->packEntitlement (payload[0], payload[1] != 0);
            break;
        }

        case MidiMorf::PhoneMessage::SeqStepSet:
        {
            listener->seqStepSet (payload[0], payload[1], payload[2], payload[3], payload[4], payload[5]);
            break;
        }

        case MidiMorf::PhoneMessage::SeqConfig:
        {
            listener->seqConfig (payload[0], payload[1], payload[2], payload[3],
                                 payload[4], payload[5], payload[6], payload[7]);
            break;
        }

        case MidiMorf::PhoneMessage::SeqTransport:
        {
            listener->seqTransport (payload[0]);
            break;
        }

        case MidiMorf::PhoneMessage::SeqRatchet:
        {
            listener->seqRatchet (payload[0], payload[1] != 0);
            break;
        }

        case MidiMorf::PhoneMessage::SeqSliceConfig:
        {
            listener->seqSliceConfig (payload[0], payload[1], payload[2],
                                      payload[3], payload[4], payload[5]);
            break;
        }

        case MidiMorf::PhoneMessage::SeqLooper:
        {
            listener->seqLooper (payload[0], payload[1] != 0, payload[2] != 0);
            break;
        }

        case MidiMorf::PhoneMessage::SeqRibbon:
        {
            const int value = (int (payload[1]) << 8) | int (payload[2]);
            listener->seqRibbon (payload[0], juce::jlimit (0, 16383, value));
            break;
        }

        case MidiMorf::PhoneMessage::SeqRibbonAssign:
        {
            listener->seqRibbonAssign (payload[0], payload[1], payload[2]);
            break;
        }
    }
}
