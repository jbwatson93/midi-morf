# MidiMorf Groovebox — Plan

_Written 2026-10-29. Replaces the business model in `midi-morf-next-plan.md` (Creative Sequencer Pack). Builds on branch `local-sequencers`._

## What we're building

MidiMorf becomes a **4-track groovebox**. Each track pairs an **input** (the way you play) with a **sound source** (what it sounds like). For example:

| Track | Input | Source |
|---|---|---|
| 1 | Morf-Chord | Wavetable "Warm Pad" |
| 2 | Step Sequencer | Wavetable "Acid Bass" |
| 3 | Slice Sequencer | Sampler, Amen Break |
| 4 | Drum Grid | Drum Synth "Boom Bap Kit" |

A **Session** screen holds the clips: tracks are rows, and scenes flow left to right. A **Mixer** that drags up from the bottom of Session balances the tracks. Every sound source has presets. **Morf FX** becomes punch-in effects on the master bus.

**On iPad, all four tracks can be played at once on one screen.** A **Perform view** lays out the four tracks' controllers in a 2×2 grid. On iPhone, you play one track at a time and swap between them.

## Business model

**Decided 2026-10-30: the whole app is a paid download, about $4.99, on the App Store and Google Play. No in-app purchase, no unlock, nothing locked.** Everything in this plan, including Session, the editors, recording and the mixer, is in the app for everyone who buys it.

- **Why:** an unlock means lock badges, a purchase flow, Restore Purchases and two versions of every screen. All of that is friction. The goal is an app people see JB play and want, and one price says that plainly.
- **Earlier plans are replaced:** the free app with a ~$4.99 Groovebox unlock (2026-10-29), and before that the Creative Sequencer Pack. `ClockedSequencer::enabled` and the `PackEntitlement` message only existed for the pack, so take them out.
- **People who already have the free app** keep it and get the update; the App Store doesn't charge existing users when the price changes.
- **Join Apple's Small Business Program** (15% commission instead of 30%). Google Play's service fee is also 15% on the first $1M a year.
- **A demo project** still ships as the first thing people open, so the app sounds good in the first ten seconds.

## Architecture

### Concepts

- **Source:** anything that makes sound from MIDI. A track can use any source type, and the same type can appear on several tracks.
- **Input:** a controller page, a sequencer, or nothing (a track that only plays clips).
- **Track:** input → source → level/pan/sends → master. There are always 4.
- **Clip:** what one track plays in one scene.
  - On a sequencer track, it's a step or slice pattern.
  - On a controller track, it's a recorded MIDI loop.
- **Scene:** one row of clips across the 4 tracks.
- **Project:** tracks, each source's full sound, clips, scenes and tempo, saved as one file.

### The Source interface

```cpp
class SoundSource
{
public:
    virtual ~SoundSource() = default;
    virtual juce::String typeId() const = 0;            // "wavetable", "sampler", "drums", "external"
    virtual void prepare (double sampleRate, int blockSize) = 0;
    virtual void render (juce::AudioBuffer<float>&, const juce::MidiBuffer&, int start, int num) = 0;
    virtual void allNotesOff() = 0;
    virtual juce::ValueTree getState() const = 0;      // presets, projects and autosave all use these two
    virtual void setState (const juce::ValueTree&) = 0;
};
```

- `getState()`/`setState()` is the **single save/load path for presets, projects and autosave**.
- It replaces the key-by-key `saveState(props)` on the synth page.

### What changes in today's code

| Today | Becomes |
|---|---|
| One global `WavetableEngine` in `AudioEngine` | A `TrackRack` holding 4 `SoundSource`s. Each `WavetableEngine` instance has a voice cap of 8–12, and the wavetable data is shared between instances. |
| `EventRouter` sends every controller to one `PhoneMidiSink` | Each controller goes to its **track's** sink. One `PhoneMidiSink` per track. |
| `LocalSequencers` owns one step engine and one slice engine | Sequencer engines become **per-track inputs**, so two step-sequencer tracks are possible. The clock (sample-accurate tick splitting) becomes the session clock. |
| `BreakPlayer` is a special case for the slicer | It becomes the **Sampler's** slice mode. |
| `PageContainer` holds exactly one of each page, full screen | Pages are **created per track**, and each is bound to its track's sink. So there can be two Morf-Chords on two tracks, and any page can draw in a quarter of an iPad screen. |
| Global WiFi/Local mode | **External (WiFi → DAW)** becomes a per-track source. Keep the global toggle until it's replaced. |
| Patch saved key by key in `ApplicationProperties` | Autosaved project file in `Documents/Projects/` |

