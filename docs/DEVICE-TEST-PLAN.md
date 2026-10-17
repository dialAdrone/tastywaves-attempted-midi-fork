# VAVEWave device test plan (engine v1 + panel UI, commit 193d2dc)

Everything below was verified on a computer (host mode), none of it on the FM-1 yet. What to look for is in
*italics*; record numbers in the "Result" column or in `docs/TASKS.md` Log. Controls: docs/UI-DESIGN.md.

## 0. Before flashing

- [ ] `rules-reviewer` verdict on HEAD is SAFE TO INSTALL (asked for 2026-10-12; do not flash without it).
- [ ] MASTER down. Headphones off or low for the first boot.
- [ ] Stock `FM-1_093` package downloaded from the FM-1 Firmware Hub (way back). Rescue: hold OCT- while powering on.
- [ ] `build/vavewave.fwsc` is from HEAD: `./build.sh` shows image 512,688 B, identity FM-1_950.
- [ ] `.venv/bin/python tools/fm1_install.py --info` reads the device (FM-1_950 bench now, or FM-1_093).

## 1. First boot (2 min)
All good

| # | Do | Expect |
| --- | --- | --- |
| 1.1 | Power on / after install restart | Screen draws HOME spectrum look, patch `001 ...`, no crash screen, no sound until a key |
| 1.2 | Play keys F3..G5 | Sound, key LED follows, screen lights the keys' voices count `V n/12` |
| 1.3 | Settings left by the old bench build (octave) | Loads without garbage (no odd page, octave -2..+2) |
| 1.4 | Leave it 40 s, then power-cycle | Boots again normally (the 30 s boot-ok clear) |
| 1.5 | Hold OCT- at power-on once | Enters rescue (USB shows update mode); then power-cycle normally. Proves the way back |

## 2. CPU and timing (the numbers we still need) (10 min)

Read `CPU %` on the status line (live) and `CPU MAX` on GLO (EDIT held + HOME restarts the max).

| # | Setup | Result (MAX %) | Expect |
| --- | --- | --- | --- |
| 2.1 | Idle, no notes | 1% | ~0 % |
| 2.2 | 1 note, default patch | 5% | ~4-6 % |
| 2.3 | 12 held notes (a cluster on the keys, plus OCT± for more) default patch (sinc, LP) | 49% | ~50 % estimate (typical) |
| 2.4 | As 2.3 + BEND +63 on OSC1 and OSC2 (SEL: BEND> BOTH), SUB and NOISE levels up, FX MODE = HP | 50% | ~65 % estimate (worst) |
| 2.5 | As 2.4 with OSC2 on a different table than OSC1 | | flash cache effect, may add a few % | 50%
| 2.6 | As 2.4 with SEL QUAL = ECO |n 48% | ~53 % estimate |
| 2.7 | HOME look 1 (waterfall): turn SELECT quickly across frames while 12 notes sound | CPU actually goes down to about 20%, no dropouts| CPU % must not rise; listen for dropouts |

Pass: worst case below 75 %, no crackle. *Fail signs:* crackle, `CPU` red (> 80 %), notes freezing (shed guard).

## 3. Sound quality (15 min, by ear)

- [ ] Each of the 12 tables (ALGORITHM), sweeping SELECT slowly: smooth morph, no zipper noise, no clicks at frame
      boundaries. *Note any table that has a rough step.*
- [x] High notes on bright tables (NOISE, SHAPE, FOLD at the top octave): aliasing vs QUAL HI/ECO. *Expected: HI
      clean; ECO audibly rougher on bright tables.*
- [x] BEND from 0 to +63 and -64 on SAWSQ: smooth brightening, no zipper, pitch unchanged. Compare with osc 2 only.
- [x] Filter: CUTOFF sweep with RESO 90+: no blow-up, no sticking tone after notes release (silence returns to true
      zero). Try HP and BP.
- [x] Mip-level step: slide GLIDE across a long range up through the top octaves: *a small timbre step at the level
      boundaries is known*, anything louder is a bug.
- [x] Filter MODE change while a note holds: *a click is known* (T16 risk 3).
- [x] Drone case: hold 8-12 notes for 5 minutes with slow LFO to POS: no drift in level, no clicks, no heat issue.
- [x] Level: 12 notes at velocity max, DRIVE 0 and DRIVE 127: no hard clipping at 0, intended saturation at 127.

## 4. Screens and design review (the main part) (20 min)

