#include "NetworkClient.h"

NetworkClient::NetworkClient()
    : Thread ("MidiMorf Network Client")
{
}

NetworkClient::~NetworkClient()
{
    stopSearching();
}

void NetworkClient::startSearching()
{
    startThread (juce::Thread::Priority::normal);
}

void NetworkClient::stopSearching()
{
    signalThreadShouldExit();
    socket.close();
    stopThread (3000);
    connected.store (false);
}

void NetworkClient::sendXYUpdate (float x, float y)
{
    uint8_t packet[MidiMorf::HEADER_SIZE + MidiMorf::XY_PAYLOAD_SIZE];
    packet[0] = static_cast<uint8_t> (MidiMorf::PhoneMessage::XYUpdate);
    MidiMorf::writeBigEndianFloat (packet + 1, x);
    MidiMorf::writeBigEndianFloat (packet + 5, y);
    queuePacket (packet, sizeof (packet));
}

void NetworkClient::sendAxisUpdate (bool isXAxis, float value)
{
    uint8_t packet[MidiMorf::HEADER_SIZE + MidiMorf::AXIS_PAYLOAD_SIZE];
    packet[0] = static_cast<uint8_t> (MidiMorf::PhoneMessage::AxisUpdate);
    packet[1] = isXAxis ? 1 : 0;
    MidiMorf::writeBigEndianFloat (packet + 2, value);
    queuePacket (packet, sizeof (packet));
}

void NetworkClient::sendButtonDown (int slotIndex)
{
    uint8_t packet[MidiMorf::HEADER_SIZE + MidiMorf::BUTTON_PAYLOAD_SIZE];
    packet[0] = static_cast<uint8_t> (MidiMorf::PhoneMessage::ButtonDown);
    packet[1] = static_cast<uint8_t> (slotIndex);
    queuePacket (packet, sizeof (packet));
}

void NetworkClient::sendButtonUp (int slotIndex)
{
    uint8_t packet[MidiMorf::HEADER_SIZE + MidiMorf::BUTTON_PAYLOAD_SIZE];
    packet[0] = static_cast<uint8_t> (MidiMorf::PhoneMessage::ButtonUp);
    packet[1] = static_cast<uint8_t> (slotIndex);
    queuePacket (packet, sizeof (packet));
}

void NetworkClient::sendTiltUpdate (float tiltX, float tiltY)
{
    uint8_t packet[MidiMorf::HEADER_SIZE + MidiMorf::TILT_PAYLOAD_SIZE];
    packet[0] = static_cast<uint8_t> (MidiMorf::PhoneMessage::TiltUpdate);
    MidiMorf::writeBigEndianFloat (packet + 1, tiltX);
    MidiMorf::writeBigEndianFloat (packet + 5, tiltY);
    queuePacket (packet, sizeof (packet));
}

void NetworkClient::sendScreenMode (MidiMorf::ControllerMode mode)
{
    uint8_t packet[MidiMorf::HEADER_SIZE + MidiMorf::SCREEN_MODE_PAYLOAD_SIZE];
    packet[0] = static_cast<uint8_t> (MidiMorf::PhoneMessage::ScreenMode);
    packet[1] = static_cast<uint8_t> (mode);
    queuePacket (packet, sizeof (packet));
}

void NetworkClient::sendMorfPadTouchDown (float x, float y)
{
    uint8_t packet[MidiMorf::HEADER_SIZE + MidiMorf::MORFPAD_TOUCH_PAYLOAD_SIZE];
    packet[0] = static_cast<uint8_t> (MidiMorf::PhoneMessage::MorfPadTouchDown);
    MidiMorf::writeBigEndianFloat (packet + 1, x);
    MidiMorf::writeBigEndianFloat (packet + 5, y);
    queuePacket (packet, sizeof (packet));
}

void NetworkClient::sendMorfPadTouchMove (float x, float y)
{
    uint8_t packet[MidiMorf::HEADER_SIZE + MidiMorf::MORFPAD_TOUCH_PAYLOAD_SIZE];
    packet[0] = static_cast<uint8_t> (MidiMorf::PhoneMessage::MorfPadTouchMove);
    MidiMorf::writeBigEndianFloat (packet + 1, x);
    MidiMorf::writeBigEndianFloat (packet + 5, y);
    queuePacket (packet, sizeof (packet));
}

