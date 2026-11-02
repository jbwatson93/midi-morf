#pragma once

#include <JuceHeader.h>
#include "TrackRack.h"
#include "BreakLibrary.h"
#include "ClockedSequencer.h"
#include <array>
#include <memory>

//==============================================================================
// The Remix grid: monome mlr on a phone.
//
// Six rows, each its own sample cut into 16 columns, all locked to the
// clock. A row plays its sample straight through, like tape: the sample
// spans its length in beats, so a faster tempo plays it faster and higher,
// and SPEED shifts it by octaves.
//
//   • press a column        — the row plays from there (from the next
//                             quantize point), and on from there
//   • two quantizes         — LAUNCH for a LOOP row starting from stopped,
//                             the transport playing and patterns starting
//                             (a bar, so rows started together stay
//                             together); PRESS for everything else. Off is
//                             no sync
//   • hold one, press another — loops between them, and stays looped until
//                             a single press
//   • groups                — rows in the same group (1–4) take turns:
//                             starting one stops the others. The group's stop
//                             key stops them all
//   • play modes            — per row: LOOP plays on from the press (mlr),
//                             SLICE plays just the pressed slice, GATE plays
//                             while a finger is on the row
//   • pattern recorders     — record presses, then loop them in time
//   • recall                — store every row's state in a slot, bring it
//                             back later
//
// Threading: presses are posted from the message thread into a FIFO and
// played out on the audio thread, which owns all playback state. Settings
// are atomics. Samples swap under `sampleLock`, briefly.
//
// The clock comes from LocalSequencers, once per audio sub-block
// (setClock): where it is in 24 PPQN ticks and how many samples a tick is.
class MlrSource : public SoundSource
{
public:
    static constexpr int NumRows = 6, Cols = 16, NumGroups = 4, NumPatterns = 2, NumRecalls = 4;
    static constexpr int TicksPerBeat = SeqPPQN;

    enum Quantize { QOff = 0, Q16, Q8, Q4, QBar, NumQuantize };
    static const juce::StringArray& quantizeNames()
    {
        static const juce::StringArray n { "Off", "1/16", "1/8", "1/4", "Bar" };
        return n;
    }

    // What a press does: play on from there, play just that slice, or play
    // while held (release()).
    enum PlayMode { ModeLoop = 0, ModeSlice, ModeGate, NumModes };
    static const juce::StringArray& playModeNames()
    {
        static const juce::StringArray n { "Loop", "Slice", "Gate" };
        return n;
    }

    // Speed in octaves: -2 (quarter speed) .. +2 (four times).
    static constexpr int MinSpeed = -2, MaxSpeed = 2;

    enum PatternState { PatEmpty = 0, PatArmed, PatRecording, PatPlaying, PatStopped };

    static inline const juce::Identifier stateType { "Mlr" };

    MlrSource()
    {
        for (int r = 0; r < NumRows; ++r)
            rowSettings[(size_t) r].group.store (0);
    }

    juce::String typeId() const override { return "mlr"; }

    void prepare (double sr, int) override
    {
        sampleRate = sr;
        fadeStep    = (float) (1.0 / (0.003 * sr));   // a jump crossfades over 3 ms
        gainStep    = (float) (1.0 / (0.004 * sr));   // starts and stops
    }

    //==========================================================================
    // The clock — audio thread, before each render(). tick < 0: stopped.
    void setClock (double tick, double samplesPerTick)
    {
        clockTick = tick;
        spt = juce::jmax (1.0, samplesPerTick);
    }

    //==========================================================================
    // Samples — message thread. Each row starts empty.
    struct Origin
    {
        enum Kind { None, BuiltIn, File };
        Kind kind = None;
        juce::String name;
        juce::File file;
    };

    bool setBuiltIn (int row, int index)
    {
        auto b = BreakLibrary::generate (index);
        if (b == nullptr) return false;
        setSample (row, std::move (b), { Origin::BuiltIn, BreakLibrary::builtInNames()[index], {} });
        return true;
    }

    bool importFile (int row, const juce::File& file, juce::String& error)
    {
        auto b = BreakLibrary::importFile (file, error);
        if (b == nullptr) return false;
        setSample (row, std::move (b), { Origin::File, file.getFileNameWithoutExtension(), file });
        return true;
    }

