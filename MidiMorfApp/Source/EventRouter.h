#pragma once

#include <JuceHeader.h>
#include "TrackRack.h"
#include "PunchFx.h"
#include "MidiSink.h"
#include "MorfPadHandler.h"
#include "OmnichordHandler.h"
#include "MorfChordHandler.h"
#include "RoseChordHandler.h"
#include "MidiMorfProtocol.h"
#include "LocalSequencers.h"

// EventRouter sits between the controller pages and the sounds: it turns
// their semantic events into MIDI notes via per-controller Handlers, which
// play the track that page is assigned to. FX Punch-in plays the punch-in FX
// on the master bus.
//
// Each track has its own router (setFixedTrack), so each track's controller
// pages have their own handlers — two Morf-Chords on two tracks each keep
// their own notes — and its sequencer pages' messages reach its own engine.
class EventRouter
{
public:
    // Default tracks (0-based): every controller plays T1.
    EventRouter (TrackRack& r, LocalSequencers& seqs)
        : rack (r),
          sequencers (seqs),
          morfPadSink (r, 0), omnichordSink (r, 0), dualMPadSink (r, 0), morfChordSink (r, 0), roseChordSink (r, 0),
          morfPad (morfPadSink, 3),
          omnichord (omnichordSink),
          dualMPadLeft (dualMPadSink, 3),
          dualMPadRight (dualMPadSink, 4),
          morfChord (morfChordSink),
          roseChord (roseChordSink, 7)
    {
    }

    //==========================================================================
    // Which track each page plays — message thread. Sequencer pages hand off
    // to LocalSequencers; FX Punch-in and Transport play no notes.
    void setTrackFor (MidiMorf::ControllerMode page, int track)
    {
        using CM = MidiMorf::ControllerMode;
        track = juce::jlimit (0, TrackRack::NumTracks - 1, track);

        switch (page)
        {
            case CM::StepSequencer:
            case CM::SliceSequencer: return;   // every track has its own (LocalSequencers::setKind)
            default: break;
        }

        if (auto* sink = sinkFor (page))
        {
            if (sink->getTrack() == track)
                return;

            // Note-offs go out before the switch, so they reach the old track.
            switch (page)
            {
                case CM::MorfPad:   morfPad.allNotesOff(); break;
                case CM::Omnichord: omnichord.allNotesOff(); break;
                case CM::DualMPad:  dualMPadLeft.allNotesOff(); dualMPadRight.allNotesOff(); break;
                case CM::MorfChord: morfChord.allNotesOff(); break;
                case CM::RoseChord: roseChord.allNotesOff(); break;
                default: break;
            }
            sink->setTrack (track);
        }
    }

    // -1 for pages that play no notes.
    int getTrackFor (MidiMorf::ControllerMode page) const
    {
        using CM = MidiMorf::ControllerMode;
        if (page == CM::StepSequencer || page == CM::SliceSequencer) return -1;   // per track
        if (auto* sink = sinkFor (page))
            return sink->getTrack();
        return -1;
    }

    TrackRack& getRack() noexcept { return rack; }

    // A track's own router: every controller plays `track`, and sequencer
    // messages go to that track's sequencer.
    void setFixedTrack (int track)
    {
        fixedTrack = juce::jlimit (0, TrackRack::NumTracks - 1, track);
        for (auto* sink : { &morfPadSink, &omnichordSink, &dualMPadSink, &morfChordSink, &roseChordSink })
            sink->setTrack (fixedTrack);
    }

    int getFixedTrack() const noexcept { return fixedTrack; }

    // Lets go of every controller's notes.
    void silenceControllers()
    {
        morfPad.allNotesOff();
        omnichord.allNotesOff();
        dualMPadLeft.allNotesOff();
        dualMPadRight.allNotesOff();
        morfChord.allNotesOff();
        roseChord.allNotesOff();
    }

    // Set once at startup.
    void setPunchFx (PunchFx* p) { punch = p; }
    PunchFx* getPunchFx() const noexcept { return punch; }

    //==========================================================================
    // FX Punch-in: the punch-in FX on the master bus.

    void sendXYUpdate (float x, float y)
    {
        if (punch != nullptr) punch->setXY (x, y);
    }

    void sendAxisUpdate (bool isXAxis, float v)
    {
        if (punch != nullptr) { if (isXAxis) punch->setX (v); else punch->setY (v); }
    }

    void sendButtonDown (int slotIndex)
    {
        if (punch != nullptr) punch->setActive (slotIndex, true);
    }

    void sendButtonUp (int slotIndex)
    {
        if (punch != nullptr) punch->setActive (slotIndex, false);
    }

    //==========================================================================
    // The sequencers, on the phone's own clock.

    void sendSeqStepSet (uint8_t step, uint8_t degree, uint8_t velocity, uint8_t gate, uint8_t mod, bool rest)
    {
        aim(); sequencers.stepSet (step, degree, velocity, gate, mod, rest);
    }

    void sendSeqConfig (uint8_t patternLength, uint8_t rate, uint8_t direction, uint8_t swing,
                        uint8_t scaleIndex, uint8_t rootNote, uint8_t baseOctave, uint8_t octaveRange)
    {
        aim(); sequencers.config (patternLength, rate, direction, swing, scaleIndex, rootNote, baseOctave, octaveRange);
    }

    void sendSeqTransport (MidiMorf::SeqCommand c)
    {
        aim(); sequencers.transport (c);
    }

