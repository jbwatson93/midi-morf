#pragma once

#include <cstdint>

namespace MidiMorf
{
    // Messages sent from phone -> plugin/server
    enum class PhoneMessage : uint8_t
    {
        // FX Punch-in (existing)
        XYUpdate       = 0x01,  // float x, float y (0.0-1.0)
        ButtonDown     = 0x02,  // uint8_t slotIndex (0-3)
        ButtonUp       = 0x03,  // uint8_t slotIndex (0-3)
        TiltUpdate     = 0x04,  // float tiltX, float tiltY (-1.0 to 1.0)
        Ping           = 0x05,
        AxisUpdate     = 0x06,  // uint8_t isXAxis (0=Y, 1=X), float value (0.0-1.0)

        // Multi-controller navigation
        ScreenMode     = 0x20,  // uint8_t mode (0=FX, 1=MorfPad, 2=Omnichord, 3=Transport)

        // Morf-Pad
        MorfPadTouchDown   = 0x21,  // float x, float y (0.0-1.0)
        MorfPadTouchMove   = 0x22,  // float x, float y (0.0-1.0)
        MorfPadTouchUp     = 0x23,  // no payload
        MorfPadScaleSelect = 0x24,  // uint8_t scaleIndex, uint8_t rootNote (0-11), uint8_t octaveRange, uint8_t baseOctave

        // Omnichord
        ChordSelect    = 0x30,  // uint8_t rootNote (0-11), uint8_t quality (0=Maj, 1=Min, 2=7th)
        ChordRelease   = 0x31,  // no payload
        StrumGesture   = 0x32,  // float position (0-1), float velocity (0-1)

        // Dual M-Pad Pad
        DualMPadTouchDown   = 0x25,  // uint8_t padIndex (0=left, 1=right), float x, float y
        DualMPadTouchMove   = 0x26,  // uint8_t padIndex, float x, float y
        DualMPadTouchUp     = 0x27,  // uint8_t padIndex
        DualMPadScaleSelect = 0x28,  // uint8_t padIndex, uint8_t scaleIndex, uint8_t rootNote, uint8_t octaveRange, uint8_t baseOctave

        // Morf-Chord
        MorfChordSelect    = 0x50,  // uint8_t degreeIndex (0-6), float x, float y (0.0-1.0)
        MorfChordRelease   = 0x51,  // no payload
        MorfChordStrum     = 0x52,  // float position (0-1), float velocity (0-1)
        MorfChordKeySelect = 0x53,  // uint8_t rootNote (0-11), uint8_t scaleMode (0=Major, 1=Minor)
        MorfChordMove      = 0x54,  // uint8_t degreeIndex (0-6), float x, float y (0.0-1.0)

        // Transport
        TransportButton = 0x40, // uint8_t buttonId (0=play, 1=stop, 2=record, 3=rewind, 4=ff, 5=loop)

        // Rose-Chord (Orchid-inspired, 7th controller)
        RoseChordNoteOn  = 0x60,  // u8 rootIdx, u8 typeBits, u8 modBits, i8 voicingOffset, i8 octaveOffset
        RoseChordNoteOff = 0x61,  // no payload
        RoseChordConfig  = 0x62,  // u8 typeBits, u8 modBits, i8 voicingOffset, i8 octaveOffset (live update)

        // Creative Sequencer Pack (paid). Timing lives on the server, driven by
        // DAW MIDI clock — the phone is a remote editor and sends state, not events.
        PackEntitlement = 0x08,  // u8 packId, u8 valid
        SeqStepSet      = 0x70,  // u8 stepIndex, u8 degree, u8 velocity, u8 gate, u8 mod, u8 flags
        SeqConfig       = 0x71,  // u8 patternLength, u8 rate, u8 direction, u8 swing, u8 scale, u8 root, u8 baseOctave, u8 octaveRange
        SeqTransport    = 0x72,  // u8 command (0=panic, 1=run, 2=reset)
        SeqRatchet      = 0x73,  // u8 division (0=1/4, 1=1/8, 2=1/16, 3=1/32), u8 active

