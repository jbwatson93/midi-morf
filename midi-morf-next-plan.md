# MidiMorf v3 — Creative Sequencer Pack

_Rewritten 2026-08-24. Supersedes the 2026-08-21 draft (paid-VST model — now rejected, see **Rejected Directions**). Companion to `midi-morf-plan.md` (the v2 plan, shipped)._

## Context

v2 shipped 2026-07-07 — iOS v2.0.0 on TestFlight, Mac server v2.0.0 signed + notarized, retro-relic.com updated. Pricing pivoted to free, growth-first.

v3 introduces **the first paid content in MidiMorf**, and does it without a new product, a new binary format, or a new build pipeline. Three sequencers ship as pages in the existing app, gated behind one in-app purchase, with their engines running in the existing standalone server.

**The core insight this plan rests on:** the phone pages are the product. The engine host is plumbing, and a working, signed, notarized plumbing app already ships.

---

## Business Model

**The free app stays exactly as it is, forever. New content arrives as paid packs.**

The free tier is already substantial and stays untouched — 7 controllers, the full built-in synth, WiFi MIDI to the free standalone server, and user-defined MIDI mappings. Nothing that shipped in v2 is ever moved behind a paywall.

**Creative Sequencer Pack** — one purchase, three sequencers, sold as a non-consumable IAP through the App Store and Google Play.

### Phone-as-dongle

> **The phone holds the entitlement. The server is free and stays free. It runs sequencer engines only while an entitled phone is connected.**

Consequences, all good:

- **No desktop licensing system.** No key files, no activation server, no web store, no accounts, no unlock codes. This was the single largest hidden cost in the rejected plugin model, and this design deletes it outright.
- **Platform stores are the trust signal.** Users buy where they already have a payment method. Restore-purchases works across their devices for free, refunds are the platform's problem, and no one ever emails about a lost licence key.
- **One code path for both stores.** `juce_product_unlocking` wraps StoreKit and Play Billing behind the same API.

Piracy is trivial — anyone who reads a packet dump can spoof the handshake. At this price and audience, ignore it. Do not engineer against it.

### Accepted trade-offs

- **Per-platform purchase.** An iOS purchase does not entitle Android. Fixing that needs our own account system, which reintroduces exactly the infrastructure this design avoids. Per-platform is normal; users expect it.
- **Platform cut.** 15% under Apple's Small Business Program and Google's equivalent first-$1M tier. The rejected direct-sale model kept 100% but required building and operating everything listed above.

### Guardrail (revised, still non-negotiable)

The v2 draft framed this as *"a plugin must never lock a page that works today."* That principle holds, but the correct statement is narrower:

> **Nothing that has shipped free ever becomes paid. New content may ship paid.**

The pack is additive by construction — it adds three pages that never existed. **Decided 2026-08-24: the sequencer pages are fully gated, no demo or limited mode.** The free tier is already generous enough to carry the growth strategy on its own; the pack does not need to be sampled to justify itself.

_Known cost of that decision, accepted:_ a fully locked page is invisible to the growth engine — nobody screen-records a paywall. If pack conversion underperforms, a demo state (8 steps, or no pattern saving) is the first lever to pull.

---

# WS-1 — The Three Sequencers

One purchase unlocks three pages. Pick three distinct musical *purposes*, not three variations on step entry.

| # | Sequencer | Purpose | Notes |
|---|---|---|---|
| **1** | **Step Sequencer** | Melodic | 16 steps, scale-locked. Moog / Beatstep Pro lineage. **Ships first.** |
| **2** | **Drum Grid** | Rhythmic | 8–16 lanes × 16 steps. Euclidean generation as a *mode inside* this page, not a fourth product. |
| **3** | **Physics** | Chaotic | Assign each wall a note; a gun fires triggers; accelerometer aims the gun / flings projectiles. Collision → `noteOn`. |

## Why the step sequencer ships first

*"Beatstep Pro on your phone, synced to your DAW"* is a proposition people already understand and already spend money on. The physics sequencer is more original, but novelty converts worse. Leading with the step sequencer means the first paid thing is the easy sell, and the weird one lands later as a reason for existing buyers to stay interested.

