#pragma once

#include <JuceHeader.h>
#include "TrackRack.h"
#include <array>
#include <map>
#include <vector>

// iOS: turns on the phone's network MIDI session (RTP-MIDI), so a Mac sees
// it in Audio MIDI Setup → Network and the session shows up here as a
// destination. Defined in MainComponent.mm; does nothing elsewhere.
void enableNetworkMidiSession();
bool isNetworkMidiSessionEnabled();

//==============================================================================
// MIDI out, per track. Each track can send what it plays to one MIDI
// destination (a USB interface, a Bluetooth device, the network session,
// another app) on its own channel, alongside or instead of its sound.
//
// Every note reaches a track through TrackRack, which hands it here (MidiTap).
// The audio thread only stamps each event with when its sample will be heard
// and pushes it into a lock-free FIFO; a realtime sender thread sends it at
// that time. So the hardware lines up with the phone's speaker, give or take
// a millisecond, and the audio thread never touches CoreMIDI.
//
// The sender remembers where every note-on went. A note-off follows its note
// there, even if the track has been pointed somewhere else since; changing a
// track's destination or channel, or closing a device, lets go of exactly the
// notes it was holding. Two inputs holding the same note on one track are
// counted, so the first to let go doesn't cut the other.
//
// MIDI clock (24 PPQN, the sequencers' own tick) and Start/Stop go to every
// destination with clock switched on, whether or not a track sends to it.
//
// Threading: setters and getters are message thread. MidiTap calls are the
// audio thread, except controllerFromMessageThread.
class MidiOutRouter : public MidiTap,
                      private juce::Thread
{
public:
    static constexpr int NumTracks = TrackRack::NumTracks;

    MidiOutRouter() : juce::Thread ("MidiMorf MIDI out")
    {
        pending.reserve (FifoSize * 2);
        startRealtimeThread (juce::Thread::RealtimeOptions{}.withPeriodMs (1.0));
        deviceList = juce::MidiDeviceListConnection::make ([this] { devicesChanged(); });
    }

    ~MidiOutRouter() override
    {
        stopThread (500);
        const juce::ScopedLock sl (lock);
        for (int t = 0; t < NumTracks; ++t)
            releaseTrackLocked (t);
        releaseClockLocked();
        outputs.clear();
    }

    //==========================================================================
    // Destinations — message thread.

    struct Destination { juce::String id, name; };

    // What can be sent to now, plus the phone's own virtual port when it
    // could be made.
    std::vector<Destination> available() const
    {
        std::vector<Destination> list;
        for (const auto& d : juce::MidiOutput::getAvailableDevices())
            list.push_back ({ d.identifier, d.name });
        if (virtualOut != nullptr)
            list.push_back ({ VirtualId, "MidiMorf (other apps)" });
        return list;
    }

    bool isAvailable (const juce::String& id) const
    {
        if (id == VirtualId)
            return virtualOut != nullptr;
        for (const auto& d : juce::MidiOutput::getAvailableDevices())
            if (d.identifier == id)
                return true;
        return false;
    }

    // Other apps (AUM and the like) read the phone's own port. iOS only allows
    // it with background audio; without, there is simply no such port.
    bool tryCreateVirtualPort()
    {
        if (virtualOut == nullptr && ! triedVirtual)
        {
            triedVirtual = true;
            virtualOut = juce::MidiOutput::createNewDevice ("MidiMorf");
            const juce::ScopedLock sl (lock);
            reopenLocked();   // tracks and clock already pointed at it connect now
        }
        return virtualOut != nullptr;
    }

    //==========================================================================
    // Tracks — message thread.

    // An empty id turns the track's MIDI out off. The name is kept so a
    // project opened without the device can still say what it wants.
    void setTrackDestination (int track, const juce::String& id, const juce::String& name)
    {
        if (! validTrack (track))
            return;
        const juce::ScopedLock sl (lock);
        auto& r = routes[(size_t) track];
        if (r.id == id)
            return;
        releaseTrackLocked (track);
        r.id = id;
        r.name = id.isEmpty() ? juce::String() : name;
        reopenLocked();
    }

    juce::String getTrackDestination (int track) const     { const juce::ScopedLock sl (lock); return validTrack (track) ? routes[(size_t) track].id : juce::String(); }
    juce::String getTrackDestinationName (int track) const { const juce::ScopedLock sl (lock); return validTrack (track) ? routes[(size_t) track].name : juce::String(); }

    // 1..16.
    void setTrackChannel (int track, int channel)
    {
        if (! validTrack (track))
            return;
        const juce::ScopedLock sl (lock);
        auto& r = routes[(size_t) track];
        channel = juce::jlimit (1, 16, channel);
        if (r.channel == channel)
            return;
        releaseTrackLocked (track);
        r.channel = channel;
    }

    int getTrackChannel (int track) const { const juce::ScopedLock sl (lock); return validTrack (track) ? routes[(size_t) track].channel : 1; }

    bool isTrackSending (int track) const { return getTrackDestination (track).isNotEmpty(); }

    // Set but not plugged in (or not paired, or the session is off).
    bool isTrackMissing (int track) const
    {
        const juce::ScopedLock sl (lock);
        return validTrack (track) && routes[(size_t) track].id.isNotEmpty() && routes[(size_t) track].out == nullptr;
    }

    // A note went out in the last 150 ms — the track's activity light.
    bool recentlyActive (int track) const
    {
        return validTrack (track) && juce::Time::getMillisecondCounterHiRes() - lastActivity[(size_t) track].load() < 150.0;
    }

    //==========================================================================
    // Clock and timing — message thread.

    void setSendsClock (const juce::String& id, bool on)
    {
        if (id.isEmpty())
            return;
        const juce::ScopedLock sl (lock);
        const bool had = clockIds.contains (id);
        if (had == on)
            return;
        if (on) clockIds.add (id);
        else    clockIds.removeString (id);
        if (! on && clockRunning)
            if (auto* out = findOutputLocked (id))
                out->sendMessageNow (juce::MidiMessage::midiStop());
        reopenLocked();
    }

    bool sendsClock (const juce::String& id) const { const juce::ScopedLock sl (lock); return clockIds.contains (id); }

    // Added to the time every message goes out, -50..+100 ms: positive when
    // the gear sounds early against the phone.
    void setOffsetMs (double ms) { offsetMs.store (juce::jlimit (-50.0, 100.0, ms)); }
    double getOffsetMs() const   { return offsetMs.load(); }

    // How long after a callback its audio reaches the speaker: the buffer
    // plus the device's output latency. AudioEngine sets it when the device
    // opens.
    void setAudioLatencyMs (double ms) { audioLatencyMs.store (juce::jmax (0.0, ms)); }

    //==========================================================================
    // Saving. The tracks' destinations go in the project; clock and offset
    // describe the gear, so they are app settings.

    void saveTrack (int track, juce::ValueTree& t) const
    {
        const juce::ScopedLock sl (lock);
        const auto& r = routes[(size_t) juce::jlimit (0, NumTracks - 1, track)];
        if (r.id.isEmpty())
            return;
        t.setProperty ("midiOut", r.id, nullptr);
        t.setProperty ("midiOutName", r.name, nullptr);
        t.setProperty ("midiChannel", r.channel, nullptr);
    }

    void loadTrack (int track, const juce::ValueTree& t)
    {
        setTrackChannel (track, t.getProperty ("midiChannel", track + 1));
        setTrackDestination (track, t["midiOut"].toString(), t["midiOutName"].toString());
        if (t["midiOut"].toString() == VirtualId)
            tryCreateVirtualPort();
    }

    void saveSettings (juce::PropertySet& p) const
    {
        const juce::ScopedLock sl (lock);
        p.setValue ("midi_clock_to", clockIds.joinIntoString ("\n"));
        p.setValue ("midi_offset_ms", offsetMs.load());
    }

    void loadSettings (juce::PropertySet& p)
    {
        {
            const juce::ScopedLock sl (lock);
            clockIds = juce::StringArray::fromLines (p.getValue ("midi_clock_to"));
            clockIds.removeEmptyStrings();
            reopenLocked();
        }
        if (clockIds.contains (VirtualId))
            tryCreateVirtualPort();
        setOffsetMs (p.getDoubleValue ("midi_offset_ms", 0.0));
    }

    //==========================================================================
    // MidiTap — audio thread.

    void beginBlock (double sampleRate) override
    {
        blockStartMs = juce::Time::getMillisecondCounterHiRes();
        msPerSample = 1000.0 / juce::jmax (1.0, sampleRate);
    }

    void setBlockPosition (int samples) override { blockPosition = samples; }

    void note (int track, bool on, int note, int velocity127) override
    {
        if (anyRoutes.load())
            push (audioFifo, audioEvents, { on ? Kind::On : Kind::Off, (int8_t) track, (uint8_t) note, (uint8_t) velocity127, dueTime() });
    }

    void allNotesOff (int track) override
    {
        if (anyRoutes.load())
            push (audioFifo, audioEvents, { Kind::AllOff, (int8_t) track, 0, 0, dueTime() });
    }

    void clock (Clock c) override
    {
        if (anyClock.load())
            push (audioFifo, audioEvents, { c == Clock::Start ? Kind::ClockStart : c == Clock::Tick ? Kind::ClockTick : Kind::ClockStop, -1, 0, 0, dueTime() });
    }

    // Message thread: CC out right away.
    void controllerFromMessageThread (int track, int cc, int value) override
    {
        if (anyRoutes.load())
            push (messageFifo, messageEvents, { Kind::CC, (int8_t) track, (uint8_t) juce::jlimit (0, 127, cc), (uint8_t) juce::jlimit (0, 127, value), 0.0 });
    }

private:
    static constexpr const char* VirtualId = "midimorf-virtual";
    static constexpr int FifoSize = 2048;

    enum class Kind : uint8_t { On, Off, AllOff, CC, ClockStart, ClockTick, ClockStop };

    struct Event
    {
        Kind kind;
        int8_t track;
        uint8_t d1, d2;
        double dueMs;
    };

    struct Route
    {
        juce::String id, name;
        int channel = 1;
        juce::MidiOutput* out = nullptr;   // under lock; null while missing
    };

    // Where a sounding note went, so its note-off follows it.
    struct Sounding
    {
        juce::MidiOutput* out = nullptr;
        int channel = 1, count = 0;
    };

    static bool validTrack (int t) { return t >= 0 && t < NumTracks; }

    // Never earlier than the last: moving TIMING while notes sound must not
    // send a note-off ahead of its note-on.
    double dueTime()
    {
        lastDueMs = juce::jmax (lastDueMs, blockStartMs + blockPosition * msPerSample + audioLatencyMs.load() + offsetMs.load());
        return lastDueMs;
    }

    static void push (juce::AbstractFifo& fifo, std::array<Event, FifoSize>& events, const Event& e)
    {
        const auto scope = fifo.write (1);
        if (scope.blockSize1 > 0)      events[(size_t) scope.startIndex1] = e;
        else if (scope.blockSize2 > 0) events[(size_t) scope.startIndex2] = e;
        // A full FIFO drops the event; the held-note table still lets go of
        // whatever was sent.
    }

    //==========================================================================
    // Sender thread.

    void run() override
    {
        while (! threadShouldExit())
        {
            drain (audioFifo, audioEvents);
            drain (messageFifo, messageEvents);

            if (! pending.empty())
            {
                const double now = juce::Time::getMillisecondCounterHiRes();
                size_t due = 0;
                while (due < pending.size() && pending[due].dueMs <= now)
                    ++due;

                if (due > 0)
                {
                    const juce::ScopedLock sl (lock);
                    for (size_t i = 0; i < due; ++i)
                        sendLocked (pending[i]);
                    pending.erase (pending.begin(), pending.begin() + (std::ptrdiff_t) due);
                }
            }

            // Nothing to send for a while: no need to wake every millisecond.
            wait (anyRoutes.load() || anyClock.load() ? 1 : 50);
        }
    }

    // Into `pending`, kept in time order. Each FIFO is already in order, so
    // this is mostly an append.
    void drain (juce::AbstractFifo& fifo, std::array<Event, FifoSize>& events)
    {
        const auto scope = fifo.read (fifo.getNumReady());
        auto take = [this, &events] (int start, int count)
        {
            for (int i = start; i < start + count; ++i)
            {
                auto e = events[(size_t) i];
                if (e.dueMs <= 0.0)
                    e.dueMs = juce::Time::getMillisecondCounterHiRes();
                auto it = pending.end();
                while (it != pending.begin() && std::prev (it)->dueMs > e.dueMs)
                    --it;
                pending.insert (it, e);
            }
        };
        take (scope.startIndex1, scope.blockSize1);
        take (scope.startIndex2, scope.blockSize2);
    }

    void sendLocked (const Event& e)
    {
        switch (e.kind)
        {
            case Kind::ClockStart:
            case Kind::ClockTick:
            case Kind::ClockStop:
            {
                const auto m = e.kind == Kind::ClockStart ? juce::MidiMessage::midiStart()
                             : e.kind == Kind::ClockTick  ? juce::MidiMessage::midiClock()
                                                          : juce::MidiMessage::midiStop();
                clockRunning = e.kind != Kind::ClockStop;
                for (auto* out : clockOuts)
                    out->sendMessageNow (m);
                return;
            }
            default: break;
        }

        if (! validTrack (e.track))
            return;
        const auto& r = routes[(size_t) e.track];

        switch (e.kind)
        {
            case Kind::On:
            {
                if (r.out == nullptr) return;
                auto& s = sounding[(size_t) e.track][e.d1];
                if (s.count > 0 && (s.out != r.out || s.channel != r.channel))
                    letGoLocked (e.track, e.d1);   // still held somewhere old
                r.out->sendMessageNow (juce::MidiMessage::noteOn (r.channel, e.d1, (juce::uint8) juce::jlimit (1, 127, (int) e.d2)));
                s.out = r.out;
                s.channel = r.channel;
                ++s.count;
                lastActivity[(size_t) e.track].store (juce::Time::getMillisecondCounterHiRes());
                return;
            }
            case Kind::Off:
            {
                auto& s = sounding[(size_t) e.track][e.d1];
                if (s.count > 0 && --s.count == 0)
                    letGoLocked (e.track, e.d1);
                return;
            }
            case Kind::AllOff:
                releaseTrackLocked (e.track);
                return;
            case Kind::CC:
                if (r.out != nullptr)
                    r.out->sendMessageNow (juce::MidiMessage::controllerEvent (r.channel, e.d1, e.d2));
                return;
            default:
                return;
        }
    }

    //==========================================================================
    // Under lock, any thread.

    void letGoLocked (int track, int note)
    {
        auto& s = sounding[(size_t) track][(size_t) note];
        if (s.out != nullptr)
            s.out->sendMessageNow (juce::MidiMessage::noteOff (s.channel, note));
        s = {};
    }

    void releaseTrackLocked (int track)
    {
        for (int n = 0; n < 128; ++n)
            if (sounding[(size_t) track][(size_t) n].out != nullptr)
                letGoLocked (track, n);
    }

    void releaseClockLocked()
    {
        if (clockRunning)
            for (auto* out : clockOuts)
                out->sendMessageNow (juce::MidiMessage::midiStop());
    }

    juce::MidiOutput* findOutputLocked (const juce::String& id) const
    {
        if (id == VirtualId)
            return virtualOut.get();
        const auto it = outputs.find (id);
        return it != outputs.end() ? it->second.get() : nullptr;
    }

    // Opens what the tracks and the clock want and is plugged in; closes the
    // rest (letting go of their notes first).
    void reopenLocked()
    {
        juce::StringArray wanted = clockIds;
        for (const auto& r : routes)
            if (r.id.isNotEmpty())
                wanted.addIfNotAlreadyThere (r.id);

        juce::StringArray present;
        for (const auto& d : juce::MidiOutput::getAvailableDevices())
            present.add (d.identifier);

        for (auto it = outputs.begin(); it != outputs.end();)
        {
            if (wanted.contains (it->first) && present.contains (it->first))
            {
                ++it;
                continue;
            }
            for (int t = 0; t < NumTracks; ++t)
                for (int n = 0; n < 128; ++n)
                    if (sounding[(size_t) t][(size_t) n].out == it->second.get())
                        letGoLocked (t, n);   // too late if it was unplugged, but harmless
            it = outputs.erase (it);
        }

        for (const auto& id : wanted)
            if (id != VirtualId && present.contains (id) && outputs.find (id) == outputs.end())
                if (auto out = juce::MidiOutput::openDevice (id))
                    outputs[id] = std::move (out);

        for (auto& r : routes)
            r.out = r.id.isEmpty() ? nullptr : findOutputLocked (r.id);

        // A track's notes still sounding on a device that's gone point at
        // nothing now.
        for (auto& track : sounding)
            for (auto& s : track)
                if (s.out != nullptr && s.out != virtualOut.get()
                    && std::none_of (outputs.begin(), outputs.end(), [&s] (const auto& o) { return o.second.get() == s.out; }))
                    s = {};

        clockOuts.clear();
        for (const auto& id : clockIds)
            if (auto* out = findOutputLocked (id))
                clockOuts.push_back (out);

        anyRoutes.store (std::any_of (routes.begin(), routes.end(), [] (const Route& r) { return r.id.isNotEmpty(); }));
        anyClock.store (! clockIds.isEmpty());
    }

    // Message thread: something was plugged in or out, or paired.
    void devicesChanged()
    {
        const juce::ScopedLock sl (lock);
        reopenLocked();
    }

    mutable juce::CriticalSection lock;
    std::array<Route, NumTracks> routes;                                  // under lock
    std::array<std::array<Sounding, 128>, NumTracks> sounding {};          // under lock
    std::map<juce::String, std::unique_ptr<juce::MidiOutput>> outputs;     // under lock
    std::unique_ptr<juce::MidiOutput> virtualOut;
    bool triedVirtual = false;
    juce::StringArray clockIds;                                            // under lock
    std::vector<juce::MidiOutput*> clockOuts;                              // under lock
    bool clockRunning = false;                                             // under lock

    std::atomic<bool> anyRoutes { false }, anyClock { false };
    std::atomic<double> offsetMs { 0.0 }, audioLatencyMs { 0.0 };
    std::array<std::atomic<double>, NumTracks> lastActivity {};

    // Audio thread.
    double blockStartMs = 0.0, msPerSample = 1000.0 / 48000.0, lastDueMs = 0.0;
    int blockPosition = 0;

    juce::AbstractFifo audioFifo { FifoSize }, messageFifo { FifoSize };
    std::array<Event, FifoSize> audioEvents {}, messageEvents {};
    std::vector<Event> pending;   // sender thread

    juce::MidiDeviceListConnection deviceList;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiOutRouter)
};
