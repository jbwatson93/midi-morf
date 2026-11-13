# Synth: morning test list (2026-11-13)

Installed on your **iPhone and iPad**. The code is on branch `synth-mod`,
**not merged** (main still has last night's filter tab + FX rack). Android
builds but hasn't been run on a phone.

Tip: use the **Glide Lead, Gate Pad, FM Warp Bass, Step Filter and Macro
Morph** presets (in Lead / Pad / Bass / FX / Pad). Each one shows off a new feature.

---

## 1. The source strip and knob rings (most important)
The row of coloured chips along the bottom of every synth tab (ENV 1–3,
LFO 1–4, VEL, NOTE, RAND, MW, TILT X/Y, M1–M4).

- [ ] Load **Evolving**. On OSC, POS has a **blue ring** (LFO 1) and it
      wobbles with a white dot while a note plays.
- [ ] Tap **LFO 2**: it lights up, a hint replaces the keyboard, and every
      knob that can be modulated gets a dashed outline.
- [ ] Drag **OSC A LEVEL** up/down: a green ring grows/shrinks both sides
      of the knob. Play a note: the level wobbles.
- [ ] Double-tap the knob (still in LFO 2 mode): the ring goes.
- [ ] Tap **LFO 2** again: assign mode ends, the knobs turn normally again.
- [ ] Is the drag speed right? (150 pt of drag = full amount.) Too slow/fast?
- [ ] Check the dots on the chips: a small dot = that source is in use.

## 2. MOD tab
- [ ] **LFO 1–4**: tap empty space to add a point, drag points, drag the
      small hollow circle in the middle of a segment to bend it, double-tap
      a point to delete it. Is it easy to hit the points with a finger?
- [ ] **PENCIL** on + GRID 16: drag across to paint steps.
- [ ] **SHAPE > Load...**: presets (Sine, Saw, Steps, Gates, Random...).
- [ ] **SYNC** on: RATE becomes a note value. Play the sequencer and check
      it locks to tempo (try Gate Pad: LFO 1 gates at 1/8).
- [ ] **MODE**: Free (runs on its own), Retrig (restarts every note), Env
      (plays once). Try Retrig with RISE/DELAY for delayed vibrato (Glide Lead).
- [ ] **ENV 1/2/3**: faders + the envelope picture. ENV 2 is the same as
      the filter envelope on the FILTER tab.
- [ ] **MACROS**: four knobs; tap a name to rename. Assign M1 to something
      from the strip. (Macro Morph uses M1 = MORPH, M2 = WIDTH.)

## 3. MATRIX tab
- [ ] All 16 slots: ON/OFF, source, amount bar (double-tap = 0), destination,
      AUX (scales the amount, e.g. MW so the wheel decides how much LFO),
      CURVE, and × to clear. Scrolls on the phone?
- [ ] Routes you made with the strip show up here, and vice versa.

## 4. OSC tab
- [ ] **WARP** menu per oscillator (Sync, Bend +/-, PWM, Mirror, Quantize, FM)
      and the WARP knob. FM uses the other oscillator (FM Warp Bass: ENV 3
      sweeps the FM for a pluck).
- [ ] **VOICE**: MODE Poly / Mono / Legato and GLIDE. Glide Lead is legato:
      hold one key, play another, it slides; let go, it slides back.
- [ ] **UNISON** now goes to 7. **NOISE** level is new.

## 5. Wavetable editor (EDIT on either oscillator)
- [ ] Opens over the page with the table's frames along the top (up to 16).
- [ ] **DRAW**: finger-paint the wave. **HARMONICS**: drag 32 bars.
      START gives a basic wave; SMOOTH / NORMALIZE / INVERT.
- [ ] **+ / −** add (copies the selected frame) or delete frames.
- [ ] **MORPH**: off / crossfade / spectral. Sweep POS afterwards.
- [ ] You hear each edit when you lift your finger (the keyboard at the
      bottom plays it).
- [ ] **SAVE** asks for a name, writes `Wavetables/<name>.wav` and the
      oscillator now shows it by name. Reload the project: still there?
- [ ] **CANCEL** puts the old table back.

## 6. FILTER tab
- [ ] **FILTER 1 / FILTER 2** buttons: each filter has its own type, curve,
      CUTOFF, RESO, ENV and ON. VEL / KEY / DRIVE are shared.
- [ ] Turn FILTER 2 on (ON in the card header) and pick **Serial** or
      **Parallel**. Try LP on 1 + HP on 2 in parallel.
- [ ] **A / B / SUB / NOISE**: switch one off and it bypasses the filters
      (e.g. keep the sub clean under a resonant filter).

## 7. FX tab
- [ ] Delay has a new **SYNC** knob (Off, 1/32 ... 1/2). With SYNC on,
      TIME is ignored and the repeats lock to tempo.

## 8. Regression checks
- [ ] Old projects and your saved presets load and sound as before.
- [ ] Morf Chord strums on tracks 2–4 (24 voices on every track now).
- [ ] CPU: four synth tracks with big pads + unison 7. Any crackles on the
      iPhone? (Mac test: worst case is about 8% of one core.)

---

## Things I know aren't perfect
- The simulator dropped about half of my taps, so I checked every screen
  visually but haven't tested the gestures much with a real finger.
- Opening the editor on a built-in table loads up to 16 of its frames; saving
  rebuilds 64 with MORPH, so it can differ slightly from the original.
- Saving a wavetable with a name that already exists overwrites that file.
- Free LFOs run at the synced rate but don't line up with bar 1 of the
  transport.
- Parallel filters are mixed at half each, so LP+LP in parallel is the same
  level as one.
- Factory presets were retuned by maths, not by ear.
- Patches are now version 3. Older ones load fine, but a version-3 patch
  won't load correctly in last night's build.

## If something's wrong
Tell me which numbered item. If it's bad enough to undo, main is still at
last night's merge, so `synth-mod` can simply wait.