### Shared server files

- The sequencer engines stay byte-identical between app and server; only `SequencerPlatform.h` differs.
- Any new protocol bytes for the External source must be mirrored in both copies of `MidiMorfProtocol.h`.

## Screens (from JB's sketch, 2026-10-29)

These screens build on the real app: the per-page dark palettes, and the wooden rail down the right edge that holds Menu, Synth and Fave today.

### Session

- **Grid:** 4 rows (tracks) × scenes running left to right. Scrolls sideways when there are more scenes than fit.
- **Scene launch:** a ▶ at the top of each scene column.
- **Clip cells:**
  - **+** when empty. On a sequencer track it makes a new pattern; on a controller track it arms recording.
  - The clip name and ▶ when filled. Tap to launch it on the next bar; long-press to edit it on its page.
- **Track column** to the right of the grid, lined up with the rows: a compact two-line "input / source" label (e.g. "Slicer / Amen"). Tapping it opens the pickers.
- **Each track has its own colour**, used on the row, the mixer strip and the rail button.
- **Mixer** below the grid, with a drag handle and three snap points:
  - **Hidden.**
  - **Compact:** per track, a **Sound ✎** button showing the preset name, then fader, pan and send.
  - **Full:** Sound ✎, insert FX, pan plus 2 sends, fader, mute and solo, with master in the right column. The grid shrinks to a coloured map you can still tap.
- ~~Unlock rectangle at the bottom right~~ — dropped with the unlock (2026-10-30). The space is free for something else (tempo, or the master level).

### The wooden rail

