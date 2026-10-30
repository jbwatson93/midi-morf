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

**The app is a free download. A one-time ~$4.99 "Groovebox" in-app purchase unlocks arranging.** Free is for playing, paid is for arranging.

| Free (everyone) | Groovebox unlock |
|---|---|
| All 7 controllers | **Session:** scenes 2 and up, multiple patterns per track |
| **Session with one scene**: launch, edit, and pick each track's input and source | Recording controller playing into clips |
| All 4 tracks playing at once | **Mixer:** level, pan, mute/solo, sends, per-track FX |
| Every source + its editor + all presets | Saving multiple projects |
| Sequencers on the phone's clock, one pattern each | |
| Global transport (sync, BPM, tap) | |
| **Morf FX punch-in effects** on the master bus | |
| Volume on each source page | |
| iPad Perform view: all 4 tracks playable on one screen (see open question 8) | |
| WiFi/DAW mode + the free Mac server | |

- **Rule:** nothing that has shipped free ever becomes paid.
- **The sequencers are now free.** `ClockedSequencer::enabled` and the `PackEntitlement` message were only there for the pack, so take them out.
- **One free scene instead of a locked preview** (JB's idea). Free users get real use out of Session. Scenes 2 and up and the mixer show a lock, and the **Unlock** button lives in a fixed rectangle at the bottom right of Session, not in a pop-up. A demo project can still ship as the first thing people open.
- **No server and no receipt checks.** The app stores the unlock locally, so it works offline. Settings gets a **Restore Purchases** button. Family Sharing is on.
- **Join Apple's Small Business Program** (15% commission instead of 30%).

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
- **Unlock rectangle** at the bottom right, under the track column.

### The wooden rail

| Where | Top | Middle | Bottom |
|---|---|---|---|
| **Session** | Menu (same spot as on every page) | **1–4**: jump to that track's playing page | Global **Play/Stop** (long-press for BPM and tap) |
| **Controller pages** | Menu | **T1–T4 badge** (which track this page plays; tap to switch track or go back to Session), **Sound** (replaces Synth; edits this track's source), Fave | **● Rec** for clip recording (paid) |

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

Phases 1–4 are entirely free-side, so they can ship as free updates while the paid half is built. Each phase is its own branch and PR.

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

- [ ] **Drum Synth** source: 8 voices on fixed notes (kick, snare, clap, closed hat, open hat, tom, rim, perc), 2–4 knobs each. It reuses the synthesis already in `BreakLibrary`.
- [ ] **Drum Grid** input: 8 lanes × 16 steps, with per-step velocity and Euclidean fill as a mode. This is the sequencer from the old plan's WS-1, now free.
- [ ] Factory kits by genre (Boom Bap, Jungle, House, Trap, Lo-fi...).

### Phase 5 — Session + projects (paid)

- [ ] Clip model. A sequencer clip stores its whole pattern, so patterns are finally saved.
- [ ] **Session screen** as in *Screens*: tracks as rows, scenes left to right, the track column, the 1–4 / Play rail, launch quantize (1 bar / 1 beat / off).
- [ ] Compact mixer row (Sound ✎, fader, pan, send) and the drag handle. The full mixer arrives in Phase 7.
- [ ] Clip editing opens the matching page (step grid, slice grid...).
- [ ] Save, load and duplicate projects. Free users keep the single autosaved state.
- [ ] Built-in **demo project**, made with JB, opened on first launch.
- [ ] `isGrooveboxUnlocked()` decides **scene 1 only vs all scenes** and whether mixer controls are locked. Locked controls show a lock badge, and the bottom-right Unlock rectangle holds the purchase. During development it's always true.

### Phase 6 — Clip recording (paid)

- [ ] Arm a controller track and record what you play, snapped to the grid, into a looping clip (1/2/4/8 bars).
- [ ] Overdub, undo the last pass, and a count-in.
- [ ] A simple piano-roll view to fix notes. This could slip to a later version.

### Phase 7 — Mixer (paid)

- [ ] Per track: level, pan, mute, solo, and 2 sends (Delay, Reverb) on shared send buses.
- [ ] Per-track insert FX, reusing `MasterFx`.
- [ ] Master level, plus a limiter so 4 tracks can't clip.
- [ ] The full snap point of the Session mixer (see *Screens*). There's no separate Mixer page.

### Phase 8 — Unlock + release

- [ ] `juce_product_unlocking`. Non-consumable product `groovebox_unlock` in App Store Connect with Family Sharing on.
- [ ] Restore Purchases in Settings. Lock overlays with the price shown.
- [ ] Testing: Xcode StoreKit configuration file plus sandbox testers. No real charges.
- [ ] App Store listing: a screen recording of the groovebox, and copy that says "free to play, $4.99 to arrange".
- [ ] retro-relic.com update.
- [ ] Mac server release if any protocol bytes changed (the External source).

## Open questions

1. **Wireframes:** JB's Session sketch is folded in (see *Screens*). The iPad Perform view and the source editors still need sketches.
2. **Track count:** fixed at 4 for now. Allowing 6–8 later is a CPU question.
3. **Scene count:** 8 visible, with scrolling for more?
4. **Controller pages without a track:** they play the selected track? Or is a controller with no track silent?
5. **Clip length limit** for recorded MIDI: 8 bars?
6. **Oldest supported iPhone.** This sets the CPU budget and the voice caps.
7. **Android:** same model through Play Billing. When?
8. **Is the iPad Perform view free or paid?** By the rule, playing is free, so it's in the free column. It is also the best-looking feature on iPad, though, so decide on purpose.
9. **Perform view on large iPhones:** a 2-track split (one on top of the other) might fit on Pro Max models. Try it after the iPad version.

## Risks

- **CPU:** 4 sources plus FX on older phones. Mitigate with voice caps, shared tables and measuring early in Phase 1.
- **Routing rewrite:** `EventRouter` and the handlers are used by every controller. Phase 1 should keep each controller's notes exactly the same and only change where they go.
- **Scope:** this is several months of work. Phases 1–4 ship as free updates, which keeps growth going while the paid half is built.
