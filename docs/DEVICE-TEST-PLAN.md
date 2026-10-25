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

## 11. Latch (PLAY button)

| # | Do | Expect |
| --- | --- | --- |
| 11.1 | Press PLAY | its LED lights; the voice counter on the status line reads `L` (accent colour) instead of `V` |
| 11.2 | Press a key and let go | the note keeps sounding, its key LED stays lit; press more keys: they all keep sounding |
| 11.3 | Press a latched key again | that note (only) is released |
| 11.4 | Turn knobs, change tables, EDIT pages with both hands while notes are latched | the sound changes live; a filter sweep over a drone works |
| 11.5 | Hold keys, then press PLAY, then let go | the held keys stay latched |
| 11.6 | Press PLAY again | every latched note is released; LED off, `V` back |
| 11.7 | With notes latched press OCT+ (then OCT-) | every latched note moves up an octave (down again with OCT-); the key at the new octave lets that note go |
| 11.8 | Latch 12+ notes | oldest are stolen as usual; nothing crashes |
| 11.9 | A DAW sends all-notes-off (CC 123) while notes are latched | silence; the next press of a key starts a note again |
| 11.10 | Latched notes with a patch change / SAVE | notes keep sounding through a patch change (SAVE works while notes sound: one short gap in the sound is possible when the log compacts) |
| 11.11 | MIDI notes from a keyboard with latch on | behave normally (not latched) |

## 12. Arpeggiator (ARP button)

Flash `build/vavewave.fwsc`. The arp is part of the patch (6 new parameters); old saved patches load with the arp off.

| # | Do | Expect |
| --- | --- | --- |
| 12.1 | Hold ARP (half a second) | ARP LED on; hold a chord on the keys: the notes play one after another, UP, 120 BPM 1/16 |
| 12.2 | Let go of the keys | the arp stops after the last note |
| 12.3 | Press ARP | the ARP page: MODE RATE OCT GATE (knobs 1-4), press again: SWING TEMPO (SET 2/2) |
| 12.4 | Turn MODE through all 8 | UP DOWN UP-DN UP-DN2 RANDOM PLAYED CHORD; each sounds as named; OFF stops it |
| 12.5 | RATE, OCT, GATE, SWING, TEMPO | faster/slower, more octaves, shorter/longer notes, shuffle, tempo 40..240 |
| 12.6 | PLAY (latch) on, tap a chord, let go | the arp keeps playing; turn the knobs with both hands (the drone / modulation use); PLAY again stops it |
| 12.7 | Play MIDI keys from a computer with the arp on | MIDI notes also go into the arp |
| 12.8 | Send MIDI clock (a DAW, tempo 90 and 140) | the arp follows it (page shows EXT CLOCK); stop the clock: back to the TEMPO knob after half a second |
| 12.9 | SAVE a patch with the arp on, power-cycle, load it | arp mode, rate, tempo come back; a patch saved before still loads (arp off) |
| 12.10 | Arp on, hold ARP | arp off; keys held at that moment sound as plain notes and end when released |
| 12.11 | CPU on the HOME status line with the arp running and 8 notes held | note the percentage (CPU headroom budget for the effects) |
| 12.12 | Timing feel | steps are in time (jitter up to 6 ms is known: the clock runs once per audio block) |
| 12.13 | Hold keys, change patch (PRESETS) and back | no hung notes, no crash |

## 13. Note sequencer (SEQ and REC buttons)

Flash `build/vavewave.fwsc`. One pattern of up to 32 steps lives in RAM (it is lost at power-off; saving it with the patch is stage S4). It uses the arp's RATE, GATE, SWING and TEMPO (ARP page 1/2 or the SEQ page). The ARP page also still has the clock readout.