For each screen note: legibility at arm's length, anything clipped or overlapping, flicker or tearing while turning
knobs, redraw delay (noticeable lag from knob to screen).

| Screen | Do | Review |
| --- | --- | --- |
| HOME look 2 (spectrum, default) | Turn SELECT, change table, play | waveform, position bar, 16 bars readable; fundamental white; labels `FRM 12/32` |
| HOME look 1 (waterfall) | HOME again | frame 0 in front; one white nearest frame; hidden-line look; redraw lag |
| HOME look 3 (minimal) | HOME again | big names; voice squares light with voices; wave small but clear |
| HOME knobs | Turn each of the 4 | CUTOFF RESO ATK REL values; last-turned lilac; bar movement |
| EDIT OSC1 | EDIT; BEND +63 | live warped waveform; POS DETUNE LEVEL BEND; chips OSC1/2/SUB/NOISE |
| EDIT OSC2/SUB/NOISE | EDIT again, or EDIT + OP keys (F#3 G#3 A#3 C#4) | chips follow; the OP key does not sound |
| FX (filter) sets 1, 2 | FX, FX again | filter response picture follows CUTOFF/RESO; MODE LP/BP/HP |
| ENV amp / filter | ENV, ENV again | both curves, edited one filled lilac; ms/s values |
| LFO 1, 2 | LFO, LFO again | SHAPE RATE routing; list 4x2 |
| SEL | SEL | MODE POLY/MONO/LEGATO, BEND>, QUAL, CURVE; voice squares |
| GLO 1, 2 | GLO, GLO again | VOLUME DRIVE GLIDE PB RNG; CPU MAX; MW/AT/VEL routings |
| PRESETS | turn PRESETS | name changes at once, sound loads after ~0.4 s; 8 patches all sensible |
| LEDs | all pages | page button lit one at a time; SAVE blinks when edited; key LEDs follow |

Review points for the design (what we most want your opinion on):
- Is the 4-column value strip readable and does it feel right under the real encoders?
- Spleen font: legible at 8x16 and 16x32? (Replaces Terminus.)
- Do `FRM`, `V5/12` and `CPU %` on the status line belong there or on a separate line?
- Is the order and wording of page names right? (FX hosts the filter in v1.)
- Does the waterfall add enough over the spectrum to justify its draw cost?

## 5. MIDI (5 min)

- [x] Keys also send USB-MIDI notes (check with a DAW / MIDI monitor).
- [x] Incoming notes play, velocity matters (VEL>AMP on GLO 2).
- [x] Mod wheel (CC 1) -> MW>POS / MW>VIB; pitch bend with PB RNG; channel aftertouch -> AT>CUT.
- [ ] NOT TESTED CC 120 all sound off silences everything; CC 123 all notes off.
- [x] Note flood from a DAW (a fast 16th arp at 180 BPM, 12 notes): no stuck notes (`ev_dropped` counter is
      not shown yet; a stuck note after a flood means the 64-entry event ring overflowed).

## 6. Save and persistence (5 min)

- [x] Edit a patch (the lilac tick appears at the patch line, SAVE LED blinks), press SAVE while silent: `SAVED`
      shows, LED goes solid.
- [ ] Press SAVE while notes sound: the write waits for silence (no audio glitch from the flash erase), then saves.
  - TEST FAILED SAVES IMMEDIATELY 
- [x] Power-cycle: patch, octave and HOME look come back.
- [x] Press SAVE repeatedly: at most one write per second (no stall).
- [ ] *Known stub:* one user patch overlay only (T17 will add real slots).

## 7. Robustness (10 min)

- [ ] Turn encoders very fast, mash page buttons, hold EDIT while turning knobs: no crash, no lock-up.
  - From HOME, SELECT Freezes on about one rotation when turned fast 
- [x] Change preset while 12 notes ring: voices release without a crash (mono presets release all).
- [x] USB cable pulled and re-plugged while playing: audio keeps running; MIDI recovers.
- [x] 15-minute soak with a drone: no crash screen, no drift. If the red crash screen appears: note vector, PC and
      return address (docs: skill `fm1-crash-triage`) and tell me.

## 8. Report back

Send me: the table 2 numbers, any table/note that sounds wrong (table, frame, note, setting), screens that are hard
to read or lag, and the answers to the design review points. I update `docs/TASKS.md` (T14, T18) and decide the
next steps (effects, patch storage T17, arp / sequencer T27-T29).

To go back: `fm1_install.py` the stock `FM-1_093` package (rescue with OCT- held at power-on if the app won't start).

## 9. User wavetables (stage 4b)

Tables in the slots: `build` them with `tools/fm1_wavetable.py` (see its --help) and `put SLOT FILE.vwt`. Re-upload after the layout change of stage 4a.

| # | Do | Expect |
| --- | --- | --- |
| 9.1 | Flash, `info`, `put 0` a table, play | the synth sounds as before (factory table), no glitch at the upload end, the normal screen returns |
| 9.2 | ALGORITHM turned up past STAIR (12 factory tables) | the user name appears (full on the minimal HOME look, 5 characters on the status line), the waveform and spectrum follow, the sound is the uploaded table, SELECT morphs through its frames; empty slots are skipped |
| 9.3 | First selection of a slot after power-on | up to about 0.15 s before the table sounds (its CRC is checked first), meanwhile the factory table 0; no click-and-silence |
| 9.4 | OSC2 chip (EDIT, EDIT) and ALGORITHM: the user table on OSC2, on both | works on each oscillator |
| 9.5 | CPU MAX (GLO, EDIT + HOME restarts it): 12 notes, default patch with user table on OSC1; on OSC1 and OSC2 (different slots); worst case (BEND both, SUB, NOISE, HP) | compare with section 2 (49 / 50 / 50 %): user tables are read through the same cache, expect the same; a rise above 75 % or crackle is a fail |
| 9.6 | Replace the table in the slot you are playing: `put` while no key is down | the upload screen, then the NEW table sounds (no restart needed) |
| 9.7 | Preset with a user table (edit, SAVE), `erase` that slot, power-cycle, load the patch | plays table 0 (never silence), the name shows EMPTY; no crash |
| 9.8 | Power-cycle with tables in all 8 slots, wait, play | normal boot, no delay (CRC checks run only for slots a patch uses) |
| 9.9 | Pull the cable / switch off in the middle of a `put` into the slot that is sounding (keys up), power-cycle | that slot is empty (EMPTY), the others intact |

## 10. User patches (T17 stage P1, docs/PATCH-STORAGE-DESIGN.md)

Keys up and no sound for every SAVE (it waits until the sound has died away). Before flashing: a patch you saved with the old
SAVE (one patch in the settings sector) is copied to U01 at the first boot.

| # | Do | Expect |
| --- | --- | --- |
| 10.1 | Flash, boot. If the old SAVE had a patch: PRESETS past 008 | a `U01` entry with that patch's name and sound (the migration); otherwise the list ends at 008 |
| 10.2 | On factory patch 1: turn CUTOFF, press SAVE (short) | `SAVED U01` (or the next free id), the "edited" mark disappears, the SAVE LED stops |
| 10.3 | Edit again, SAVE | `SAVED U01`, replaced in place |
| 10.4 | PRESETS back to a factory patch, edit, SAVE | `SAVED U02` (first free id); the list now has U01, U02 after 008 |
| 10.5 | Edit a factory patch, hold SAVE and turn PRESETS two clicks, release | `SAVE TO Unn` while held (a `*` marks an occupied id); on release an empty id is saved at once |
| 10.6 | Same, but pick an occupied id | on release `REPLACE Unn?`; pressing SAVE again replaces it; pressing any other button or waiting 5 s cancels |
| 10.7 | On a user patch hold SAVE for 2 s | `DELETE Unn?`; SAVE again deletes it (`DELETED Unn`, the patch before it in the list loads); another button cancels |
| 10.8 | Power-cycle | all saved patches are listed and load exactly (sound, name); the last patch used comes up |
| 10.9 | Fill all 32 ids (hold SAVE + PRESETS), keep saving over them 100+ times | always `SAVED`; at some save the screen stalls up to about 0.3 s (compaction), no sound is cut (the save waits for quiet) |
| 10.10 | Patch that uses a user table: SAVE, `erase` that table slot, load the patch | plays table 0 and shows EMPTY (as 9.7), no crash |
| 10.11 | Switch off during a SAVE (press SAVE and cut power within a second) several times | after power-on every previous patch is intact; the patch being saved is either its old or its new version |
| 10.12 | Firmware update over the top (`fm1_install.py` with this build again) | all patches still there |
| 10.13 | Wavetable `put` and patch SAVE do not disturb each other: upload a table, then SAVE, `info` | both fine |