void NetworkClient::sendMorfPadTouchUp()
{
    uint8_t packet[MidiMorf::HEADER_SIZE];
    packet[0] = static_cast<uint8_t> (MidiMorf::PhoneMessage::MorfPadTouchUp);
    queuePacket (packet, sizeof (packet));
}

void NetworkClient::sendMorfPadScaleSelect (uint8_t scaleIndex, uint8_t rootNote, uint8_t octaveRange, uint8_t baseOctave)
{
    uint8_t packet[MidiMorf::HEADER_SIZE + MidiMorf::MORFPAD_SCALE_PAYLOAD_SIZE];
    packet[0] = static_cast<uint8_t> (MidiMorf::PhoneMessage::MorfPadScaleSelect);
    packet[1] = scaleIndex;
    packet[2] = rootNote;
    packet[3] = octaveRange;
    packet[4] = baseOctave;
    queuePacket (packet, sizeof (packet));
}

void NetworkClient::sendChordSelect (uint8_t rootNote, uint8_t quality)
{
    uint8_t packet[MidiMorf::HEADER_SIZE + MidiMorf::CHORD_SELECT_PAYLOAD_SIZE];
    packet[0] = static_cast<uint8_t> (MidiMorf::PhoneMessage::ChordSelect);
    packet[1] = rootNote;
    packet[2] = quality;
    queuePacket (packet, sizeof (packet));
}

void NetworkClient::sendChordRelease()
{
    uint8_t packet[MidiMorf::HEADER_SIZE];
    packet[0] = static_cast<uint8_t> (MidiMorf::PhoneMessage::ChordRelease);
    queuePacket (packet, sizeof (packet));
}

void NetworkClient::sendStrumGesture (float position, float velocity)
{
    uint8_t packet[MidiMorf::HEADER_SIZE + MidiMorf::STRUM_PAYLOAD_SIZE];
    packet[0] = static_cast<uint8_t> (MidiMorf::PhoneMessage::StrumGesture);
    MidiMorf::writeBigEndianFloat (packet + 1, position);
    MidiMorf::writeBigEndianFloat (packet + 5, velocity);
    queuePacket (packet, sizeof (packet));
}

void NetworkClient::sendTransportButton (uint8_t buttonId)
{
    uint8_t packet[MidiMorf::HEADER_SIZE + MidiMorf::TRANSPORT_PAYLOAD_SIZE];
    packet[0] = static_cast<uint8_t> (MidiMorf::PhoneMessage::TransportButton);
    packet[1] = buttonId;
    queuePacket (packet, sizeof (packet));
}

void NetworkClient::sendDualMPadTouchDown (uint8_t padIndex, float x, float y)
{
    uint8_t packet[MidiMorf::HEADER_SIZE + MidiMorf::DUAL_MPAD_TOUCH_PAYLOAD_SIZE];
    packet[0] = static_cast<uint8_t> (MidiMorf::PhoneMessage::DualMPadTouchDown);
    packet[1] = padIndex;
    MidiMorf::writeBigEndianFloat (packet + 2, x);
    MidiMorf::writeBigEndianFloat (packet + 6, y);
    queuePacket (packet, sizeof (packet));
}

void NetworkClient::sendDualMPadTouchMove (uint8_t padIndex, float x, float y)
{
    uint8_t packet[MidiMorf::HEADER_SIZE + MidiMorf::DUAL_MPAD_TOUCH_PAYLOAD_SIZE];
    packet[0] = static_cast<uint8_t> (MidiMorf::PhoneMessage::DualMPadTouchMove);
    packet[1] = padIndex;
    MidiMorf::writeBigEndianFloat (packet + 2, x);
    MidiMorf::writeBigEndianFloat (packet + 6, y);
    queuePacket (packet, sizeof (packet));
}

void NetworkClient::sendDualMPadTouchUp (uint8_t padIndex)
{
    uint8_t packet[MidiMorf::HEADER_SIZE + MidiMorf::DUAL_MPAD_TOUCHUP_PAYLOAD_SIZE];
    packet[0] = static_cast<uint8_t> (MidiMorf::PhoneMessage::DualMPadTouchUp);
    packet[1] = padIndex;
    queuePacket (packet, sizeof (packet));
}