    void clearSample (int row) { setSample (row, nullptr, {}); }

    const Origin& getOrigin (int row) const { return origins[(size_t) clampRow (row)]; }
    bool hasSample (int row) const          { return origins[(size_t) clampRow (row)].kind != Origin::None; }

    // A row's waveform for the page: peak per column of `width`.
    std::vector<float> getPeaks (int row, int width) const
    {
        std::vector<float> peaks ((size_t) juce::jmax (1, width), 0.0f);
        const juce::SpinLock::ScopedLockType sl (sampleLock);
        const auto* b = samples[(size_t) clampRow (row)].get();
        if (b == nullptr) return peaks;
        const int n = b->audio.getNumSamples();
        for (int x = 0; x < width; ++x)
        {
            const int s0 = (int) ((int64_t) n * x / width), s1 = juce::jmax (s0 + 1, (int) ((int64_t) n * (x + 1) / width));
            float m = 0.0f;
            for (int ch = 0; ch < b->audio.getNumChannels(); ++ch)
                m = juce::jmax (m, b->audio.getMagnitude (ch, s0, juce::jmin (s1, n) - s0));
            peaks[(size_t) x] = m;
        }
        return peaks;
    }

    //==========================================================================
    // Row settings — any thread.
    void  setLengthBeats (int row, double beats) { rowSettings[(size_t) clampRow (row)].beats.store (juce::jlimit (0.25, 128.0, beats)); }
    double getLengthBeats (int row) const        { return rowSettings[(size_t) clampRow (row)].beats.load(); }
    void  setSpeed (int row, int octaves)        { rowSettings[(size_t) clampRow (row)].speed.store (juce::jlimit (MinSpeed, MaxSpeed, octaves)); }
    int   getSpeed (int row) const               { return rowSettings[(size_t) clampRow (row)].speed.load(); }
    void  setReverse (int row, bool r)           { rowSettings[(size_t) clampRow (row)].reverse.store (r); }
    bool  getReverse (int row) const             { return rowSettings[(size_t) clampRow (row)].reverse.load(); }
    void  setVolume (int row, float v)           { rowSettings[(size_t) clampRow (row)].volume.store (juce::jlimit (0.0f, 1.5f, v)); }
    float getVolume (int row) const              { return rowSettings[(size_t) clampRow (row)].volume.load(); }
    void  setGroup (int row, int g)              { rowSettings[(size_t) clampRow (row)].group.store (juce::jlimit (0, NumGroups, g)); }
    int   getGroup (int row) const               { return rowSettings[(size_t) clampRow (row)].group.load(); }
    void  setPlayMode (int row, int m)           { rowSettings[(size_t) clampRow (row)].mode.store (juce::jlimit (0, NumModes - 1, m)); }
    int   getPlayMode (int row) const            { return rowSettings[(size_t) clampRow (row)].mode.load(); }

    void setQuantize (int q) { quantize.store (juce::jlimit (0, NumQuantize - 1, q)); }
    int  getQuantize() const { return quantize.load(); }
    void setLaunchQuantize (int q) { launchQuantize.store (juce::jlimit (0, NumQuantize - 1, q)); }
    int  getLaunchQuantize() const { return launchQuantize.load(); }

    //==========================================================================
    // Presses — message thread. They play out from the next quantize point.
    void press (int row, int col)            { post ({ Cmd::Jump, row, col, col }); }
    void loop (int row, int a, int b)        { post ({ Cmd::Loop, row, juce::jmin (a, b), juce::jmax (a, b) }); }
    void stopRow (int row)                   { post ({ Cmd::Stop, row, 0, 0 }); }
    // The last finger left the row: a GATE row stops (now, not quantized).
    // A press still waiting for its quantize point plays one slice instead.
    void release (int row)                   { post ({ Cmd::Release, row, 0, 0 }); }
    void stopGroup (int group)               { post ({ Cmd::StopGroup, group, 0, 0 }); }
    void stopAll()                           { post ({ Cmd::StopAll, 0, 0, 0 }); }

