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
        RoseChordNoteOn  = 0x60,  // u8 rootIdx, u8 typeBits, u8 modBits, i8 voicingOffset
        RoseChordNoteOff = 0x61,  // no payload
        RoseChordConfig  = 0x62,  // u8 typeBits, u8 modBits, i8 voicingOffset (live update while holding)
    };

    // Messages sent from plugin/server -> phone
    enum class PluginMessage : uint8_t
    {
        SlotConfig = 0x10,  // uint8_t slotIndex + null-terminated name string
        Pong       = 0x15,
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
    };

    // Packet format: [1 byte type][payload bytes]
    // All floats are 4 bytes, big-endian IEEE 754

    static constexpr int NUM_SLOTS = 4;
    static constexpr int NUM_CONTROLLERS = 7;
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
    static constexpr int ROSECHORD_NOTEON_PAYLOAD_SIZE  = 4;  // root, typeBits, modBits, voicing(i8)
    static constexpr int ROSECHORD_NOTEOFF_PAYLOAD_SIZE = 0;
    static constexpr int ROSECHORD_CONFIG_PAYLOAD_SIZE  = 3;  // typeBits, modBits, voicing(i8)

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
