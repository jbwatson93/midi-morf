# Synth flow plan — learning from Serum 2

Goal: make the wavetable synth flow like Serum 2 (routing, LFOs, filters,
voices, FX) and add drawable LFOs and drawable wavetables, without turning
it into a desktop synth crammed onto a phone. One engine, as now
(see `WavetableEngine.h`).

Sources: Xfer's *What's New in Serum 2* guide (static.xferrecords.com),
plus EDMProd, Splice and RouteNote write-ups.

---

## 1. What Serum 2 does that matters to us

| Area | Serum 2 |
|---|---|
| Oscillators | 3 main oscs (Wavetable / Sample / Multisample / Granular / Spectral) + sub + noise. Smooth position interpolation, phase memory and random phase per osc, **dual warp modes** (FM, PD, sync, bend…), copy/paste an osc. |
| Wavetable editor | Draw single frames, harmonic (additive) view, morph between frames, import audio. |
| Filters | **Two filters**, series or parallel, new virtual-analog types, a clean drive mode, a mixer that sends each osc to filter 1, filter 2 or straight out. |
| Envelopes | **4** envelopes, curved segments, BPM-sync, "invert legato" (always retrigger). |
| LFOs | **Up to 10**. Each one is a **drawn shape**: points plus curved segments, an independent X/Y grid, presets. Modes: free / retrigger per note / envelope (one-shot). Rise and delay, smoothing, phase. Rates up to 1000 Hz, follows swing, plays in either direction. Path mode (draw on an X/Y pad) and chaos modes (Lorenz, Rossler). |
| Mod matrix | Drag a source onto any knob; the knob shows a coloured ring. The matrix lists every link: amount, **curve on the source**, **aux source** (a second source scales the depth, also with a curve), uni/bipolar, **bypass**, reorder, remove. Oscs and filters can be mod sources too. **8 macros**, each a source *and* a destination. |
| Voicing | Poly count, mono / legato, portamento with curve, always-retrigger, unison with width, blend and stack modes. |
| FX | **FX matrix**: 13 effects, any effect several times, drag to reorder, bypass per module, **2 FX buses** plus L/H, L/M/H and M/S splitters. New ones include Bode (frequency shifter), Convolve, HQ delay, Overdrive. |
| Flow | Pages: OSC, FX, MATRIX, GLOBAL, with the mod sources (envs, LFOs, macros, velocity, note) in a strip along the bottom that stays visible on every page. That strip is why Serum feels fluid: grab a source and drop it on any knob from anywhere. |

## 2. Where ours stands

What we have (`WavetableEngine.h`, `WavetableSynthPage.h`):

- 2 table oscs + sine sub, unison up to 5, drive, one 4-pole ladder LP.
- 2 ADSRs: amp, and a shared filter/position envelope (`ENV`, `ENV>POS`).
- 1 LFO: 4 fixed shapes, 0.05–20 Hz, **one global free-running LFO**, no
  sync or retrigger.
- 4 routing slots: 5 sources (mod wheel, velocity, accel X/Y, LFO) into
  10 destinations, set from two combo boxes on a separate ROUTING tab.
- FX: drive → delay → reverb, fixed order (`MasterFx.h`); the mixer adds
  per-track filter/drive/crush and two send buses (`MixerFx.h`).

The gaps, roughly in the order you feel them:

1. **Modulation is computed once per block for the whole synth**
   (`processModulation`). Velocity is the *last* note's velocity applied
   to every voice, and the LFO is shared. Nothing can differ per note
   except the envelopes.
2. **Cutoff modulation is in linear Hz** (`+ mod * 5000 Hz`, env amount
   `+ 0..10000 Hz`). That's huge at low cutoffs and barely audible at high
   ones, and it's why the filter feels stuck open (section 3).
3. Routing lives on its own tab with combo boxes, so you can't see what a
   knob is modulated by while you're tweaking it.
4. 1 LFO, 2 envelopes (one already doing two jobs), no macros.
5. One filter type, no keytracking, the oscs all go through it.
6. No mono / legato / glide.
7. Fixed FX order, no chorus/phaser/EQ/compressor.
8. No way to draw LFOs or wavetables.

---

## 3. Phase 0: the filter that never closes (small, ship first)

**Why it's always open.** Several things all *add* Hz to the cutoff and
none of them can go below it:

- **Velocity is unipolar and almost always high.** The pads, the step
  sequencer and the clips send velocity 100 (≈0.79), so a `Vel → Cutoff`
  route at 0.35 adds 0.79 × 0.35 × 5000 ≈ **+1,380 Hz on every note**,
  permanently. Acid Bass (`WavetablePresets.h:112`) is set to 420 Hz and
  actually sits around 1,800 Hz before the envelope even starts.
- **It's global, not per note.** `lastVelocity` is the most recent note's
  velocity, so one hard note opens the filter on every held note too.
- **The envelope amount is in Hz and only positive** (0–10,000 Hz), and its
  sustain stays added: sustain 0.3 × 2,000 Hz keeps +600 Hz on a held note,
  more than an octave above a 420 Hz cutoff.