    // A pattern recorder's key: empty → armed (it records from the next
    // press) → recording → playing ↔ stopped. clearPattern empties it.
    void patternKey (int p)                  { post ({ Cmd::PatternKey, p, 0, 0 }); }
    void clearPattern (int p)                { post ({ Cmd::PatternClear, p, 0, 0 }); }
    PatternState getPatternState (int p) const { return (PatternState) patternStates[(size_t) juce::jlimit (0, NumPatterns - 1, p)].load(); }

    // Recall: an empty slot stores, a stored one recalls.
    void recallKey (int slot)                { post ({ Cmd::RecallKey, slot, 0, 0 }); }
    void clearRecall (int slot)              { post ({ Cmd::RecallClear, slot, 0, 0 }); }
    bool hasRecall (int slot) const          { return recallStored[(size_t) juce::jlimit (0, NumRecalls - 1, slot)].load(); }

    //==========================================================================
    // What the page shows — any thread.
    struct RowView { bool playing = false, looped = false; float position = -1.0f; int loopA = 0, loopB = Cols - 1; };
    RowView getRowView (int row) const
    {
        const auto& v = views[(size_t) clampRow (row)];
        return { v.playing.load(), v.looped.load(), v.position.load(), v.loopA.load(), v.loopB.load() };
    }
    bool isGroupPlaying (int g) const
    {
        for (int r = 0; r < NumRows; ++r)
            if (getGroup (r) == g && views[(size_t) r].playing.load()) return true;
        return false;
    }
    bool isAnyRowPlaying() const
    {
        for (auto& v : views) if (v.playing.load()) return true;
        return false;
    }

    //==========================================================================
    // SoundSource. The grid plays from presses, not notes.
    void noteOnAudio (int, int, int) override {}
    void noteOffAudio (int, int) override {}
    void allNotesOff() override { post ({ Cmd::Panic, 0, 0, 0 }); }

    void render (juce::AudioBuffer<float>& buffer, int start, int num) override
    {
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            buffer.clear (ch, start, num);

        drainCommands();

        // The transport stopped or started.
        const bool running = clockTick >= 0.0;
        if (running != wasRunning)
        {
            wasRunning = running;
            if (running) resumeRows();
            else         pauseRows();
        }

        const juce::SpinLock::ScopedTryLockType sl (sampleLock);
        if (! sl.isLocked())
            return;   // a sample is being swapped: one silent block

        auto* L = buffer.getWritePointer (0, start);
        auto* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1, start) : nullptr;

        for (int i = 0; i < num; ++i)
        {
            if (running)
            {
                lastTick = clockTick + i / spt;
                runDue (lastTick);
            }

            for (int r = 0; r < NumRows; ++r)
                renderRow (r, L[i], R != nullptr ? R[i] : L[i]);
        }

        if (R == nullptr)
            for (int i = 0; i < num; ++i) L[i] *= 0.5f;   // both sides were summed into one

        publishViews();
    }

    //==========================================================================
    juce::ValueTree getState() const override
    {
        juce::ValueTree t (stateType);
        t.setProperty ("quantize", quantize.load(), nullptr);
        t.setProperty ("launch_q", launchQuantize.load(), nullptr);
        for (int r = 0; r < NumRows; ++r)
        {
            juce::ValueTree row ("Row");
            const auto& o = origins[(size_t) r];
            row.setProperty ("break", o.kind == Origin::BuiltIn ? o.name : juce::String(), nullptr);
            row.setProperty ("file", o.kind == Origin::File ? o.file.getFullPathName() : juce::String(), nullptr);
            row.setProperty ("beats", getLengthBeats (r), nullptr);
            row.setProperty ("speed", getSpeed (r), nullptr);
            row.setProperty ("reverse", getReverse (r), nullptr);
            row.setProperty ("volume", getVolume (r), nullptr);
            row.setProperty ("group", getGroup (r), nullptr);
            row.setProperty ("mode", getPlayMode (r), nullptr);
            t.appendChild (row, nullptr);
        }
        return t;
    }

    void setState (const juce::ValueTree& t) override
    {
        if (! t.hasType (stateType)) return;
        setQuantize (t.getProperty ("quantize", (int) Q16));
        setLaunchQuantize (t.getProperty ("launch_q", (int) QBar));
        for (int r = 0; r < NumRows; ++r)
        {
            const auto row = t.getChild (r);
            if (! row.isValid()) continue;
            juce::String error;
            const auto file = juce::File (row["file"].toString());
            const int index = BreakLibrary::indexForName (row["break"].toString());
            if (row["file"].toString().isNotEmpty() && file.existsAsFile()) importFile (r, file, error);
            else if (index >= 0)                                             setBuiltIn (r, index);
            else                                                             clearSample (r);
            setLengthBeats (r, row.getProperty ("beats", getLengthBeats (r)));
            setSpeed (r, row.getProperty ("speed", 0));
            setReverse (r, row.getProperty ("reverse", false));
            setVolume (r, (float) (double) row.getProperty ("volume", 1.0));
            setGroup (r, row.getProperty ("group", 0));
            setPlayMode (r, row.getProperty ("mode", (int) ModeLoop));
        }
    }

