#pragma once

#include <JuceHeader.h>
#include "NetworkClient.h"
#include "WavetableEngine.h"
#include "MidiSink.h"
#include "MorfPadHandler.h"
#include "OmnichordHandler.h"
#include "MorfChordHandler.h"
#include "RoseChordHandler.h"
#include "MidiMorfProtocol.h"
#include "LocalSequencers.h"

// EventRouter sits between controller pages and the two MIDI backends:
//   • WiFi mode  → forward semantic events to NetworkClient (existing behaviour).
//   • Local mode → translate semantic events into MIDI notes via per-controller
//                  Handlers, then call the built-in synth for audio.
// FX Punch-in and Transport are always WiFi-only — they don't play notes that
// the local synth model can sensibly render.
class EventRouter
{
public:
    enum class Mode { WiFi, Local };

    // Where the sequencers get their clock — independent of Mode, which only
    // says where controllers send notes. Local: the phone's clock, playing the
    // built-in synth and breaks. DAW: the server, following DAW MIDI clock.
    enum class SyncSource { Local, DAW };

    EventRouter (NetworkClient& nc, WavetableEngine& synthRef, LocalSequencers& seqs)
        : network (nc),
          sequencers (seqs),
          sink (synthRef),
          morfPad (sink, 3),
          omnichord (sink),
          dualMPadLeft (sink, 3),
          dualMPadRight (sink, 4),
          morfChord (sink),
          roseChord (sink, 7)
    {
    }

    void setMode (Mode m)
    {
        if (mode.load() == m)
            return;

        // Silence local synth on every transition so we don't leave hanging notes.
        morfPad.allNotesOff();
        omnichord.allNotesOff();
        dualMPadLeft.allNotesOff();
        dualMPadRight.allNotesOff();
        morfChord.allNotesOff();
        roseChord.allNotesOff();

        mode.store (m);
    }

    Mode getMode() const noexcept { return mode.load(); }

    void setSyncSource (SyncSource s)
    {
        // The phone's clock has no business running once the DAW drives.
        if (s == SyncSource::DAW)
            sequencers.stopAll();

        syncSource.store (s);
    }

    SyncSource getSyncSource() const noexcept { return syncSource.load(); }
    bool isLocalSync() const noexcept         { return syncSource.load() == SyncSource::Local; }

    //==========================================================================
    // FX Punch-in / Transport / global — always WiFi.

    void sendXYUpdate (float x, float y)         { network.sendXYUpdate (x, y); }
    void sendAxisUpdate (bool isXAxis, float v)  { network.sendAxisUpdate (isXAxis, v); }
    void sendButtonDown (int slotIndex)          { network.sendButtonDown (slotIndex); }
    void sendButtonUp (int slotIndex)            { network.sendButtonUp (slotIndex); }
    void sendTiltUpdate (float tx, float ty)     { network.sendTiltUpdate (tx, ty); }
    void sendScreenMode (MidiMorf::ControllerMode m) { network.sendScreenMode (m); sequencers.setActivePage (m); }
    void sendTransportButton (uint8_t buttonId)  { network.sendTransportButton (buttonId); }

    // Creative Sequencer Pack. With DAW sync the server runs the sequencers on
    // DAW clock; with Local sync LocalSequencers runs the same engines on the
    // phone's own clock. Pattern and config edits go to both so either side
    // is current the moment the source flips; performance gestures go only to
    // whichever side is playing.
    void sendSeqStepSet (uint8_t step, uint8_t degree, uint8_t velocity, uint8_t gate, uint8_t mod, bool rest)
    {
        network.sendSeqStepSet (step, degree, velocity, gate, mod, rest);
        sequencers.stepSet (step, degree, velocity, gate, mod, rest);
    }

