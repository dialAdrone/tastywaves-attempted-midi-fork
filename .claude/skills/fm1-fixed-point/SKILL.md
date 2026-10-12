---
name: fm1-fixed-point
description: Integer-only DSP idioms for the FM-1 chip (no FPU, no 64-bit divide) - Q15 math, phase increments, filters, envelopes, FM, wavetables. Use when writing or converting any audio or control math.
---
# Fixed-point on the FM-1

Full recipes: `docs/SYNTH-DESIGN.md`. Quick card:

- Q15 multiply: `(a * b) >> 15` (both |x| ≤ 32767 → fits int32). Q15 × Q15 × Q15 chains: shift between.
- Phase: `uint32_t ph; ph += inc;` `inc = round(f * 2^32 / 44100)`; compute tables with a PC script, paste as numbers.
- Frequency table: 12 notes for the top octave, `>> (octaves down)`; ratio multiply uses `(uint64_t)` mul + `>> 15`.
- Smoothing: `y += ((target - y) * k) >> 15` (k Q15), once per block for control values.
- Clamp BEFORE shifting left; never create the constant `-32767 << 7` (ROM address check).
- Division: `/` only on 32-bit, divisor checked non-zero (zero does not trap, gives garbage).
- Sine: reuse `sine(ph)` in vavewave.c (±32767 over 2^32 phase).
- Signed shifts of negatives are arithmetic on this toolchain; `x >> n` rounds toward −∞ — fine for audio, but
  watch DC offsets in filters (add rounding `+ (1 << (n-1))` if a DC bias matters).
- Test numerics on host with the same integer code (host cc is fine), but remember host has float: grep for
  `\d\.\d|float|double` before building.
