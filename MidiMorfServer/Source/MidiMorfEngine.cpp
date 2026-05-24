#include "MidiMorfEngine.h"
#include "TransportHandler.h"
#include "MorfPadHandler.h"
#include "OmnichordHandler.h"
#include "MorfChordHandler.h"
#include "RoseChordHandler.h"

MidiMorfEngine::MidiMorfEngine()
{
    transportHandler = std::make_unique<TransportHandler> (virtualMidiPort);
    morfPadHandler  = std::make_unique<MorfPadHandler> (virtualMidiPort, 3);
    dualMPadHandlers[0] = std::make_unique<MorfPadHandler> (virtualMidiPort, 3);
    dualMPadHandlers[1] = std::make_unique<MorfPadHandler> (virtualMidiPort, 4);
    omnichordHandler = std::make_unique<OmnichordHandler> (virtualMidiPort);
    morfChordHandler   = std::make_unique<MorfChordHandler> (virtualMidiPort);
    roseChordHandler = std::make_unique<RoseChordHandler> (virtualMidiPort, 7);

    networkServer.setListener (this);
    loadState();
}

MidiMorfEngine::~MidiMorfEngine()
{
    stop();
}

void MidiMorfEngine::start()
{
    virtualMidiPort.open();
    networkServer.setListener (this);
    networkServer.start();

    for (int i = 0; i < MidiMorf::NUM_SLOTS; ++i)
        networkServer.sendSlotConfig (i, slotConfigs[(size_t) i].name);
}

void MidiMorfEngine::stop()
{
    saveState();
    networkServer.stop();
    virtualMidiPort.close();
}

juce::File MidiMorfEngine::getConfigFile() const
{
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("MidiMorf");
    dir.createDirectory();
    return dir.getChildFile ("config.xml");
}

void MidiMorfEngine::saveState()
{
    auto xml = std::make_unique<juce::XmlElement> ("MidiMorfState");

    for (int i = 0; i < MidiMorf::NUM_SLOTS; ++i)
    {
        auto* slot = xml->createNewChildElement ("Slot");
        const auto& cfg = slotConfigs[(size_t) i];
        slot->setAttribute ("index", i);
        slot->setAttribute ("name", cfg.name);
        slot->setAttribute ("ccButton", cfg.ccButton);
        slot->setAttribute ("ccXY_X", cfg.ccXY_X);
        slot->setAttribute ("ccXY_Y", cfg.ccXY_Y);
        slot->setAttribute ("channel", cfg.midiChannel);
        slot->setAttribute ("noteMode", cfg.noteMode);
        slot->setAttribute ("midiNote", cfg.midiNote);
    }

    auto* tilt = xml->createNewChildElement ("GlobalTilt");
    tilt->setAttribute ("ccTiltX", globalTiltConfig.ccTiltX);
    tilt->setAttribute ("ccTiltY", globalTiltConfig.ccTiltY);
    tilt->setAttribute ("channel", globalTiltConfig.midiChannel);
    tilt->setAttribute ("alwaysOn", globalTiltConfig.alwaysOn);

    auto* dualMPad = xml->createNewChildElement ("DualMPad");
    dualMPad->setAttribute ("leftChannel", dualMPadHandlers[0]->getChannel());
    dualMPad->setAttribute ("rightChannel", dualMPadHandlers[1]->getChannel());

    auto* morfChord = xml->createNewChildElement ("MorfChord");
    morfChord->setAttribute ("chordChannel", morfChordHandler->getChordChannel());
    morfChord->setAttribute ("strumChannel", morfChordHandler->getStrumChannel());

    auto* omni = xml->createNewChildElement ("StrumChord");
    omni->setAttribute ("chordChannel", omnichordHandler->getChordChannel());
    omni->setAttribute ("strumChannel", omnichordHandler->getStrumChannel());

    xml->writeTo (getConfigFile());
}