void NetworkClient::sendDualMPadScaleSelect (uint8_t padIndex, uint8_t scaleIndex, uint8_t rootNote, uint8_t octaveRange, uint8_t baseOctave)
{
    uint8_t packet[MidiMorf::HEADER_SIZE + MidiMorf::DUAL_MPAD_SCALE_PAYLOAD_SIZE];
    packet[0] = static_cast<uint8_t> (MidiMorf::PhoneMessage::DualMPadScaleSelect);
    packet[1] = padIndex;
    packet[2] = scaleIndex;
    packet[3] = rootNote;
    packet[4] = octaveRange;
    packet[5] = baseOctave;
    queuePacket (packet, sizeof (packet));
}

void NetworkClient::sendMorfChordSelect (uint8_t degreeIndex, float x, float y)
{
    uint8_t packet[MidiMorf::HEADER_SIZE + MidiMorf::MORFCHORD_SELECT_PAYLOAD_SIZE];
    packet[0] = static_cast<uint8_t> (MidiMorf::PhoneMessage::MorfChordSelect);
    packet[1] = degreeIndex;
    MidiMorf::writeBigEndianFloat (packet + 2, x);
    MidiMorf::writeBigEndianFloat (packet + 6, y);
    queuePacket (packet, sizeof (packet));
}

void NetworkClient::sendMorfChordMove (uint8_t degreeIndex, float x, float y)
{
    uint8_t packet[MidiMorf::HEADER_SIZE + MidiMorf::MORFCHORD_MOVE_PAYLOAD_SIZE];
    packet[0] = static_cast<uint8_t> (MidiMorf::PhoneMessage::MorfChordMove);
    packet[1] = degreeIndex;
    MidiMorf::writeBigEndianFloat (packet + 2, x);
    MidiMorf::writeBigEndianFloat (packet + 6, y);
    queuePacket (packet, sizeof (packet));
}

void NetworkClient::sendMorfChordRelease()
{
    uint8_t packet[MidiMorf::HEADER_SIZE];
    packet[0] = static_cast<uint8_t> (MidiMorf::PhoneMessage::MorfChordRelease);
    queuePacket (packet, sizeof (packet));
}

void NetworkClient::sendMorfChordStrum (float position, float velocity)
{
    uint8_t packet[MidiMorf::HEADER_SIZE + MidiMorf::MORFCHORD_STRUM_PAYLOAD_SIZE];
    packet[0] = static_cast<uint8_t> (MidiMorf::PhoneMessage::MorfChordStrum);
    MidiMorf::writeBigEndianFloat (packet + 1, position);
    MidiMorf::writeBigEndianFloat (packet + 5, velocity);
    queuePacket (packet, sizeof (packet));
}

void NetworkClient::sendMorfChordKeySelect (uint8_t rootNote, uint8_t scaleMode)
{
    uint8_t packet[MidiMorf::HEADER_SIZE + MidiMorf::MORFCHORD_KEY_SELECT_PAYLOAD_SIZE];
    packet[0] = static_cast<uint8_t> (MidiMorf::PhoneMessage::MorfChordKeySelect);
    packet[1] = rootNote;
    packet[2] = scaleMode;
    queuePacket (packet, sizeof (packet));
}

void NetworkClient::sendRoseChordNoteOn (uint8_t rootIdx, uint8_t typeBits, uint8_t modBits, int8_t voicingOffset, int8_t octaveOffset)
{
    uint8_t packet[MidiMorf::HEADER_SIZE + MidiMorf::ROSECHORD_NOTEON_PAYLOAD_SIZE];
    packet[0] = static_cast<uint8_t> (MidiMorf::PhoneMessage::RoseChordNoteOn);
    packet[1] = rootIdx;
    packet[2] = typeBits;
    packet[3] = modBits;
    packet[4] = static_cast<uint8_t> (voicingOffset);  // i8 transported as raw byte
    packet[5] = static_cast<uint8_t> (octaveOffset);   // i8 transported as raw byte
    queuePacket (packet, sizeof (packet));
}