    void sendSeqRatchet (MidiMorf::SeqRatchetDiv d, bool on)
    {
        aim(); sequencers.ratchet ((uint8_t) d, on);
    }

    void sendSeqSliceConfig (uint8_t bars, uint8_t division, uint8_t sliceCount,
                             uint8_t rootNote, uint8_t direction, uint8_t swing)
    {
        aim(); sequencers.sliceConfig (bars, division, sliceCount, rootNote, direction, swing);
    }

    void sendSeqLooper (uint8_t division, bool active, bool randomOn)
    {
        aim(); sequencers.looper (division, active, randomOn);
    }

    void sendSeqRibbon (uint8_t ribbonIndex, int value14bit)
    {
        aim(); sequencers.ribbon (ribbonIndex, value14bit);
    }

    void sendSeqRibbonAssign (uint8_t ribbonIndex, MidiMorf::RibbonTarget target, uint8_t ccNumber)
    {
        aim(); sequencers.ribbonAssign (ribbonIndex, target, ccNumber);
    }

    LocalSequencers& getSequencers() noexcept { return sequencers; }

    //==========================================================================
    // Morf-Pad

    void sendMorfPadTouchDown (float x, float y) { morfPad.touchDown (x, y); }
    void sendMorfPadTouchMove (float x, float y) { morfPad.touchMove (x, y); }
    void sendMorfPadTouchUp()                    { morfPad.touchUp(); }

    void sendMorfPadScaleSelect (uint8_t scaleIdx, uint8_t root, uint8_t octRange, uint8_t baseOct)
    {
        morfPad.setScale (scaleIdx, root, octRange, baseOct);
    }

    //==========================================================================
    // Omnichord

    void sendChordSelect (uint8_t rootNote, uint8_t quality)  { omnichord.selectChord (rootNote, quality); }
    void sendChordRelease()                                   { omnichord.releaseChord(); }
    void sendStrumGesture (float position, float velocity)    { omnichord.strum (position, velocity); }

    //==========================================================================
    // Dual M-Pad

    void sendDualMPadTouchDown (uint8_t padIndex, float x, float y) { (padIndex == 0 ? dualMPadLeft : dualMPadRight).touchDown (x, y); }
    void sendDualMPadTouchMove (uint8_t padIndex, float x, float y) { (padIndex == 0 ? dualMPadLeft : dualMPadRight).touchMove (x, y); }
    void sendDualMPadTouchUp (uint8_t padIndex)                     { (padIndex == 0 ? dualMPadLeft : dualMPadRight).touchUp(); }

    void sendDualMPadScaleSelect (uint8_t padIndex, uint8_t scaleIdx, uint8_t root, uint8_t octRange, uint8_t baseOct)
    {
        (padIndex == 0 ? dualMPadLeft : dualMPadRight).setScale (scaleIdx, root, octRange, baseOct);
    }

    //==========================================================================
    // Morf-Chord

    void sendMorfChordSelect (uint8_t degreeIndex, float x, float y) { morfChord.selectDegree (degreeIndex, x, y); }
    void sendMorfChordMove (uint8_t degreeIndex, float x, float y)   { morfChord.moveDegree (degreeIndex, x, y); }
    void sendMorfChordRelease()                                      { morfChord.releaseDegree(); }
    void sendMorfChordStrum (float position, float velocity)         { morfChord.strum (position, velocity); }
    void sendMorfChordKeySelect (uint8_t rootNote, uint8_t scaleMode) { morfChord.setKey (rootNote, scaleMode); }

    //==========================================================================
    // Rose-Chord

    void sendRoseChordNoteOn (uint8_t rootIdx, uint8_t typeBits, uint8_t modBits, int8_t voicingOffset, int8_t octaveOffset)
    {
        roseChord.setConfig (typeBits, modBits, voicingOffset, octaveOffset);
        roseChord.noteOn (rootIdx);
    }

    void sendRoseChordNoteOff() { roseChord.noteOff(); }

    void sendRoseChordConfig (uint8_t typeBits, uint8_t modBits, int8_t voicingOffset, int8_t octaveOffset)
    {
        // Configured now so the handler is ready when the next noteOn arrives.
        roseChord.setConfig (typeBits, modBits, voicingOffset, octaveOffset);
    }

private:
    PhoneMidiSink* sinkFor (MidiMorf::ControllerMode page)
    {
        return const_cast<PhoneMidiSink*> (std::as_const (*this).sinkFor (page));
    }

    const PhoneMidiSink* sinkFor (MidiMorf::ControllerMode page) const
    {
        using CM = MidiMorf::ControllerMode;
        switch (page)
        {
            case CM::MorfPad:   return &morfPadSink;
            case CM::Omnichord: return &omnichordSink;
            case CM::DualMPad:  return &dualMPadSink;
            case CM::MorfChord: return &morfChordSink;
            case CM::RoseChord: return &roseChordSink;
            default:            return nullptr;
        }
    }

    TrackRack& rack;
    LocalSequencers& sequencers;
    PhoneMidiSink morfPadSink, omnichordSink, dualMPadSink, morfChordSink, roseChordSink;

    MorfPadHandler morfPad;
    OmnichordHandler omnichord;
    MorfPadHandler dualMPadLeft;
    MorfPadHandler dualMPadRight;
    MorfChordHandler morfChord;
    RoseChordHandler roseChord;

    PunchFx* punch = nullptr;
    // A track's router aims the sequencer messages at its track.
    void aim() { if (fixedTrack >= 0) sequencers.setActiveTrack (fixedTrack); }

    int fixedTrack = -1;
};