| Where | Top | Middle | Bottom |
|---|---|---|---|
| **Session** | Menu (same spot as on every page) | **1–4**: jump to that track's playing page | Global **Play/Stop** (long-press for BPM and tap) |
| **Controller pages** | Menu | **T1–T4 badge** (which track this page plays; tap to switch track or go back to Session), **Sound** (replaces Synth; edits this track's source), Fave | **● Rec** for clip recording |

**Sound ✎ (mixer strip) and Sound (rail) open the same thing:** the track's source editor. Leaving the editor returns you to wherever you came from.

### Morf FX = punch-in effects on the master bus

- **The 4 FX buttons are momentary:** hold to apply, let go for dry. Hold several to stack them, and double-tap to latch.
- **XY pad:** shapes the held effect, with X and Y as its two main parameters. The X/Y sliders stay for fine control.
- **Tempo-synced** to the global clock.
- **Effects to choose from** (assigned to buttons through the existing slot config):

  | Effect | X | Y |
  |---|---|---|
  | Filter sweep | cutoff (LP ← → HP) | resonance |
  | Stutter / repeat | division 1/4 → 1/32 | mix |
  | Tape stop | stop speed | — |
  | Delay throw | synced time | feedback |
  | Reverb wash / freeze | size | mix |
  | Bitcrush | bits | sample rate |
  | Trance gate | rate | depth |
  | Vinyl brake / pitch | pitch drop | speed |

  Default buttons: Filter, Stutter, Tape stop, Delay throw.
- **Signal chain:** after the mixer, before the master limiter, so it hits all 4 tracks.
- **WiFi mode is unchanged:** it still sends CC 20/21 and the button messages to the DAW. Local mode drives the built-in master FX, the same idea as Transport's LOCAL/DAW switch.

## Phases

Each phase is its own branch, merged to main when JB has played it. The "(free)" / "(paid)" on the earlier phase names are from the unlock plan and no longer mean anything: it's all one paid app now.

### Phase 0 — Land what exists

- [ ] Play `local-sequencers` on the phone: local clock, breaks, the global transport.
- [ ] **Set up a git remote.** There's none today, so branches can't be pushed and PRs can't be opened. A free private GitHub repo is enough.
- [ ] Merge into main in order: `wavetable-synth` → `local-sequencers`.

### Phase 1 — Track rack (free)

> **Done 2026-10-29** on `phase1-track-rack`, plus a layout change JB asked for after trying it.
>
> - **Track-first layout.** The rail is just ⌂ Home, tracks 1–4 and ▶/■.
>   - Tap a track for its instrument; tap it again for its sound.
>   - A header strip says what you're looking at, e.g. "T2 · Step-Seq → Bass".
> - **Home** sets each track's instrument, sound and volume, and links to Transport, Morf FX, Settings and About.
> - **Removed:** the Menu, Sound and Fave buttons and the pop-up track picker. They were too confusing.
> - **One instrument per track, one sound per track.** Each track has its own synth. The break player can be on only one track.
> - **Project file:** `Documents/Projects/Current.midimorf`.
> - **Home grows into the Session screen.** Its rows become Session's track column, and the rail's 1–4 replace the sketch's track buttons.

- [ ] `SoundSource` interface. `WavetableEngine` implements it, and `getState`/`setState` replace the page's key-by-key save.
- [ ] `TrackRack`: 4 sources, mixed to master, rendering inside the existing sample-accurate clock.
- [ ] Multiple wavetable instances with a voice cap. **Measure CPU on the oldest iPhone you support** with 4 busy tracks.
- [ ] `EventRouter` routes input → track. Each track has its own sink and its own all-notes-off.
- [ ] **Track setup:** pick the input and source for each track (until Session exists, a simple picker reached from the menu; later, the Session track column).
- [ ] Synth page edits **the selected track's** instance, with a track switcher at the top.
- [ ] **Rail changes on controller pages:** a T1–T4 badge, and Synth becomes Sound (this track's source).
- [ ] Pages stop being singletons. Each page instance takes the track it plays and gets no global state. This is the groundwork for the iPad Perform view, so don't skip it.
- [ ] Volume knob on each source page.
- [ ] Autosaved project file (replaces the per-key synth save). Migrate the existing saved patch into track 1.

**Done when:** Morf-Chord plays one wavetable sound while the step sequencer plays a different one, and both survive a relaunch.

### Phase 1.5 — Punch-in FX (free)

- [ ] Master bus in `TrackRack`: the sum of the tracks → punch FX → limiter. Today `MasterFx` only covers the synth and the breaks bypass it.
- [ ] `PunchFx` engine: the 8 effects, tempo-synced from the global clock, with a short fade in and out so punching never clicks.
- [ ] Morf FX page in Local mode: momentary/latch buttons, XY sets the held effect's parameters, and a picker to choose which effect sits on each button.
- [ ] WiFi mode still sends CCs to the DAW, as today.

**Done when:** you can hold Stutter over a playing beat and it locks to the clock, and letting go drops cleanly back to dry.

### Phase 2 — Presets (free)

- [ ] `PresetManager`: factory presets bundled read-only, plus user presets in `Documents/Presets/<SourceType>/`. Preset files are visible in the Files app.
- [ ] **Preset bar** at the top of each source editor: ◀ name ▶, tap the name to browse by category, **SAVE**, and a "modified" dot.
- [ ] CLEAR becomes "load the Init preset".
- [ ] Picking a source in track setup can also pick a preset.
- [ ] **20–30 factory Wavetable presets** in Bass, Lead, Pad, Keys, Pluck and FX. I'll draft them; JB tunes them by ear.

**Done when:** you can browse, load, tweak and save presets on any track, and a saved preset shows up in Files.

### Phase 2.5 — iPad Perform view (free)

- [ ] **Perform view:** a 2×2 grid. Each quarter shows that track's input page, playable, with a slim header (track name, source, preset, mute).
- [ ] **Compact layouts:** every controller page has to work at quarter size. On iPad landscape a quarter is about 4:3, while the phone is about 2.2:1.
  - The pages with rotation transforms (Morf-Pad keys, Strum-Chord, Transport) need the most work.
  - Set a minimum touch size, so chord pads and keys stay playable at quarter size.
- [ ] **Multi-touch across quarters:** four hands, or two people, playing four tracks. Check that each quarter tracks its own touches and nothing gets stolen at the borders.
- [ ] Tap a quarter's header to make it full screen, and tap again to go back.
- [ ] Use the iPad's extra space: Session and Mixer can sit beside the Perform view in later phases.
- [ ] iPhone: no Perform view. It keeps the one-page-at-a-time model with the track switcher.
- [ ] Test on a real iPad. Remember the iPad audio startup fixes in memory (the sample-rate patch and `UIRequiresFullScreen`).

**Done when:** on an iPad, Morf-Chord, the step sequencer, the slicer and a Morf-Pad can all be played together, with no dropped touches and no audio dropouts.

### Phase 3 — Sampler (free)

- [ ] Generalise `BreakPlayer` into a **Sampler** source with three modes:
  - **Slice:** today's breaks.
  - **Chromatic:** one sample played across the keys, with ADSR.
  - **One-shot.**
- [ ] The slice sequencer drives a Sampler track instead of its built-in player.
- [ ] Import .wav and the built-in breaks carry over.
- [ ] Sampler editor page, with a waveform and slice markers.
- [ ] Factory presets: the Amen and the 9 generated breaks, plus some chromatic instruments rendered from our own synths.
- [ ] **Licensing:** only sounds we made or that are cleared. The All The Breaks pack must never be bundled.

### Phase 4 — Drum synth + Drum Grid (free)

- [x] **Drum Synth** source: 8 voices on fixed notes (kick, snare, clap, closed hat, open hat, tom, rim, perc), 2–4 knobs each. It reuses the synthesis already in `BreakLibrary`.
- [x] **Drum Grid** input: 8 lanes × 16 steps, with per-step velocity and Euclidean fill as a mode. This is the sequencer from the old plan's WS-1, now free.
- [x] Factory kits by genre (Boom Bap, Jungle, House, Trap, Lo-fi...).

Done: `DrumSynth.h` renders each drum from Tune / Decay / Tone / Level (plus kit Drive and Dust) into three one-shot takes and plays them back, GM notes, hats choke. The Drum Grid (`DrumGridPage.h`) runs on the local clock with sample-exact swing, loads the built-in break patterns as beats, and saves its pattern in the project. Kits: 808, Boom Bap, Jungle, House, Trap, Lo-fi, Disco, Techno. Not yet: saving your own kits; the Drum Grid in WiFi mode (it always plays the phone's sounds).

### Phase 5 — Session + projects

- [x] **Clips** (`SessionModel.h`). A clip on a sequencer track is that sequencer's own pattern (Step-Seq steps and length, Slice-Seq slices, Drum Grid lanes). Key, scale, rate and the slice grid stay with the track. *Changed from "a list of notes for everything":* a note list would lose the step settings. Phase 6 adds note clips for controller tracks, and the editors convert patterns to and from notes.
- [x] **Launching on the bar:** `LocalSequencers` swaps a queued pattern in on the exact sample of the next bar line, and a queued stop lands on the bar too. On DAW sync the page sends it at once.
- [x] **Session screen** (`SessionPage.h`, where Home was): tracks as rows, 8 scenes left to right (scrolls), ▶ per scene, the track column (instrument, sound, volume, ■). Tap a clip to launch, tap the playing one to edit it, hold for Edit / Duplicate / Delete, "+" copies the track's current pattern into a new clip.
- [x] Every pattern is saved, as clips in the project. A project from before Session gets its current patterns as Pattern 1.
- [x] **Projects** (`ProjectBrowser.h`): New (a starter project), Save As, Duplicate, Open, Delete. A named project autosaves to its own file as well as the working copy.
- [x] **Compact mixer:** a CLIPS / MIXER switch on Session (instead of a drag handle, which doesn't fit an iPhone in landscape). Per track: its sound (tap to edit), pan (balance law, centre exactly unity) and mute; volume stays in the track column. Sends wait for the send buses in Phase 7.
- [x] Launch quantize: bar / beat / now, saved with the project.
- [ ] ~~Built-in demo project~~ — replaced (2026-10-30) by a **first-run walkthrough** that has the player make their own beat: see Phase 7.5.

### Phase 6 — Clip recording + editors

- [x] **Recording** (`LocalSequencers`, `NoteClip.h`): on a controller track, tap an empty Session slot (or the rail's ● on the track's page, or on the Drums pads) to record what you play into a looping clip of 1/2/4/8 bars. Notes are captured on the audio thread as they reach the track, to within one audio block.
- [x] Overdub in passes (the clip plays underneath while you record the next pass), undo the last pass, a one-bar count-in when the clock is stopped, a metronome, and optional quantize-on-record (1/16). Settings are in Session's REC button.
- [x] Note clips play on their own per-track player, sample-exact and looping, launched like the other clips.
- [x] **Piano roll** (`NoteEditorPage.h`): add, move, resize and delete notes, a velocity lane, snap (1/4…1/32 or off), quantize, zoom and scroll, the playhead.
- [x] **Drum editor**: the same page with a row per drum when the track plays the drum synth. Hits can sit off the grid.
- [ ] Not yet: converting a Drum Grid pattern into a note clip for off-grid editing; a scale highlight in the piano roll; copy/paste of notes.

### Phase 7 — Mixer

- [x] Per track: level, pan, mute, **solo**, and 2 sends (Delay, Reverb) on shared send buses (`MixerFx.h`: a tempo-synced ping-pong delay — 1/4, dotted 1/8, 1/8, 1/16 — and a reverb). Sends are post-fader.
- [x] **Per-track insert:** Filter (one knob, low-pass left / high-pass right), Drive and Crush. *Changed from "reusing MasterFx":* that chain is the synth's own distortion → delay → reverb, which fits the send buses better than an insert; delay and reverb live on the buses instead.
- [x] Master level before the limiter (already there), and the bus settings: delay time and feedback, reverb size.
- [x] The full mixer is Session's MIXER view (no separate page): sound, six knobs, M and S per track, the master section across the top.
- Untouched, the mix is bit-exact; inserts and buses cost nothing until used (all of it busy is under 1% of a Mac core).

### Phase 7.5 — First-run walkthrough + more breaks

- [x] **Walkthrough** on first launch (skippable, replayable from Settings): coach marks over the real screens, each step waiting for the player to do the thing — not a slideshow. Make a beat on the Drum Grid → press Play → a bassline on Step-Seq → record a chord take on Morf-Chord → launch it from Session → Save As. Ends with their own project, named.
  Done (`Walkthrough.h`), JB's flow (2026-10-30): on an empty project, pick Step-Seq + Synth for track 1, pick a synth preset, switch steps on and drag the note sliders, play; then Breaker + Sampler (the Amen) on track 2, and glitch it: move slices in the grid, the stutter ribbon, the pitch ribbon, the looper (with RND). 19 steps. An untitled project in progress is kept as "Before the tour"; Settings → Take the tour replays it.
- [x] Slice-Seq is called **Breaker** in the app (the rail says "Break").
- [ ] **Live remix lane** (JB, 2026-10-30), monome-mlr style: a sample laid across a row of 16 positions with the playhead walking along it; tap a position to jump playback there (quantized), hold two to loop between them, release to fall back in time. Builds on the Sampler's slice playback.
- [ ] **More of JB's recreated breaks** bundled beside the Amen (`BreakLibrary::recorded()`). Never the "All The Breaks" pack.

### Phase 8 — iOS release ($4.99)

- [ ] Set the price in App Store Connect. No StoreKit, no unlock code.
- [ ] App Store listing: a screen recording of JB playing the groovebox.
- [ ] retro-relic.com update.
- [ ] Mac server release if any protocol bytes changed (the External source).

### Phase 9 — Android ($4.99 on Google Play)

JUCE builds for Android (Projucer's Android exporter plus Android Studio, with Oboe for low-latency audio), and most of the app is plain C++ already. What needs doing:

- [ ] **Android exporter** in the `.jucer`: landscape only, minimum SDK, microphone permission, and the same resources.
- [ ] **The Objective-C++ files:** `Main.mm`, `MainComponent.mm` and `Haptics.mm` only compile on Apple. Move the iOS parts (UIKit, AVAudioSession mic permission, kiosk/status bar) behind small platform files and keep the shared code in `.cpp`.
- [ ] **Tilt:** `AccelerometerManager.h` uses CoreMotion. Android needs its own sensor reader, or tilt is off at first.
- [ ] **Haptics:** Android vibration, or none at first.
- [ ] **Files:** Documents/Projects, Recordings and Presets map to app storage. Importing audio goes through Android's file picker.
- [ ] **WiFi mode:** server discovery already uses JUCE's cross-platform `NetworkServiceDiscovery`, so it should just work. Check it on a real device.
- [ ] **Audio latency on test phones:** Android varies a lot by device. Test on at least one cheap phone and one recent Pixel or Samsung.
- [ ] Google Play Console: developer account (one-time $25), paid app at $4.99, store listing.

## Open questions

1. **Wireframes:** JB's Session sketch is folded in (see *Screens*). The iPad Perform view and the source editors still need sketches.
2. **Track count:** fixed at 4 for now. Allowing 6–8 later is a CPU question.
3. **Scene count:** 8 visible, with scrolling for more?
4. **Controller pages without a track:** they play the selected track? Or is a controller with no track silent?
5. **Clip length limit** for recorded MIDI: 8 bars?
6. **Oldest supported iPhone.** This sets the CPU budget and the voice caps.
7. ~~Android: same model through Play Billing. When?~~ Paid app on Google Play, Phase 9 (2026-10-30).
8. ~~Is the iPad Perform view free or paid?~~ Moot: the whole app is paid (2026-10-30).
9. **Perform view on large iPhones:** a 2-track split (one on top of the other) might fit on Pro Max models. Try it after the iPad version.

## Risks

- **CPU:** 4 sources plus FX on older phones. Mitigate with voice caps, shared tables and measuring early in Phase 1.
- **Routing rewrite:** `EventRouter` and the handlers are used by every controller. Phase 1 should keep each controller's notes exactly the same and only change where they go.
- **Scope:** this is several months of work.
- **Paid upfront means fewer downloads** than a free app would get. The bet is that videos of JB playing it sell it.