void NetworkClient::sendRoseChordNoteOff()
{
    uint8_t packet[MidiMorf::HEADER_SIZE];
    packet[0] = static_cast<uint8_t> (MidiMorf::PhoneMessage::RoseChordNoteOff);
    queuePacket (packet, sizeof (packet));
}

void NetworkClient::sendRoseChordConfig (uint8_t typeBits, uint8_t modBits, int8_t voicingOffset, int8_t octaveOffset)
{
    uint8_t packet[MidiMorf::HEADER_SIZE + MidiMorf::ROSECHORD_CONFIG_PAYLOAD_SIZE];
    packet[0] = static_cast<uint8_t> (MidiMorf::PhoneMessage::RoseChordConfig);
    packet[1] = typeBits;
    packet[2] = modBits;
    packet[3] = static_cast<uint8_t> (voicingOffset);
    packet[4] = static_cast<uint8_t> (octaveOffset);
    queuePacket (packet, sizeof (packet));
}

void NetworkClient::sendPackEntitlement (uint8_t packId, bool valid)
{
    uint8_t packet[MidiMorf::HEADER_SIZE + MidiMorf::PACK_ENTITLEMENT_PAYLOAD_SIZE];
    packet[0] = static_cast<uint8_t> (MidiMorf::PhoneMessage::PackEntitlement);
    packet[1] = packId;
    packet[2] = valid ? 1 : 0;
    queuePacket (packet, sizeof (packet));
}

void NetworkClient::sendSeqStepSet (uint8_t step, uint8_t degree, uint8_t velocity, uint8_t gate, uint8_t mod, bool rest)
{
    uint8_t packet[MidiMorf::HEADER_SIZE + MidiMorf::SEQ_STEP_SET_PAYLOAD_SIZE];
    packet[0] = static_cast<uint8_t> (MidiMorf::PhoneMessage::SeqStepSet);
    packet[1] = step;
    packet[2] = degree;
    packet[3] = velocity;
    packet[4] = gate;
    packet[5] = mod;
    packet[6] = rest ? MidiMorf::SEQ_FLAG_REST : 0;
    queuePacket (packet, sizeof (packet));
}

void NetworkClient::sendSeqRatchet (MidiMorf::SeqRatchetDiv division, bool active)
{
    uint8_t packet[MidiMorf::HEADER_SIZE + MidiMorf::SEQ_RATCHET_PAYLOAD_SIZE];
    packet[0] = static_cast<uint8_t> (MidiMorf::PhoneMessage::SeqRatchet);
    packet[1] = static_cast<uint8_t> (division);
    packet[2] = active ? 1 : 0;
    queuePacket (packet, sizeof (packet));
}

void NetworkClient::sendSeqConfig (uint8_t patternLength, uint8_t rate, uint8_t direction, uint8_t swing,
                                   uint8_t scaleIndex, uint8_t rootNote, uint8_t baseOctave, uint8_t octaveRange)
{
    uint8_t packet[MidiMorf::HEADER_SIZE + MidiMorf::SEQ_CONFIG_PAYLOAD_SIZE];
    packet[0] = static_cast<uint8_t> (MidiMorf::PhoneMessage::SeqConfig);
    packet[1] = patternLength;
    packet[2] = rate;
    packet[3] = direction;
    packet[4] = swing;
    packet[5] = scaleIndex;
    packet[6] = rootNote;
    packet[7] = baseOctave;
    packet[8] = octaveRange;
    queuePacket (packet, sizeof (packet));
}

void NetworkClient::sendSeqTransport (MidiMorf::SeqCommand command)
{
    uint8_t packet[MidiMorf::HEADER_SIZE + MidiMorf::SEQ_TRANSPORT_PAYLOAD_SIZE];
    packet[0] = static_cast<uint8_t> (MidiMorf::PhoneMessage::SeqTransport);
    packet[1] = static_cast<uint8_t> (command);
    queuePacket (packet, sizeof (packet));
}

