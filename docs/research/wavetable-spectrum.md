# Wavetable mip scheme: host spectrum test (task T15, 2026-10-12)

Measured on the host with `tests/wavetable_spectrum.py`, an integer model of the oscillator planned in
`docs/WAVETABLE_DESIGN.md` section 2, run on the factory tables from `tools/gen_wavetables.py`. Nothing here was
run on the FM-1. Full output: `build/wavetable_spectrum.txt`, plot: `build/wavetable_spectrum.png`. To reproduce:

```
.venv/bin/python tools/gen_wavetables.py                 # firmware/gen/wavetables.h + build/wavetables.bin
.venv/bin/python tests/wavetable_spectrum.py              # shipped scheme only, about 10 s
.venv/bin/python tests/wavetable_spectrum.py --variants   # plus the candidate fixes, about 95 s
```

## Verdict

**The scheme as designed fails the 60 dB target, by a wide margin, and storing more data does not fix it.** In the
playable range (MIDI 41..100) the worst alias/image component is **19.6 dB below the strongest harmonic** (bright
tables). Saw-like 1/k tables reach 38 dB at level 3 and 56 dB at level 0. The cause is the **linear interpolator**, not
the mip levels: every level keeps `64 >> L` harmonics in `256 >> L` samples, so each level is only 4x oversampled.
A linear interpolator's images of harmonic k fall at `N - k`, `N + k`, ... with gain `sinc^2(1 - k/N)`. For the
top harmonic (k = N/4) that is **-19 dB**, whatever the level and whatever the note. Those images sit at 3..4x the
top harmonic, above Nyquist, and fold back into the audio band at inharmonic frequencies.

**The smallest fix that meets the target is an 8-tap windowed-sinc (polyphase) interpolator in phase**: it keeps
the table format, adds 6 more guard samples per level (+27 KB, 409 KiB in total) and measures **63.3 dBc** with
integer Q15 taps and 1024 phases (59.8 dBc with 256 phases). CPU cost is about 4-5x the linear reader per oscillator
**[estimate, unmeasured]**; see "Fixes" below.

## Model (what was measured)

- `inc = INC_C9[n % 12] >> (10 - n / 12)` exactly as `dinghy.c`, 44.1 kHz, uint32 phase.
- Level: the smallest L in 0..5 with `inc < 2^(25 + L)`. That is the rule `(64 >> L) * f0 < 22050` with no divide
  (it is one `clz`). Clamp at 5.
- `idx = ph >> (24 + L)`, `frac = (ph >> (8 + L)) & 0xFFFF`, `s = a + (((b - a) * frac) >> 16)`, guard sample, no mask.
- Position: 12-bit, `f = pos >> 7`, `x = (pos & 127) << 8` (Q15; 32 frames, so 128 steps per frame, not the
  `>> 6` in the design note, which assumed 64 frames). `out = s0 + (((s1 - s0) * x) >> 15)`.
- Per table: positions 0, 10.5 (a morph), 15 and 31; notes 41..100 plus 24, 30, 36, 105, 110, 116, 122 and 127;
  1 s render, 4-term Blackman-Harris window, 1 Hz bins. The harmonics `k * f0` with `k <= 64 >> L` below Nyquist
  are masked (+/-6 bins). The largest remaining bin is the spur.
