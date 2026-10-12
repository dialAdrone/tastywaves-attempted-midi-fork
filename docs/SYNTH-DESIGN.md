# Designing a synth on Dinghy — constraints, budgets, recipes

Companion to `RULES.md` (chip rules) and `API.md` (platform). This is the *how to build a synth here* sheet.

## Budgets

| resource | limit | note |
| --- | --- | --- |
| audio block | 256 frames / 5.8 ms @44.1 kHz | aim ≤ 2.9 ms; CPU clock not documented here → **measure** with `fm1_ticks()` on hardware |
| RAM | 96 KB `.data+.bss` | build fails above |
| pool | 336 KB `.pool` | delay lines, wavetables built at boot, canvases |
| flash code | 568 KB | `const` tables are free of RAM |
| settings | 256 B / one sector | erase stalls audio ~50 ms: save only when `quiet()` |
| voices | 4 now (`NV`) | raise only after measuring per-voice cost |

## Per-block, not per-frame

Anything that changes slowly (envelope stage, filter coefficients, LFO, portamento, parameter smoothing) should be
computed once per 16/32 frames inside `app_audio`, then interpolated or held. Per-frame work: phase advance,
oscillator lookup/shape, filter state update, mix.

## Architecture pattern for a synth

```
main loop:  inputs (keys, MIDI, encoders)  ->  patch params (volatile int32, one writer)  ->  voice alloc (gate/inc)
ISR:        per voice: env -> osc(s) -> filter -> amp -> sum -> (optional global fx: delay/chorus/drive) -> out
```

- Keep a `struct patch` (all params, Q15 or small ints) in RAM; main writes fields, ISR reads them. Persist the
  struct via `settings_save` (≤256 B; add a version byte + validate ranges after load as dinghy.c does).
- Parameters from encoders: `fm1_enc_take(ENC_KNOBn)` returns signed detents; clamp, apply, redraw value.
- Presets: 256 B = e.g. 8 presets x 32 B, or widen flash range for more.

## Fixed-point recipes (no float, no 64-bit division)

- Levels Q15 (`x*y >> 15`), gains Q12, phase `uint32` where 2^32 = one cycle, `inc = f * 2^32 / 44100`
  (precompute on a PC; see `INC_C9`). Octave shift: `inc >> n`.
- Pitch bend / detune: multiply `inc` by a Q15 ratio table (`inc = (uint32_t)(((uint64_t)inc * ratio) >> 15)`:
  64-bit multiply is fine, 64-bit divide is not).
- One-pole low-pass: `y += ((x - y) * k) >> 15`, `k` Q15 from cutoff table (precomputed, per block).
- State-variable / ladder filters: keep states `int32`, saturate; coefficient from a 64–128 entry log table
  indexed by cutoff knob; do not compute `tan()` at runtime.
- Envelope ADSR: linear in Q15 with per-stage step computed from time via table; exponential = multiply by Q15
  coefficient each block.
- LFO: second phase accumulator, `sine()` from dinghy.c or triangle `(ph>>16) ^ ...`.
- Square/saw: straight from phase (`(int32_t)ph >> 17`), aliasing is audible — consider PolyBLEP-like correction
  in fixed point, or bandlimited wavetables per octave in `.pool` built at boot.
- FM (the FM-1 name!): operator out = `sine(ph + (mod * depth >> n))`; `ENC_ALGORITHM` is the natural algorithm
  selector. 2- or 4-operator per voice costs ~(ops × sine) per frame; sine() is ~15 integer ops.
- Wavetable: 2048 x int16 in flash (`const`) → read with linear interpolation, index `ph >> 21`, frac `ph >> 5 & 0xFFFF`.
- Noise: `rng()` in libc.c.
- Division: replace with reciprocal tables, shifts, or 32-bit `/` only. Guard divisors against 0.
- Never write a literal that equals 0xFFC00000..0xFFCFFFFF (e.g. `-32767 << 7`): build fails (ROM-reference check).

## Verification ladder

1. `tests/run_host.sh` (+ custom script) → WAV/PNG: sanity of musical result and UI layout.
2. `./build.sh` → size/RAM/pool numbers, ROM-ref and register checks.
3. Hardware: `fm1_install.py --info`, install, listen; time `app_audio` and show on screen/crash log.
4. Release only via `./build.sh --release X.Y` and a fresh identity.
