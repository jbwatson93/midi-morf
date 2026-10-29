# Spikes

Throwaway experiments that answer one question each. Not shipped code.

## `midiclock-spike.cpp` — RUN 2026-08-24, PASSED

**Result:** 0.006 ms rms stamp regularity, ~0.08 ms actual delivery spread,
−0.001 ms drift over 18 bars, 92.999 BPM vs 93.000 set. Live stamps clock with
precise host times but delivers at time — no forward lead to exploit, so the
engine derives its own via one tick of lookahead. **Server-side engines confirmed;
no plugin needed.** Full write-up in `../midi-morf-next-plan.md` (WS-2).

Kept runnable for the untested cases: under DAW load, at large audio buffers, and
in other DAWs.


**Question:** is DAW MIDI clock into the server tight enough to run the Creative
Sequencer Pack engines there, instead of building a VST/AU plugin?

This is the assumption the whole v3 plan rests on (see `../midi-morf-next-plan.md`,
WS-2 "Why the server and not a plugin"), and it was argued from principle rather
than measured. This measures it.

### Build & run

```sh
clang++ -std=c++17 -O2 midiclock-spike.cpp \
        -framework CoreMIDI -framework CoreFoundation -o midiclock-spike
./midiclock-spike
```

No Projucer, no Xcode, no JUCE. Ctrl-C for the final report.

### Ableton setup

1. **Preferences > Link/Tempo/MIDI**
2. Output list → `MidiMorf Sync` → enable **Sync**
3. Input list → `MidiMorf Spike Out` → enable **Track**
4. New MIDI track, input `MidiMorf Spike Out`, monitor **In**, **arm** it
5. **Record Quantization OFF** (Edit menu) — we want the raw truth, not a cleaned-up version
6. Press play, record a few bars

### Reading the output

The number that matters is **jitter (rms)** — residuals from a least-squares line
fitted to the 16th-note arrival times. Fitting a line deliberately removes
constant tempo offset and slow drift, both of which are correctable, leaving only
genuine wander, which is not.

| Jitter (rms) | Meaning |
|---|---|
| < 1 ms | Server-side engines are fine. Plan stands as written. |
| 1–3 ms | Comfortably inside hardware-sequencer territory. |
| 3–8 ms | Audible but musical. Record Quantization cleans up capture. |
| > 8 ms | Escalate to Ableton Link, or reconsider the plugin. |

**Watch consistency, not lateness.** A constant offset is a number you subtract.
Wander is the thing that would push the design back toward a plugin.

`drift` should stay near zero over many bars. If it grows steadily, the clock
source and our tick counting disagree about tempo — that's a bug on our side,
not a verdict on Live.

### Tempo changes

The fit is over a **rolling 64-step window**, so tempo ramps no longer invalidate
the statistic. (The original global fit assumed constant tempo: ramping 93 → 102 BPM
made arrival times follow a curve, and residuals from a straight line exploded to
hundreds of ms. The clock was fine; the metric wasn't. Measured 2026-08-24.)

### `CLOCK STAMPING` line

The report says whether the DAW stamps its clock packets for a precise future host
time, or sends them as "deliver now" (timestamp 0). This decides how the engine
emits notes:

- **Scheduled ahead** → stamp each note for its tick's host time. CoreMIDI delivers
  at that instant, so output is sample-accurate — as good as a plugin.
- **Timestamp 0** → nothing to exploit; fire immediately. The jitter figure is then
  genuine delivery jitter rather than the DAW's intent.

The spike now stamps its own notes with the tick's timestamp either way, which is
never worse than 0. **A very low jitter number with scheduled stamps means we
measured the DAW's arithmetic, not delivery** — trust the recorded round trip in
the DAW over the number in that case.

### Also worth trying

- Same test with a **large audio buffer** (1024) vs small (128) — Live's clock
  send granularity is tied to its buffer, so this bounds the worst case.
- Same test in **Logic** if cross-DAW parity matters.
- Same test with the machine **under load** (a few plugins running) — that's the
  realistic condition, and an idle-machine number flatters the result.
