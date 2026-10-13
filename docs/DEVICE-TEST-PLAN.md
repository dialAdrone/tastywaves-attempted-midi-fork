# VAVEWave device test plan (engine v1 + panel UI, commit 193d2dc)

Everything below was verified on a computer (host mode), none of it on the FM-1 yet. What to look for is in
*italics*; record numbers in the "Result" column or in `docs/TASKS.md` Log. Controls: docs/UI-DESIGN.md.

## 0. Before flashing

- [ ] `rules-reviewer` verdict on HEAD is SAFE TO INSTALL (asked for 2026-10-12; do not flash without it).
- [ ] MASTER down. Headphones off or low for the first boot.
- [ ] Stock `FM-1_093` package downloaded from the FM-1 Firmware Hub (way back). Rescue gesture (docs/research/rescue-entry.md): slide switch OFF at the back, hold OCT- ALONE (not OCT+, the button next to it), slide the switch ON, keep holding until `USB RESCUE` shows. Plugging USB in does not trigger it. NOT yet confirmed on this unit (failed once, see 1.6).
- [ ] `build/vavewave.fwsc` is from HEAD: `./build.sh` shows image 512,688 B, identity FM-1_950.
- [ ] `.venv/bin/python tools/fm1_install.py --info` reads the device (FM-1_950 bench now, or FM-1_093).

## 1. First boot (2 min)

| # | Do | Expect |
| --- | --- | --- |
| 1.1 | Power on / after install restart | Screen draws HOME spectrum look, patch `001 ...`, no crash screen, no sound until a key |
| 1.2 | Play keys F3..G5 | Sound, key LED follows, screen lights the keys' voices count `V n/12` |
| 1.3 | Settings left by the old bench build (octave) | Loads without garbage (no odd page, octave -2..+2) |
| 1.4 | Leave it 40 s, then power-cycle | Boots again normally (the 30 s boot-ok clear) |
| 1.5 | Switch OFF; hold OCT- alone with the left button only; slide switch ON; hold ~2 s | Enters rescue (`VAVEWAVE USB RESCUE`); then power-cycle normally. Try it with USB unplugged (battery) and with USB connected; note which works. Proves the way back |
| 1.6 | If 1.5 did not enter rescue (normal app started): hold OCT- at power-on, let the app start, press GLO three times | BOOT KEYS page (`GLOBAL`s status line reads `BOOT KEYS 3/3`). Read `BTN 0x....`: bit 0 (`0x0001`) = OCT- was seen held by the boot scan; `0x0000` = the scan saw no key. `FR` = key-scan frames counted. `RAW` = 11 hex bytes, one per scan column 0..10, bit = row, 1 = closed; note which byte is non-zero and write all of it down (photo) for the report. Compare with a normal boot without holding anything |

## 2. CPU and timing (the numbers we still need) (10 min)

Read `CPU %` on the status line (live) and `CPU MAX` on GLO (EDIT held + HOME restarts the max).

| # | Setup | Result (MAX %) | Expect |
| --- | --- | --- | --- |
| 2.1 | Idle, no notes | | ~0 % |
| 2.2 | 1 note, default patch | | ~4-6 % |
| 2.3 | 12 held notes (a cluster on the keys, plus OCT± for more) default patch (sinc, LP) | | ~50 % estimate (typical) |
| 2.4 | As 2.3 + BEND +63 on OSC1 and OSC2 (SEL: BEND> BOTH), SUB and NOISE levels up, FX MODE = HP | | ~65 % estimate (worst) |
| 2.5 | As 2.4 with OSC2 on a different table than OSC1 | | flash cache effect, may add a few % |
| 2.6 | As 2.4 with SEL QUAL = ECO | | ~53 % estimate |
| 2.7 | HOME look 1 (waterfall): turn SELECT quickly across frames while 12 notes sound | | CPU % must not rise; listen for dropouts |

Pass: worst case below 75 %, no crackle. *Fail signs:* crackle, `CPU` red (> 80 %), notes freezing (shed guard).

## 3. Sound quality (15 min, by ear)

- [ ] Each of the 12 tables (ALGORITHM), sweeping SELECT slowly: smooth morph, no zipper noise, no clicks at frame
      boundaries. *Note any table that has a rough step.*
- [ ] High notes on bright tables (NOISE, SHAPE, FOLD at the top octave): aliasing vs QUAL HI/ECO. *Expected: HI
      clean; ECO audibly rougher on bright tables.*
- [ ] BEND from 0 to +63 and -64 on SAWSQ: smooth brightening, no zipper, pitch unchanged. Compare with osc 2 only.
- [ ] Filter: CUTOFF sweep with RESO 90+: no blow-up, no sticking tone after notes release (silence returns to true
      zero). Try HP and BP.
- [ ] Mip-level step: slide GLIDE across a long range up through the top octaves: *a small timbre step at the level
      boundaries is known*, anything louder is a bug.
- [ ] Filter MODE change while a note holds: *a click is known* (T16 risk 3).
- [ ] Drone case: hold 8-12 notes for 5 minutes with slow LFO to POS: no drift in level, no clicks, no heat issue.
- [ ] Level: 12 notes at velocity max, DRIVE 0 and DRIVE 127: no hard clipping at 0, intended saturation at 127.

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

- [ ] Keys also send USB-MIDI notes (check with a DAW / MIDI monitor).
- [ ] Incoming notes play, velocity matters (VEL>AMP on GLO 2).
- [ ] Mod wheel (CC 1) -> MW>POS / MW>VIB; pitch bend with PB RNG; channel aftertouch -> AT>CUT.
- [ ] CC 120 all sound off silences everything; CC 123 all notes off.
- [ ] Note flood from a DAW (a fast 16th arp at 180 BPM, 12 notes): no stuck notes (`ev_dropped` counter is
      not shown yet; a stuck note after a flood means the 64-entry event ring overflowed).

## 6. Save and persistence (5 min)

- [ ] Edit a patch (the lilac tick appears at the patch line, SAVE LED blinks), press SAVE while silent: `SAVED`
      shows, LED goes solid.
- [ ] Press SAVE while notes sound: the write waits for silence (no audio glitch from the flash erase), then saves.
- [ ] Power-cycle: patch, octave and HOME look come back.
- [ ] Press SAVE repeatedly: at most one write per second (no stall).
- [ ] *Known stub:* one user patch overlay only (T17 will add real slots).

## 7. Robustness (10 min)

- [ ] Turn encoders very fast, mash page buttons, hold EDIT while turning knobs: no crash, no lock-up.
- [ ] Change preset while 12 notes ring: voices release without a crash (mono presets release all).
- [ ] USB cable pulled and re-plugged while playing: audio keeps running; MIDI recovers.
- [ ] 15-minute soak with a drone: no crash screen, no drift. If the red crash screen appears: note vector, PC and
      return address (docs: skill `fm1-crash-triage`) and tell me.

## 8. Report back

Send me: the table 2 numbers, any table/note that sounds wrong (table, frame, note, setting), screens that are hard
to read or lag, and the answers to the design review points. I update `docs/TASKS.md` (T14, T18) and decide the
next steps (effects, patch storage T17, arp / sequencer T27-T29).

To go back: with the app running, `fm1_install.py` the stock `FM-1_093` package (OTA stays on). If the app won't start: two failed boots enter the rescue by themselves; the manual OCT- gesture is unconfirmed on this unit until 1.5/1.6 are resolved.