## Step Sequencer — v1 scope

Table stakes to read as a genuine Moog/Beatstep-class instrument:

- 16 steps, **pitch quantized to key + scale** — slider position maps to a scale degree, so it is always in key. This is the entire appeal and it is cheap.
- Velocity and gate length per step, plus rest
- **Pattern length independent of 16** — this is what produces the polymetric Moog feel, and it costs almost nothing
- Rate division: 1/4, 1/8, 1/16, triplets
- Swing
- Direction: forward, reverse, ping-pong, random

**Explicitly out of v1:** ratchets, per-step probability, multiple patterns/chaining. These are v1.1 and must not be allowed to creep in.

### UI: one lane row, mode-switched

16 pitch sliders *plus* 16 velocity sliders in iPhone landscape is roughly 40px per lane — tight and ugly.

Use the hardware answer: **16 lanes, full height, with a mode selector for what the sliders control — `PITCH / VEL / GATE`.** Comfortable touch targets, a pattern users already understand from limited-control hardware, and adding per-step probability later costs a button instead of a redesign.

New per-page palette per the established convention (see `Glassmorphism.h` and the per-page palette pattern).

## No local mode

**Decided 2026-08-24: the sequencer pages do not drive the built-in synth.** They are pure remote editors — slider state goes up, playhead position comes back.

This removes the entire phone-side clock problem. There is no free-running mode, no drift caveat, and **no engine on the phone at all.** The engine exists in exactly one place: the server.

_Second-order benefit — see WS-2:_ with no audio path on the phone, the pack carries **zero Android latency risk**.

---

# WS-2 — Server-Side Engines & Sync

The server gains three sequencer engines and a clock input. No new binary, no new format.

## Why the server and not a plugin

A plugin would give sample-accurate timing via `AudioPlayHead`. That advantage is much smaller than it first appears:

- **Record Quantization.** For clip capture — the actual goal — Live's input quantization at 1/16 snaps incoming notes to the grid on the way in. A 16th-note sequencer captures perfectly regardless of input jitter.
- **Hardware precedent.** Every MIDI-clock-slaved sequencer since 1983 has carried a few ms of slop, and that is the sound of most electronic music ever recorded. 3–8ms of live-monitoring looseness is less than standing two metres from an amp.

Against that, the plugin route costs VST3 + AU + Windows builds, code signing, notarization, installers, and a permanent stream of *"it isn't showing up in my plugin list"* support tickets. The server is already built, shipping, signed, notarized, and already owns the virtual port and the network layer.

**Monetization is unaffected either way, because the gate lives on the phone.** See **Rejected Directions** for the full plugin comparison, retained so it doesn't get re-proposed.

## Work items

| Item | Evidence | Work |
|---|---|---|
| **MIDI clock input** | Server currently exposes an output port only | Add a second virtual port — `MidiMorf Sync`, a *destination* the DAW writes to, alongside the existing `MidiMorf` source it reads. On macOS that is `juce::MidiInput::createNewDevice()`. Parse `0xF8` clock (24 PPQN), `0xFA/0xFB/0xFC` start/continue/stop, `0xF2` song position. |
| **Three engines** | New | Plain classes in an app that already exists. Clock-driven, emitting to the existing virtual MIDI out. |
| **Entitlement check** | New | Engines refuse to run unless the connected phone presents a valid pack entitlement. |
| **Mode → engine dispatch** | `MidiMorfEngine.cpp:307` already stores the phone's page in an atomic `currentMode` | Three new `ControllerMode` values; the server selects the engine from the page the phone is on. **Half of this already works.** |

Item 1 is the only genuinely new ground.

### Timing spike — MEASURED 2026-08-24, PASSED

`spikes/midiclock-spike.cpp` (standalone CoreMIDI, no JUCE or Xcode) measured Ableton
Live's MIDI clock into a virtual destination. **Result: the plugin is unnecessary.
Settled with data.**