    void sendSeqConfig (uint8_t patternLength, uint8_t rate, uint8_t direction, uint8_t swing,
                        uint8_t scaleIndex, uint8_t rootNote, uint8_t baseOctave, uint8_t octaveRange)
    {
        network.sendSeqConfig (patternLength, rate, direction, swing, scaleIndex, rootNote, baseOctave, octaveRange);
        sequencers.config (patternLength, rate, direction, swing, scaleIndex, rootNote, baseOctave, octaveRange);
    }

    void sendSeqTransport (MidiMorf::SeqCommand c)
    {
        if (! isLocalSync()) network.sendSeqTransport (c);
        else                           sequencers.transport (c);
    }

    void sendSeqRatchet (MidiMorf::SeqRatchetDiv d, bool on)
    {
        if (! isLocalSync()) network.sendSeqRatchet (d, on);
        else                           sequencers.ratchet ((uint8_t) d, on);
    }

    void sendSeqSliceConfig (uint8_t bars, uint8_t division, uint8_t sliceCount,
                             uint8_t rootNote, uint8_t direction, uint8_t swing)
    {
        network.sendSeqSliceConfig (bars, division, sliceCount, rootNote, direction, swing);
        sequencers.sliceConfig (bars, division, sliceCount, rootNote, direction, swing);
    }

    void sendSeqLooper (uint8_t division, bool active, bool randomOn)
    {
        if (! isLocalSync()) network.sendSeqLooper (division, active, randomOn);
        else                           sequencers.looper (division, active, randomOn);
    }

    void sendSeqRibbon (uint8_t ribbonIndex, int value14bit)
    {
        if (! isLocalSync()) network.sendSeqRibbon (ribbonIndex, value14bit);
        else                           sequencers.ribbon (ribbonIndex, value14bit);
    }

    void sendSeqRibbonAssign (uint8_t ribbonIndex, MidiMorf::RibbonTarget target, uint8_t ccNumber)
    {
        network.sendSeqRibbonAssign (ribbonIndex, target, ccNumber);
        sequencers.ribbonAssign (ribbonIndex, target, ccNumber);
    }

    LocalSequencers& getSequencers() noexcept { return sequencers; }

    //==========================================================================
    // Morf-Pad

    void sendMorfPadTouchDown (float x, float y)
    {
        if (mode.load() == Mode::WiFi) network.sendMorfPadTouchDown (x, y);
        else                            morfPad.touchDown (x, y);
    }

    void sendMorfPadTouchMove (float x, float y)
    {
        if (mode.load() == Mode::WiFi) network.sendMorfPadTouchMove (x, y);
        else                            morfPad.touchMove (x, y);
    }

    void sendMorfPadTouchUp()
    {
        if (mode.load() == Mode::WiFi) network.sendMorfPadTouchUp();
        else                            morfPad.touchUp();
    }

    void sendMorfPadScaleSelect (uint8_t scaleIdx, uint8_t root, uint8_t octRange, uint8_t baseOct)
    {
        // Always send to network so the server stays in sync; also configure local handler.
        network.sendMorfPadScaleSelect (scaleIdx, root, octRange, baseOct);
        morfPad.setScale (scaleIdx, root, octRange, baseOct);
    }

    //==========================================================================
    // Omnichord

    void sendChordSelect (uint8_t rootNote, uint8_t quality)
    {
        if (mode.load() == Mode::WiFi) network.sendChordSelect (rootNote, quality);
        else                            omnichord.selectChord (rootNote, quality);
    }

    void sendChordRelease()
    {
        if (mode.load() == Mode::WiFi) network.sendChordRelease();
        else                            omnichord.releaseChord();
    }

    void sendStrumGesture (float position, float velocity)
    {
        if (mode.load() == Mode::WiFi) network.sendStrumGesture (position, velocity);
        else                            omnichord.strum (position, velocity);
    }

    //==========================================================================
    // Dual M-Pad

    void sendDualMPadTouchDown (uint8_t padIndex, float x, float y)
    {
        if (mode.load() == Mode::WiFi)
            network.sendDualMPadTouchDown (padIndex, x, y);
        else
            (padIndex == 0 ? dualMPadLeft : dualMPadRight).touchDown (x, y);
    }

