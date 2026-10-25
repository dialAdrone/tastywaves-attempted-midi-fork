<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Arpeggiator, note sequencer, modulation sequencer (T27): design note

Status: decisions G-N accepted as recommended (2026-10-17). Stage S0 (clock + arp engine) next.

## 1. Goal

Three things that share one clock and one note path:

1. **Arp**: turns the notes you hold (or latch) into a pattern.
2. **Note sequencer**: up to 32 steps of note / velocity / gate / tie / rest, played from the clock.
3. **Modulation sequencer**: a lane of per-step values that moves chosen parameters (table position, cutoff, BEND, drive) in time.

Latch (done) is the glue: latch a chord, the arp plays it, turn knobs with both hands.

## 2. Clock

- Internal tempo 40..240 BPM, 24 ticks per quarter note (MIDI clock resolution). Tick period is derived from the sample count in the audio block: a 32-bit phase accumulator, one add per sample block, so jitter is one block (about 1-3 ms). Sample-accurate offsets are not needed for a first version.
- External: MIDI clock (0xF8) and Start/Stop (0xFA/FB/FC) from the host already reach `midi_get` (usb.c:746). When a clock has been seen in the last 0.5 s the tick follows it; otherwise internal. The tempo screen shows `EXT` while following.
- MIDI clock out: not in v1.
- Swing: shifts every second 16th by 0..50 % of a 16th.
- The clock runs in the audio ISR once per block and only sets event flags; patterns are read in the ISR from small RAM arrays, no flash access, no division, integers only.

## 3. Note path

The arp and the sequencer emit the same note-on / note-off events as the panel keys, through the existing `ev_ring`, with a source tag so voice stealing and the latch see them as ordinary notes. The panel keys, MIDI input and arp stay mixed: with the arp on, **held panel and MIDI keys feed the arp's note list instead of sounding**.

Gate length is a fraction of the step (10..100 %). Notes are always released by the engine, never left to a later tick, so a stop (or a patch change) cannot hang a note: stop sends note-off for everything the arp or sequencer started.

## 4. Arp

- Modes: UP, DOWN, UP-DOWN, UP-DOWN (repeat ends), RANDOM, PLAYED (order pressed), CHORD (all at once).
- Octaves 1..4, rate 1/4, 1/8, 1/8T, 1/16, 1/16T, 1/32, gate, swing.
- Note list: up to 16 notes held (key or latch). Releasing all keys stops the arp unless latched. Latch keeps the list when keys go up; pressing a latched key removes it (same rule as today).
- A new key while the arp runs joins the list at the next step. The pattern phase is kept, no restart (restart on first key after empty).

## 5. Note sequencer

- 1..32 steps, one pattern at a time in RAM (4 B per step: note offset 0..127 or rest/tie flag, velocity, gate), transposed by the panel key you press (a held key = root) or free-running at the root note C3 when no key is held (decision J).
- Record: REC arms step input; each key press writes the note at the cursor and advances; the encoder sets length; a hold on REC clears.
- Direction forward only in v1.

## 6. Modulation sequencer

- One lane of up to 32 steps (same length as the note pattern or its own, decision K), value 0..127 per step, optional glide to the next step.
- Targets (choose one per lane in v1, depth -100..+100 %): table position (P_POS), filter cutoff, BEND, drive. Value goes into the smoother, the same path as a knob, so zipper noise is already handled.
- Second lane later, not in v1.

## 7. Panel

- **ARP** and **SEQ** buttons open the screens (ids 10, 11, LED on while the engine is on).
- **PLAY conflict:** PLAY is the latch button now. Proposal (decision G): **PLAY stays latch**; the arp/sequencer start with ARP / SEQ themselves (a short press toggles on/off, a long press opens the screen), and REC is step record. If you prefer PLAY as start/stop, latch moves to a SHIFT combination (hold GLO + PLAY).
- Screens use the existing 4-knob pages: ARP page 1 MODE / RATE / OCT / GATE, page 2 SWING / TEMPO / LATCH state. SEQ page 1 LENGTH / TEMPO / TARGET / DEPTH.
- Step grid on the 27 keys while a SEQ screen is open: key = step cursor, LED shows the step state. Keys stop sounding as notes in that screen (step edit mode); a mode button leaves it.

## 8. Storage

