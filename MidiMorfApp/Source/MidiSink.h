#pragma once

#include "TrackRack.h"
#include <atomic>

// PhoneMidiSink — matches the server's VirtualMidiPort API so the per-
// controller handlers (MorfPadHandler etc.) keep their note-mapping logic
// identical to the server's. Each handler gets its own sink, pointed at the
// track that controller plays; notes go through the rack's message-thread
// queue. Channels are kept, so two controllers on one track can hold the
// same note independently.
class PhoneMidiSink
{
public:
    PhoneMidiSink (TrackRack& r, int initialTrack) : rack (r), track (initialTrack) {}

    // Callers release held notes first (the handler's allNotesOff) so the
    // note-offs still reach the old track.
    void setTrack (int t) { track.store (juce::jlimit (0, TrackRack::NumTracks - 1, t)); }
    int  getTrack() const { return track.load(); }

    void sendNoteOn (int channel, int note, int velocity)
    {
        rack.postNoteOn (track.load(), channel, note, juce::jlimit (1, 127, velocity));
    }

    void sendNoteOff (int channel, int note)
    {
        rack.postNoteOff (track.load(), channel, note);
    }

    // e.g. CC1, the mod wheel. controller() is safe off the audio thread.
    void sendCC (int /*channel*/, int cc, int value)
    {
        if (auto* s = rack.getSource (track.load()))
            s->controller (cc, value);
    }

private:
    TrackRack& rack;
    std::atomic<int> track;
};