void NetworkClient::sendSeqSliceConfig (uint8_t bars, uint8_t division, uint8_t sliceCount,
                                        uint8_t rootNote, uint8_t direction, uint8_t swing)
{
    uint8_t packet[MidiMorf::HEADER_SIZE + MidiMorf::SEQ_SLICE_CONFIG_PAYLOAD_SIZE];
    packet[0] = static_cast<uint8_t> (MidiMorf::PhoneMessage::SeqSliceConfig);
    packet[1] = bars;
    packet[2] = division;
    packet[3] = sliceCount;
    packet[4] = rootNote;
    packet[5] = direction;
    packet[6] = swing;
    queuePacket (packet, sizeof (packet));
}

void NetworkClient::sendSeqLooper (uint8_t division, bool active, bool randomOn)
{
    uint8_t packet[MidiMorf::HEADER_SIZE + MidiMorf::SEQ_LOOPER_PAYLOAD_SIZE];
    packet[0] = static_cast<uint8_t> (MidiMorf::PhoneMessage::SeqLooper);
    packet[1] = division;
    packet[2] = active ? 1 : 0;
    packet[3] = randomOn ? 1 : 0;
    queuePacket (packet, sizeof (packet));
}

void NetworkClient::sendSeqRibbon (uint8_t ribbonIndex, int value14bit)
{
    const int v = juce::jlimit (0, 16383, value14bit);

    uint8_t packet[MidiMorf::HEADER_SIZE + MidiMorf::SEQ_RIBBON_PAYLOAD_SIZE];
    packet[0] = static_cast<uint8_t> (MidiMorf::PhoneMessage::SeqRibbon);
    packet[1] = ribbonIndex;
    packet[2] = static_cast<uint8_t> (v >> 8);
    packet[3] = static_cast<uint8_t> (v & 0xFF);
    queuePacket (packet, sizeof (packet));
}

void NetworkClient::sendSeqRibbonAssign (uint8_t ribbonIndex, MidiMorf::RibbonTarget target, uint8_t ccNumber)
{
    uint8_t packet[MidiMorf::HEADER_SIZE + MidiMorf::SEQ_RIBBON_ASSIGN_PAYLOAD_SIZE];
    packet[0] = static_cast<uint8_t> (MidiMorf::PhoneMessage::SeqRibbonAssign);
    packet[1] = ribbonIndex;
    packet[2] = static_cast<uint8_t> (target);
    packet[3] = ccNumber;
    queuePacket (packet, sizeof (packet));
}

void NetworkClient::queuePacket (const uint8_t* data, int size)
{
    juce::ScopedLock lock (outgoingLock);

    int start1, size1, start2, size2;
    outgoingFifo.prepareToWrite (size, start1, size1, start2, size2);

    if (size1 + size2 < size)
    {
        DBG ("NetworkClient::queuePacket DROPPED - buffer full, size=" << size);
        return; // buffer full, drop packet
    }

    if (size1 > 0)
        std::memcpy (outgoingBuffer + start1, data, (size_t) size1);
    if (size2 > 0)
        std::memcpy (outgoingBuffer + start2, data + size1, (size_t) size2);

    outgoingFifo.finishedWrite (size1 + size2);
    DBG ("NetworkClient::queuePacket OK size=" << size << " type=0x" << juce::String::toHexString (data[0]) << " ready=" << outgoingFifo.getNumReady());
    notify(); // wake up the network thread
}

void NetworkClient::flushOutgoing()
{
    juce::ScopedLock lock (outgoingLock);

    int numReady = outgoingFifo.getNumReady();
    if (numReady == 0)
        return;

    int start1, size1, start2, size2;
    outgoingFifo.prepareToRead (numReady, start1, size1, start2, size2);

    int written = 0;
    if (size1 > 0)
        written += socket.write (outgoingBuffer + start1, size1);
    if (size2 > 0)
        written += socket.write (outgoingBuffer + start2, size2);

    outgoingFifo.finishedRead (size1 + size2);

    DBG ("NetworkClient::flushOutgoing flushed " << (size1 + size2) << " bytes, socket.write returned " << written);
}

void NetworkClient::run()
{
    while (! threadShouldExit())
    {
        if (tryConnect())
        {
            handleConnection();

            // Disconnected
            connected.store (false);
            socket.close();

            if (listener != nullptr)
                listener->disconnectedFromPlugin();
        }

        if (! threadShouldExit())
            wait (1000);
    }
}

