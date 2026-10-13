# Code map (read this instead of re-reading the source)

Dinghy = one compilation unit. `build.py` compiles **only** `firmware/src/vavewave.c`; it `#include`s
`platform.c`, which `#include`s everything else (`libc.c lcd.c gfx.c usb.c fm1_flash.h ota.c recovery.c`, HAL
headers). Static functions everywhere, no linking between .c files. Target: JieLi AC791N, pi32v2, JieLi clang
4.0.1, no FPU, no libgcc. Goal of this repo: custom **M-VAVE FM-1 synth firmware** by editing `vavewave.c`.

## Files that matter (and sizes)

| file | lines | what | touch? |
| --- | --- | --- | --- |
| `firmware/src/vavewave.c` | ~220 | THE APP: main loop (keys, buttons, encoders, MIDI, LEDs, settings, draw); BENCH 1 = the CPU bench loop | **yes** |
| `firmware/src/panel.h` | ~410 | the front panel: pages / knob table `PG`, factory patches `PRESET`, SAVE stub, view filling, EDIT BEND preview | **yes** |
| `firmware/src/platform.c` | 344 | boot, ISRs (timer5, alnk0), `platform_poll`, settings, MIDI, OTA glue, crash screen | rarely |
| `firmware/src/gfx.c` | 183 | canvas (`cv_*`), `draw_text_box`, fonts (data in `firmware/gen/felucca_font.h`) | no |
| `firmware/src/lcd.c` | 130 | ST7789 SPI DMA driver | no |
| `firmware/src/usb.c` | 1305 | USB-MIDI + UAC1 audio + OTA SysEx; polled from timer5 ISR | no |
| `firmware/src/ota.c`, `recovery.c`, `bootguard.h` | | updater / OCT- rescue / crash-loop guard. **Never break** | no |
| `firmware/src/libc.c` | 86 | memset/memcpy, `str_*`, `fmt_int`, `fmt_fix`, `rng()` (all usable from app) | maybe |
| `firmware/hal/*.h` | | the ONLY place registers may be touched (build enforces) | add helpers only |
| `firmware/app.ld`, `crt0.S` | | memory map: XIP 568K, RAM 96K, pool 336K, stacks | rarely |
| `firmware/loader/*` | | the RAM update loader packed into the .fwsc | no |
| `tools/build.py` | 311 | compile, link, post-build checks, package. `PRODUCT` = identity | release only |
| `tools/fm1_install.py` | 528 | USB-MIDI SysEx installer (`--info`, asks before writing) | no |
| `firmware/src/ui_screens.h` | the screens (T18, `docs/UI-DESIGN.md`): drawn from a plain `ui_view_t`, redraw only what differs from `ui_shadow_t` | yes |
| `tests/ui_demo.c`, `tests/run_ui.sh` | the phase-1 demo (fake values) -> `build/host/ui-*.png` | |
| `tests/run_panel.sh`, `tests/scripts/panel_ui.txt` | the real app on the fake FM-1 -> `build/host/panel-*.png` (HOME looks, EDIT with BEND, ENV, FX, LFO, SEL, GLO, SAVE) | |
| `tools/gen_font.py`, `tests/font_sheet.{c,py}` | BDF / TTF -> `felucca_font.h`; font comparison sheets (not applied) | |
| `tests/host.c` + `run_host.sh` | | fake FM-1 on the computer: PPM screens + WAV | extend for new inputs |

## Runtime structure

```
fm1_cstart (platform.c)  -> init HW -> app_main()      (never returns; must call platform_poll())
TIMER5 ISR 10 kHz        -> fm1_input_tick (keys/enc/LED scan), fm1_ms, usb_poll @2 kHz (prio outranks audio)
ALNK0 ISR every 5.8 ms   -> zero 256 stereo frames -> app_audio(out,256) -> * master_q12 -> clip -> UAC tap -> <<7 -> DAC
main loop (app_main)     -> platform_poll(); read fm1_in / fm1_input_edges / fm1_enc_take / midi_get;
                            update voice params; draw changed UI; settings_save when quiet
```

Audio ISR output: Q15 ±32767, `out` pre-zeroed, you **add**. Budget 5.8 ms/block, aim < 2.9 ms.
Main↔ISR sharing: `volatile` 32-bit, one writer each (vavewave.c: voice `inc`/`gate` written by main, `amp`/`ph` by ISR).

