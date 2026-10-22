# MidiMorf v3 — Handoff

_Written 2026-08-27 by the previous session. Read alongside `midi-morf-next-plan.md`
(strategy, measurements, rejected directions). This file is the working state._

**Branch:** `v3-sequencer-pack`, 7 commits ahead of `main`. Tree clean.

---

## TL;DR of where things stand

| Piece | State |
|---|---|
| Timing spike | **Done, passed decisively.** Measured, not assumed. |
| `ClockedSequencer` base | **Done.** Shared timing core for all pack engines. |
| `StepSequencerEngine` | **Done.** Working on device. JB: *"incredibly fun and playable."* |
| `StepSequencerPage` | **Done.** 16 lanes, 4 modes, 909 boxes, roller, drag-paint. |
| Protocol for step seq | **Done.** Both `MidiMorfProtocol.h` copies byte-identical. |
| `SliceSequencerEngine` | **Done and wired** — protocol `0x74`–`0x77`, server dispatch, page. |
| `SliceSequencerPage` | **Done, builds. NOT yet run on device.** ← test next |
| IAP / entitlement | Plumbed but inert. Engines default to enabled. |

**The immediate next task:** play the slice page against Live on device —
grid drag feel, 32×32 usability, beat repeat, ribbons → Simpler pitch bend.
Slicer wiring (2026-08-27): shared messages `SeqStepSet`/`SeqTransport`/
`SeqRatchet` now route to whichever engine matches the phone's page
(`MidiMorfEngine::activeSequencer()`), as does the playhead stream.
`ControllerMode::DrumGrid` was renamed `SliceSequencer` (= 8);
`NUM_CONTROLLERS` is 9. `LaneStrip` gained variable column count + a Cell
render mode; `StepBox` gained `setStyle()`. Slice page state: identity
pattern per grid, fixed vel 100 / gate 95, root combo uses Live's octave
naming (C1 = 36).

First device-test round (2026-08-27, JB: "pretty fun"): looper is now one
slideable `LooperStrip` (buttons dropped holds when the finger drifted),
fastest division at top; RST button restores the identity pattern; and a
looper window shorter than a step was silent (no step boundary inside the
window) — those divisions now ride the base-class roller. New: ribbon 2
defaults to a per-step repeater (`RibbonTarget::StepRepeat = 2`,
`ClockedSequencer::setStepRepeat`) — zoned x1/x2/x3/x4/x6/x8, springs back
to x1 on release; SeqRibbon's value carries the count itself for that
target. x8 caps at one hit per tick (6 ticks/step at 1/16). Not yet
re-tested on device.

---

## READ THIS FIRST — four traps that cost the last session hours

### 1. Many files are CRLF. Editing them in text mode destroys them.

`pathlib.read_text()` / `write_text()` on a CRLF file **deletes one character
from the end of every line**. It corrupted `MainComponent.h` and
`NetworkClient.h` (~80 lines each) and it was only recoverable because most
lines matched `git show HEAD:<file>` exactly one character longer.

**Always check first, always edit in binary:**

```python
d = open(p,'rb').read()          # never read_text()
assert d.count(old) == 1         # assert your match, do not assume
open(p,'wb').write(d.replace(old, new))
```

**CRLF files** (28) — includes `MainComponent.h`, `NetworkClient.h`,
`VirtualMidiPort.h/.cpp`, both `.jucer` files, and most pre-v2 sources.

**LF files** (37) — includes everything created recently:
`ClockedSequencer.h`, `MidiClockReceiver.h`, `StepSequencerEngine.h`,
`SliceSequencerEngine.h`, `StepSequencerPage.h`, `MidiMorfEngine.h/.cpp`,
`NetworkServer.h/.cpp`, both `MidiMorfProtocol.h`.

A match string written with `\n` will silently fail on a CRLF file. That
happened twice — the second time it shipped a declaration whose definition
never landed.

### 2. Never build the iOS app for the simulator.

```
CONFIGURATION_BUILD_DIR = "$(PROJECT_DIR)/build/$(CONFIGURATION)"
```

No platform suffix, so **a simulator build overwrites the device `.app` Xcode
deploys from**. This put a simulator binary on JB's iPad and iPhone; both died
at launch with `dyld __abort_with_payload` / *"wrong platform to load into
process"*, which looks exactly like a code-signing failure and is not.

To verify iOS compilation, redirect the products away from the project:

```sh
xcodebuild -project MidiMorfApp.xcodeproj -target "MidiMorfApp - App" \
  -configuration Debug -sdk iphoneos build \
  CODE_SIGNING_ALLOWED=NO CONFIGURATION_BUILD_DIR=<scratch>
vtool -show-build <scratch>/MidiMorfApp.app/MidiMorfApp | grep platform   # must be IOS
```

### 3. A passing build is not evidence an edit was correct.

Both of the above compiled clean while broken. Corruption sat latent for
several turns; a missing function definition linked fine because nothing
called it yet. **Verify the bytes changed as intended**, and force a link
error with a temporary caller when adding a definition.