void MidiMorfEngine::loadState()
{
    auto configFile = getConfigFile();
    if (! configFile.existsAsFile())
        return;

    auto xml = juce::XmlDocument::parse (configFile);
    if (xml == nullptr || ! xml->hasTagName ("MidiMorfState"))
        return;

    for (auto* child : xml->getChildIterator())
    {
        if (child->hasTagName ("Slot"))
        {
            int idx = child->getIntAttribute ("index", -1);
            if (idx >= 0 && idx < MidiMorf::NUM_SLOTS)
            {
                auto& cfg = slotConfigs[(size_t) idx];
                cfg.name        = child->getStringAttribute ("name", cfg.name);
                cfg.ccButton    = child->getIntAttribute ("ccButton", cfg.ccButton);
                cfg.ccXY_X      = child->getIntAttribute ("ccXY_X", cfg.ccXY_X);
                cfg.ccXY_Y      = child->getIntAttribute ("ccXY_Y", cfg.ccXY_Y);
                cfg.midiChannel = child->getIntAttribute ("channel", cfg.midiChannel);
                cfg.noteMode    = child->getBoolAttribute ("noteMode", cfg.noteMode);
                cfg.midiNote    = child->getIntAttribute ("midiNote", cfg.midiNote);
            }
        }
        else if (child->hasTagName ("GlobalTilt"))
        {
            globalTiltConfig.ccTiltX     = child->getIntAttribute ("ccTiltX", globalTiltConfig.ccTiltX);
            globalTiltConfig.ccTiltY     = child->getIntAttribute ("ccTiltY", globalTiltConfig.ccTiltY);
            globalTiltConfig.midiChannel = child->getIntAttribute ("channel", globalTiltConfig.midiChannel);
            globalTiltConfig.alwaysOn    = child->getBoolAttribute ("alwaysOn", globalTiltConfig.alwaysOn);
        }
        else if (child->hasTagName ("DualMPad"))
        {
            dualMPadHandlers[0]->setChannel (child->getIntAttribute ("leftChannel", 3));
            dualMPadHandlers[1]->setChannel (child->getIntAttribute ("rightChannel", 4));
        }
        else if (child->hasTagName ("MorfChord"))
        {
            morfChordHandler->setChordChannel (child->getIntAttribute ("chordChannel", 5));
            morfChordHandler->setStrumChannel (child->getIntAttribute ("strumChannel", 6));
        }
        else if (child->hasTagName ("StrumChord"))
        {
            omnichordHandler->setChordChannel (child->getIntAttribute ("chordChannel", 2));
            omnichordHandler->setStrumChannel (child->getIntAttribute ("strumChannel", 2));
        }
    }
}

// Direct to VirtualMidiPort — no FIFO queue needed
void MidiMorfEngine::sendCC (int channel, int ccNumber, int value)
{
    DBG ("sendCC: ch=" << channel << " cc=" << ccNumber << " val=" << value);
    virtualMidiPort.sendCC (channel, ccNumber, value);
}

void MidiMorfEngine::sendNoteOn (int channel, int note, int velocity)
{
    virtualMidiPort.sendNoteOn (channel, note, velocity);
}

void MidiMorfEngine::sendNoteOff (int channel, int note)
{
    virtualMidiPort.sendNoteOff (channel, note);
}

// NetworkServer::Listener callbacks

void MidiMorfEngine::xyUpdated (float x, float y)
{
    DBG ("xyUpdated: " << x << ", " << y);
    currentX.store (x);
    currentY.store (y);

    int valX = juce::jlimit (0, 127, (int) (x * 127.0f));
    int valY = juce::jlimit (0, 127, (int) (y * 127.0f));

    int mask = activeSlotMask.load();
    if (mask != 0)
    {
        for (int i = 0; i < MidiMorf::NUM_SLOTS; ++i)
        {
            if (mask & (1 << i))
            {
                const auto& cfg = slotConfigs[(size_t) i];
                sendCC (cfg.midiChannel, cfg.ccXY_X, valX);
                sendCC (cfg.midiChannel, cfg.ccXY_Y, valY);
            }
        }
    }
    else
    {
        const auto& cfg = slotConfigs[(size_t) lastActiveSlot];
        sendCC (cfg.midiChannel, cfg.ccXY_X, valX);
        sendCC (cfg.midiChannel, cfg.ccXY_Y, valY);
    }
}

void MidiMorfEngine::axisUpdated (bool isXAxis, float value)
{
    int val = juce::jlimit (0, 127, (int) (value * 127.0f));

    if (isXAxis)
        currentX.store (value);
    else
        currentY.store (value);

    int mask = activeSlotMask.load();
    if (mask != 0)
    {
        for (int i = 0; i < MidiMorf::NUM_SLOTS; ++i)
        {
            if (mask & (1 << i))
            {
                const auto& cfg = slotConfigs[(size_t) i];
                if (isXAxis)
                    sendCC (cfg.midiChannel, cfg.ccXY_X, val);
                else
                    sendCC (cfg.midiChannel, cfg.ccXY_Y, val);
            }
        }
    }
    else
    {
        const auto& cfg = slotConfigs[(size_t) lastActiveSlot];
        if (isXAxis)
            sendCC (cfg.midiChannel, cfg.ccXY_X, val);
        else
            sendCC (cfg.midiChannel, cfg.ccXY_Y, val);
    }
}