## vavewave.c anatomy (the original sine build; the engine is engine.h, the panel panel.h)

- `voice[NV=4]`: `inc` (phase step), `gate` (target level Q15), `amp` (ISR-owned glide), `ph`, `age`, `note`.
- `sine(ph)`: parabola approximation (~0.1%), integer only.
- `app_audio`: per voice per frame: linear attack +48/frame, release -6/frame toward `gate`; skips silent voices.
- `INC_C9[12]` = phase increments for MIDI 120..131 at 44.1 kHz; `inc = INC_C9[n%12] >> (10 - n/12)`.
- `play()` voice stealing: same note → free (lowest amp) → oldest. `stop()`, `quiet()`.
- Keys: `fm1_in.notes` bit0=F3 (MIDI 53) .. bit26=G5; `base = 53 + 12*oct` (oct −2..2 from `settings`).
- Screen: note name (FONT_L), 16-white-key keyboard canvas (240x56 at y=140), octave text; redrawn only on change.
- Settings: `struct {int8_t oct;}` loaded at start, saved 2 s after last change when `quiet()`.
- MIDI in: any channel; keys also `midi_send` note on/off.

## Panel / UI (T18, `docs/UI-DESIGN.md`)

- `app_main` (non-BENCH): `settings_load` -> `pn_validate` -> `pn_init`; each pass: `platform_poll`, buttons
  (`pn_buttons`), keys (EDIT held + black keys 1 3 5 8 = OP1..OP4 pick the oscillator chip instead of playing),
  `pn_encoders`, 400 ms preset load, `pn_save_poll`, MIDI in, CPU sample (4 Hz, `cpu_last` -> live %, `cpu_max` -> GLO
  page), `pn_view_fill` -> `ui_draw` (shadow compare), LEDs, 2 s autosave when quiet.
- `PG[page][set][knob]` (panel.h) is the whole control map: label, `P_*` id, format, step. Add a parameter there.
- Gestures: page button again = next set, HOME again = next look (2 -> 1 -> 3, kept), EDIT held + HOME = reset CPU max,
  SAVE = write the settings sector (stub: one user-patch overlay on the factory slot; T17 does real slots).
- Settings (`app_set_t`, vavewave.c, 102 B): ver, oct, look, slot, uslot, uval[P_COUNT]; `pn_validate` range-checks.
- BEND preview: `pn_edit_wave` uses `eng_warp_preview` (pure, const tables) from the main loop.

## Hardware facts (FM-1)

- 27 keys (F3..G5), 14 buttons (`BTN_*` 0..13), 7 encoders (`ENC_*`; SELECT, ALGORITHM, KNOB1-4, PRESETS),
  MASTER pot (platform scales volume; read raw via `fm1_adc_read(FM1_ADC_MASTER)`), battery ADC.
- LEDs: `fm1_led_key(id,on)`; key LED id = `KEY_LED(k)` = 14+k; button LED id = `BTN_*`.
- Screen 240x240 RGB565; `lcd_fill`, `draw_text_box`, canvas ≤ 240x64 (`CV_MAX`). FONT_L = caps/digits only.
- Audio: 44.1 kHz stereo, 24-bit I2S, DAC ceiling −6 dBFS; USB audio 44.1/48 (FIR resampler in usb.c).
- Flash 1 MiB: settings sector 0xFC000 (4 KiB, ≤256 B payload), OTA staging 0xE0000. More storage → widen
  `FL_RANGE_OK` in platform.c (0x97000..0xFBFFF is free in Dinghy).