        // Slice sequencer (mlr / Re:Mix style). Its steps reuse SeqStepSet —
        // the degree byte is an index into the note table, which for the
        // slicer is a slice number. SeqStepSet/SeqTransport/SeqRatchet are
        // routed to whichever engine matches the phone's current page.
        SeqSliceConfig  = 0x74,  // u8 bars, u8 division, u8 sliceCount, u8 rootNote, u8 direction, u8 swing
        SeqLooper       = 0x75,  // u8 division (0=1/4..3=1/32), u8 active, u8 randomOn
        SeqRibbon       = 0x76,  // u8 ribbonIndex, u16 value (big-endian hi/lo; 14-bit for
                                 //   pitchbend/CC, repeat count 1..16 for stepRepeat)
        SeqRibbonAssign = 0x77,  // u8 ribbonIndex, u8 target (0=pitchbend, 1=CC, 2=stepRepeat), u8 ccNumber
    };

    // Messages sent from plugin/server -> phone
    enum class PluginMessage : uint8_t
    {
        SlotConfig  = 0x10,  // uint8_t slotIndex + null-terminated name string
        Pong        = 0x15,
        SeqPlayhead = 0x16,  // uint8_t currentStep (0-15, or SEQ_STEP_NONE when stopped)
    };

    // Controller modes
    enum class ControllerMode : uint8_t
    {
        FXPunchIn  = 0,
        MorfPad   = 1,
        Omnichord  = 2,
        Transport  = 3,
        DualMPad  = 4,
        MorfChord  = 5,
        RoseChord  = 6,

        // Creative Sequencer Pack
        StepSequencer  = 7,
        SliceSequencer = 8,
        Physics        = 9,
    };

    // Packet format: [1 byte type][payload bytes]
    // All floats are 4 bytes, big-endian IEEE 754

    static constexpr int NUM_SLOTS = 4;
    // MUST equal the number of controller pages registered in PageContainer —
    // MainComponent uses it as the index base for the synth/about/settings
    // pages and to bound controllerNames[]. Bump only when a page ships.
    static constexpr int NUM_CONTROLLERS = 9;
    static constexpr int DEFAULT_PORT = 17405;    // TCP connection port
    static constexpr int DISCOVERY_PORT = 17406;  // UDP broadcast port for service discovery

    // Header size is just the 1-byte message type
    static constexpr int HEADER_SIZE = 1;

    // Payload sizes for each phone message type
    static constexpr int XY_PAYLOAD_SIZE       = 8;  // 2 floats
    static constexpr int BUTTON_PAYLOAD_SIZE   = 1;  // 1 uint8_t
    static constexpr int TILT_PAYLOAD_SIZE     = 8;  // 2 floats
    static constexpr int PING_PAYLOAD_SIZE     = 0;
    static constexpr int AXIS_PAYLOAD_SIZE     = 5;  // 1 uint8_t + 1 float

    static constexpr int SCREEN_MODE_PAYLOAD_SIZE     = 1;  // 1 uint8_t
    static constexpr int MORFPAD_TOUCH_PAYLOAD_SIZE     = 8;  // 2 floats
    static constexpr int MORFPAD_TOUCHUP_PAYLOAD_SIZE   = 0;
    static constexpr int MORFPAD_SCALE_PAYLOAD_SIZE     = 4;  // 4 uint8_t
    static constexpr int CHORD_SELECT_PAYLOAD_SIZE    = 2;  // 2 uint8_t
    static constexpr int CHORD_RELEASE_PAYLOAD_SIZE   = 0;
    static constexpr int STRUM_PAYLOAD_SIZE           = 8;  // 2 floats
    static constexpr int TRANSPORT_PAYLOAD_SIZE       = 1;  // 1 uint8_t

    // Dual M-Pad Pad payload sizes
    static constexpr int DUAL_MPAD_TOUCH_PAYLOAD_SIZE   = 9;  // 1 uint8_t padIndex + 2 floats
    static constexpr int DUAL_MPAD_TOUCHUP_PAYLOAD_SIZE = 1;  // 1 uint8_t padIndex
    static constexpr int DUAL_MPAD_SCALE_PAYLOAD_SIZE   = 5;  // 1 uint8_t padIndex + 4 uint8_t