- Arp settings (mode, rate, octaves, gate, swing, tempo) are patch parameters appended to the enum (append-only rule, section 4 of PATCH-STORAGE-DESIGN), so they save with the patch. They add 7 parameters: P_COUNT 47 to 54, which exceeds the limit that keeps 32 records in one sector (about 50 parameters). Decision L: widen the log window to 5 sectors (0xE5000..0xE9FFF), still before the two future table slots.
- Patterns (note + mod lane, 32 steps x 4 B + lane 32 B = 160 B) use the log's reserved record type 3 with the owning patch id; saved with SAVE together with the patch (decision M: one pattern per patch, loaded with it).

## 9. Cost and memory

- Clock and event emit: well under 0.5 % of a block. Mod lane write to the smoother: one store per tick.
- RAM: pattern 32 x 4 B + lane 32 B + arp list 16 B + state, under 300 B. Adds nothing to flash write paths except the log widening in L.

## 10. Stages

| stage | content | check |
| --- | --- | --- |
| S0 | clock + arp engine on host: tests for each mode, latch, stop, no hung notes, external clock | host |
| S1 | arp wired to ARP button, page 1 | device |
| S2 | note sequencer + step grid + REC | device |
| S3 | mod lane | device |
| S4 | patterns in the log, widen the window (reviewer, flash-write path change) | device |

## 11. Decisions for the user

- **G.** PLAY stays latch; ARP / SEQ buttons start and stop. OK, or PLAY as start/stop with latch moved?
- **H.** Clock: internal tempo plus follow MIDI clock in; no clock out in v1. OK?
- **I.** Arp modes and rates as listed in section 4. Anything to add or drop?
- **J.** Sequence transposes from the key you hold, plays at C3 otherwise. OK?
- **K.** Mod lane shares the note pattern's length (simple), or its own length (polyrhythms, more UI)?
- **L.** Arp parameters live in the patch; widen the patch log to 5 sectors. OK?
- **M.** One pattern per patch, saved with it. OK, or a separate pattern library?
- **N.** Build order S0..S4 as in section 10, arp first. OK?

## 12. Built so far (S2: note sequencer), where it differs from the plan

- Pattern is global RAM, 32 steps, one note per step (no per-step velocity input yet: 100), saved with the patch only in S4. Not yet the transposition root C3: held key minus C4 (60), so C4 plays it as recorded; no key held plays it as recorded.
- Step input: REC short press starts/stops (stop sets the length to the steps entered), PLAY enters a rest, SEQ a tie, hold REC clears. SEQ short press switches the sequencer, hold opens the SEQ page. The sequencer shares RATE / GATE / SWING / TEMPO with the arp and bypasses it while on. (Superseded 2026-10-25: with the arp on, the arp plays the sequencer, see "SEQ + ARP, CHORD" below.)
- The first quick pass had a diagnostic readout on the ARP page; removed after device confirmation.

## SEQ + ARP, CHORD (2026-10-25)

User request: ARP on while SEQ plays did nothing (the sequencer bypassed the arp). Now:

- P_CHORD (appended after the FX parameters; older records load it as 0 = OFF), on a third SEQ tab CHORD with ARP (mode) and OCT.
  Values: OFF (the step's note alone, as before), HELD (the keys held or latched give the shape; the lowest key transposes, so
  C4 plays as recorded), MAJ MIN 7 MAJ7 MIN7 SUS4 (fixed shapes on the step's note; the newest held key transposes, as before).
- ARP off: a step plays its chord as a block, a step a RATE (CHORD OFF = the old sequencer exactly).
- ARP on (timing option A, user choice): a step lasts one beat (96 sub-ticks); the step's chord becomes the arp's notes and the
  arp plays them at RATE with its MODE, OCT, GATE and SWING, from the start of the pattern each step (a tied step goes on with the
  chord before, a rest is silent). 1/16 gives 4 arp notes a step; a 4-step pattern is one bar. The mod lane follows the beat.
- ARP alone, SEQ alone with CHORD OFF and LATCH are unchanged. Changing the patch while SEQ plays keeps CHORD (as the arp).
- The CHORD picture shows the chord the step playing sounds (step 1 when stopped, C4 with no pattern): its name, two octaves
  of keys with the chord lit (root amber), and what the arp does. HELD names what the keys hold (MAJ MIN 7 MAJ7 MIN7 SUS4 SUS2
  DIM AUG 5, else CHORD), so the shape picked out on the small keys is visible.