### 4. `NUM_CONTROLLERS` is load-bearing.

It is the page-index base for synth/about/settings in `MainComponent.mm`
**and** the bound on `controllerNames[]`. Setting it to 10 with 7 pages
registered caused an out-of-bounds read on a 7-element array. A
`static_assert` in `MainComponent.mm` now guards it.

**When adding the slice page:** bump to 9, add `"Slice"` to `controllerNames`,
and register the page in `MainComponent`'s ctor list + `addPage` call.
Page order must match `ControllerMode` values.

---

## Architecture, and why (do not relitigate)

```
Phone (editor only)  ──WiFi──▶  Server (engines)  ──virtual MIDI──▶  DAW
        ▲                            ▲
        └── SeqPlayhead ─────────────┘   DAW MIDI clock in ──┘
```

**Engines live on the server, not in a plugin.** Settled with measurement, not
argument — `spikes/midiclock-spike.cpp` against Ableton Live gave 0.006 ms rms
stamp regularity, ~0.08 ms actual delivery spread, −0.001 ms drift over 18
bars. A plugin would cost VST3 + AU + Windows + signing + notarisation +
installers for a margin nobody can hear. Full reasoning in
`midi-morf-next-plan.md` → *Rejected Directions*.

**The sequencer pages have no Local mode.** All timing is DAW clock. This
removes the phone-side clock problem entirely — there is no engine on the
phone at all.

**Notes fire immediately from `clockTick()`, not on lookahead.** JUCE's
`MidiOutput` can only schedule via `sendBlockOfMessages()`, a background
thread at millisecond resolution — that would *add* ~1 ms, an order of
magnitude worse than firing now. Real lookahead needs bypassing
`juce::MidiOutput` for CoreMIDI timestamps. Documented in
`MidiClockReceiver.h`. Not worth it unless measurement shows a problem.

**Playhead is polled, not pushed.** A 30 Hz timer in `MidiMorfEngine` reads
the engine's atomic and sends on change. The MIDI thread must never touch the
socket.

**Drag is ~40% of the playability** (JB's words, on device). Lanes are one
component, not 16 sliders, so a finger sweeps across several. Step boxes
drag-paint too, with the first box pressed setting the direction for the
sweep. **Any new grid must be drag-first, not tap-per-cell.**

---

## Business model (affects UI decisions)

Free app stays free forever; new content ships as paid IAP packs via App Store
and Google Play. **The phone holds the entitlement** — the server is free and
runs pack engines only while an entitled phone is connected. No desktop
licensing system, no key files, no activation server.

**Decided: sequencer pages are fully gated, no demo mode.** JB's reasoning:
7 controllers and a full synth free is already generous. Do not re-pitch a
demo tier unless conversion underperforms.

`setEnabled()` exists on `ClockedSequencer` and `PackEntitlement` is plumbed
end to end, but engines currently **default to enabled** so they run without a
phone. Flip that default when the IAP actually ships.

---

## Next task: the slice sequencer

Monome mlr / Re:Mix. Drop a 1–2 bar loop into Simpler in slice mode; this
re-orders the slices live. Slices map chromatically from a root, so a step's
index **is** its slice number, and identity (step N → slice N) reproduces the
untouched loop. Remixing means pulling steps off that diagonal.

`SliceSequencerEngine` is written and compiles. What remains:

### Protocol (both `MidiMorfProtocol.h` copies must stay byte-identical)

Existing sequencer messages are `0x70`–`0x73` plus `PackEntitlement 0x08` and
`SeqPlayhead 0x16`. Suggested additions:

| Message | Payload |
|---|---|
| `SeqSliceConfig` | bars, division, sliceCount, rootNote, direction, swing |
| `SeqLooper` | division (0=1/4…3=1/32), active, randomOn |
| `SeqRibbon` | ribbonIndex, value (14-bit, 2 bytes) |
| `SeqRibbonAssign` | ribbonIndex, target (0=pitchbend, 1=CC), ccNumber |

`SeqStepSet` can be reused as-is — its `degree` byte is already "index into the
note table", which is a slice here. Check `MAX_PACKET_SIZE` (currently
`HEADER_SIZE + DUAL_MPAD_TOUCH_PAYLOAD_SIZE` = 10) still covers the largest.

### The page

**Reuse `LaneStrip` from `StepSequencerPage.h`** with a cell-marker render mode
instead of a filled bar. A column is not a stack of buttons — it is a vertical
control whose value is a slice index, with the chosen cell shaded. That is what
makes 32 columns viable: you drag to a position and watch the shading move
rather than hitting a 10px target.

Layout from JB's wireframe (`IMG_2223`, 2026-08-27):

- **Top:** two assignable ribbons, side by side
- **Middle:** N columns × N cells, one shaded per column
- **Bottom of each column:** step on/off box, same idea as the 909 boxes
- **Right rail:** edit button (top) → popup with root note, division etc;
  random-arm button; then the looper strip `1/4 · 1/8 · 1/16 · 1/32`