private:
    static int clampRow (int r) { return juce::jlimit (0, NumRows - 1, r); }

    //==========================================================================
    // Commands, message → audio.
    struct Cmd
    {
        enum Type { Jump, Loop, Stop, Release, StopGroup, StopAll, PatternKey, PatternClear, RecallKey, RecallClear, Panic };
        Type type; int a, b, c;
    };

    void post (const Cmd& c)
    {
        int s1, n1, s2, n2;
        fifo.prepareToWrite (1, s1, n1, s2, n2);
        if (n1 > 0) cmds[(size_t) s1] = c;
        else if (n2 > 0) cmds[(size_t) s2] = c;
        fifo.finishedWrite (n1 + n2);
    }

    // A row action, as pressed, recorded and recalled. Slice plays one
    // column and stops.
    struct Action
    {
        enum Type { None, Jump, Slice, Loop, Stop, StopGroup };
        Type type = None; int row = 0, a = 0, b = 0;
    };

    // Ticks between quantize points: a press's, a launch's.
    double quantTicks() const  { return ticksFor (quantize.load()); }
    double launchTicks() const { return ticksFor (launchQuantize.load()); }

    static double ticksFor (int q)
    {
        switch (q)
        {
            case Q16:  return TicksPerBeat / 4;
            case Q8:   return TicksPerBeat / 2;
            case Q4:   return TicksPerBeat;
            case QBar: return TicksPerBeat * 4;
            default:   return 0.0;
        }
    }

    // The next quantize point from `now` (now itself if it's on one, or Off).
    static double nextTick (double now, double q)
    {
        now = juce::jmax (0.0, now);
        return q > 0.0 ? std::ceil (now / q - 1.0e-6) * q : now;
    }
    double nextQuantTick() const  { return nextTick (clockTick, quantTicks()); }
    double nextLaunchTick() const { return nextTick (clockTick, launchTicks()); }

    // A LOOP row starting from stopped launches; anything else is a press.
    // SLICE and GATE rows are played like pads, so they never wait a bar.
    bool launches (int r) const { return getPlayMode (r) == ModeLoop && ! rows[(size_t) r].playing; }

    void drainCommands()
    {
        int s1, n1, s2, n2;
        fifo.prepareToRead (fifo.getNumReady(), s1, n1, s2, n2);
        for (int i = 0; i < n1; ++i) handle (cmds[(size_t) (s1 + i)]);
        for (int i = 0; i < n2; ++i) handle (cmds[(size_t) (s2 + i)]);
        fifo.finishedRead (n1 + n2);
    }

    void handle (const Cmd& c)
    {
        const double due = nextQuantTick();
        // A row's start: a launch or a press.
        auto start = [this] (const Action& a)
        {
            if (launches (a.row)) schedule (a, nextLaunchTick(), launchTicks());
            else                  schedule (a, nextQuantTick(), quantTicks());
        };
        switch (c.type)
        {
            case Cmd::Jump:
                start ({ getPlayMode (clampRow (c.a)) == ModeSlice ? Action::Slice : Action::Jump, clampRow (c.a), juce::jlimit (0, Cols - 1, c.b), 0 });
                break;
            case Cmd::Loop:      start ({ Action::Loop, clampRow (c.a), juce::jlimit (0, Cols - 1, c.b), juce::jlimit (0, Cols - 1, c.c) }); break;
            case Cmd::Stop:      schedule ({ Action::Stop, clampRow (c.a), 0, 0 }, due); break;
            case Cmd::Release:   releaseRow (clampRow (c.a)); break;
            case Cmd::StopGroup: schedule ({ Action::StopGroup, 0, juce::jlimit (1, NumGroups, c.a), 0 }, due); break;
            case Cmd::StopAll:
                for (int r = 0; r < NumRows; ++r) schedule ({ Action::Stop, r, 0, 0 }, due);
                break;
            case Cmd::PatternKey:   patternKeyAudio (juce::jlimit (0, NumPatterns - 1, c.a)); break;
            case Cmd::PatternClear: setPatternState (juce::jlimit (0, NumPatterns - 1, c.a), PatEmpty); patterns[(size_t) juce::jlimit (0, NumPatterns - 1, c.a)].count = 0; break;
            case Cmd::RecallKey:    recallKeyAudio (juce::jlimit (0, NumRecalls - 1, c.a), due); break;
            case Cmd::RecallClear:  recallStored[(size_t) juce::jlimit (0, NumRecalls - 1, c.a)].store (false); break;
            case Cmd::Panic:
                for (auto& p : pendings) p.active = false;
                for (int r = 0; r < NumRows; ++r) stopHead (r);
                for (int p = 0; p < NumPatterns; ++p) if (getPatternState (p) == PatPlaying) setPatternState (p, PatStopped);
                break;
        }
    }

    // A press from the user: it waits for its quantize point, every `q`
    // ticks (a stop of a whole group waits on the group's first row's slot).
    void schedule (const Action& a, double due, double q, bool user = true)
    {
        if (clockTick < 0.0) due = -1.0;   // the first quantize point once the clock runs
        const int slot = a.type == Action::StopGroup ? NumRows + a.a - 1 : a.row;
        pendings[(size_t) slot] = { a, due, q, true, user };
    }
    void schedule (const Action& a, double due) { schedule (a, due, quantTicks()); }

    // Audio thread: everything due by `tick`.
    void runDue (double tick)
    {
        const double eps = 0.5 / spt;   // within half a sample
        for (auto& p : pendings)
        {
            if (p.active && p.due < 0.0)
                p.due = nextTick (tick, p.q);
            if (p.active && p.due <= tick + eps)
            {
                p.active = false;
                execute (p.action, tick, p.user);
            }
        }

        for (int i = 0; i < NumPatterns; ++i)
            playPattern (i, tick + eps);
    }

    void execute (const Action& a, double tick, bool fromUser)
    {
        if (fromUser)
            for (int i = 0; i < NumPatterns; ++i)
                recordInto (i, a, tick);

        switch (a.type)
        {
            case Action::Jump:
                takeTurn (a.row);
                rows[(size_t) a.row].looped = false;
                startHead (a.row, a.a);
                break;
            case Action::Slice:
            {
                takeTurn (a.row);
                auto& row = rows[(size_t) a.row];
                row.looped = false;
                startHead (a.row, a.a);
                row.oneShot = true;
                row.oneShotEnd = getReverse (a.row) ? (double) a.a : a.a + 1.0;
                break;
            }
            case Action::Loop:
                takeTurn (a.row);
                rows[(size_t) a.row].looped = true;
                rows[(size_t) a.row].loopA = a.a;
                rows[(size_t) a.row].loopB = a.b;
                startHead (a.row, getReverse (a.row) ? a.b : a.a);
                break;
            case Action::Stop:
                stopHead (a.row);
                break;
            case Action::StopGroup:
                for (int r = 0; r < NumRows; ++r)
                    if (getGroup (r) == a.a) stopHead (r);
                break;
            default: break;
        }
    }

    // A GATE row's fingers are off: it stops now. A press still waiting for
    // its quantize point becomes one slice, so a quick tap is never lost.
    void releaseRow (int r)
    {
        if (getPlayMode (r) != ModeGate)
            return;
        auto& p = pendings[(size_t) r];
        if (p.active && (p.action.type == Action::Jump || p.action.type == Action::Loop))
        {
            p.action.type = Action::Slice;   // from the press (a loop's first column)
            return;
        }
        if (rows[(size_t) r].playing)
            execute ({ Action::Stop, r, 0, 0 }, juce::jmax (0.0, clockTick), true);
    }

    // Starting a row in a group stops the group's other rows.
    void takeTurn (int row)
    {
        const int g = getGroup (row);
        if (g == 0) return;
        for (int r = 0; r < NumRows; ++r)
            if (r != row && getGroup (r) == g) stopHead (r);
    }

    //==========================================================================
    // Playback. Positions are in columns, 0..Cols.
    struct Row
    {
        bool playing = false, resume = false, looped = false;
        bool oneShot = false; double oneShotEnd = 0.0;   // a slice: stops there
        int loopA = 0, loopB = Cols - 1;
        double pos = 0.0;
        float gain = 0.0f, target = 0.0f;   // start/stop ramp
        double oldPos = 0.0; float oldGain = 0.0f;   // a jump's old head going out
    };

    double columnsPerSample (int r) const
    {
        const double beats = getLengthBeats (r);
        return Cols / (beats * TicksPerBeat) / spt * std::pow (2.0, getSpeed (r)) * (getReverse (r) ? -1.0 : 1.0);
    }

    std::pair<double, double> range (int r) const
    {
        const auto& row = rows[(size_t) r];
        return row.looped ? std::pair<double, double> { (double) row.loopA, (double) row.loopB + 1.0 } : std::pair<double, double> { 0.0, (double) Cols };
    }

    void startHead (int r, int col)
    {
        auto& row = rows[(size_t) r];
        const double newPos = getReverse (r) ? col + 1.0 - 1.0e-6 : (double) col;
        if (row.playing && row.gain > 0.0f)
        {
            row.oldPos = row.pos;          // crossfade from where it was
            row.oldGain = 1.0f;
        }
        else
        {
            row.gain = 0.0f;               // a fresh start ramps in
            row.oldGain = 0.0f;
        }
        row.pos = newPos;
        row.playing = true;
        row.oneShot = false;
        row.target = 1.0f;
    }

    void stopHead (int r)
    {
        auto& row = rows[(size_t) r];
        row.playing = false;
        row.resume = false;
        row.oneShot = false;
        row.target = 0.0f;
    }

    // The transport: rows pause where they are and come back from their
    // start (or their loop's) when it plays again, on the next launch point,
    // so a grid started under a running beat lands on its bar.
    void pauseRows()
    {
        for (int r = 0; r < NumRows; ++r)
        {
            auto& row = rows[(size_t) r];
            const bool was = row.playing && ! row.oneShot;   // a slice doesn't come back
            stopHead (r);
            row.resume = was;
        }
        for (auto& p : pendings) p.active = false;
        for (int p = 0; p < NumPatterns; ++p)
        {
            if (getPatternState (p) == PatPlaying) setPatternState (p, PatStopped);
            if (getPatternState (p) == PatRecording) finishRecording (p, lastTick);
        }
    }

    void resumeRows()
    {
        for (int r = 0; r < NumRows; ++r)
            if (std::exchange (rows[(size_t) r].resume, false))
            {
                const auto& row = rows[(size_t) r];
                const Action a = row.looped ? Action { Action::Loop, r, row.loopA, row.loopB } : Action { Action::Jump, r, 0, 0 };
                schedule (a, nextLaunchTick(), launchTicks(), false);
            }
    }

    // Cubic (Hermite) read at a fractional position in the sample.
    static float readAt (const float* d, int n, double pos)
    {
        const int i = (int) std::floor (pos);
        const float f = (float) (pos - i);
        auto at = [d, n] (int k) { k %= n; if (k < 0) k += n; return d[k]; };
        const float xm1 = at (i - 1), x0 = at (i), x1 = at (i + 1), x2 = at (i + 2);
        const float c1 = 0.5f * (x1 - xm1), c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2, c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + x0;
    }

    void renderRow (int r, float& outL, float& outR)
    {
        auto& row = rows[(size_t) r];
        const auto* b = samples[(size_t) r].get();
        if (row.gain <= 0.0f && row.target <= 0.0f)
            return;
        if (b == nullptr || b->audio.getNumSamples() < 4)
        {
            row.gain = 0.0f;
            return;
        }

        const int n = b->audio.getNumSamples();
        const float* sl = b->audio.getReadPointer (0);
        const float* sr = b->audio.getReadPointer (b->audio.getNumChannels() > 1 ? 1 : 0);
        const double toSample = n / (double) Cols;
        const float vol = getVolume (r);

        // Start/stop ramp.
        if (row.gain < row.target)      row.gain = juce::jmin (row.target, row.gain + gainStep);
        else if (row.gain > row.target) row.gain = juce::jmax (row.target, row.gain - gainStep);

        // A jump: the old head fades out as the new one fades in.
        const double step = columnsPerSample (r);
        const float in = 1.0f - row.oldGain;
        float l  = readAt (sl, n, row.pos * toSample) * in;
        float rr = readAt (sr, n, row.pos * toSample) * in;
        if (row.oldGain > 0.0f)
        {
            l  += readAt (sl, n, row.oldPos * toSample) * row.oldGain;
            rr += readAt (sr, n, row.oldPos * toSample) * row.oldGain;
            row.oldGain = juce::jmax (0.0f, row.oldGain - fadeStep);
            row.oldPos += step;
        }

        const float g = row.gain * vol;
        outL += l * g;
        outR += rr * g;

        // On, wrapping in its range; a wrap crossfades like a jump unless the
        // whole sample loops (it's cut to loop).
        row.pos += step;
        if (row.oneShot && (step >= 0.0 ? row.pos >= row.oneShotEnd : row.pos <= row.oneShotEnd))
            stopHead (r);   // the slice is done: it ramps out from here
        const auto [lo, hi] = range (r);
        if (row.pos >= hi || row.pos < lo)
        {
            const double wrapped = row.pos >= hi ? row.pos - (hi - lo) : row.pos + (hi - lo);
            if (row.looped && row.gain > 0.0f)
            {
                row.oldPos = row.pos;
                row.oldGain = 1.0f;
            }
            row.pos = juce::jlimit (lo, hi - 1.0e-9, wrapped);
        }

        if (! row.playing && row.gain <= 0.0f)
            row.oldGain = 0.0f;
    }

    void publishViews()
    {
        for (int r = 0; r < NumRows; ++r)
        {
            const auto& row = rows[(size_t) r];
            auto& v = views[(size_t) r];
            v.playing.store (row.playing);
            v.looped.store (row.looped);
            v.loopA.store (row.loopA);
            v.loopB.store (row.loopB);
            v.position.store (row.playing ? (float) row.pos : -1.0f);
        }
    }

    //==========================================================================
    // Pattern recorders — audio thread.
    struct Pattern
    {
        static constexpr int MaxEvents = 512;
        struct Event { double offset; Action action; };
        std::array<Event, MaxEvents> events {};
        int count = 0, next = 0;
        double start = 0.0, length = 0.0;   // ticks
    };

    void setPatternState (int p, PatternState s) { patternStates[(size_t) p].store ((int) s); }

    void patternKeyAudio (int p)
    {
        auto& pat = patterns[(size_t) p];
        const double now = juce::jmax (0.0, clockTick);
        switch (getPatternState (p))
        {
            case PatEmpty:     pat.count = 0; setPatternState (p, PatArmed); break;
            case PatArmed:     setPatternState (p, PatEmpty); break;
            case PatRecording: finishRecording (p, now); break;
            case PatPlaying:   setPatternState (p, PatStopped); break;
            case PatStopped:
                pat.start = nextLaunchTick();
                pat.next = 0;
                setPatternState (p, PatPlaying);
                break;
        }
    }

    void recordInto (int p, const Action& a, double tick)
    {
        auto& pat = patterns[(size_t) p];
        const auto st = getPatternState (p);
        if (st == PatArmed)
        {
            pat.count = 0;
            pat.start = tick;
            setPatternState (p, PatRecording);
        }
        else if (st != PatRecording)
        {
            return;
        }
        if (pat.count < Pattern::MaxEvents)
            pat.events[(size_t) pat.count++] = { tick - pat.start, a };
    }

    // Recording ends on the key: the loop is the time since the first
    // press, to the nearest beat, and plays straight on.
    void finishRecording (int p, double now)
    {
        auto& pat = patterns[(size_t) p];
        if (pat.count == 0) { setPatternState (p, PatEmpty); return; }
        pat.length = juce::jmax ((double) TicksPerBeat, std::round ((now - pat.start) / TicksPerBeat) * TicksPerBeat);
        // Where in the loop `now` is, so it carries on from here.
        const double into = std::fmod (juce::jmax (0.0, now - pat.start), pat.length);
        pat.start = now - into;
        pat.next = 0;
        while (pat.next < pat.count && pat.events[(size_t) pat.next].offset <= into) ++pat.next;
        setPatternState (p, PatPlaying);
    }

    void playPattern (int p, double tick)
    {
        if (getPatternState (p) != PatPlaying) return;
        auto& pat = patterns[(size_t) p];
        if (pat.length <= 0.0 || pat.count == 0) return;

        for (int guard = 0; guard < Pattern::MaxEvents + 2; ++guard)
        {
            if (pat.next < pat.count && pat.start + pat.events[(size_t) pat.next].offset <= tick)
            {
                execute (pat.events[(size_t) pat.next++].action, tick, false);
                continue;
            }
            if (pat.start + pat.length <= tick)
            {
                pat.start += pat.length;
                pat.next = 0;
                continue;
            }
            break;
        }
    }

    //==========================================================================
    // Recall — audio thread.
    struct Snapshot
    {
        struct R { bool playing = false, looped = false; int loopA = 0, loopB = Cols - 1, col = 0; };
        std::array<R, NumRows> rows {};
    };

    void recallKeyAudio (int slot, double due)
    {
        if (! recallStored[(size_t) slot].load())
        {
            auto& s = snapshots[(size_t) slot];
            for (int r = 0; r < NumRows; ++r)
            {
                const auto& row = rows[(size_t) r];
                s.rows[(size_t) r] = { row.playing, row.looped, row.loopA, row.loopB,
                                       juce::jlimit (0, Cols - 1, (int) std::floor (row.pos)) };
            }
            recallStored[(size_t) slot].store (true);
            return;
        }

        const auto& s = snapshots[(size_t) slot];
        for (int r = 0; r < NumRows; ++r)
        {
            const auto& sr = s.rows[(size_t) r];
            if (! sr.playing)      schedule ({ Action::Stop, r, 0, 0 }, due);
            else if (sr.looped)    schedule ({ Action::Loop, r, sr.loopA, sr.loopB }, due);
            else                   schedule ({ Action::Jump, r, sr.col, 0 }, due);
        }
    }

    //==========================================================================
    void setSample (int row, std::unique_ptr<Break> b, const Origin& o)
    {
        row = clampRow (row);
        if (b != nullptr)
            setLengthBeats (row, b->beats);
        std::unique_ptr<Break> old;
        {
            const juce::SpinLock::ScopedLockType sl (sampleLock);
            old = std::exchange (samples[(size_t) row], std::move (b));
        }
        origins[(size_t) row] = o;
        // `old` is freed here, off the audio thread.
    }

    struct RowSettings
    {
        std::atomic<double> beats { 4.0 };
        std::atomic<int> speed { 0 }, group { 0 }, mode { ModeLoop };
        std::atomic<bool> reverse { false };
        std::atomic<float> volume { 1.0f };
    };

    struct View
    {
        std::atomic<bool> playing { false }, looped { false };
        std::atomic<float> position { -1.0f };
        std::atomic<int> loopA { 0 }, loopB { Cols - 1 };
    };

    struct Pending { Action action; double due = 0.0, q = 0.0; bool active = false, user = true; };

    double sampleRate = 48000.0, spt = 1000.0, clockTick = -1.0, lastTick = 0.0;
    bool wasRunning = false;
    float fadeStep = 0.007f, gainStep = 0.005f;

    std::array<std::unique_ptr<Break>, NumRows> samples;
    std::array<Origin, NumRows> origins;   // message thread
    juce::SpinLock sampleLock;

    std::array<RowSettings, NumRows> rowSettings;
    std::array<Row, NumRows> rows;                          // audio thread
    std::array<Pending, NumRows + NumGroups> pendings {};   // audio thread
    std::array<View, NumRows> views;
    std::atomic<int> quantize { Q16 }, launchQuantize { QBar };

    std::array<Pattern, NumPatterns> patterns;              // audio thread
    std::array<std::atomic<int>, NumPatterns> patternStates {};
    std::array<Snapshot, NumRecalls> snapshots;             // audio thread
    std::array<std::atomic<bool>, NumRecalls> recallStored {};

    juce::AbstractFifo fifo { 256 };
    std::array<Cmd, 256> cmds {};
};
