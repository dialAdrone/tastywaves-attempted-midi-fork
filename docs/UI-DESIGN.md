# VAVEWave UI design (T18)

Status: phase 2 done (2026-10-12): the mock-ups are wired to the engine and the panel works. Code:
`firmware/src/ui_screens.h` (screens drawn from a plain `ui_view_t`), `firmware/src/panel.h` (what each control does,
the factory patches, SAVE, filling the view from `patch.p[]`), `firmware/src/vavewave.c` (main loop).
Tests: `tests/run_panel.sh` (the real app, `tests/scripts/panel_ui.txt` -> `build/host/panel-*.png`, 480x480 = 2x) and
`tests/run_ui.sh` (the phase-1 demo with fake values, `build/host/ui-*.png`).

## Decisions (user, 2026-10-12)

1. **HOME looks.** All three stay; HOME again (while on HOME) cycles them in the order 2 spectrum -> 1 waterfall ->
   3 minimal -> 2; the look is kept in the settings sector. Default: look 2 (spectrum).
2. **Four knobs are fixed per page** (below); no per-patch assignment.
3. **Waterfall.** Frame 0 in front, the highest frame at the back; only the nearest frame is highlighted (no
   interpolated trace).
4. **Value formats** as drawn: times in ms / s (the engine's own 0.5 ms .. 10 s curve), levels in % (0..127 -> 0..100),
   detune in cents (+-50), BEND / FENV / E>BEND in raw steps (-64..63; FENV = semitones), LFO rate in Hz.
5. **BEND is drawn live**: EDIT shows the frame as the engine reads it (position morph + `eng_warp_preview()`), so the
   picture bends while BEND != 0. (How: below, "BEND preview".)
6. **SAVE saves at once** to the current slot, no prompt: the SAVE LED goes solid and the patch line shows `SAVED` for
   0.9 s. Persistence of real slots is T17; until then SAVE is a stub (below).
7. The frame position is labelled `FRM` (`FRM 13/32`).

## Common frame (all pages, all looks)

| rows | what |
| --- | --- |
| y 0..35 | four knob columns, 60 px each: label (FONT_S), value (FONT_M 12x24, FONT_S when > 4 characters or a time / name), bar. The last turned knob is lilac. Matches the stock screen (values on top, over the knobs) |
| y 38..65 | every page but HOME: the tab strip (15 rows), the tab shown filled; under it, when that tab holds two knob sets, a tag `1/2` / `SET 2/2` |
| y 66..199 | every page but HOME: the page's picture (134 rows), the biggest thing on the screen. The knob values are not repeated in it. HOME: the middle y 56..181 |
| y 202 | status line (14 rows): left = what this is (table + frame, or page + tab n/N), right = `V 5/12 CPU 29%` (voices sounding / 12; CPU red above 80 %) |
| y 218 | patch name, FONT_M, centred (`001 BRIGHT PAD`); a lilac tick at the left edge while the patch differs from what was loaded / saved; `SAVED` for 0.9 s after SAVE. The minimal HOME look keeps it big (FONT_L) |

Pictures: EDIT the oscillator's wave (2 px line) and the position bar; FX FILT the filter response with the cutoff as a white
line and its frequency (`2.5K`), the envelope's reach as an arrow; FX1-FX4 the chain of four slots (the one shown filled) and where delay and reverb
are; ENV the envelope of the tab shown; LFO two cycles of the shape; SEL a panel per knob (MODE: the 12 voices lit
while they sound, or one MONO / LEGATO voice; BEND>: OSC1 / OSC2 lit; QUAL: a smooth or coarse cycle; CURVE: a
straight or exponential decay); GLO OUTPUT the drive curve
and CPU MAX, CONTROL the wheel / aftertouch / velocity routes (thicker = more), BOOT the boot key scan; ARP 16 steps of
the pattern a three-note chord plays (octaves, gate, swing); SEQ the state, the steps and the 4 x 8 grid. A picture
that keeps changing (a knob turning) is redrawn at most every 60 ms.

FONT_S is 8 px/char, FONT_M 12, FONT_L 16: a column holds 7 / 4 / 3 characters, the patch line 15 (FONT_M) — 20 fit, names are 15. Lower case is folded to
capitals by the font code, so names may be stored either way.

## A. HOME looks (renders: `panel-home2_spectrum.png`, `panel-home1_waterfall.png`, `panel-home3_minimal.png`)

1. **Waterfall.** The 32 frames of the table as stacked lines (frame 0 front, each next one 3 px back and 2 px
   right), hidden-line, the nearest frame in white with an arrow.
2. **Spectrum** (default). The current frame as a 240x56 waveform, a 32-tick position bar with a white marker, and 16
   harmonic bars (log scale, 4 px = 6 dB, fundamental white).
3. **Minimal.** Patch name big under the knobs, table name big in lilac, a wide position bar, a small waveform,
   voice squares (one per voice, lit while sounding) and the status line.

All three show: 4 knob values + labels, patch name, table name, frame number, voices in use / 12, CPU %.
HOME knobs: CUTOFF RESO ATK REL (ATK / REL as times, like on ENV, because they are the same parameters).

## B. Pages (knobs are fixed per page and set)

Same button again = the next set (tab). Since U1 (docs/UI-NAV-PLAN.md) every page but HOME shows a tab strip at y 54..71
(all tab names, the shown one filled; replaces the EDIT chips), its picture starts at y 72, and a page opens on the tab it
was left on (session only). The status line reads `PAGE n/N`. Hold HOME: help for the page it was pressed on.

| page (button) | set | KNOB1 | KNOB2 | KNOB3 | KNOB4 | middle |
| --- | --- | --- | --- | --- | --- | --- |
| HOME | - | CUTOFF | RESO | ATK | REL | look 1 / 2 / 3 |
| EDIT | OSC1 | POS | DETUNE (cents) | LEVEL | BEND | the frame as read (morph + BEND), position bar, osc chips |
| EDIT | OSC2 | POS | DETUNE | LEVEL | BEND | same, for osc 2 |
| EDIT | SUB, NOISE | - | - | LEVEL | BEND | a sine / noise picture |
| FX (the filter) | 1 | CUTOFF | RESO | FENV | KEYTRK | filter response picture (log axis 20 Hz..19 kHz), list |
| FX | 2 | MODE LP/BP/HP | FVEL | E>POS | E>BEND | same |
| ENV | amp | ATK | DEC | SUS | REL | both curves, the edited one lilac and filled |
| ENV | filter | ATK | DEC | SUS | REL | same |
| LFO | 1 | SHAPE | RATE | >POS | >PITCH | the shape, list |
| LFO | 2 | SHAPE | RATE | >CUT | >AMP | same |
| SEL (voices) | - | MODE POLY/MONO/LEGATO | BEND> BOTH/OSC1/OSC2 | QUAL HI/ECO | CURVE LIN/EXP | the mode big, 12 voice squares, list |
| GLO | 1 | VOLUME | DRIVE | GLIDE | PB RNG | drive transfer curve, CPU max, list |
| GLO | 2 | MW>POS | MW>VIB | AT>CUT | VEL>AMP | same |
| GLO | 3 (BOOT KEYS) | - | - | - | - | diagnostic: what the key scan saw at power-on (`BTN 0x0001 FR 80`, `RAW` = 11 column bytes in hex), list |

BOOT KEYS: `app_main` copies `fm1_in.buttons`, `fm1_in.frames` and `fm1_in.raw[0..10]` into `boot_btn`, `boot_frames`,
`boot_raw` first thing (the values the `recovery_key()` scan left; host mode shows fake ones). BTN is the debounced
button mask (bit 0 = OCT-), RAW is one byte per scan column, bit = row, 1 = closed. Reached by GLO pressed three times.

FX has no effects in v1 (the chain is empty), so the FX button hosts the filter page; the name changes when effects
exist. FX / LFO / SEL / GLO show the page's eight values as a 4 x 2 list under the picture (the shown set bright, the
other grey), so nothing is hidden behind the second set.