The right-rail strip is a **beat-repeat looper, not a rate control** — hold a
division and playback loops that window. Rate comes from bars × division.

### Decided details

- **Grid:** JB selects 1 or 2 bars × 1/4 · 1/8 · 1/16 → 4/8/16/32 steps.
  `setGrid(bars, division)` already does this. JB will test 32×32 usability.
- **Ribbons:** both assignable. **Ribbon 1 defaults to pitch bend**, which
  Simpler honours natively with no MIDI mapping. Ribbon 2 → CC; the user
  MIDI-maps it once in Ableton and the set remembers it.
- **Looper:** beat repeat, snaps its loop start down to the grid. `tickCounter`
  keeps advancing underneath so releasing lands where the DAW expects.
  Random = **one random slice per repeat**, only while held.
- **Controlling Simpler automatically is deferred** to a separate "Ableton
  sidekick app." It requires an Ableton Remote Script (MIDI cannot configure
  Simpler; the Live Object Model can). Do not build it into v3.

---

## Untested / uncertain

- **`StepSequencerEngine` after the `ClockedSequencer` refactor.** Behaviour
  should be identical and it builds and starts clean, but it has not been
  played on device since. Worth confirming first — it is the one change that
  could have altered feel.
- **32×32 grid usability.** JB will judge.
- **Beat repeat in practice.** The grid-snap and release-in-place logic is
  reasoned, not heard.
- **Recording offset.** JB recorded a note on beat 1 and it lands late in the
  clip. That is Live's MIDI input path, not our timing. Not yet confirmed
  whether it is constant (correctable via Record Quantization or
  Preferences → Audio → Driver Error Compensation) or varying. If it needs
  fixing on our side it means a nudge setting, which needs the lookahead
  scheduling described above.

## Known issues not yet addressed

- **`MidiMorfApp/Builds/iOS` is a nested git repository** — mode `160000`
  gitlink, no `.gitmodules`. The iOS project files are therefore **not in this
  repo**; a fresh clone gets an empty directory. Fix is to delete the nested
  `.git` and absorb the files. JB has been told; not yet done.
- `spikes/midiclock-spike.cpp` still worth running under DAW load, at large
  buffers, and in other DAWs. Margin is vast so none of this is blocking.

---

## File map

**Server** (`MidiMorfServer/Source/`)
- `ClockedSequencer.h` — shared timing core, `MAX_STEPS = 32`
- `StepSequencerEngine.h` — scale note table + demo pattern (89 lines)
- `SliceSequencerEngine.h` — chromatic slice table, beat repeat, random
- `MidiClockReceiver.h` — virtual `MidiMorf Sync` destination, 24 PPQN
- `MidiMorfEngine.h/.cpp` — owns engines, 30 Hz playhead timer
- `VirtualMidiPort.h/.cpp` — `Midi-Morf` output; has `sendPitchBend`
- Step seq is **channel 8**, slice seq **channel 9** (1–7 and 16 taken)

**App** (`MidiMorfApp/Source/`)
- `StepSequencerPage.h` — `LaneStrip`, `StepBox`, `RollerButton`, content
- `EventRouter.h` — sequencer sends are WiFi-only passthroughs
- `NetworkClient.h/.cpp` — **CRLF, edit in binary**
- `MainComponent.h` (**CRLF**) / `.mm` (LF) — page registration

**Docs**
- `midi-morf-next-plan.md` — strategy, measurements, rejected directions
- `spikes/README.md` — timing spike results and how to re-run

## Build commands

```sh
# Server
cd MidiMorfServer/Builds/MacOSX
xcodebuild -project MidiMorfServer.xcodeproj -target "MidiMorfServer - App" \
  -configuration Debug build CODE_SIGNING_ALLOWED=NO

# iOS — device SDK, scratch output. NEVER -sdk iphonesimulator.
cd MidiMorfApp/Builds/iOS
xcodebuild -project MidiMorfApp.xcodeproj -target "MidiMorfApp - App" \
  -configuration Debug -sdk iphoneos build \
  CODE_SIGNING_ALLOWED=NO CONFIGURATION_BUILD_DIR=<scratch>
```

New header-only files compile via their `#include` without a Projucer re-save,
but should be added to the `.jucer` file list so they appear in Xcode. Note the
`.jucer` files are **CRLF**, and `iosDevelopmentTeamID` must stay
`"69KM48W4TU"` — it previously had a leading space and a missing trailing `U`,
which would have broken signing on the next Projucer save.

## Testing the sequencers with Live

1. Run the server. Two ports appear: `Midi-Morf` (out) and `MidiMorf Sync` (in).
2. Live → Preferences → Link/Tempo/MIDI → find `MidiMorf Sync` in **Output**,
   enable **Sync**.
3. MIDI track, input `Midi-Morf`, **channel 8** (step) or **9** (slice),
   an instrument on it, monitor **In**.
4. Press play in Live. The engines follow its transport; they never self-start.