    void sendDualMPadTouchMove (uint8_t padIndex, float x, float y)
    {
        if (mode.load() == Mode::WiFi)
            network.sendDualMPadTouchMove (padIndex, x, y);
        else
            (padIndex == 0 ? dualMPadLeft : dualMPadRight).touchMove (x, y);
    }

    void sendDualMPadTouchUp (uint8_t padIndex)
    {
        if (mode.load() == Mode::WiFi)
            network.sendDualMPadTouchUp (padIndex);
        else
            (padIndex == 0 ? dualMPadLeft : dualMPadRight).touchUp();
    }

    void sendDualMPadScaleSelect (uint8_t padIndex, uint8_t scaleIdx, uint8_t root, uint8_t octRange, uint8_t baseOct)
    {
        network.sendDualMPadScaleSelect (padIndex, scaleIdx, root, octRange, baseOct);
        (padIndex == 0 ? dualMPadLeft : dualMPadRight).setScale (scaleIdx, root, octRange, baseOct);
    }

    //==========================================================================
    // Morf-Chord

    void sendMorfChordSelect (uint8_t degreeIndex, float x, float y)
    {
        if (mode.load() == Mode::WiFi) network.sendMorfChordSelect (degreeIndex, x, y);
        else                            morfChord.selectDegree (degreeIndex, x, y);
    }

    void sendMorfChordMove (uint8_t degreeIndex, float x, float y)
    {
        if (mode.load() == Mode::WiFi) network.sendMorfChordMove (degreeIndex, x, y);
        else                            morfChord.moveDegree (degreeIndex, x, y);
    }

    void sendMorfChordRelease()
    {
        if (mode.load() == Mode::WiFi) network.sendMorfChordRelease();
        else                            morfChord.releaseDegree();
    }

    void sendMorfChordStrum (float position, float velocity)
    {
        if (mode.load() == Mode::WiFi) network.sendMorfChordStrum (position, velocity);
        else                            morfChord.strum (position, velocity);
    }

    void sendMorfChordKeySelect (uint8_t rootNote, uint8_t scaleMode)
    {
        network.sendMorfChordKeySelect (rootNote, scaleMode);
        morfChord.setKey (rootNote, scaleMode);
    }

    //==========================================================================
    // Rose-Chord

    void sendRoseChordNoteOn (uint8_t rootIdx, uint8_t typeBits, uint8_t modBits, int8_t voicingOffset, int8_t octaveOffset)
    {
        if (mode.load() == Mode::WiFi)
        {
            network.sendRoseChordNoteOn (rootIdx, typeBits, modBits, voicingOffset, octaveOffset);
        }
        else
        {
            roseChord.setConfig (typeBits, modBits, voicingOffset, octaveOffset);
            roseChord.noteOn (rootIdx);
        }
    }

    void sendRoseChordNoteOff()
    {
        if (mode.load() == Mode::WiFi) network.sendRoseChordNoteOff();
        else                            roseChord.noteOff();
    }

    void sendRoseChordConfig (uint8_t typeBits, uint8_t modBits, int8_t voicingOffset, int8_t octaveOffset)
    {
        // Always send config to network so the server stays in sync.
        network.sendRoseChordConfig (typeBits, modBits, voicingOffset, octaveOffset);
        // Always apply locally so the handler is configured when the next noteOn arrives.
        roseChord.setConfig (typeBits, modBits, voicingOffset, octaveOffset);
    }

private:
    NetworkClient& network;
    LocalSequencers& sequencers;
    PhoneMidiSink sink;

    MorfPadHandler morfPad;
    OmnichordHandler omnichord;
    MorfPadHandler dualMPadLeft;
    MorfPadHandler dualMPadRight;
    MorfChordHandler morfChord;
    RoseChordHandler roseChord;

    std::atomic<Mode> mode { Mode::WiFi };
    std::atomic<SyncSource> syncSource { SyncSource::Local };
};