- Black keys are silk-screened (F#3..): OP1 OP2 OP3 | OP4 OP5 | OP6 PIT GLO | MONO POLY | unlabelled; no encoder shows a push switch. Stock screen: 4 icon+value columns top, waveform middle, patch name bottom.
- Rescue: hold OCT- at power-on, or 2 boot crashes. Web hub / `fm1_install.py` always works if OTA kept on.

## Host mode (tests/host.c) — what it can and cannot do

- Script lines (T in ms): `T keys MASK`, `T btn ID` (press edge only), `T bdown ID` / `T bup ID` (pressed and held: `fm1_in.buttons`; bdown also gives the edge, bup none), `T enc ID DELTA` (ID 0..6 = ENC_SELECT..ENC_PRESETS, signed
  detents, + = clockwise; `fm1_enc_take` returns the sum since the last call, then 0), `T midi ST D1 D2`, `T shot NAME` (writes `OUT-NAME.ppm` at that time; any shot line replaces the default 1 s / 3 s
  screens), `T end`.
  Output: `-<ms>.ppm→png`, `.wav`, stdout MIDI-out / settings-saved / `LED id on|off` (on change) log. 3rd arg FPS →
  frame sequence. Example: `tests/scripts/enc_demo.txt`; `fm1_input_note_edges` reports scripted key presses.
- Self-check of the fake: `cc -Wall -Wno-unused-function -DDINGHY_HOST -Ifirmware/src -Ifirmware/gen -o build/host/selftest
  tests/host_selftest.c && build/host/selftest` (asserts enc accumulation/reset, note edges).
- **Gaps** (extend host.c when a feature needs them): no ADC/MASTER, no flash persistence between runs,
  `fm1_adc_read` and `fm1_ticks` absent, no timing/CPU measurement (task T5).
- Host is x86 with `float` available: **host success does not prove the chip build** (float, 64-bit div, ROM refs
  only fail in `./build.sh`).

## Environment notes (uncommitted WIP on branch `add_uv`)

- `build.sh` has `source .venv/bin/activte` (typo: `activate`) and sets `PY=python`; `tools/install.sh` is being
  turned into a uv-based setup. Toolchain: `~/.jieli/toolchain`, SDK: `~/fw-AC79_AIoT_SDK`, macOS builds run in
  Docker (`linux/amd64`, must be running).
- `.venv` has Pillow requirement for build and `run_host.sh`; `mido python-rtmidi` for `fm1_install.py`.

## Agents and skills for this goal (`.claude/`)

| agent | use for |
| --- | --- |
| `synth-dsp` | audio engine: oscillators, envelopes, filters, FM, FX, CPU budget |
| `panel-ui` | keys/buttons/encoders/LEDs/screen/settings, presets, host-mode input gaps |
| `fm1-build-verifier` | build, host test, check failures, environment |
| `rules-reviewer` | read-only review vs. chip rules / brick risks before commit |
| `release-engineer` | releases, identity, install on device (confirms first), hub listing |

Skills: `fm1-build-test`, `fm1-fixed-point`, `fm1-add-control`, `fm1-crash-triage`, `fm1-release-install`.
Controller flow for a feature: `synth-dsp` and/or `panel-ui` (parallel if files don't collide: both edit
`vavewave.c`, so serialise or split the code into headers first) → `fm1-build-verifier` → `rules-reviewer` →
(user ok) `release-engineer`.

## Build baseline (unmodified Dinghy, 2026-10-11)

image 55076 B of 581564; RAM .data+.bss 12368 B of 98304; pool 30720 B of 344064; package 610110 B, identity FM-1_990 (since changed to FM-1_950, same size).
Python is pinned to 3.12 (`.python-version`): python-rtmidi has no cp313 macOS wheel and fails to compile.

## Wavetables (T15, branch `wavetable_synth`; not yet used by firmware)

- `tools/gen_wavetables.py` (numpy, PC only) synthesises the 12 factory tables x 32 frames (SAWSQ PWM HARM VOWEL
  FMIDX FOLD ORGAN BELL NOISE SHAPE REED STAIR) → `firmware/gen/wavetables.h` (`WT_DATA` int16, `WT_DIR[12]`
  {name, frames, offset}, `WT_LEVEL_OFF[6]`) + `build/wavetables.bin`. Frame = 6 mip levels of `256>>L` samples + 1
  guard, harmonics `64>>L`, 510 int16 = 1020 B; table 32,640 B; total **391,680 B**. Per-table gain, peak 0.9 FS.
  Not included from `firmware/src` yet; while it stays unincluded the image does not change (55,076 B).
- Level rule without a divide: the smallest L with `inc < 2^(25+L)`.
- `tests/wavetable_spectrum.py [--variants]` is an integer model of the oscillator plus an FFT alias report →
  `build/wavetable_spectrum.{txt,png}`. Findings are in `docs/research/wavetable-spectrum.md`: with **linear**
  interpolation the scheme measures only about 20 dBc in the worst case, because each level is just 4x oversampled.
  An 8-tap Q15 polyphase sinc reaches 63 dBc.
