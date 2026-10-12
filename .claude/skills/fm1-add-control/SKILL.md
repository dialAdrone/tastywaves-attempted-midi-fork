---
name: fm1-add-control
description: Recipe for adding a new synth parameter end to end - encoder or button input, audio-ISR use, screen readout, LED, flash persistence, host test. Use whenever a new knob/page/preset value is introduced.
---
# Add a parameter (end to end)

1. **State**: add the field to the settings struct in `app_main` (≤ 256 B total; add a `ver` byte first time).
   Validate/clamp after `settings_load` and fall back to defaults.
2. **Audio side**: a `volatile int32_t` (or in the voice struct) written only by the main loop; read in
   `app_audio` into a local at the top of the block. Derive coefficients per block, not per frame.
3. **Input**: `fm1_enc_take(ENC_KNOB1..4 | ENC_SELECT | ENC_ALGORITHM | ENC_PRESETS)` → clamp → set `dirty=1;
   t_change=fm1_ms`. Buttons: `fm1_input_edges(&rel)` bit `1u<<BTN_x`. Use `BTN_SEL/EDIT/FX/ENV/LFO` as page keys.
4. **Screen**: add a `*_drawn` shadow; redraw only on change with `draw_text_box` (FONT_S 16 px / FONT_L caps+digits)
   or a canvas ≤ 240x64. Number formatting: `fmt_int`, `fmt_fix` (libc.c).
5. **LEDs**: `fm1_led_key(BTN_x, on)` for the active page/state.
6. **Persist**: existing `dirty && quiet() && fm1_ms - t_change > 2000` block saves. Do not add other save sites.
7. **Test**: if the input is an encoder, host mode needs an `enc` script verb (extend `tests/host.c`:
   `fm1_enc_take` currently returns 0). Run `tests/run_host.sh`, read the PNGs, then `./build.sh`.
8. Update `docs/CODEMAP.md` (dinghy.c anatomy) with the parameter and its range.