- **dBf** = spur below the fundamental; **dBc** = below the strongest wanted harmonic. Several tables have a weak
  fundamental by design (VOWEL formants, ORGAN registrations without 16', BELL, HARM late frames), where dBf mostly
  measures the recipe. The table below scores on dBf, or on dBc (marked *) where the fundamental is more than
  20 dB under the strongest harmonic. The variant comparison uses dBc.
- Limits: an alias landing within +/-6 Hz of a wanted harmonic is hidden; hysteresis and pitch modulation are not
  modelled (static notes); the morph is linear, so it cannot add aliasing, and the measurements confirm it.

## Results, shipped scheme (linear, levels 256..8)

| table | worst score 41..100 | dBc | at | spur | worst over all notes (24..127) |
| --- | --- | --- | --- | --- | --- |
| SAWSQ | 38.3 | 38.3 | MIDI 97, L3 | 9119 Hz | 7.6 (MIDI 127) |
| PWM | 21.7 | 22.2 | 97, L3 | 9119 Hz | 1.7 |
| HARM | 15.8 | 21.0 | 97, L3 | 9119 Hz | 6.4 |
| VOWEL | 8.7 | 22.2 | 95, L3 | 5288 Hz | -4.0 |
| FMIDX | 18.7 | 28.1 | 97, L3 | 13554 Hz | -1.5 |
| FOLD | 20.6 | 22.2 | 95, L3 | 5288 Hz | 10.1 |
| ORGAN | 20.4* | 20.4 | 80, L2 | 4231 Hz | -0.7 |
| BELL | 15.5 | 25.2 | 97, L3 | 9119 Hz | -7.5 |
| NOISE | 19.6 | 19.6 | 46, L0 | 21491 Hz | 3.0 |
| SHAPE | 38.3 | 38.3 | 97, L3 | 9119 Hz | 7.6 |
| REED | 32.1 | 32.1 | 95, L3 | 5288 Hz | 16.6 |
| STAIR | 40.0 | 40.0 | 95, L3 | 5288 Hz | 11.3 |

All 12 FAIL (pass >= 60, warn >= 50). Worst dBc per level, playable range: **L0 19.6 (MIDI 41..64), L1 26.2
(65..76), L2 20.4 (77..88), L3 20.4 (89..100)**. For saw-like content the figure follows the top harmonic's level
(a_H = 1/H) minus the 19 dB image rejection: SAWSQ / SHAPE measure 56.4 dBc at L0, 51.2 at L1, 44.7 at L2 and
38.4 at L3 (theory: 55, 49, 43, 37). At level 0 only REED (62.2) passes. FMIDX and FOLD pass on dBc (62.1, 60.5)
but not on dBf. NOISE (19.6) and HARM (25.1) are the worst, because their strongest harmonics sit near the top.

The integer arithmetic is safe for this data: the largest adjacent-sample step is 28,940, so
`(b - a) * frac` peaks at 1.90e9, inside int32. **It would overflow for steps above 32,767** (a full-scale
square edge, which per-table normalisation to 0.9 FS rules out today). User tables (section 9 of the design) must
keep that limit, or use a 15-bit fraction.

## Other problems found

1. **Above MIDI 124 (f0 > 11,025 Hz) level 5 still holds 2 harmonics**, and the 2nd aliases (G9 = MIDI 127:
   spur 19,012 Hz, about 0 dB). These notes are unreachable from the keys, but MIDI can send them. Fix: when
   `inc >= 2^30`, read a fundamental-only level (a 7th level of 4 samples: +10 B per frame, +3.8 KB) or clamp the
   oscillator pitch below 11 kHz.
2. **Hysteresis must sit on the dark side.** The level rule is a hard ceiling (the top harmonic is just under
   Nyquist), so a "1/4-octave hysteresis" that stays on the brighter level past the threshold aliases real
   harmonics, which is worse than the images. Switch to fewer harmonics as soon as `inc >= 2^(25 + L)`, and only
   switch back once `inc < 2^(25 + L) * 2^(-1/4)`.
3. **Low notes are dull by format:** 64 harmonics at F2 (87 Hz) cap the bandwidth at 5.6 kHz, and below MIDI 65
   level 0 always tops out under 21 kHz. This is a sound-design limit, not aliasing. A 512-sample top level fixes
   brightness but not the images (variant F).

## Fixes (measured, worst dBc over all tables and positions)

| variant | 41..100 | L0 | L1 | L2 | L3 | L4 | flash, 12 x 32 frames |
| --- | --- | --- | --- | --- | --- | --- | --- |
| A linear (shipped) | 19.6 | 19.6 | 26.2 | 20.4 | 20.4 | 18.8 | 382 KiB |
| B Catmull-Rom 4-point | 23.6 | 23.8 | 29.4 | 23.6 | 24.6 | 23.3 | 391 KiB |
| C 8-tap Kaiser(6) sinc, float | 62.0 | 62.0 | 64.3 | 64.8 | 62.4 | 67.2 | 409 KiB |
| C2 8-tap, int Q15, 256 phases | 59.8 | 59.8 | 62.3 | 59.9 | 60.8 | 59.6 | 409 KiB (+4 KiB taps) |
| **C3 8-tap, int Q15, 1024 phases** | **63.3** | 63.3 | 65.1 | 64.7 | 63.7 | 65.1 | 409 KiB (+16 KiB taps) |
| D linear + top-octave taper per level | 24.9 | 32.9 | 33.8 | 32.8 | 24.9 | 25.5 | 382 KiB |
| E Catmull-Rom + taper | 31.7 | 39.2 | 41.3 | 40.6 | 31.7 | 32.3 | 391 KiB |
| F linear, levels 512..16 (8x oversampled) | 33.8 | 33.9 | 39.6 | 33.8 | 34.8 | 33.2 | 760 KiB |
| G linear, 256 samples at every level | 19.6 | 19.6 | 39.6 | 46.8 | 60.8 | 71.7 | 1156 KiB |

(Flash includes the guard samples each reader needs: 1 linear, 3 Catmull-Rom, 7 for 8 taps. With 32-frame tables
the budget is about 400 KB.)

Reading the table:

- **Flash cannot buy it.** Doubling every level (F) gives 34 dB at 2x flash; full-length levels (G) do not help
  level 0 at all. Linear interpolation needs about 32x oversampling for -58 dB (sinc^2 at 31/32).
- **Cheap interpolators help a little.** Catmull-Rom gives +4 dB (its kernel rejects only 23.5 dB at 3/4). Tapering the top octave of each level costs
  brightness and gives +5..13 dB. Together (E) they reach 32..41 dB for about 2 extra multiplies per read and the
  same flash.
- **Only a real low-pass interpolator passes.** The passband is 0..1/4 and images start at 3/4 of the table rate,
  so an 8-tap Kaiser-windowed sinc already gives about 67 dB of image rejection (Kaiser's estimate:
  (60 - 8) / (14.36 * 0.5) = 7.2 taps). Integer Q15 taps cost about 2 dB with 1024 phases (`frac >> 6`; table
  1024 x 8 x int16 = 16 KB const) or 4 dB with 256 phases (4 KB).

**Recommendation, smallest change that meets 60 dB:** keep the generated format; add guards so each level is stored
as `[3 pre][N][4 post]` (the generator change is one line); read with 8 taps from a 1024-phase Q15 table, morphing
first (8 frame lerps) and then filtering (8 MACs). Rough per-oscillator-sample cost: linear about 15 ops
(2 x (2 loads + lerp) + morph); 8-tap morph-first about 70 ops (16 table loads, 8 lerps, 8 tap loads, 8 MACs,
index math). That is 8 M vs 37 M ops/s for 12 voices x 2 oscillators **[estimate, needs the T10 timing readout;
XIP read cost unknown]**. If that is too expensive, a per-voice morphed frame baked into RAM once per block (only
N_L samples at the active level) cuts the inner loop to 8 RAM loads + 8 tap loads + 8 MACs. If the budget still
does not allow it, E (Catmull-Rom + taper, about 32..41 dBc) is the honest fallback, with the target relaxed.