| control | what |
| --- | --- |
| ALGORITHM | wavetable of osc 1 (osc 2 while the EDIT OSC2 chip is shown), 0..11, one per detent |
| SELECT | position 0..4095 of the same oscillator, 32 per detent, a quick turn goes d x |d| faster, from any page |
| KNOB1-4 | the page's four values, a quick turn goes faster (enumerations step by one) |
| PRESETS | patch 1..8: the name changes at once, the sound loads after 400 ms without a turn (edits are dropped) |
| HOME / EDIT / FX / ENV / LFO / SEL / GLO | the page; the same button again: the next set (HOME: the next look) |
| SAVE | save the patch (below) |
| EDIT held + OP1..OP4 (black keys F#3 G#3 A#3 C#4) | pick OSC1 / OSC2 / SUB / NOISE (the key does not sound) |
| EDIT held + HOME | restart the CPU maximum (GLO page shows `CPU MAX`); stays on the page |
| OCT- OCT+ | octave -2..+2 (kept in the settings) |

## BEND preview (item 5)

`pn_edit_wave()` in panel.h computes the 256 drawn samples in the main loop: for each output sample i it takes the
phase `ph = i << 24`, maps it through `eng_warp_preview(ph, P_WARP)` (synth.h; the same `ph + A sin(ph)` map the
voices use, set up with `warp_setup` at a low note so the per-note limit does not apply) and reads the frame there,
morphed between the two neighbour frames at the patch position, linearly interpolated. It is safe from the main loop
because that function is a pure function of its two arguments over `const` tables (SIN_T, the wavetables): it touches
no voice, filter or ring state and is never called from the ISR concurrently with a writer. The only shared data read
is `patch.p[]` (volatile, 32-bit loads, one writer: the main loop). It is recomputed only when table, position, BEND,
BEND destination or the chip changes (`wave_key`), then drawn as a 27 KB canvas. BEND applies to the chip's oscillator
only when P_WARP_DEST selects it (BEND> on SEL).

## Settings, SAVE and presets

- Settings struct (`app_set_t` in vavewave.c, 102 of the 256 bytes): version byte, octave, HOME look, last patch slot,
  `uslot` + `uval[47]` (the saved user patch). Validated after load (version, ranges of every `uval` against
  `PDESC`); anything wrong = defaults. Saved 2 s after the last change when `quiet()`.
- Factory patches: 8 in a const table in panel.h (`PRESET[]`: tables, positions, filter, envelopes, a few
  modulations on top of `patch_reset()` defaults), so the synth is playable out of the box.
- **SAVE (stub, T17 is real slots).** The working patch is copied to `uval` and the settings sector is written; the
  patch then replaces that factory slot when it is loaded (`uslot`). One user patch at a time: a SAVE on another slot
  replaces it. The write waits for `quiet()` (the flash erase stops the sound) and at most one a second; nothing
  changed = only the `SAVED` flash. The blinking SAVE LED shows the patch is edited. `FL_RANGE_OK` and the flash
  write path are untouched.
- "Modified" = the patch differs from what was loaded or saved, so a MIDI CC that moves a value shows the tick too.

## Redraw cost (what is sent over SPI; 115 KB = whole screen)

Everything compares its inputs with `ui_shadow_t` and draws only on change. Costs per event:

| event | drawn | bytes |
| --- | --- | --- |
| knob turn | one column, 60x52 canvas | 6 KB |
| page / look change | clear + everything once | 115 KB + pieces (about 25 ms) |
| position move, same frame | position bar only (240x12..20) | 6..10 KB |
| nearest frame changes | waterfall: 2 strips of 240x63; look 2: wave 27 KB + bars 23 KB; look 3: wave 19 KB | 61 / 50 / 19 KB |
| EDIT: position / BEND / table | the waveform 240x56 | 27 KB |
| FX / LFO / GLO value | picture 240x56 (13 KB) + list 240x64 (15 KB) | 28 KB |
| voices or CPU change | status line 240x16, CPU sampled 4 times a second | 7.6 KB |
| env knob | the two curves 240x63 | 30 KB |

Staging of the waterfall: the region is 240x126, bigger than `CV_MAX` (240x64), so it is two canvases of 240x63 filled
from the same traces with `cv_oy = -strip_top`, skipping traces that do not touch the strip; back to front, each
segment fills black below its line (hides the lines behind), the nearest frame goes last without fill. Integer only.
The spectrum is a 64-sample DFT of mip level 2 (exactly harmonics 1..16): 2048 multiplies, only on a frame change.

Main loop vs audio: a pass draws at most one frame-view change plus 4 knob columns; `ui_waterfall()` calls
`platform_poll()` between its two strips. Not measured on the chip: draw time (host timings mean nothing). Expected
from the blit sizes: a waterfall pass is a few ms of drawing plus 61 KB of SPI. The ISR is not blocked by drawing (the
LCD is DMA; the audio and key ISRs outrank the main loop); the cost is only main-loop latency, so `platform_poll()` in
the loop stays well under the 8 s watchdog and the USB service rate. Measure the longest pass on the FM-1 (`fm1_ticks`)
before adding anything bigger.

## LEDs

- Page buttons: the shown page's button is lit (HOME / EDIT / FX / ENV / LFO / SEL / GLO), one at a time.
- SAVE blinks (about 2 Hz) while the patch is edited; solid for 0.9 s after a save.
- Key LEDs: lit while the key's note sounds; OCT- / OCT+ lit when the octave is below / above 0.
- While EDIT is held the four oscillator black keys (OP1..OP4) light as the osc chips.
- ARP / SEQ / PLAY / REC dark in v1.

## Fonts (task B, not applied)

The shipped font is Terminus 8x16 (SIL OFL 1.1; FONT_L is the same bitmap doubled). `tools/gen_font.py` converts a
BDF or TTF/OTF to `felucca_font.h` (it reproduces the shipped header from Terminus' `ter-u16n.bdf` byte for byte except
for one trailing newline). `tests/font_sheet.py` builds the comparison sheets `build/host/fonts/font-sheet-text.png`
and `font-sheet-screens.png`. The font is NOT replaced: pick from the sheet.

Now shipped: Spleen (BSD-2-Clause) 8x16 (FONT_S), 16x32 (FONT_L) and 12x24 (FONT_M, Spleen 2.0.0, for the knob
values and the patch name), from `assets/fonts/` with `tools/gen_font.py ... --medium assets/fonts/spleen-12x24.bdf`.

## Left open

- The position marker shows the patch position, not the modulated one (LFO / envelope moving it): `eng_pos_now()` exists
  for a live marker.
- Osc 2 coarse tune (SEMI2) and the pitch / MW amounts have no knob yet (MIDI CC only): they fit a second EDIT set.
- A saved user patch is not marked in the name; real slots, rename and copy are T17.

## HELP screens (2026-10-13)

Hold HOME for about 0.7 s (from any page) to open two text pages: 1/2 what each button does, 2/2 what the knobs
control on each page. HOME steps to the next help page and then leaves; any page button goes straight to that page.
Because a hold must not cycle the HOME look first, the look now changes when HOME is released after a short press
(from the HOME page); from another page HOME still goes home at once. Source: `UI_HELP` in `ui_screens.h`, entry in
`pn_buttons` (`panel.h`). Test: `tests/scripts/panel_ui.txt` (shots help1, help2, after_help).