| # | Do | Expect |
| --- | --- | --- |
| 13.1 | Short press REC | REC LED on, the SEQ page opens (step grid, "RECORD STEP 1") |
| 13.2 | Play 4 keys | each sounds as a plain note and is a step; the key LEDs show the steps entered, the next one blinks |
| 13.3 | Press PLAY | a rest step (PLAY's latch does nothing while recording) |
| 13.4 | Press SEQ | a tie: the note before goes on through this step |
| 13.5 | Short press REC again | recording ends; the pattern length is the steps entered (grid shows it) |
| 13.6 | Hold SEQ (half a second) | SEQ LED on; the pattern plays at the tempo, rests are silent, a tied note runs on |
| 13.7 | RATE, GATE, SWING, TEMPO knobs on the SEQ page | speed, note length and shuffle change live |
| 13.8 | Hold a key while it plays | the pattern is transposed by (that key minus C4); let go: back as recorded. The tiles keep showing the recorded notes (no change, no gold). A latched key (PLAY) keeps it transposed |
| 13.9 | Hold SEQ again | stops; keys sound as plain notes again |
| 13.10 | Hold REC (0.7 s) | pattern cleared (LEN 0, NO PATTERN) |
| 13.11 | Record 32 steps | recording ends by itself at 32 |
| 13.12 | SEQ on, ARP on | the sequencer plays (the arp waits); SEQ off: the arp comes back |
| 13.13 | MIDI clock (tools/send_clock.py) | the sequencer follows it like the arp |
| 13.14 | Change patch (PRESETS) while it plays | it keeps playing (the pattern is not part of the patch yet) |
| 13.15 | CPU with the sequencer running | note the percentage |


## 14. Modulation lane (SEQ page, second knob set)

Flash `build/vavewave.fwsc`. The lane is one value a step (shown 0..127, 64 = neutral), as long as the note pattern, and moves one parameter while the sequencer plays. Like the pattern it is RAM only (lost at power-off, not in the patch; TARGET, DEPTH and GLIDE are in the patch). Record a pattern first (section 13). PLAY starts / stops the sequencer while SEQ is on.

| # | Do | Expect |
| --- | --- | --- |
| 14.1 | On the SEQ page press SEQ again | second set: TARGET, DEPTH, GLIDE, VALUE (the SELECT knob picks the step); the grid shows a bar per step (middle line = 64) |
| 14.2 | TARGET = POS, DEPTH 100, turn SELECT (step) and VALUE; SEQ on | the table position moves with each step's value; the edited step has a yellow frame |
| 14.3 | Value 64 on every step | no change in the sound (neutral) |
| 14.4 | TARGET = CUTOFF, then BEND, then DRIVE | each step moves that parameter; the knob on its own page is not changed |
| 14.5 | DEPTH negative | the movement is inverted |
| 14.6 | TARGET = OFF | no modulation; nothing else changes |
| 14.7 | PLAY (stop) | the modulation stops with the sequence (the parameter returns to the knob value) |
| 14.8 | Hold REC | pattern and lane cleared (all values 64) |
| 14.9 | CPU | note the percentage |
| 14.10 | GLIDE ON, different values on neighbouring steps | the target slides from each step's value to the next over the step instead of jumping |

## 15. Effects chain (FX page, sets 3 to 6)

Flash `build/vavewave.fwsc`. Press FX: set 1 and 2 are the filter, sets 3..6 are effect slots 1..4 (TYPE, A, B, MIX). The slots run in order, 1 first. Types so far: CHORUS, FLANGER (delay, reverb and phaser come later).

| # | Do | Expect |
| --- | --- | --- |
| 15.1 | Press FX five times | "FX SLOT 3": big text OFF (a slot does nothing while OFF), the chain line "OFF>OFF>OFF>OFF" |
| 15.2 | TYPE = CHORUS, play a held chord | the sound thickens and moves; MIX 0 = unchanged, MIX up = more; RATE changes the speed, DEPTH the width (stereo: left and right differ) |
| 15.3 | TYPE = FLANGER, FEEDBK up | a sweeping comb; with FEEDBK at maximum it rings but does not run away |
| 15.4 | TYPE back to OFF while a note holds | no click, the dry sound goes on |
| 15.5 | Chorus in slot 1, flanger in slot 2 | both are heard; swap them (the order changes the sound a little) |
| 15.6 | SAVE a patch with an effect, change patch, come back | the effect settings return with the patch |
| 15.7 | Effects while the arp / sequencer plays | still in time, no dropouts |
| 15.9 | Worst case: hold a 12-note chord (or latch it) with all 4 slots on (two chorus, two flanger) | no dropouts or crackle; note the CPU (the effects are not part of the voice-shedding budget) |
| 15.8 | CPU with one chorus and one flanger running (HOME / GLO page reading) | note the percentage |

## 16. Phaser, delay, reverb (FX slots)

Flash `build/vavewave.fwsc`. TYPE now also has PHASER, DELAY, REVERB. DELAY and REVERB can be in only one slot each.

| # | Do | Expect |
| --- | --- | --- |
| 16.1 | PHASER, held chord, RATE and FEEDBK | a slow sweeping swirl, stereo; FEEDBK makes it sharper; MIX at maximum is half wet on purpose |
| 16.2 | DELAY, play a short note, TIME 40 | ping-pong echoes left and right; FEEDBK sets how many; MIX the level |
| 16.3 | Turn TIME while echoes ring | the pitch glides, no click |
| 16.4 | REVERB, play a short note | a smooth tail; SIZE longer, DAMP darker |
| 16.5 | Reverb with a held pad, then release | the tail decays to silence (no hum, no hiss that stays) |
| 16.6 | Put DELAY in slot 1, then in slot 2 try TYPE | the TYPE knob skips DELAY (and the bottom line says DELAY SLOT 1) |
| 16.7 | Delay in slot 1 and reverb in slot 2, then swap the order | both work; reverb before delay and delay before reverb sound different |
| 16.8 | SAVE with a delay and a reverb, load another patch, come back | both return with their settings |
| 16.9 | CPU with a reverb running, one note and 12 notes | note both numbers |
| 16.10 | CPU with chorus + flanger + delay + reverb, 12 notes | note it; any crackle? |

## 17. Sequencer pattern saved with the patch (S4)

| # | Do | Expect |
| --- | --- | --- |
| 17.1 | SEQ on, REC a 6 step pattern, set a few mod lane values (SEQ page, set 2, MOD target on), SAVE | SAVED Unn |
| 17.2 | Power off and on, PRESETS to that patch | SEQ comes on and plays the same notes, ties and rests, with the lane's movement |
| 17.3 | Load a factory patch while the pattern plays | the sound changes, the pattern keeps playing |
| 17.4 | Record a different pattern, then PRESETS to the saved one | the saved pattern replaces it |
| 17.5 | Load the saved patch, change one lane value, look at SAVE | SAVE blinks (a lane edit counts); SAVE again replaces it, reload shows the new value |
| 17.6 | Save several patches with 16 step patterns (about 20) | each is saved; when the store is full the message says STORE FULL (delete one to go on) |
| 17.6b | While a pattern plays, switch to a user patch saved without a pattern | the pattern is emptied and the sound stops sequencing; switching to a factory preset leaves the pattern playing |
| 17.7 | Record a pattern, turn SEQ off, SAVE, PRESETS elsewhere and back | the pattern comes back with the patch (it is saved whether SEQ is on or off); SEQ on plays it |

## 18. Patch library in the web tool (P2)

Serve `tools/web` (see its README), connect, and look at "Patches on the FM-1". Keep the FM-1 quiet while writing.

| # | Do | Expect |
| --- | --- | --- |
| 18.1 | Connect with several user patches saved (one with a SEQ pattern, one using a user table) | each patch listed as U01.. with its name; the pattern patch says "N step pattern", the table patch "user table slot n" |
| 18.2 | Rename a patch to "TEST NAME" | the list shows it; on the FM-1 PRESETS shows the new name; loading it sounds the same as before |
| 18.3 | Move a patch down one place (▼) | the two patches change places; on the FM-1 the PRESETS order changed; both still sound right |
| 18.4 | Backup all, save the file | a JSON file |
| 18.5 | Delete one patch in the page, then on the FM-1 browse PRESETS | it is gone from the list |
| 18.6 | Restore the backup file | the deleted patch is back under its number, the others unchanged (names, sounds, patterns) |
| 18.7 | On the FM-1 save a new patch while the page is open, press Refresh | it appears |
| 18.8 | Press a key (hold it) and try Rename in the page | refused: keys down or sound playing; release and it works |
| 18.8b | Fill the store (about 20 pattern patches), Restore a backup that does not fit | the page reports the store is full; patches already there still load |
| 18.9 | Pull the cable during a restore, reconnect | every patch is either the old or the new version, nothing half written; the FM-1 works |

## 19. Navigation U1: tab strip, tab memory, help, REC clear asks (docs/UI-NAV-PLAN.md)

| # | Do | Expect |
| --- | --- | --- |
| 19.1 | Visit EDIT, FX, ENV, LFO, SEL, GLO; press ARP and SEQ | every page but HOME shows a tab strip under the knobs with the shown tab filled; the status line reads the page and n/N (e.g. `FX 1/5`, `ENV 2/2`, `SEQ 1/2`; SEL `VOICE`, LFO `LFO`, ARP `ARP`) |
| 19.2 | Press FX until FX3 (slot 2), go to ENV, press FX | FX opens on FX3 again (the tab is remembered); press FX again: FX4, then FILT 1/2 |
| 19.3 | EDIT on OSC2, HOME, EDIT | EDIT opens on OSC2; ALGORITHM and SELECT act on osc 2 |
| 19.4 | Power off and on | every page opens on its first tab again (tab memory is not saved) |
| 19.5 | Look at each page: pictures, SEQ grid, FX slot chain | nothing overlaps the strip or the status line; the SEQ grid shows all 4 rows; the lane frame is visible |
| 19.6 | On FX, hold HOME | HELP 1/2 explains FX (its tabs and knobs, FX AGAIN, delay/reverb once); HOME: HELP 2/2 (buttons); HOME: out |
| 19.7 | Hold HOME on ENV, GLO, ARP, SEQ, EDIT and on HOME | each help page 1 is about that page; no line runs off the screen |
| 19.8 | Record a pattern, hold REC | the name line asks `CLEAR PATTERN?`; the pattern keeps playing |
| 19.9 | Press REC | `CLEARED`, the pattern is gone |
| 19.10 | Hold REC again, then press any other button (or wait 5 s) | the question goes away, the pattern stays |
| 19.11 | With no pattern, hold REC | nothing is asked |
| 19.12 | Every page | no list of the knob values in the middle any more: the picture fills y 66..199; knob values and the patch name are in the middle-size font; status reads `CPU 16%` (a space) |
| 19.13 | FX: press FX twice | the FILT tab stays filled and its tag changes `1/2` -> `2/2` (CUTOFF.. then MODE..); third press: FX1. Same on LFO (`SET 1/2`, `SET 2/2`) and ARP |
| 19.14 | FX FILT: turn CUTOFF, RESO, FENV, MODE | the curve follows; FENV draws an arrow from the cutoff line (right = up, left = down) |
| 19.15 | FX1: set TYPE to CHORUS, look at FX2 | the chain shows CHO in slot 1, slot 2 filled (the one shown); DELAY / REVERB FREE under it |
| 19.16 | GLO CONTROL: turn MW>POS from 0 up | the WHEEL > POS line goes from grey to thicker lilac |
| 19.17 | ARP: change MODE, OCT, GATE, SWING | the 16-step picture follows (rows = notes over the octaves, bar length = gate, odd steps late with swing); OFF shows `ARP OFF` |
| 19.19 | SEL: play notes, then turn each knob | MODE panel: POLY shows 12 voice blocks lit by the notes, MONO / LEGATO one voice (LEGATO with a tie); BEND> lights OSC1, OSC2 or both; QUAL HI smooth / ECO a coarse line with points; CURVE LIN straight / EXP curved |
| 19.20 | FX FILT 1/2: turn CUTOFF from 0 to 100 | a white line with its frequency (20 .. ~19K) moves across the curve; at 70 it reads about 2.5K |
| 19.21 | Press SEQ, press SEQ again; same with ARP | SEQ: NOTES, then LANE (the sequencer is not switched); ARP: SET 1/2, then SET 2/2 |
| 19.22 | Hold SEQ half a second; hold ARP half a second | the sequencer / the arp switches on or off at the half second, the page does not change |
| 19.23 | LFO: turn RATE from 0 to max | the picture goes from 1 cycle to 8 cycles; SHAPE still changes the shape |
| 19.24 | GLO OUTPUT: note DROPS, play the fast same-note repeats that click | DROPS stays 0 (if it counts up with the clicks, the audio is overloaded: report CPU MAX and the page you were on) |
| 19.25 | Hold HOME on HOME | HELP 1/2 HOME: knobs 1-4 named as HOME's own (each page has its own), ALGORITHM / SELECT / PRESETS, HOME AGAIN, HOLD HOME, the page buttons; no "ALG SEL" line |
| 19.26 | On HELP press FX, then SEQ, then SEQ again | the help switches to FX, then SEQ, staying on HELP; the second SEQ opens the SEQ page |
| 19.27 | SEQ on, record a pattern, hold REC, press REC | `CLEARED`; the SEQ state reads `EMPTY` (not PLAY); REC again: record mode, REC LED on |
| 19.28 | GLO BOOT: press and hold each button, watch HELD NOW | each button lights only while held; if REC stays lit after letting go, the REC switch sticks (hardware) |
| 19.29 | ARP GATE (and any knob): turn slowly, then fast | slow = 1 a detent, fast = up to 4 a detent; no jump to 0 |
| 19.30 | FX FILT: CUTOFF 0 and 100 | the label reads 20 and 18K |
| 19.18 | Turn a knob fast on FX FILT while playing | the picture keeps up (at most ~16 redraws a second); notes do not stutter |
