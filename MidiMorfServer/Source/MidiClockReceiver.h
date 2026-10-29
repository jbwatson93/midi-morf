#pragma once

#include <JuceHeader.h>

// Receives MIDI clock from the DAW on a virtual destination port.
//
// The DAW sends 24 PPQN clock (0xF8) plus start/continue/stop and song position.
// This class does no musical interpretation — it just counts and dispatches.
// Sequencer engines subscribe as Listeners and do the rest.
//
// MEASURED 2026-08-24 against Ableton Live (see spikes/midiclock-spike.cpp):
//   • 0.006 ms rms stamp regularity, ~0.08 ms actual delivery spread
//   • -0.001 ms drift over 289 steps (18 bars)
//   • Live stamps with precise host times but delivers AT time, no forward lead
//
// Because there is no forward lead to ride, engines fire notes immediately from
// clockTick(). That measured ~0.08 ms, roughly 4 samples at 48 kHz.
//
// NOTE ON LOOKAHEAD: scheduling notes for a future host time would in principle
// be tighter, but JUCE's MidiOutput can only schedule via sendBlockOfMessages(),
// which dispatches from a background thread at millisecond resolution — that
// would ADD about 1 ms of jitter, an order of magnitude worse than firing now.
// Real lookahead requires bypassing juce::MidiOutput for CoreMIDI packet
// timestamps. Not worth it unless measurement ever shows a problem.
class MidiClockReceiver : private juce::MidiInputCallback
{
public:
    static constexpr int PPQN = 24;   // MIDI clock ticks per quarter note

    struct Listener
    {
        virtual ~Listener() = default;

        virtual void clockStart() {}
        virtual void clockContinue() {}
        virtual void clockStop() {}
        virtual void clockTick() {}                       // one 0xF8
        virtual void clockSongPosition (int /*sixteenths*/) {}
    };

    MidiClockReceiver() = default;
    ~MidiClockReceiver() override { close(); }

    bool open (const juce::String& portName = "MidiMorf Sync")
    {
        if (input != nullptr)
            return true;

        input = juce::MidiInput::createNewDevice (portName, this);

        if (input == nullptr)
        {
            DBG ("MidiClockReceiver: failed to create virtual MIDI input '" << portName << "'");
            return false;
        }

        input->start();
        DBG ("MidiClockReceiver: listening on '" << portName << "'");
        return true;
    }

    void close()
    {
        if (input != nullptr)
        {
            input->stop();
            input.reset();
            DBG ("MidiClockReceiver: closed");
        }

        running.store (false);
        bpm.store (0.0);
    }

    bool   isOpen()    const { return input != nullptr; }
    bool   isRunning() const { return running.load(); }
    double getBpm()    const { return bpm.load(); }

    // Add before start(). Not safe to call concurrently with incoming clock.
    void addListener    (Listener* l) { listeners.add (l); }
    void removeListener (Listener* l) { listeners.remove (l); }

private:
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m) override
    {
        if (m.isMidiClock())
        {
            updateTempo();
            listeners.call ([] (Listener& l) { l.clockTick(); });
        }
        else if (m.isMidiStart())
        {
            resetTempo();
            running.store (true);
            listeners.call ([] (Listener& l) { l.clockStart(); });
        }
        else if (m.isMidiContinue())
        {
            resetTempo();
            running.store (true);
            listeners.call ([] (Listener& l) { l.clockContinue(); });
        }
        else if (m.isMidiStop())
        {
            running.store (false);
            listeners.call ([] (Listener& l) { l.clockStop(); });
        }
        else if (m.isSongPositionPointer())
        {
            // SPP counts sixteenth notes from the start of the song.
            const int sixteenths = m.getSongPositionPointerMidiBeat();
            listeners.call ([sixteenths] (Listener& l) { l.clockSongPosition (sixteenths); });
        }
    }

    void handlePartialSysexMessage (juce::MidiInput*, const juce::uint8*, int, double) override {}

    // Tempo is display-only — step timing is derived from tick counts, never
    // from wall-clock, so this needs no better than "good enough for a readout".
    void updateTempo()
    {
        const double now = juce::Time::getMillisecondCounterHiRes();

        if (lastTickMs > 0.0)
        {
            const double interval = now - lastTickMs;

            if (interval > 0.1 && interval < 500.0)   // ignore scrubs and pauses
            {
                const double instant = 60000.0 / (interval * PPQN);
                const double current = bpm.load();
                bpm.store (current <= 0.0 ? instant : current * 0.9 + instant * 0.1);
            }
        }

        lastTickMs = now;
    }

    void resetTempo()
    {
        lastTickMs = 0.0;
        bpm.store (0.0);
    }

    std::unique_ptr<juce::MidiInput> input;
    juce::ListenerList<Listener> listeners;

    std::atomic<bool>   running { false };
    std::atomic<double> bpm { 0.0 };
    double lastTickMs = 0.0;   // MIDI thread only

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiClockReceiver)
};