void MidiMorfEngine::buttonDown (int slotIndex)
{
    lastActiveSlot = slotIndex;
    activeSlotMask.fetch_or (1 << slotIndex);
    const auto& cfg = slotConfigs[(size_t) slotIndex];

    if (cfg.noteMode)
        sendNoteOn (cfg.midiChannel, cfg.midiNote, 127);
    else
        sendCC (cfg.midiChannel, cfg.ccButton, 127);
}

void MidiMorfEngine::buttonUp (int slotIndex)
{
    activeSlotMask.fetch_and (~(1 << slotIndex));
    const auto& cfg = slotConfigs[(size_t) slotIndex];

    if (cfg.noteMode)
        sendNoteOff (cfg.midiChannel, cfg.midiNote);
    else
        sendCC (cfg.midiChannel, cfg.ccButton, 0);
}

void MidiMorfEngine::tiltUpdated (float tiltX, float tiltY)
{
    currentTiltX.store (tiltX);
    currentTiltY.store (tiltY);

    if (! globalTiltConfig.alwaysOn && activeSlotMask.load() == 0)
        return;

    int valTiltX = juce::jlimit (0, 127, (int) ((tiltX + 1.0f) * 0.5f * 127.0f));
    int valTiltY = juce::jlimit (0, 127, (int) ((tiltY + 1.0f) * 0.5f * 127.0f));

    sendCC (globalTiltConfig.midiChannel, globalTiltConfig.ccTiltX, valTiltX);
    sendCC (globalTiltConfig.midiChannel, globalTiltConfig.ccTiltY, valTiltY);
}

void MidiMorfEngine::clientConnected (const juce::String&)
{
    for (int i = 0; i < MidiMorf::NUM_SLOTS; ++i)
        networkServer.sendSlotConfig (i, slotConfigs[(size_t) i].name);
}

void MidiMorfEngine::clientDisconnected()
{
    int mask = activeSlotMask.exchange (0);
    for (int i = 0; i < MidiMorf::NUM_SLOTS; ++i)
    {
        if (mask & (1 << i))
        {
            const auto& cfg = slotConfigs[(size_t) i];
            if (cfg.noteMode)
                sendNoteOff (cfg.midiChannel, cfg.midiNote);
            else
                sendCC (cfg.midiChannel, cfg.ccButton, 0);
        }
    }

    // Zero out global tilt
    sendCC (globalTiltConfig.midiChannel, globalTiltConfig.ccTiltX, 0);
    sendCC (globalTiltConfig.midiChannel, globalTiltConfig.ccTiltY, 0);

    currentX.store (0.0f);
    currentY.store (0.0f);
    currentTiltX.store (0.0f);
    currentTiltY.store (0.0f);
    currentMode.store (0);

    // Clean up all handlers
    morfPadHandler->allNotesOff();
    dualMPadHandlers[0]->allNotesOff();
    dualMPadHandlers[1]->allNotesOff();
    omnichordHandler->allNotesOff();
    morfChordHandler->allNotesOff();
    roseChordHandler->allNotesOff();
    transportHandler->allNotesOff();
}

// Multi-controller callbacks

void MidiMorfEngine::screenModeChanged (MidiMorf::ControllerMode mode)
{
    currentMode.store (static_cast<int> (mode));
    DBG ("Screen mode changed to: " << static_cast<int> (mode));
}

void MidiMorfEngine::morfPadTouchDown (float x, float y)
{
    morfPadHandler->touchDown (x, y);
}

void MidiMorfEngine::morfPadTouchMove (float x, float y)
{
    morfPadHandler->touchMove (x, y);
}

void MidiMorfEngine::morfPadTouchUp()
{
    morfPadHandler->touchUp();
}

void MidiMorfEngine::morfPadScaleSelected (uint8_t scaleIndex, uint8_t rootNote, uint8_t octaveRange, uint8_t baseOctave)
{
    morfPadHandler->setScale (scaleIndex, rootNote, octaveRange, baseOctave);
}

void MidiMorfEngine::chordSelected (uint8_t rootNote, uint8_t quality)
{
    omnichordHandler->selectChord (rootNote, quality);
}

void MidiMorfEngine::chordReleased()
{
    omnichordHandler->releaseChord();
}

void MidiMorfEngine::strumGesture (float position, float velocity)
{
    omnichordHandler->strum (position, velocity);
}

void MidiMorfEngine::transportButtonPressed (uint8_t buttonId)
{
    transportHandler->buttonPressed (buttonId);
}

void MidiMorfEngine::dualMPadTouchDown (uint8_t padIndex, float x, float y)
{
    if (padIndex < 2)
        dualMPadHandlers[padIndex]->touchDown (x, y);
}

