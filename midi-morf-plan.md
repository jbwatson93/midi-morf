# MidiMorf v2 — Plan

_Last updated: 2026-05-24. Approved plan also archived at `/Users/jb/.claude/plans/lexical-forging-sun.md`._

## Context

V1 of MidiMorf shipped as a remote-control surface only: every gesture on the iOS app travels over WiFi to a macOS server, which generates MIDI. There's no audio code in the iOS app at all (the `.jucer` doesn't even include `juce_audio_*` modules). That's a usability cliff — without your laptop + the server running + a DAW, the app makes no sound.

V2's big upgrade is **immediate playability**: a built-in synth engine on the phone so the app makes sound on its own, plus a global toggle so you can still send MIDI over WiFi to a DAW when you want to. The synth core has already been proof-of-concepted in `/Users/jb/Desktop/Coding/maker space/Morf-chord/Morf-chord` — clean, header-only, `juce::AudioSource`-based, 32-voice poly, saw/square osc + state-variable LPF + dual ADSR + reverb.

V2 also adds a 7th controller inspired by Telepathic Instruments' Orchid (Kevin Parker): one-octave chromatic keyboard for root selection + 2×4 chord type/modifier grid + voicing/inversion control. Gets a flower name (proposals: **Iris**, **Lotus**, **Dahlia**, **Magnolia**).

**Locked v2 decisions:**
- **Global** WiFi/Local toggle (one app-wide mode).
- Synth applies to **all playable controllers**: Morf-Pad, Omnichord, Dual M-Pad, Morf-Chord, and the new flower-named controller. FX Punch-in and Transport stay WiFi-only.
- **Single global synth patch** shared by all playable controllers.
- Synth editor is a **dedicated "Synth" page** in the hamburger menu.
- Single paid app on App Store, no IAP.

---

## Architecture

```
                  ┌────────────────────────────────────┐
                  │  MainComponent (owns audio device) │
                  └────────────────────────────────────┘
                       │           │           │
                       ▼           ▼           ▼
              ┌──────────────┐  ┌──────┐  ┌──────────────────┐
              │ PageContainer│  │Mode  │  │ AudioEngine      │
              │  (7 pages +  │  │ Mgr  │  │ ├─ SynthEngine   │
              │  Synth page) │  │      │  │ └─ AudioIO       │
              └──────────────┘  └──────┘  └──────────────────┘
                       │             │             ▲
                       ▼             ▼             │
              ┌──────────────────────────────┐    │
              │ Page emits gesture           │    │
              └──────────────────────────────┘    │
                       │                          │
                       ▼                          │
              ┌──────────────────────────────┐    │
              │ EventRouter                  │    │
              │  if mode == WiFi  → NetworkClient
              │  if mode == Local → Handler → SynthEngine
              └──────────────────────────────┘
```

**Key idea:** an `EventRouter` sits between pages and the network/synth backends. Pages don't change their gesture-handling code — they just call `eventRouter.send*(...)` instead of `networkClient.send*(...)`. The router decides whether each event goes over WiFi, gets translated locally into MIDI notes (via per-controller handlers ported from the macOS server), or both.

---

## Workstreams

### WS1 — Synth Engine Integration (foundation)
- Port `SynthEngine.h` from POC, strip chord-specific API, expose pure MIDI (`noteOn/noteOff/allNotesOff` + parameter setters).
- New `AudioEngine.h/.mm` wraps `juce::AudioDeviceManager` + hosts the synth.
- Update `.jucer`: add `juce_audio_basics`, `juce_audio_devices`, `juce_audio_utils`, `juce_dsp`. Configure iOS audio session + background-audio capability.
- Wire `AudioEngine` into `MainComponent`. Temp test-tone button for verification, then remove.

### WS2 — EventRouter / Mode Toggle
- New `EventRouter.h/.cpp` — mirrors `NetworkClient.send*` API.
- Port per-controller handlers (`MorfPadHandler`, `OmnichordHandler`, `DualMPadHandler`, `MorfChordHandler`) from `MidiMorfServer/Source/` to phone-side. These translate semantic gestures → concrete MIDI notes.
- Mechanical find/replace `networkClient.send*` → `eventRouter.send*` in pages.
- Mode toggle UI in top bar (WiFi ↔ Local). Persist via `appProperties`. Panic all notes on switch.

### WS3 — Synth Editor Page
- Port `SynthSettingsPage.h` from POC (sliders for Amp ADSR, Filter, Filter ADSR, Saw/Square mix, master gain, Reverb wet/size).
- Register in `PageContainer` + hamburger menu.
- Persist patch (~15 floats) in `MainComponent::saveState/restoreState`.

### WS4 — Flower Controller (Orchid-inspired)
- Extract `PianoKeySelector` from `MorfPadPage.h` into its own file; add momentary-trigger mode.
- New `FlowerPage.h` — landscape: 2×4 chord button grid on left, 1-octave keyboard on right, voicing scrub bar at bottom.
- New `FlowerHandler.h` — pure chord-construction function.
- Add protocol messages (0x60-0x6F range). Mirror handler on macOS server.

### WS5 — Polish & Ship
- Update auto-memory + About page.
- TestFlight build, then App Store. Companion macOS server release with `FlowerHandler` mirror.

---

## Critical Files

**Port from POC:**
- `/Users/jb/Desktop/Coding/maker space/Morf-chord/Morf-chord/Source/SynthEngine.h`
- `/Users/jb/Desktop/Coding/maker space/Morf-chord/Morf-chord/Source/SynthSettingsPage.h`
- `/Users/jb/Desktop/Coding/maker space/Morf-chord/Morf-chord/Source/MainComponent.cpp` (audio device setup reference)

**Modify in MidiMorfApp:**
- `MidiMorfApp.jucer`, `MainComponent.h/.mm`, `NetworkClient.h/.cpp`, `MidiMorfProtocol.h`, all controller pages

**New files:**
- `AudioEngine.h/.mm`, `EventRouter.h/.cpp`, `SynthEngine.h`, `SynthSettingsPage.h`, `PianoKeySelector.h`, `FlowerPage.h`, `FlowerHandler.h`, plus the four per-controller handlers ported from the server.

**Files that must stay in sync (phone ↔ server) grows to include:** all `*Handler.h` files in addition to the existing `MidiMorfProtocol.h`, `NetworkServer.h/.cpp`, `MidiMorfColors.h`, `SlotConfig.h`.

---

## Open Design Questions (iterate during/after WS4)

1. **Flower name** — Iris, Lotus, Dahlia, Magnolia, or other?
2. **Voicing control UI** — horizontal scrub bar, two-finger pinch, dedicated vertical slider, or rotary?
3. **Portrait vs. landscape** for the Flower controller.
4. **Sub-bass engine** (Orchid has one) — defer.
5. **Performance modes** (Strum/Arp/Pattern/Harp/Slop) — defer to v2.1.
6. **Background audio** — yes/no? Adds entitlement + power cost.
7. **JB's twist on the Orchid model** — still want to capture.

---

## Out of Scope for v2
- Preset bank / patch library (just the single live patch)
- Sub-bass engine
- Performance modes
- Effects beyond reverb
- Android, iPad-optimized layout, AUv3 wrapper (possible v3)

---

## Other Ideas (parking lot)
_JB has more ideas to capture iteratively._

- _(placeholder)_