| Metric | Measured |
|---|---|
| Stamp regularity (Live's intent) | **0.006 ms rms**, 0.010 ms peak |
| Actual delivery spread | **~0.08 ms peak-to-peak** (stamps arrive 26–105 µs after their own time) |
| Tempo accuracy | 92.999 BPM derived vs 93.000 set |
| Cumulative drift | **−0.001 ms over 289 steps** (18 bars) |
| Clock stamping | Scheduled with precise host times, delivered at time — no forward lead |

Even the pessimistic reading (delivery, not intent) is **~0.08 ms worst case — about
4 samples at 48 kHz**, an order of magnitude inside the "excellent" threshold.

**Tempo changes are handled correctly.** An earlier run ramping 93 → 102 BPM showed
huge apparent jitter, but drift stayed at 0.000 and BPM tracked smoothly throughout —
the linear-fit *statistic* was invalid under changing tempo, not the clock handling.
The spike now fits over a rolling 64-step window.

### Engine design consequence — one tick of lookahead

Live stamps precisely but does **not** schedule ahead, so there is no free lead to
ride. Derive one instead:

> When tick N arrives, compute the host time of tick N+1 from the measured tick
> interval and **stamp the note for that time**. CoreMIDI holds it and delivers at
> exactly that instant.

This removes our callback latency and processing jitter from the output path rather
than inheriting them. At 93 BPM one tick is ~27 ms of margin against ~0.08 ms of
dispatch jitter.

**One tick, not a full 16th.** Enough to eliminate jitter, short enough that tempo
changes and slider edits still feel immediate. More lookahead buys nothing and costs
responsiveness.

_Still unmeasured:_ behaviour under DAW load, at large audio buffers, and in other
DAWs. See `spikes/README.md`. None of these are blocking — the margin is vast.

## Per-track routing

One virtual port carries all three sequencers, split by **MIDI channel** — the DAW filters input per track. This is an established pattern in the product already (Transport on ch 16, Rose-Chord on ch 7), not a new concept.

## Sync roadmap

**v1 — MIDI clock.** Universal, no SDK, works in every DAW. Its weakness is that it carries no reliable bar position (only coarse Song Position Pointer on locate), so aligning to bar 1 is fiddly.

**Fast-follow — Ableton Link.** Zero configuration, no port setup at all, and it provides beat *phase*, so the sequencer knows where it is in the bar. Live-strong, weak in Logic and Pro Tools — which is why it is second in, not first. Already sitting in the parking lot as an open item; this is what finally justifies it.

## Protocol changes

**Finding: the pack does not need the bulk-message class the old plan called for.** Per-step messages fit the existing `[1 byte type][payload]` framing comfortably.

| Message | Direction | Payload | Size |
|---|---|---|---|
| `SeqStepSet` | phone → server | `u8 step, u8 pitchDegree, u8 velocity, u8 gate, u8 flags` | 5 |
| `SeqConfig` | phone → server | rate division, pattern length, direction, swing, key, scale | ~6 |
| `SeqTransport` | phone → server | run/stop/reset | 1 |
| `SeqPlayhead` | server → phone | `u8 currentStep` | 1 |
| `PackEntitlement` | phone → server | pack id + validity | 2 |

All well under `MAX_PACKET_SIZE = 10` (`MidiMorfProtocol.h:118`). A full pattern dump on connect is 16 small messages, which is fine. Playhead traffic at 16ths/120bpm is 8 messages/sec — negligible.

`NUM_CONTROLLERS` goes 7 → 10. Both copies of `MidiMorfProtocol.h` must stay in sync, per the existing convention.

---

# WS-3 — IAP & Entitlement

## Implementation

JUCE ships **`juce_product_unlocking`** — `getProductsInformation()`, `purchaseProduct()`, `restoreProductsBoughtList()`, plus a `Listener`. Not currently in the `.jucer`; adding it is a Projucer re-save. The same module covers Google Play Billing.

**Verify before building on it:** the iOS backend is built on `SKPaymentQueue` / `SKProductsRequest` — that is **StoreKit 1**. Apple still accepts it, but it is legacy and they have been steering developers to StoreKit 2 for years. Confirm it passes review, and budget for a small Obj-C++ StoreKit 2 shim if it does not.

## Gate — three touch points, all small

- **`PageContainer`** — pages registered, rendering a locked state
- **Menu** — entries visible and marked locked. Visible-but-locked outperforms hidden.
- **`MainComponent::restoreState`** — entitlement cached in `appProperties` so it survives offline launches; refreshed on connect

## Store requirements that bite people

- **"Restore Purchases" is mandatory.** Missing it is a standard rejection.
- Non-consumable product. Decide on Family Sharing.
- App Review tests on a device with **no Mac server running** — the locked page must clearly describe what is being sold rather than appearing blank or broken.

## Pricing

**$14.99–19.99.** Three instruments justifies the top of the IAP band in a way a single page would not. In-app conversion substantially beats *"go buy it on our website from your computer."*

The **"pack"** framing is deliberate: it covers all three sequencers now and gives a container to keep shipping into. Every future addition is a reason for existing buyers to feel good about the purchase and for holdouts to convert.

---

# WS-4 — Android Port

_Researched 2026-08-21. Reframed 2026-08-24 — the pack changes the risk profile substantially._

## The pack is Android-safe at every device tier

**The sequencer pages have no audio path on the phone.** They are WiFi state plus a moving playhead. Every Android audio-latency concern below — Oboe, AAudio, buffer sizes, the entire WS-5 capability gate — is **irrelevant to this feature**. A $150 Android phone runs the sequencer pack exactly as well as a Pixel, because nothing is rendered on it.

Combined with Play Billing arriving through the same JUCE module as StoreKit, **Android stops being a reach play and becomes a revenue play — on the one feature that carries zero porting risk.**

That is a real argument for either pulling the Android port forward, or scoping a **sequencer-pack-first Android build** ahead of a full-parity one.

## Verdict on the full port

The **code** port is easy; the **platform** port is not. ~95% of the app is pure JUCE C++ that compiles unchanged. Cost concentrates in audio latency, paint performance, and fragmentation — not in porting logic.

**~1–2 weeks to a good build, 3–4 weeks to shippable.**

## Already portable

All seven controller pages, `SynthEngine.h`, `Glassmorphism.h`, `SynthLookAndFeel.h`, `EventRouter.h`, every `*Handler.h`, `MidiMorfProtocol.h`, `NetworkClient.cpp`. JUCE 8 has a first-class Android exporter.

**Lucky break:** `NetworkClient.cpp:329` uses `juce::NetworkServiceDiscovery` (UDP broadcast) — *not* Apple Bonjour. The `NSBonjourServices` plist key is iOS permission theatre only. **The Mac server needs zero changes.**

## Complete iOS-specific surface — only 4 things

| File | What | Android replacement |
|---|---|---|
| `AccelerometerManager.h:29` | CoreMotion `CMMotionManager` gravity | JNI → `SensorManager` / `TYPE_GRAVITY`. ~100 lines. JUCE has no cross-platform accelerometer API — that is why this class exists. |
| `Haptics.mm:15` | `UIImpactFeedbackGenerator` | JNI → `Vibrator.vibrate(VibrationEffect)`. ~30 lines, 3 call sites. |
| `MainComponent.mm:157` | `UIDevice` iPad-idiom check for the right-gutter floor | JUCE exposes `safeAreaInsets` on Android (display cutouts). Swap for a screen-size heuristic. |
| `Main.mm`, `MainComponent.mm`, `Haptics.mm` | `.mm` = Obj-C++, NDK will not compile it | Rename to `.cpp`; split haptics/accel into `_iOS.mm` + `_Android.cpp` with per-exporter compile flags. Mechanical, touches the `.jucer` file list. |

About a day's work total.

## The parts that actually cost

**1. Audio latency — the real risk, and irrelevant to the pack.** iOS gives ~5ms round trip on every device ever made. Android: Pixel ~20–40ms, budget/midrange routinely 100ms+. For a strum plate and momentary chord buttons that is the line between "instrument" and "broken."
- Needs `JUCE_USE_ANDROID_OBOE=1` plus explicitly requesting native sample rate + buffer size.
- `AudioEngine.h:30` calls `initialiseWithDefaultDevices(0, 2)` — on Android that silently lands on the safe/slow path. Must change.
- `JUCE_IOS_AUDIO_EXPLICIT_SAMPLERATES=48000.0` is iOS-only; Android needs its own handling.
- Unlike the iPad `AudioQueue` saga there is **no single patch**. It is per-device.

**2. Paint performance.** 5-layer glassmorphism per page, per-widget glass, halos, shadows; XY pads and audition ribbon repaint at touch rate. JUCE's software renderer is meaningfully slower on Android. Expect to cache static glass layers into `juce::Image` buffers, possibly attach an `OpenGLContext`. **This one does affect the sequencer pages** — a 16-lane slider bank with a moving playhead repaints often.

**3. Multicast lock.** Android's WiFi chipset drops broadcast/multicast unless the app holds a `WifiManager.MulticastLock` (JNI). Silent failure — discovery simply never finds the server. Manifest needs `INTERNET`, `CHANGE_WIFI_MULTICAST_STATE`, `NEARBY_WIFI_DEVICES` (API 33+). **Blocking for the pack**, since the pack is network-only.

**4. Fragmentation.** Foldables, 20:9, tablets. Pixel constants (`btnW = 56`, `rightGutter = 64` at `MainComponent.mm:144`) need dp-aware scaling.

## Ship strategy — WiFi-first

**Latency only affects Local mode.** `EventRouter.h:70` and siblings are a strict either/or: `if (mode == Mode::WiFi) network.send…()`, else the local handler. In WiFi mode the phone renders no audio at all — touch → TCP → server → virtual MIDI → DAW. A cheap Android phone sends the same bytes as fast as a Pixel.

_Not dodged by WiFi mode:_ Android's touch-to-event pipeline is ~a frame slower than iOS in both modes. ~10ms, not ~100ms — the tax every Android music app pays.

## Phases

| Phase | Work | Est. |
|---|---|---|
| **0** | Android Studio exporter, rename `.mm`, stub haptics + accel. Full UI + synth should run on an emulator immediately. | ~½ day |
| **1** | Multicast lock, permissions, manifest. **Promoted ahead of audio** — it blocks the pack. | 2–4 days |
| **2** | Audio: Oboe, native buffer/rate, latency measurement on real hardware. | 2–3 days |
| **3** | Sensors + haptics via JNI. | 1–2 days |
| **4** | Render perf, layouts, device testing, Play Store (AAB, targetSdk 35+, data safety form — $25 one-time vs Apple's $99/yr). | 1–2 weeks |

---

# WS-5 — Local-Audio Capability Gate

Gates who may set `EventRouter::Mode::Local`. Android-only in effect; iOS is a one-line `Tier::Full` stub.

**Not required by the sequencer pack** (no local audio there). This is about protecting the *free* synth experience on weak Android hardware.

## Detection — three signals, cheapest to most truthful

**1. Static feature flags** (JNI, ~40 lines, free)
- `PackageManager.hasSystemFeature(FEATURE_AUDIO_PRO)` — strict tier, <20ms round trip / <10ms continuous. Gold-standard green light.
- `FEATURE_AUDIO_LOW_LATENCY` — weaker claim (<45ms).
- `AudioManager.PROPERTY_OUTPUT_FRAMES_PER_BUFFER` / `PROPERTY_OUTPUT_SAMPLE_RATE` — 96–256 frames @48k healthy; 960+ = slow path.

**2. Measured latency after stream open** — more truthful than flags. Check whether Oboe got AAudio or fell back to OpenSL ES, and whether the requested `PerformanceMode::LowLatency` was *granted* (we request; the system may decline). JUCE's `getOutputLatencyInSamples()` is approximate on Android — verify against Oboe directly, don't trust it.

**3. DSP headroom — do not skip.** Latency and CPU are *separate* failure modes; a cheap ARM core can have fine latency and still glitch on 32 voices through the ladder filter. Pure JUCE, no JNI: time `getNextAudioBlock` against the block's wall-clock budget. Consistently >70% = crackle regardless of flags. ~20 lines, and the check most likely to catch a genuinely bad experience.

## Tiering — soft-gate, never hard-block

Hard-blocking is wrong in both directions: many devices without `FEATURE_AUDIO_PRO` play fine; some claiming low latency choke on the voice count. On a free growth-first app a false negative is a lost user with no recourse.

- **Green** (`AUDIO_PRO`, or <30ms + healthy headroom) → Local enabled, default on, user never sees any of this.
- **Amber** (low-latency flag, or 30–60ms) → Local enabled but **not default**. Inline note: _"Your device reports ~42ms audio latency — WiFi mode will feel tighter."_
- **Red** (no flags, >60ms, xruns, DSP overload) → Local off, toggle disabled, tap-to-reveal explainer + **"Enable anyway"** escape hatch one level down. **Always show the measured number** — "48ms measured" reads as engineering; "your phone isn't supported" reads as an insult and becomes a one-star review.

## Wiring notes

- `MainComponent.mm:351` currently defaults first launch to **Local** (`getIntValue("mode", 1)`). On Android that must become tier-dependent, and the gate must apply **after** the restore at line 352 — otherwise a saved `Local` sails straight past it.
- Probe once after device open, cache in `ApplicationProperties`, re-run only on OS/device change. **Never probe on the audio thread.**
- **Bluetooth caveat:** A2DP adds 100–200ms regardless of tier. Probing over BT permanently red-flags a good phone. Make the verdict route-aware, or refuse to cache a red recorded over Bluetooth. Otherwise expect confusing bug reports.
- Free bonus: Settings readout — `AAudio · 48kHz · 96 frames · 12.4ms · Pro Audio ✓`. Costs nothing, makes support email answerable.

**Deferred:** a *Lite* local mode for amber/red — 8 voices, FX bypassed, bigger buffer. Converts reds into playable ambers instead of turning users away. Defer past first Android ship.

**Est:** 1–2 days for v1 (flags + DSP load + tiered UI, skipping Oboe introspection). Build alongside WS-4 Phase 0 — before that there is no Android target and it would be an untestable iOS stub.

---

# WS-6 — Carried-Over Backlog

From auto-memory; none were v2 scope. **Needs triage — not yet committed.**

**Features previously deferred**
- **Sequencer + Arpeggiator pages** — **superseded by WS-1.** The arp specifically is still open, and would be natural pack content #4.
- **Rose-Chord performance modes** from the Orchid: Strum / Arp / Pattern / Harp / Slop.
- **Preset bank / patch library** — v2 ships a single live patch. Pattern save/recall for the sequencers is a related need (see WS-2 note on state not persisting in the DAW project).
- **Sub-bass engine**, **AUv3 wrapper** — long-standing parking lot.
- **LinkKit tempo sync** — now has a concrete justification (WS-2 sync roadmap).

**Cleanup / debt**
- Remove oversized `wood1.jpg` (6711×4474) from Resources.
- Remove unused `circuit-relic.png` (2.8 MB) and `circuit1.png` (3.0 MB) — both rejected as substrate.
- Delete dead `GlassPurpleButtonLookAndFeel` in `RoseChordPage.h` — defined but unreferenced.
- Dual M-Pad octave buttons are cramped.
- **Carry the local JUCE patch forward — there is no upstream fix coming.** Verified 2026-08-24 against JUCE 9.0.1: both `jassertfalse` sites in `getSampleRateFromAudioQueue` (`juce_Audio_ios.cpp`) are unchanged. The patch at `/Applications/JUCE/modules/juce_audio_devices/native/juce_Audio_ios.cpp` (both sites marked `LOCAL PATCH (MidiMorf)`) must be **re-applied after every JUCE upgrade**. Treat as a recurring tax, not a pending revert.
- Synth page still uses global theme tokens (`panelSubstrate*`) instead of a `synthPagePalette`.
- `LiveControl/CLAUDE.md` says discovery is Bonjour; the code uses `juce::NetworkServiceDiscovery` (UDP broadcast). Same mislabel as MidiMorf's own docs — worth correcting in both, since it behaves differently on Windows and Android than real Bonjour would.

**Possible**
- Runtime theme picker on About — themes 1/2/3 already exist behind `MIDIMORF_THEME`.

---

## JUCE Upgrade Assessment

_Checked 2026-08-24. Local: **8.0.12**. Latest: **9.0.1** (released 2026-08-10). Path: 8.0.13 → 8.0.14 → 8.0.15 → 9.0.0 → 9.0.1._

**Breaking-change exposure: zero.** Every JUCE 9 breaking change was checked against the source and none apply — the codebase uses no `Drawable`, no SVG, no `OpenGLContext`, no `WebBrowserComponent`, no `DrawableShape`. Windows multi-touch and Linux EGL changes are irrelevant.

**The one real payoff: the software renderer got faster in JUCE 9.** That lands directly on the biggest unsolved risk in WS-4 — 5-layer glassmorphism plus a 16-lane slider bank repainting on Android's software renderer. This is the single most valuable thing in JUCE 9 for this project.

**Two things the upgrade does _not_ fix:**

| Assumption | Reality on 9.0.1 |
|---|---|
| StoreKit 2 might arrive and settle WS-3 open question 4 | **No.** `juce_product_unlocking` still uses `SKPaymentQueue` / `SKProductsRequest` / `SKPaymentTransaction` exclusively. No StoreKit 2 anywhere. The review-acceptance question is **version-independent** — upgrading buys nothing here. |
| The iPad `AudioQueue` asserts would be fixed upstream | **No.** Both `jassertfalse` sites remain in `getSampleRateFromAudioQueue`. The local patch is permanent. |

**Also worth testing if upgrading:** JUCE 9 ships a **new macOS CoreAudio implementation**, which touches the server's audio path.

**Recommendation: don't upgrade now.** Do it immediately *before* the Android work starts — that is where the render-performance benefit lands and where everything gets re-tested anyway. Upgrading between now and shipping the pack on iOS is pure risk against a codebase that already has a launched build. 9.0.1 is also only two weeks old, and the `Drawable`-no-longer-a-`Component` change is large enough that other people's bug reports are still arriving.

---

## Prior Art — `LiveControl` (harvest, then archive)

`../LiveControl/` is a dormant proof-of-concept (last touched 2026-03-12, 1,508 lines) that already solved the Ableton integration problem end to end. **It is a de-risking asset, not a base to build on.**

| Piece | Lines | Value |
|---|---|---|
| `LiveControlScript/` (Python remote script) | ~500 | **High** — the hard part, solved, portable as-is |
| `LiveControlServer/` (relay) | ~530 | Medium — the dual-listener + `AbletonAcceptThread` pattern is liftable |
| `LiveControlApp/` (iOS) | ~470 | **Near zero** — a stub, and built on the portrait-plus-90°-rotation approach v2 abandoned |

**What it proves:** a `_Framework.ControlSurface` subclass polling a non-blocking socket from `update_display()` (~10Hz) works. `SocketConnection.py` already handles the fiddly parts — non-blocking `connect_ex` with EINPROGRESS, `SO_ERROR` completion check, ~1s retry, and a 64KB send-buffer cap so a dead phone can't balloon memory inside Live.

**What it does not have:** any note editing whatsoever. It does transport, clip-fire, scene-fire, and mixer. `create_clip`, `add_new_notes`, and `get_notes_extended` appear nowhere. Phases 2–5 of its own plan are entirely unchecked.

**Decision:** leave it on disk untouched. It does not expire, and it becomes relevant only if the clip editor is ever revived (see below).

---

## Rejected Directions

Retained with reasoning so they don't get re-proposed.

### Paid VST/AU plugins as the monetization vehicle — **rejected 2026-08-24**

The 2026-08-21 plan built the business model on paid desktop plugins sold direct (0% platform cut), with the plugin's presence acting as the licence. Superseded because:

- **The gate belongs on the phone, not the plugin.** Once the phone holds the entitlement, the plugin has no monetization role left — it is only an engine host, and the server already is one.
- **Sample accuracy was overweighted.** Record Quantization neutralizes input jitter for clip capture, and MIDI-clock-grade timing is the historical standard for slaved sequencers.
- **Cost is wildly asymmetric.** VST3 + AU + Windows + signing + notarization + installers + plugin-scanning support tickets, versus adding classes to an app that already ships.
- **0% was illusory.** Direct sale avoids the platform cut but requires building and operating a licensing system, store, and support flow. 15% buys all of that plus the trust signal.

**Genuinely lost, both survivable:** per-track plugin instances (replaced by MIDI-channel splitting, already a product convention) and state saving into the DAW project (replaced by pattern save/recall on the phone, wanted regardless).

**Revisit only if** users complain about routing setup, or ask for project-saved state — as an optimization evaluated after conversion data exists, not as a prerequisite paid for before finding out.

### Phone-side sequencer clock over WiFi — **rejected 2026-08-24**

At 120bpm a 16th step is 125ms. WiFi gives 1–5ms typically but iOS throws 20–50ms spikes from power management and background scans — a quarter of a step, audibly late, unpredictably. A free-running phone clock also has no relationship to project tempo and drifts continuously. Ableton Link plus timestamped early-send would work, but it is a lot of machinery to approximate what the server gets from a clock port.

### Clip editor / Ableton Remote Script — **demoted to parking lot 2026-08-24**

The original pitch was *"sequence a drum beat in a grid on your phone and it shows up in the Ableton clip."*

**The drum grid in WS-1 delivers that experience without any Ableton-specific code.** Build the pattern on the phone, it plays in sync, record-arm captures it into a clip. Works in every DAW.

What a Remote Script would still uniquely add is *editing notes that already exist* and *writing without performing* — a narrower need than it first appears, and it carries Ableton-only scope, install friction (drop a folder in Remote Scripts, restart Live, select it in Preferences), and permanent version fragility. **A VST/AU plugin cannot write host clips in any format** — no such API exists — so a Remote Script is the only real vehicle, which is exactly why it is expensive.

Parked. `LiveControlScript/` is the head start if it is ever revived.

---

## Recommended Sequencing

0. ~~Timing spike~~ — **done 2026-08-24, passed decisively.** See WS-2.
1. **WS-1 step sequencer page + WS-2 engine + MIDI clock in.** The vertical slice. Get one sequencer playing in sync end to end before building the other two.
2. **WS-3 IAP + entitlement.** Verify the StoreKit 1 question early — it gates the whole revenue path and the answer might cost a shim.
3. **WS-1 drum grid.** Second engine, reusing all the clock and transport plumbing.
4. **Ship the pack.** Two sequencers is a defensible pack; three is better but should not block revenue.
5. **WS-1 physics sequencer.** Pack content #3, and the most video-friendly thing on the list — the piece most likely to produce the creator moment the free strategy is betting on.
6. **WS-4 / WS-5 Android.** Independent; can run in parallel. The pack itself is tier-safe, so a sequencer-first Android build is a legitimate shortcut.
7. **Ableton Link.** Once someone asks for tighter bar alignment.

---

## Open Questions

1. **Is the third sequencer the physics one, or an arp?** The arp would pair naturally with the existing Morf-Chord and Rose-Chord pages, taking chord input from them. Physics is more distinctive; an arp is more useful.
2. **Ship at two sequencers or hold for three?** Two is defensible and gets revenue moving sooner.
3. **Exact price** within $14.99–19.99.
4. **Does `juce_product_unlocking`'s StoreKit 1 backend still pass App Review?** Blocking for WS-3.
5. **Pattern save/recall** — in the pack v1, or v1.1? It partly substitutes for the DAW project-state persistence the server can't provide.
6. Android: separate Play listing, or launch alongside an iOS point release? Does a **sequencer-pack-first** Android build make sense ahead of full parity?
7. Is the **Lite local mode** (WS-5) worth building for first Android ship?
8. Which WS-6 items are actually in scope vs. parked again?
9. Version number — v3.0.0 assumed.

---

## Parking Lot

- **Clip editor / Ableton Remote Script** — see Rejected Directions. `LiveControlScript/` is the head start.
- **Paid desktop plugins** — revisit only on user demand for per-track routing or project-saved state.
- **Ratchets, per-step probability, pattern chaining** — step sequencer v1.1.
- **Euclidean generation** — as a mode inside the drum grid, not a separate product.
- **Demo/limited sequencer state** — first lever to pull if pack conversion underperforms.
- **Cross-platform entitlement** (iOS purchase honoured on Android) — requires an account system; deliberately avoided.
- _JB has additional items to capture._