void MidiMorfEngine::dualMPadTouchMove (uint8_t padIndex, float x, float y)
{
    if (padIndex < 2)
        dualMPadHandlers[padIndex]->touchMove (x, y);
}

void MidiMorfEngine::dualMPadTouchUp (uint8_t padIndex)
{
    if (padIndex < 2)
        dualMPadHandlers[padIndex]->touchUp();
}

void MidiMorfEngine::dualMPadScaleSelected (uint8_t padIndex, uint8_t scaleIndex, uint8_t rootNote, uint8_t octaveRange, uint8_t baseOctave)
{
    if (padIndex < 2)
        dualMPadHandlers[padIndex]->setScale (scaleIndex, rootNote, octaveRange, baseOctave);
}

void MidiMorfEngine::morfChordSelected (uint8_t degreeIndex, float x, float y)
{
    morfChordHandler->selectDegree (degreeIndex, x, y);
}

void MidiMorfEngine::morfChordMoved (uint8_t degreeIndex, float x, float y)
{
    morfChordHandler->moveDegree (degreeIndex, x, y);
}

void MidiMorfEngine::morfChordReleased()
{
    morfChordHandler->releaseDegree();
}

void MidiMorfEngine::morfChordStrum (float position, float velocity)
{
    morfChordHandler->strum (position, velocity);
}

void MidiMorfEngine::morfChordKeySelected (uint8_t rootNote, uint8_t scaleMode)
{
    morfChordHandler->setKey (rootNote, scaleMode);
}

void MidiMorfEngine::roseChordNoteOn (uint8_t rootIdx, uint8_t typeBits, uint8_t modBits, int8_t voicingOffset)
{
    roseChordHandler->setConfig (typeBits, modBits, voicingOffset);
    roseChordHandler->noteOn (rootIdx);
}

void MidiMorfEngine::roseChordNoteOff()
{
    roseChordHandler->noteOff();
}

void MidiMorfEngine::roseChordConfig (uint8_t typeBits, uint8_t modBits, int8_t voicingOffset)
{
    roseChordHandler->setConfig (typeBits, modBits, voicingOffset);
}

// Local UI interaction

void MidiMorfEngine::localXYUpdated (float x, float y)
{
    currentX.store (x);
    currentY.store (y);

    int valX = juce::jlimit (0, 127, (int) (x * 127.0f));
    int valY = juce::jlimit (0, 127, (int) (y * 127.0f));

    int mask = activeSlotMask.load();
    if (mask != 0)
    {
        for (int i = 0; i < MidiMorf::NUM_SLOTS; ++i)
        {
            if (mask & (1 << i))
            {
                const auto& cfg = slotConfigs[(size_t) i];
                sendCC (cfg.midiChannel, cfg.ccXY_X, valX);
                sendCC (cfg.midiChannel, cfg.ccXY_Y, valY);
            }
        }
    }
    else
    {
        const auto& cfg = slotConfigs[(size_t) lastActiveSlot];
        sendCC (cfg.midiChannel, cfg.ccXY_X, valX);
        sendCC (cfg.midiChannel, cfg.ccXY_Y, valY);
    }
}

void MidiMorfEngine::localAxisUpdated (bool isXAxis, float value)
{
    int val = juce::jlimit (0, 127, (int) (value * 127.0f));

    if (isXAxis)
        currentX.store (value);
    else
        currentY.store (value);

    int mask = activeSlotMask.load();
    if (mask != 0)
    {
        for (int i = 0; i < MidiMorf::NUM_SLOTS; ++i)
        {
            if (mask & (1 << i))
            {
                const auto& cfg = slotConfigs[(size_t) i];
                if (isXAxis)
                    sendCC (cfg.midiChannel, cfg.ccXY_X, val);
                else
                    sendCC (cfg.midiChannel, cfg.ccXY_Y, val);
            }
        }
    }
    else
    {
        const auto& cfg = slotConfigs[(size_t) lastActiveSlot];
        if (isXAxis)
            sendCC (cfg.midiChannel, cfg.ccXY_X, val);
        else
            sendCC (cfg.midiChannel, cfg.ccXY_Y, val);
    }
}

void MidiMorfEngine::localButtonDown (int slotIndex)
{
    buttonDown (slotIndex);
}

void MidiMorfEngine::localButtonUp (int slotIndex)
{
    buttonUp (slotIndex);
}

void MidiMorfEngine::sendTiltAssignBurst (bool isXAxis)
{
    int cc = isXAxis ? globalTiltConfig.ccTiltX : globalTiltConfig.ccTiltY;
    int ch = globalTiltConfig.midiChannel;
    sendCC (ch, cc, 0);
    sendCC (ch, cc, 127);
    sendCC (ch, cc, 0);
}