- **The Init patch opens it too.** The mod wheel starts at 0.5 and Init
  routes it to cutoff at 0.5, so +1,250 Hz before you've touched anything.

**Fix:**

1. **Per-voice velocity.** Store velocity in the voice at `startNote` and
   evaluate velocity routes per voice (this is the first slice of the
   phase 1 per-voice matrix).
2. **Cutoff modulation in octaves**, not Hz: `fc = cutoff × 2^(octaves)`.
   Full matrix depth = ±6 octaves; the env amount becomes **bipolar**
   (−1…+1 → ±6 octaves), so an envelope can also close the filter.
3. **A VEL knob on the filter card** that scales the *envelope depth* by
   velocity (the classic hardware approach): soft notes get a smaller
   sweep, hard notes a bigger one, and with sustain at 0 every note closes
   back down to the cutoff, however hard it was played.
4. **Keytracking** knob (0–100%) so low notes aren't darker than high ones
   at the same setting.
5. Mod wheel defaults to 0 and Init drops the wheel → cutoff route, or makes
   it bipolar around the knob.
6. Re-tune the factory presets that use `Vel → Cut` (Acid Bass, and the
   ones at lines 140, 245, 255) and bump the patch `version` to 2 with a
   one-time migration of old Hz amounts to octaves, so saved user patches
   sound close to how they did.

Size: about a day including re-tuning by ear on the phone.

---

## 4. Phase 1: a modulation system that flows

This is the big one. Everything else plugs into it.

**Engine**
- Modulation runs **per voice**, at a control rate (every 32 samples) and
  smoothed, rather than once per block for the whole synth. Global sources
  (mod wheel, accel, macros, a free-running LFO) are computed once and
  shared; per-note sources (velocity, note, envelopes, retriggered LFOs)
  per voice.
- **Sources:** Env 1 (amp), Env 2, Env 3, LFO 1–4, Velocity, Note
  (keytrack), Mod wheel, Accel X/Y, Macro 1–4, Random per note.
  Split today's shared filter/pos envelope into Env 2 (pre-routed to
  cutoff) and Env 3 (free).