bool NetworkClient::tryConnect()
{
    serviceList = std::make_unique<juce::NetworkServiceDiscovery::AvailableServiceList> (
        "_midimorf._tcp", MidiMorf::DISCOVERY_PORT);

    for (int attempt = 0; attempt < 30 && ! threadShouldExit(); ++attempt)
    {
        wait (500);

        auto services = serviceList->getServices();
        if (services.size() > 0)
        {
            auto& service = services[0];
            DBG ("NetworkClient: found service at " << service.address.toString() << ":" << service.port);

            // Cache the service info — we're about to release serviceList.
            const auto serviceAddr = service.address.toString();
            const int  servicePort = service.port;

            // AvailableServiceList inherits from AsyncUpdater. Destroying it
            // off the message thread while a Bonjour callback is pending
            // trips the AsyncUpdater destructor assertion (juce_AsyncUpdater
            // .cpp:69). Move ownership to the message thread for destruction.
            juce::MessageManager::callAsync (
                [sl = std::move (serviceList)]() mutable {});

            if (socket.connect (serviceAddr, servicePort, 3000))
            {
                connected.store (true);
                DBG ("NetworkClient: connected!");

                if (listener != nullptr)
                    listener->connectedToPlugin (serviceAddr);
                return true;
            }

            DBG ("NetworkClient: TCP connect failed");
            return false;
        }
    }

    // No service found this round — also hand off to the message thread.
    juce::MessageManager::callAsync (
        [sl = std::move (serviceList)]() mutable {});
    return false;
}

void NetworkClient::handleConnection()
{
    DBG ("NetworkClient::handleConnection STARTED");
    uint8_t buffer[256];

    while (! threadShouldExit() && socket.isConnected())
    {
        // Flush any queued outgoing packets
        flushOutgoing();

        // Non-blocking check for incoming data
        int ready = socket.waitUntilReady (true, 0);

        if (ready == 1)
        {
            int bytesRead = socket.read (buffer, sizeof (buffer), false);

            if (bytesRead < 0)
            {
                DBG ("NetworkClient: read error, disconnecting");
                break;
            }

            if (bytesRead > 0)
                processIncoming (buffer, bytesRead);
        }
        else if (ready < 0)
        {
            DBG ("NetworkClient: socket error in waitUntilReady");
            break;
        }

        // Sleep briefly — notify() from queuePacket wakes us immediately
        wait (10);
    }

    DBG ("NetworkClient::handleConnection ENDED");
}

void NetworkClient::processIncoming (const uint8_t* data, int size)
{
    int offset = 0;

    while (offset < size)
    {
        auto msgType = static_cast<MidiMorf::PluginMessage> (data[offset]);

        switch (msgType)
        {
            case MidiMorf::PluginMessage::SlotConfig:
            {
                if (offset + 2 >= size)
                    return;

                int slotIndex = data[offset + 1];

                int nameStart = offset + 2;
                int nameEnd = nameStart;
                while (nameEnd < size && data[nameEnd] != 0)
                    ++nameEnd;

                if (nameEnd >= size)
                    return;

                juce::String name (reinterpret_cast<const char*> (data + nameStart),
                                   (size_t) (nameEnd - nameStart));

                offset = nameEnd + 1;

                if (slotIndex < MidiMorf::NUM_SLOTS && listener != nullptr)
                {
                    juce::MessageManager::callAsync ([this, slotIndex, name]()
                    {
                        if (listener != nullptr)
                            listener->slotConfigReceived (slotIndex, name);
                    });
                }
                break;
            }

            case MidiMorf::PluginMessage::Pong:
                offset += 1;
                break;

            case MidiMorf::PluginMessage::SeqPlayhead:
            {
                if (offset + 1 >= size)
                    return;

                const uint8_t raw = data[offset + 1];
                const int step = (raw == MidiMorf::SEQ_STEP_NONE) ? -1 : (int) raw;
                offset += 2;

                if (listener != nullptr)
                {
                    juce::MessageManager::callAsync ([this, step]()
                    {
                        if (listener != nullptr)
                            listener->seqPlayheadChanged (step);
                    });
                }
                break;
            }

            default:
                return;
        }
    }
}