    // Morf-Chord payload sizes
    static constexpr int MORFCHORD_SELECT_PAYLOAD_SIZE     = 9;  // 1 uint8_t degreeIndex + 2 floats
    static constexpr int MORFCHORD_RELEASE_PAYLOAD_SIZE    = 0;
    static constexpr int MORFCHORD_STRUM_PAYLOAD_SIZE      = 8;  // 2 floats
    static constexpr int MORFCHORD_KEY_SELECT_PAYLOAD_SIZE = 2;  // 2 uint8_t
    static constexpr int MORFCHORD_MOVE_PAYLOAD_SIZE       = 9;  // 1 uint8_t degreeIndex + 2 floats

    // Rose-Chord payload sizes
    static constexpr int ROSECHORD_NOTEON_PAYLOAD_SIZE  = 5;  // root, typeBits, modBits, voicing(i8), octave(i8)
    static constexpr int ROSECHORD_NOTEOFF_PAYLOAD_SIZE = 0;
    static constexpr int ROSECHORD_CONFIG_PAYLOAD_SIZE  = 4;  // typeBits, modBits, voicing(i8), octave(i8)

    // Creative Sequencer Pack payload sizes
    static constexpr int PACK_ENTITLEMENT_PAYLOAD_SIZE = 2;  // packId, valid
    static constexpr int SEQ_STEP_SET_PAYLOAD_SIZE     = 6;  // step, degree, velocity, gate, mod, flags
    static constexpr int SEQ_CONFIG_PAYLOAD_SIZE       = 8;  // length, rate, direction, swing, scale, root, baseOctave, octaveRange
    static constexpr int SEQ_TRANSPORT_PAYLOAD_SIZE    = 1;  // command
    static constexpr int SEQ_RATCHET_PAYLOAD_SIZE      = 2;  // division, active
    static constexpr int SEQ_PLAYHEAD_PAYLOAD_SIZE     = 1;  // currentStep

    // Slice sequencer payload sizes
    static constexpr int SEQ_SLICE_CONFIG_PAYLOAD_SIZE  = 6;  // bars, division, sliceCount, rootNote, direction, swing
    static constexpr int SEQ_LOOPER_PAYLOAD_SIZE        = 3;  // division, active, randomOn
    static constexpr int SEQ_RIBBON_PAYLOAD_SIZE        = 3;  // ribbonIndex, value hi, value lo
    static constexpr int SEQ_RIBBON_ASSIGN_PAYLOAD_SIZE = 3;  // ribbonIndex, target, ccNumber

    // SeqStepSet flag bits
    static constexpr uint8_t SEQ_FLAG_REST = 1 << 0;

    // Sentinel for "sequencer is not running" in SeqPlayhead
    static constexpr uint8_t SEQ_STEP_NONE = 0xFF;

    // SeqTransport commands
    enum class SeqCommand : uint8_t { Panic = 0, Run = 1, Reset = 2 };

    // Momentary note-repeat divisions, matching the Beatstep Pro roller
    enum class SeqRatchetDiv : uint8_t { Quarter = 0, Eighth = 1, Sixteenth = 2, ThirtySecond = 3 };

    // SeqRibbonAssign targets. StepRepeat retriggers every step N times
    // within its own step length — a per-step ratchet performance control.
    enum class RibbonTarget : uint8_t { PitchBend = 0, CC = 1, StepRepeat = 2 };

    // Pack identifiers for PackEntitlement
    enum class PackId : uint8_t { CreativeSequencer = 1 };

    // Max packet size we'll ever receive
    static constexpr int MAX_PACKET_SIZE = HEADER_SIZE + DUAL_MPAD_TOUCH_PAYLOAD_SIZE;

    inline float readBigEndianFloat (const uint8_t* data)
    {
        uint32_t bits = (uint32_t (data[0]) << 24)
                      | (uint32_t (data[1]) << 16)
                      | (uint32_t (data[2]) << 8)
                      |  uint32_t (data[3]);
        float result;
        std::memcpy (&result, &bits, sizeof (float));
        return result;
    }

    inline void writeBigEndianFloat (uint8_t* dest, float value)
    {
        uint32_t bits;
        std::memcpy (&bits, &value, sizeof (float));
        dest[0] = uint8_t (bits >> 24);
        dest[1] = uint8_t (bits >> 16);
        dest[2] = uint8_t (bits >> 8);
        dest[3] = uint8_t (bits);
    }
}