- **Matrix:** 16 slots. Each: source, destination, amount (bipolar),
  uni/bipolar, **aux source** (scales depth, so "mod wheel controls how
  much LFO goes to cutoff"), source curve (lin / exp / log), bypass.
- **Destinations:** every knob on the synth page, each with a sensible
  range (cutoff in octaves, pitch in semitones, levels 0–1, LFO rate,
  env times, FX params). A small table maps each destination to its scale
  so it isn't hand-coded per case like now.
- Envelopes: curved attack/decay/release, optional tempo sync.

**UI (the "flow" part, adapted for touch)**
- A **mod-source strip** along the bottom of every synth tab: ENV2, ENV3,
  LFO1–4, VEL, NOTE, MW, TILT, MACROS. It's always there, like Serum's.
- **Assign mode:** tap a source chip (it lights up), then drag up/down on
  any knob to set that source's amount on it. Tap the chip again to leave.
  This is Serum's drag-onto-knob, done with thumbs.
- Every modulated knob shows **coloured rings** for its mod amounts and a
  moving dot for the live value. Long-press a knob → list of its mods with
  amount, bypass and remove.
- The ROUTING tab becomes **MATRIX**: the full list of links, with aux,
  curve and bypass, for when you want the overview.
- **4 macros** on the main page, renameable, assignable like any source.

Size: the biggest phase, about 1–1.5 weeks.

## 5. Phase 2: drawable LFOs

- An LFO is a **shape**: a list of points (x, y) with a curve per segment,
  stored in the patch ValueTree. Today's 4 waves become built-in shape
  presets, so nothing new is needed for old patches.
- **Editor** (full-width card on the MOD tab):
  - tap empty space → add point; drag a point → move; double-tap → delete;
  - drag the middle of a segment up/down → bend it (Serum's curve handle);
  - grid X (1–32) and Y (1–16) with snap on/off; a **pencil** mode that
    paints steps into the grid, the quick way to make stepped/rhythmic LFOs;
  - shape presets (sine, tri, saw, ramp, square, S&H-style steps, a few
    rhythmic gates), plus save your own.
- **Per LFO:** mode Free / Retrigger / Envelope (one-shot); rate in Hz or
  synced to tempo (1/32 … 8 bars, dotted, triplet) from the groovebox
  clock; rise, delay, phase, smooth; uni/bipolar.
- Rendering: the shape is baked into a 1,024-point table when edited, so
  the audio thread just reads a table.
- Later, if wanted: chaos mode (Lorenz) as a slow, never-repeating source;
  that's easy once LFOs are tables.

Size: about a week, mostly the editor.

## 6. Phase 3: filters

- Replace the hand-rolled ladder with a **zero-delay-feedback (TPT)**
  design: correctly tuned cutoff, stable at high resonance, bass doesn't
  vanish as resonance goes up. Modes: LP 12/24, HP 12/24, BP, Notch, Comb,
  and a "Formant/vowel" type for fun.
- **Filter routing toggles** per source (Osc A, Osc B, Sub, Noise → filter
  or straight out), so a sub stays solid under a squelchy filter.
- **Second filter** with Serial / Parallel switch, once the first one feels
  right. Worth it, but not before Phase 1.
- Drive goes pre- or post-filter.

Size: 3–4 days for one new filter with modes; +2–3 days for dual.

## 7. Phase 4: voices and oscillators

- **Voice mode:** Poly / Mono / Legato, voice count, **glide** (time +
  always / legato only), retrigger. This matters a lot on bass and leads.
- **Unison:** up to 7, plus width and blend (how loud the outer voices
  are vs the centre).
- **Phase per osc:** start phase + random amount (Serum's phase memory).
- **Noise osc** with a level and a few colours. Cheap and useful.
- **One warp per osc** to start: Sync, Bend+/-, PWM, Mirror, FM from B,
  FM from sub. Each is a single knob and a destination in the matrix.
  This gives most of Serum's movement without a third osc.
- A third osc: only if JB asks; two plus noise plus warps covers a lot and
  stays inside the CPU budget for 4 tracks × 32 voices.

Size: about a week.

## 8. Phase 5: drawable wavetables

The engine already builds tables from per-frame spectra
(`Wavetable.h`), so a drawn frame just goes through an FFT and in.

- **Wavetable editor** opens from the osc card:
  - **Frame strip** at the top: up to 64 frames; add, duplicate, delete,
    reorder.
  - **Draw view:** finger-paint the single cycle (smoothed, snapped to
    a grid if wanted), with tools: pencil, line, smooth, normalise, invert,
    mirror.
  - **Harmonics view:** 32–64 bars of harmonic level (additive), the
    easiest way to make something musical by finger.
  - **Morph:** mark keyframes, then fill between them by crossfade or
    spectral morph (the spectral one is how Serum gets smooth sweeps).
  - Start from any existing table (built-in or imported) and edit it.
- **Save** as a 2048-samples-per-frame `.wav` in the app's user tables
  folder. That's the Serum-compatible format our importer already reads, so
  tables travel both ways and patches keep saving tables by file.
- Live preview: the osc's wave display and the audition strip play the
  table while you draw.

Size: about 1.5 weeks.

## 9. Phase 6: FX

- Turn the fixed chain into a **rack**: add modules, drag to reorder,
  bypass each, the same module more than once.
- Add: **Chorus**, **Phaser/Flanger**, **EQ** (3-band), **Compressor**
  (with a multiband/OTT-style mode), **Filter** (one more filter as an
  effect), keep Distortion (add modes), Delay (add ping-pong, sync),
  Reverb.
- Every FX knob is a matrix destination.
- Skip dual buses and splitters at first. The mixer already has per-track
  inserts and send buses (`MixerFx.h`); the synth FX should be for sound
  design, the mixer for the mix. Revisit splitters if people ask.

Size: about a week, mostly from JUCE's `dsp` modules.

---

## 10. Proposed page layout

| Tab | Contents |
|---|---|
| **OSC** | Osc A, Osc B (table, position, warp, pitch, level, phase), Sub, Noise, Unison |
| **FILTER** | Filter 1 (and later 2): type, cutoff, reso, drive, keytrack, VEL, routing toggles; ENV 2 right there |
| **MOD** | Envelopes 2/3 and LFOs 1–4 as sub-tabs, with the LFO drawing editor |
| **MATRIX** | Full mod list with aux, curve, bypass |
| **FX** | The FX rack |
| **VOICE** | Poly/mono/legato, glide, voice count, bend range, macros' names |

…with the **mod-source strip** fixed along the bottom of all of them.

## 11. Order and rough size

| # | Phase | Size | Why here |
|---|---|---|---|
| 0 | Filter fix | ~1 day | Small, fixes what you hear now |
| 1 | Per-voice mod system + assign UI | 1–1.5 wk | Everything plugs into it |
| 2 | Drawable LFOs | ~1 wk | Biggest "fun" gain once mods flow |
| 3 | Filters | ~1 wk | Better tone, routing |
| 4 | Voices + warps + noise | ~1 wk | Mono/glide matters for bass |
| 5 | Drawable wavetables | ~1.5 wk | Self-contained, can move earlier |
| 6 | FX rack | ~1 wk | |

Phases 2, 3, 4 and 5 don't depend on each other once phase 1 is in, so
they could run in parallel on separate branches.

## 12. Questions for JB

1. **Patch compatibility:** OK to re-tune factory presets and migrate saved
   patches (Hz → octaves) in phase 0, accepting they'll sound slightly
   different?
2. **CPU:** per-voice LFOs × 32 voices × 4 tracks. Fine on recent iPhones;
   do you want to keep supporting older devices/Android low-end, which may
   mean dropping the voice cap to 16 per track?
3. **Tempo sync:** LFOs and delay synced to the groovebox clock: yes?
4. **Third oscillator:** worth it, or are warps + noise enough?
5. **Drawable wavetables:** should user tables also show up in the browser
   next to built-ins and be shareable (AirDrop the .wav)?
