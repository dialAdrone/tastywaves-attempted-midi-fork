# Wavetable engine for the FM-1 (Dinghy platform, branch `wavetable_synth`)

Research date 2026-10-11. All repos were shallow-cloned to a scratchpad and read, not run. Commits cited as
`repo@shortsha`. Anything marked **[estimate]** is my inference, not a measurement; **[measure]** means it needs the
device. Project licence is GPL-3.0-only, so "reuse" below means: code can be copied (GPL-3, MIT, BSD, Apache with
notices kept) or only the idea can be used (NC / no licence).

---------------------------------------------------------------------------------------------------------------

## 0. Summary of what matters

- The FM-1 CPU is much stronger than the Dinghy docs imply: **pi32v2 at 240 MHz** (PLL from the 24 MHz crystal), with
  **two cores** of which the stock firmware uses the second for its 12 FM voices, and 578 KB SRAM. Budget: 240e6 *
  5.8 ms = **1.39 M cycles per 256-frame block = ~5,400 cycles per output frame** for everything. Braids runs a whole
  wavetable macro-oscillator in 750 cycles per sample (72 MHz STM32F1 at 96 kHz). So a wavetable voice at 100-150
  cycles/frame is comfortable. **[estimate]**; the clock under Dinghy and the pi32v2 cycle costs are unmeasured.
- Nobody has published a real morphing wavetable engine for the FM-1 yet (see 1.3); the closest are Bubba Machine's
  16-wave bank (not run on hardware) and Felucca's PHASE (phase distortion) engine. This is a free niche.
- The right design for this chip is the Braids/Shruthi/Plinky one: **short single-cycle frames (256 samples), 64
  frames per table, linear interpolation in phase and in position, band-limited by pre-computed per-octave mip
  levels with halved length, integer only.** Tables are generated on the PC by a build script and sit in flash as
  `const` (zero RAM).
- v1 = 8 voices, 2 wavetable oscillators + sub + noise, SVF, 2 ADSR, 1 LFO, drive + delay. v2 = warp modes, unison,
  mod matrix, arp, reverb/chorus, user tables, preset bank in widened flash.

---------------------------------------------------------------------------------------------------------------

## 1. Open-source implementations

### 1.1 Table

| # | Project (commit) | Licence | Lang / target | Table format | Interp | Anti-alias | Voices / cost | Reusable for us |
|---|---|---|---|---|---|---|---|---|
| 1 | **Braids** `pichenettes/eurorack` braids/ (@d8f4739) | MIT (code, README "Code (STM32F projects): MIT") | C++, STM32F103 Cortex-M3 72 MHz, **no FPU, integer**, 96 kHz | 8-bit waves, 128 samples + 1 guard (129 B), 256 waves = 33 KB (`wt_waves`), 20 wavetable definitions (`wavetable_definitions[]`, up to 16 steps each) and a 16x16 `wt_map` | linear in phase (`Interpolate824`), linear crossfade between two waves (`Crossfade`) | none beyond 2x naive oversampling + averaging (`RenderWavetables`) | mono; whole module 72e6/96e3 = **750 cycles per output sample** | **High (idea + code).** Shows the minimal integer wavetable: wave pointer from a 16-bit parameter, two lookups, one crossfade. 8-bit is too noisy for us; use int16 |
| 2 | **Plaits** `pichenettes/eurorack` plaits/dsp/engine/wavetable_engine.cc (@d8f4739) | MIT | C++, STM32F373 Cortex-M4F 72 MHz, float, 48 kHz | "8x8x3 wave terrain": 4 banks x 64 waves (192 + 15 user waves), 128 samples + 4 guard, int16 = 50.7 KB (`wav_integrated_waves`) | Hermite in phase, trilinear in (x, y, z) | **integrated wavetables**: `numpy.cumsum` of the wave, played, then differentiated with a one-pole whose cutoff tracks f0 (Franck & Valimaki DAFX-12, `plaits/resources/wavetables.py`) | mono per engine; 12-sample blocks | **Medium.** Integrated-wavetable trick is clever and mip-free but needs float-ish headroom and a high-order read; for us mips are simpler and cheaper. Borrow: 3-D parameter smoothing, `Clamp`-style position quantisation, wave-map shuffling idea |
| 3 | **Shruthi-1** `pichenettes/shruthi-1` shruthi/oscillator.cc (@56bfe78) | GPL-3.0 | C++, ATmega644 8-bit AVR 20 MHz | 8-bit, 128 samples + 1 guard, `wav_res_waves` indexed `*129`, wavetable = 18 B definition (num_steps + up to 17 wave indices), plus a user wavetable in RAM (`RenderSweepingWavetableRam`) | linear in phase, 8-bit crossfade between 2 waves (`InterpolateTwoTables`) | none (tables are pre-filtered by hand; aliasing accepted) | mono | **High for table-definition idea**: a wavetable is a list of wave indices with steps, so waves are shared between tables (saves flash). GPL, directly copyable |
| 4 | **Plinky wavetable generator** `plinkysynth/wavetable` (@c956a83) | **CC BY-NC-SA 4.0** (README: "All Plinky-contributed material") | C++ PC tool, target Plinky = STM32L4 Cortex-M4 (32 kHz) | 15 shapes x **9 octave mips**; top octave 512 samples, halved per octave down to 2 -> `WAVETABLE_SIZE = 1022+9` samples/shape (`#define WAVETABLE_SIZE (1022+9) // 9 octaves, top octave is 512 samples`), int16 | linear | **per-octave pre-filtered tables, halving length, windowed-sinc downsample** (`kernel[i] = (0.5+0.5cos(...))*sin(x)/x`) | n/a | **Idea only (NC licence).** Exactly the mip layout we want; implement our own generator |
| 5 | **Bubba Machine** `Erbubar23/bubba-machine` core/bm_engines.c, core/bm_rom.h (@be5f512) | GPL-3.0 (LICENSE) | C99, **FM-1 target** (host + wasm only; README: "has not been tested on real hardware") | `bm_rom_waves[16][8][256]` int16: 16 waves x 8 band-limit levels (127, 64, ... 1 harmonics) x 256 = 64 KB | linear in phase and wave pair (`T0[ix] + ((T0[ix+1]-T0[ix])*fr>>16)`, then mix by `mf`) | **band-limit level chosen from phase increment** (`wave_level(inc)`: `maxh = 0x80000000/inc`) | `BM_VOICES 12`, SVF per voice | **High as a reference.** Same chip, same rules. **Pitfall**: its SVF setup does `((int64_t)1<<32)/den` (int64 divide), which will not link on the chip (`docs/RULES.md`). Levels all 256 long (8x waste); halve instead |
| 6 | **WvTable-logue** `vuki/WvTable-logue` src/wtgen.h (@e33b6d0) | GPL-3.0 (LICENSE.md) | C, Korg minilogue-xd (Cortex-M4F 168 MHz), float | PPG Wave 2.3 ROM: 8-bit, 64 samples per half cycle, 61 positions per table, 32 tables x 3 modes (`WtGenState.wavetable[61][4]`) | UQ7.25 phase, bilinear (`WTMODE_INT2D`), integer-only (`NOINT`) modes | **2x/4x oversampling + half-band decimators** (`decimator.h`, `WvTable.c`) | mono, params: position, skew, 3-mode, env, LFO | **Medium.** Good parameter set (position, wave "skew" phase warp, index envelope, index LFO). Data is PPG ROM content: legal grey area, do not import |
| 7 | **logue-osc `wavebank.h`** `dukesrg/logue-osc` inc/wavebank.h (@8d9db38) | **no LICENSE file** in repo | C, logue-sdk (Cortex-M4F), Q31 | WaveEdit-compatible: `SAMPLE_COUNT` power of two, `WAVE_COUNT`, optional grid X*Y, formats A-law/u-law/PCM8/12/16/32/float | linear, optional guard sample | none (user tables) | n/a | **Low (unlicensed).** Useful as design reference: guard sample per wave, 2-D grid mode, compact A-law storage |
| 8 | **SWN** `4ms/SWN` src/oscillator.c, inc/sphere.h (@3f833dd) | MIT (LICENSE: "Software ... MIT") | C, STM32F765 Cortex-M7 FPU | `WT_TABLELEN 512`, 16-bit; waveforms in a "sphere"; float processing | linear, 2-buffer crossfade on table change (`wt_xfade`) | none (recorded/edited tables) | 6 channels | **Medium for UX**: table-switch crossfade (`XFADE_INC`) and wavetable editing/recording ideas; DSP is float, not directly reusable |
| 9 | **usynth** `Jacajack/usynth` (@3f44717) / **avr-ppg** (@ab19d9d) | MIT / GPL | C + AVR asm, ATmega328 20 MHz, 28 kHz | PPG Wave 2.2 EPROM data in flash (copyright grey); 99 % flash, 77 % RAM | linear, 8-bit | none | 2 oscillators + 2 ASR env + 1-pole LP; work spread over 21-sample slices | **Low code, high lesson**: shows that slow work must be spread across blocks; same advice as `docs/RULES.md` |
| 10 | **PicoSynth** `raybellis/PicoSynth` src/voice.cxx (@5a52efc) | GPL-3.0 (README), no LICENSE file | C++, RP2350 Cortex-M33 250 MHz | 4 x 2048 x int16 (sine/square/saw/tri), not morphing | none (direct indexing, measured faster than interpolating, see comment in `voice.cxx`) | none | 64 voices | **Low.** Useful measurement-led comments (tables must live in RAM on RP2040, shared accumulator for sub osc) |
| 11 | **Mozzi `Oscil`** `sensorium/Mozzi` Oscil.h (@7f1e5b4) | LGPL-2.1+ | C++ Arduino | power-of-two tables (2048 typical), int8, 16 fractional bits | optional | none | n/a | **Low.** Fixed-point phase conventions; LGPL needs care |
| 12 | **WaveEdit** `AndrewBelt/WaveEdit` src/WaveEdit.hpp, bank.cpp (@31d5fb2) | GPL-3.0 (LICENSE.txt) | C++ editor (PC) | **`WAVE_LEN 256`, `BANK_LEN 64`**, bank `.wav` = 16-bit mono, 64 x 256 samples back to back (`Bank::saveWAV`) | n/a | n/a | n/a | **High as format**: de-facto interchange for hardware wavetable modules (E352/E370, Morphagene tooling). Our native source format |
| 13 | **Korg logue-sdk** `korginc/logue-sdk` platform/nts-1_mkii/common/osc_api.h (@e848993) | BSD-3-Clause | C | `k_waves_size_exp 7` (128 samples) + guard, float, wavesA/B/C x 16 | linear | none | n/a | Reference for a small fixed wave set; not reusable |
| 14 | **Vital** `mtytel/vital` | GPL-3.0 (README; commercial licence available) | C++/JUCE, SSE | 2048-sample frames, up to 256 frames (feature reference only) | high order | spectral, band-limited per octave | unlimited | **Features only** (too heavy to port); code could be read for warp-mode maths (GPL) |
| 15 | **AKWF waveforms** `DatanoiseTV/AKWF_WaveForms` (@ba4819c) | **CC BY 3.0 as stated by this repo (`License.md`); verify upstream adventurekid.se** | C headers, int16 | ~4,000 single-cycle waves (600-sample cycles) as C arrays | n/a | n/a | n/a | **High as content**: free single-cycle source to build the first tables (attribution needed) |

Not read in depth (context only): `FigBug/Wavetable` (GPLv3 JUCE plugin), `TeensyAudio/Wavetable-Synthesis`
(sample-based, SoundFont-style, not morphing), `icsga/Yazz` (Rust), `zlosynth/achordion` (Rust no_std wavetable
oscillator for Daisy, not fetched).

### 1.2 What the code tells us (cross-cutting)

1. **Frame size in the small-synth world is 64-256 samples, 8-bit or 16-bit.** Braids/Shruthi 128x8-bit, Plaits 128x16,
   PPG 64x8 per half cycle, WaveEdit 256x16, Bubba 256x16, SWN 512x16, Plinky 512x16 (top octave). Only the desktop
   synths use 2048.
2. **Anti-aliasing options seen**: none (Braids/Shruthi, PPG heritage), oversample + decimate (WvTable), integrated
   waves + differentiator (Plaits), per-octave band-limited mips (Plinky, Bubba). On an integer chip with idle
   headroom and 300+ KB of flash, **mips are the cheapest at run time**: zero extra per-sample work, only a pointer
   pick per block.
3. **Position morph = linear crossfade of two adjacent frames, per sample**, at the same mip level. All of them do it
   that way; nobody pre-bakes in-between frames.
4. **Tables are built on the PC** (Python/numpy in `plaits/resources/wavetables.py`, `braids/resources/waveforms.py`,
   `plinky wavetable.cpp`), emitted as C arrays. Matches this repo's practice (`firmware/gen/felucca_font.h`).
5. **A "wavetable" is often an index list over shared waves** (Braids, Shruthi `wavetable_definitions`), not a
   blob of frames. Saves flash when tables overlap; costs a lookup per block only.

### 1.3 On the FM-1 itself (what already exists)

Directory: `cicloid/awesome-fm-1` README (@current) and `aroum/fm1-custom-fw`. Findings relevant to us:

- **Felucca** (`hugelton/Felucca`@ef1f561, GPL-3.0-only): 13 engines (ANALOG, DIGITAL(4-op FM), FM6 (msfa/Dexed, 6
  voices per track), PHASE (CrispyZebra phase distortion), LOFI, SAMPLE, SLICE, GRAIN, PHYS, WHEEL, FORMANT, NOISE,
  DRUM); 4 tracks, **8 voices shared**, USB audio, MIDI learn, mod matrix, INSERT effects. **No wavetable engine**
  (`grep -i wavetable` finds only the asset-licence line and LOFI's 32-sample 4-bit "WRAM" wave). Reusable GPL code we
  should read before writing our own: `dsp.c` (`blep`, trapezoidal SVF `tsvf`), `eng_phase.c` (phase-bend with Q16
  reciprocal slopes: a division-free warp), `voice.c` (shared voice budget, stealing, overload shedding at 85 %),
  `mod.c` (matrix), `fx.c` (reverb/chorus/delay), `params.c` (parameter descriptors).
- **SLOOP / sloopDX / zp12** (`isod89/sloop-fm1`@cb6dedc, `zvenson/dxsloop`, `zvenson/zp12`): Felucca family;
  SLOOP is 4 tracks, 8 shared voices, 6 drum voices, `HALF_FRAMES 256` (5.8 ms, same as Dinghy), FM6 with DX7 SysEx.
  UI vocabulary is "button = page, tap again = next page, hold = quick layer, KNOB1-4 = the page's four columns".
- **Bubba Machine** (`Erbubar23/bubba-machine`@be5f512): see row 5; 12 voices, T3 "BAJO" wavetable bass. README says
  not yet flashed. Also `fm1-custom-fw` README lists it.
- **FM-1+VA** (closed), **Groove OS** (closed, "up to 20 voices: 12 FM + 8 VA", awesome-fm-1), **Lunar Modulator**
  (`ip2k/lunar-modulator`@621490b, MIT; research stage, Plaits/Braids ported, "has not run on an FM-1 yet").
- GitHub search `wavetable` and `FM-1 wavetable` (gh, 2026-10-11) found no FM-1 firmware dedicated to wavetable
  synthesis. (Search is not exhaustive: private forks and non-listed repos exist.)

---------------------------------------------------------------------------------------------------------------

## 2. Typical features of wavetable synths

Sources: Vital (vital.audio, github.com/mtytel/vital), Serum KVR threads (clm chunk: kvraudio.com/forum/viewtopic.php?p=7291246),
Serum warp mode list (secondary sources via search), WaveEdit, Plaits, PPG (WvTable-logue docs), Braids.
E = essential for v1, N = nice-to-have, X = out of scope for this chip.

| Area | Typical (desktop) | Embedded norm | v1 | Note |
|---|---|---|---|---|
| Table format | 2048-sample frames, up to 256 frames; Serum `.wav` with `clm ` chunk stating 2048 (KVR: "Serum currently assumes 2048 at all times"); Vital accepts own frame sizes; WaveEdit 64x256 | 128-256 samples, 16-64 frames | **E** 256 x 64 | Build tool accepts 2048-frame Serum/Vital `.wav` and WaveEdit `.wav`, resamples to 256 |
| Position scan | continuous 0..1 across frames with linear crossfade | same | **E** | 12-bit position, 64 frames |
| Frame interpolation | linear (spectral morph in Vital) | linear | **E** linear | Hermite later (N) |
| Oscillators | 2-3 + sub + noise, per-osc level/pan/transpose/detune | 1-2 | **E** 2 + sub + noise | |
| Unison | up to 16 voices, detune/spread/blend | 3-7 | N (v2: 3) | costs voices |
| Warp modes | Serum: Bend+/-, Asym, Flip, Mirror, Remap, PWM, Sync, FM, AM/RM, Quantize, Vowel; Vital: 15 spectral + 6 phase morph modes, FM, RM | 1-2 (PPG skew, CZ bend) | N (v2: BEND, MIRROR, SYNC, PM, QUANTIZE) | all are phase maps (cheap) except spectral |
| Filters | 2 filters, 12/24 dB, SVF, ladder, comb, formant, 20+ shapes | 1 x SVF/ladder | **E** 1 SVF LP/BP/HP, Q12 | ladder N (v2) |
| Envelopes | 3-6 DAHDSR | 2 ADSR | **E** 2 ADSR (amp, mod) | |
| LFOs | 4-8 with custom shapes, sync | 1-2 | **E** 1 (v2: 2) | |
| Mod matrix | drag-and-drop, 32+ slots | 4-8 slots | N (v2: 4 slots like Felucca `mod.c`) | v1 = fixed routings with depth knobs |
| Effects | chorus, delay, reverb, phaser, flanger, comb, compressor, EQ, distortion | drive + delay | **E** drive + delay; N chorus/reverb | pool RAM is plentiful |
| Arpeggiator | per patch | rare | N (v2) | ARP button exists |
| Voicing | poly, mono, legato, glide, portamento | poly + mono | **E** poly/mono/legato + glide | |
| MIDI | CC learn, MPE | CC map, PC | **E** CC map, PB, PC; N learn | |
| Presets | thousands | 32-128 | **E** 32-64 factory + 32-64 user | |
| Wavetable editor / import | built-in | none or PC tool | PC build tool only | user upload X for v1 |
| Oversampling / spectral AA | yes | no | mips | |

---------------------------------------------------------------------------------------------------------------

## 3. Fit to the FM-1

### 3.1 Platform facts (with sources)

| Fact | Value | Source |
|---|---|---|
| CPU | JieLi **AC791N (family WL82)**, **pi32v2**, 240 MHz from 24 MHz crystal (`pll_clock_init(24000000, 240000000)`); family max 320 MHz | `AL-255/FM-1-RE` docs/architecture.md, docs/io/01-boot.md (@ec832f2); `ip2k/lunar-modulator` docs/01-hardware.md (@621490b) |
| Cores | **two** pi32v2 cores; stock renders its 12 msfa voices on cpu1, UI/MIDI/FX on cpu0 | lunar-modulator docs/01-hardware.md, docs/05 s4. Dinghy/Felucca/SLOOP run bare metal on one core |
| SRAM | 578 KB total, stock static use ~135 KB | lunar-modulator docs/01-hardware.md s2 |
| Flash | 1 MiB NOR (JEDEC 0x856014, XIP at 0x02000000), contents **encrypted**; user-written data above flash 0x93000 needs the "plain window" (`fl_plain_window_init`) to read via XIP | `sloop-fm1/firmware/hal/fm1_flash.h`@cb6dedc; `sloop-fm1/firmware/src/project.c:722`; Dinghy has the helper in `firmware/hal/fm1_flash.h` but does not call it |
| Stock engine | 12 voices, 6-op msfa/Dexed, 44,118 Hz, 64-sample blocks, 12 x 468 B voices | FM-1-RE docs/io/04-synth-engine.md |
| Audio out | I2S (ALNK0) to external codec, 44,117.6 Hz = 24 MHz/544, 24-bit | `Felucca/firmware/src/audio.c:9`; lunar docs/01 s3.1 |
| Block | Dinghy/SLOOP 256 frames (5.8 ms); Felucca 128 (2.9 ms, `core.h:15`) | repo files |
| Display | ST7789V 240x240, SPI1 ~12 MHz (`LCD_BAUD 4`), full frame ~115 KB | `Felucca/firmware/src/lcd.c:11`; `docs/RULES.md` |
| Controls | 27 keys F3-G5, 14 buttons, 7 encoders, MASTER pot; black keys silk-screened OP1-OP6 etc. on the stock | `Felucca/docs/controls.jpg`; Dinghy `docs/API.md`; synthanatomy review |
| Stock UX | ALGORITHM = FM algorithm select; SELECT scrolls pages/params; OP1..OP6 keys edit operators; effects chain; arp; 16-step seq; banks A-D (128 patches) | synthanatomy.com/2026/07/m-vave-fm-1-review-low-budget-pocket-fm-ynthesizer-with-iconic-sounds.html |

Not found: manufacturer manual text (the M-VAVE product page rendered no specs via fetch), exact pi32v2
cycle/latency table, XIP cache size/miss cost, whether Dinghy leaves the PLL at 240 MHz. Photos of the underside,
the silk-screen of the black keys, and the manual PDF (m.media-amazon.com/images/I/A1WOydif9HL.pdf) would settle the
UX questions.

### 3.2 Community evidence on voices/operators

- Stock: 12 x 6-op FM on cpu1 (FM-1-RE).
- Felucca: 8 voices shared by 3 synth tracks, FM6 up to 6 voices per track, overload shedding when two consecutive
  halves exceed 85 % of the block (`Felucca/firmware/src/audio.c`, `voice.c`@ef1f561). Its own CPU readout is
  MENU > SYSTEM > INFO (README s menu).
- Groove OS (closed): up to 20 voices (12 FM + 8 VA). Not verified.
- **No published per-voice cycle figures** for pi32v2 anywhere I could find; lunar-modulator docs say "How loaded
  either core is has not been measured". Felucca's `tests/cpu_baseline.txt` is host-relative only.

### 3.3 Cost model (estimate)

Block budget: 5.8 ms * 240 MHz = 1.39 M cycles; per frame 5,440 cycles. Aim <= 50 % (Dinghy `RULES.md`): **2,700
cycles/frame**.

| Part of a voice, per output frame | Ops | Cycles **[estimate]** |
|---|---|---|
| phase advance + index/frac split (osc) | 1 add, 3 shift/and | 4-6 |
| 4 table reads + 2 phase lerps + 1 frame lerp (per osc) | 4 ld, 3 mul, shifts | 20-30 |
| 2 oscillators + sub (read of a 256 sine or square from phase) + noise | | 50-75 |
| trapezoidal SVF (Simper, as `Felucca/dsp.c tsvf`) | ~4 mul, 6 add, 2 clamp | 20-30 |
| amp env ramp + VCA, mix to L/R | | 8-12 |
| **Voice total** | | **~100-150** |
| 8 voices | | **~800-1,200 = 15-22 % of 5,440** |
| 12 voices | | ~1,200-1,800 = 22-33 % |
| + global drive/delay/(reverb) | | 100-400 |

XIP reads: Felucca plays 80 KiB sample slots and ADPCM sample sets straight from XIP at 8 voices (eng_sample.c), so
random reads from flash are workable. The cost of a cache miss on the FM-1 is unknown (for scale only: PicoSynth's
CLAUDE.md@5a52efc discusses ~98 cycles per read for a QSPI XIP miss on RP2350 and says not to quote it as a measured
constant). Worst case if every voice misses on every read:
that would dominate. Mitigation if measured bad: copy the active table (64 KB) to `.pool` on table change.

---------------------------------------------------------------------------------------------------------------

## 4. Recommendation

### 4.1 Decisions I recommend (summary)

| | v1 | v2 |
|---|---|---|
| Voices | 8, poly/mono/legato, glide | 12; unison 3 per voice at fewer voices |
| Oscillators | 2 wavetable + sub + noise | + warp per osc, osc sync |
| Frame | 256 samples int16, 64 frames, 6 mip levels | optional runtime user tables, 2048-source import |
| Filter | 1 SVF LP/BP/HP | + 4-pole ladder, formant |
| Env / LFO | 2 ADSR, 1 LFO | 2 LFO, S&H, 4-slot matrix |
| FX | drive + stereo delay | chorus, reverb (port Felucca) |
| Arp | none | ARP button, 5 modes |
| Presets | 32 factory in flash + 16 user in settings-compatible store | 64 user in a widened-flash log |

### 4.2 (a) Audio processing

**Oscillator structure (per voice).** `osc1`, `osc2` (own position offset and detune), `sub` (square/sine -1
oct from osc1 phase, as PicoSynth shares the accumulator), `noise` (`rng()` from `libc.c`). Mix, then SVF, then
amp env, then to L/R (v1 mono-to-both with a fixed small pan spread per voice for width).

**Table format and storage.**

```
frame i, level L (L = 0..5): N_L = 256 >> L samples + 1 guard sample (copy of sample 0), int16
per frame: 256+128+64+32+16+8 + 6 guards = 510 samples = 1,020 B
table (64 frames) = 65,280 B (~64 KB); a 32-frame table = 32 KB
```

- Level L keeps `64 >> L` harmonics. Choose level per voice per block as the smallest L with
  `(64 >> L) * f0 < 22050` (use `maxh = 0x80000000 / inc` as Bubba does but with a 32-bit-safe reciprocal; **not** a
  64-bit divide). With the 27-key range (F3 +/- 2 octaves = 41 Hz..3 kHz) levels 0-4 are used.
- Phase: `ph` uint32; `idx = ph >> (24 + L)`, `frac = (ph >> (8 + L)) & 0xFFFF`; read `t[idx]`, `t[idx+1]`
  (guard sample means no mask). 16-bit lerp as `Interpolate824` in `stmlib/utils/dsp.h`.
- Position: 12-bit `pos`; `f = pos >> 6`, `x = (pos & 63) << 9` (Q15). Output = lerp(frame f, frame f+1, x),
  both read at the same level. Hold the two frame pointers per block.
- Budget: flash headroom is 581,564 - 55,076 (baseline) = 526 KB; reserve ~120 KB for code -> **~400 KB of tables =
  6 tables x 64 frames, or 12 x 32 frames**. Table definitions as in Braids/Shruthi (`wavetable_definitions`:
  list of frame indices) would let tables share frames; start with plain blocks, add sharing only if flash is short.
- **Build step**: `tools/gen_wavetables.py` (PC, float allowed): input `.wav` files (WaveEdit 64x256 16-bit,
  Serum/Vital 2048-sample frames resampled to 256 by FFT, AKWF single cycles morphed spectrally to N frames),
  per frame: FFT, zero harmonics above `64 >> L`, IFFT to `256 >> L` samples, normalise per table (not per frame, so
  the loudness stays honest), write `firmware/gen/wavetables.h` with a table directory
  (`name[12], frames, offset`). Same pipeline as `felucca_font.h`.
- v2 alternative that frees flash: store only the 256-sample top level (512 B/frame, 2x more frames) and **derive
  the mips in `.pool` when a table is selected** with a 256-point integer DFT (O(N^2) = 65 k MACs/frame, ~4 M MACs
  per 64-frame table, tens of ms **[estimate]**, run sliced from the main loop with a double buffer). Also what user
  tables will need. Needs 64 KB pool per active table.

**Anti-alias summary.** Mips remove aliasing of the table content. Pitch modulation across a level boundary changes
the timbre slightly: add 1/4-octave hysteresis in v1, a level crossfade in v2. Warp modes (bend, sync, FM) create
new harmonics: apply one level lower (fewer harmonics) while a warp is active. Linear interpolation at 256 samples
and up to 64 harmonics gives roughly -50..-60 dB image noise for typical content **[estimate, to check on host
with a spectrum script]**; if too coarse, double the top level to 512 (x2 flash) rather than changing the interpolator.

**Interpolation.** Linear in phase (16-bit fraction), linear in position. Per-sample 2nd lerp is as cheap as the
alternative (baking the morph into a 256-sample voice buffer each ~128 frames = 2 MAC/frame); the buffer option
is worth it only when warps/unison make the morphed frame reused.

**Filter.** Per-voice trapezoidal SVF with Q12 coefficients: cutoff -> `g` from a 128-entry table, `k` from a
resonance table, recomputed every 32 frames (not per sample); `ic1/ic2` int32 with clamps like
`Felucca/dsp.c tsvf` and `eng_analog.c analog_tap` (@ef1f561; GPL-3.0, direct reuse). Do not use the `int64`
division of `bubba-machine/core/bm_engines.c`. Outputs LP/BP/HP; key tracking and envelope amount add to the cutoff
index before the table lookup.

**Envelopes.** Amp ADSR + mod ADSR, linear/exp in Q15 per 32-frame block with time tables (as `SYNTH-DESIGN.md`).
Release declick: voice-stolen fade over 2 blocks like Felucca `KILL_BLOCKS`.

**LFO.** One per patch (v1), phase accumulator advanced per block, shapes sine/tri/saw/square/S&H, free or
retrigger. Destinations: position (both osc), pitch, cutoff, amp.

**Effects.** v1: soft-clip drive (256-entry table) per voice-sum, stereo delay in `.pool`
(`int16 delay[2][22050]` = 88 KB, 0.5 s) with feedback and low-pass; v2: chorus and reverb. Reverb code exists in
Felucca `fx.c` (`HALL`, `ROOM`, `SPRING`); reuse the code rather than writing a new one.

**Modulation routing.** v1: fixed sources (ENV2, LFO, velocity, key track, mod wheel) -> fixed destinations with
bipolar depth parameters, so every depth is a Q8 value in the patch. v2: 4 slots `source -> dest, depth` like
Felucca's matrix (`mod.c`), with the same sources plus CC.

**Voice allocation.** In the ISR, via an event ring (see b). Same priorities as Felucca: same-note retrigger >
released > oldest held. Overload shedding: if two consecutive blocks exceed 85 % of the budget, fade out one
voice (copy Felucca `audio.c`/`voice.c`).

**Pitch.** `inc = INC_C9[n%12] >> (10 - n/12)` as Dinghy, times a Q15 ratio table for cents, pitch bend, LFO,
glide, detune; 64-bit multiply fine, no divide.

### 4.3 (b) UI backend

**Parameter model.** Table-driven: `struct pdesc { char name[5]; int8 min, max, def; uint8 fmt; }` and
`int8_t p[NP]`. The panel needs about 56 values in v1. Page = 4 `pdesc` indices (KNOB1-4). Adding a control then
follows the `fm1-add-control` skill. Names and ranges all in one `const` array shared by UI, MIDI CC map and
preset (de)serialiser.

**Patch struct (sized, v1).**

```
struct patch {                /* 64 bytes, version in byte 0 */
    uint8_t ver, table1, table2, flags;
    int16_t pos1, pos2;        /* 12-bit */
    int8_t  p[52];             /* all other 0..127 / -64..63 params */
    char    name[6];           /* 5 chars + NUL */
};
```

- **settings sector (256 B, today)**: 1 current patch (64 B) + globals (oct, MIDI channel, tune, velocity curve,
  CC map, last preset index: ~16 B) + 3 more spare patch slots = fits, but 3 user presets is too few.
- **Preset storage (recommended)**: 64-byte records in a dedicated 4 KB sector in the widened range, e.g.
  `0xE5000` (the OTA staging is 0xE0000..0xE5000, so 0xE5000..0xFBFFF is free). 4 KB / 64 B = 64 slots/sector.
  Use it as an **append-only log with an erase only when full** -> wear divided by ~64 (the sector survives 100 k
  erases of a *full* log = ~6 M saves). Record = `{magic:1, slot:1, seq:2, patch:60, crc:... }`; loader takes the newest
  per slot. Needs: widen `FL_RANGE_OK` in `firmware/src/platform.c:71` to that sector only; save when silent.
  Factory presets: `const struct patch factory[48]` in the image (3 KB).
- If the widened range is to hold *table* data too, user-written bytes above flash 0x93000 must be read through
  `fl_plain_window_init()` (call once at boot) or through `st_read` (SPI path as settings), not plain XIP, else they
  read decrypted garbage. **[verify on device]**

**MIDI CC map** (follows Felucca's convention so editors and controllers behave: README "MIDI CCs set track
parameters: 5 glide, 7 level, 10 pan, 71 resonance, 72/73/75 release/attack/decay, 74 brightness, 91/93/94 sends"):

| CC | Function | CC | Function |
|---|---|---|---|
| 1 | mod wheel -> position (depth in patch) | 71 | resonance |
| 5 | glide time | 72 / 73 / 75 | release / attack / decay (amp) |
| 7 | level | 74 | cutoff |
| 10 | pan (v2) | 70 | wavetable position |
| 20 / 21 | table1 / table2 | 22 | osc2 position offset |
| 23 | osc mix | 24 / 25 | sub / noise level |
| 26 / 27 | LFO rate / depth | 28 | mod-env amount |
| 29 | drive | 91 / 93 | delay / reverb send |
| 64 | sustain (v2) | 120 / 123 | all sound off / all notes off |
| PB | +/- 2 st (patch) | PC | preset number |

20-31 are "undefined" in the MIDI spec, which is why they carry the synth-specific ones.

**Main loop <-> audio ISR sharing.**

1. Controls (main loop) write `patch.p[]` (one writer; each byte is atomic).
2. Main loop calls `derive()` into the **inactive** of two `struct derived` buffers (cutoff table index base, frame
   pointers per table, LFO increment, envelope step sizes, mix gains), then flips `volatile struct derived *cur`
   (32-bit store = atomic). The ISR reads `cur` once at block start.
3. Notes: SPSC **event ring** `{type, note, vel}` (32 entries, `head` written by main, `tail` by ISR). Allocation
   and stealing happen in the ISR, so voice state has a single writer. (Dinghy's direct `voice[].inc/gate` writes
   do not scale to envelopes, filters and stealing.)
4. ISR -> main: `volatile` display taps: `pos_now` (voice 0 or last-note voice), `peak`, `cpu_q8`, `voices_busy`
   (as Felucca `song.cpu_q8`).
5. Save only when quiet (`RULES.md`), 2 s after last change, as Dinghy.

**Host test hooks** needed first (tasks T4, T5, T9, T10 in `docs/TASKS.md`): encoder events, LED log, ticks, WAV
analysis. A wavetable-specific host test: render one table at all octaves and compute the spectrum to prove the
mips (aliasing below -60 dB).

### 4.4 (c) UI design

**Control map (v1).**

| Control | Function |
|---|---|
| Keys (27) | play; transposed by OCT-/OCT+ (-2..+2 as Dinghy). LEDs: lit while held |
| OCT-, OCT+ | octave; both together = reset; OCT- at power-on is the platform rescue (never remap) |
| MASTER | volume (platform) |
| KNOB1-4 | the four values of the current page, shown as four cards on the screen |
| ALGORITHM | **wavetable select** (the stock firmware's ALGORITHM picks the FM algorithm; this is its closest equivalent: it picks the oscillator's "algorithm"). Hold + KNOB: osc 2 table |
| PRESETS | preset browse (name shown on HOME); press-and-hold not available (no push), so confirm loads automatically after 400 ms of no turning |
| SELECT | **wavetable position, from any page** (the live performance control; 64 frames x 4 sub-steps, with the screen waterfall following). In menus: scroll |
| HOME | home screen; hold: system menu (MIDI channel in/out, tune, velocity curve, LED brightness, screen brightness, CPU/ISR readout) |
| EDIT | OSC pages: 1 TABLE1 / POS1 / TABLE2 / POS2 offset; 2 MIX / SUB / NOISE / DETUNE; 3 (v2) WARP type / amount / sync / PM |
| FX | FILTER page: CUT / RES / TYPE / KEYTRK; page 2: ENV AMT / DRIVE / DLY TIME / DLY MIX; page 3 (v2): chorus, reverb |
| ENV | amp ADSR (A D S R); tap again: MOD ENV ADSR; third tap: destinations (POS AMT, CUT AMT, PITCH AMT, VEL) |
| LFO | RATE / SHAPE / DEPTH / SYNC-or-DEST; second page: depth per destination (POS, PITCH, CUT, AMP); v2: matrix |
| SEL | VOICE: MODE (POLY, MONO, LEGATO, UNISON) / GLIDE / UNISON DETUNE / BEND RANGE |
| GLO | global: TUNE, MIDI CH, VEL CURVE, MIDI LEARN (v2) |
| SAVE | PATCH SAVE: KNOB1 slot, OCT+ saves, OCT- cancel; held: undo to last loaded |
| ARP | v2: MODE / RATE / OCTAVES / GATE; LED flashes on the beat |
| SEQ, PLAY, REC | v2 (arp transport on PLAY; REC reserved). Unused in v1; their LEDs stay dark |

Hold-EDIT quick layer (v2): 16 white keys pick tables 1-16 directly, 11 black keys pick warp mode. Held keys always
sound as normal.

**LEDs.** Page button lit while its page is shown; SAVE blinks when the patch is modified (unsaved); ARP blinks on
the beat; PLAY lit while the arp runs; key LEDs: held notes at full, and in the hold-EDIT quick layer the current
table full and the other 15 dim (`fm1_led_dim`).

**Screen (240x240)**:

```
y   0..24   header: patch name (FONT_L), page name right, voice dots (8 small squares, lit = sounding)
y  28..124  WAVETABLE VIEW 240 x 96 : waterfall of 12 frames (every 5th) as stacked polylines (back = dim lilac,
            front = brighter); the CURRENT position frame drawn bright white over it; if osc2 differs, a second,
            amber marker beside; a thin position bar under it (12-bit)
y 128..170  the live waveform of the current frame (single polyline 240 x 42), after the mip level in use
y 176..239  four value cards (KNOB1-4): label (FONT_S), value (FONT_L), small bar; the touched card highlights
```

- Draw only what changed: static frame once; cards when their value changes (`draw_text_box`); waterfall and
  live wave when `pos`, table, osc2 offset or the modulated position (`pos_now`) changes, rate-limited to 20 fps;
  the waterfall is redrawn into a canvas (`cv_begin`, raise `CV_MAX` to 240x96 = 46 KB, or draw in two 240x48
  strips to stay within the existing 240x64 limit). The DMA'd LCD at ~12 MHz sends 46 KB in about 31 ms **[estimate; measure]**.
- The waterfall is re-computed from the const table by reading level 0 (256 samples) decimated to 120 points:
  cheap enough for the main loop (no ISR involvement).
- Page change: full redraw of cards only; table change: waterfall redraw; patch load: all.
- Idle / playing: while a key is held and the position moves (env/LFO), update the highlight at 20 fps from `pos_now`.
- Overlay: after any parameter change, a one-line hint (name + value in FONT_L) for 700 ms in the live-wave region.

### 4.5 Needs hardware measurement (nothing here was measurable from the repo)

1. **CPU clock under Dinghy** (is the PLL still 240 MHz?) and real cycles per MAC/load/branch on pi32v2: loop of N
   operations timed with `fm1_ticks()` (24 ticks/us).
2. **Cost per voice** of the osc, the SVF, the env and of the whole ISR at 1, 4, 8, 12 voices (T10: on-screen
   `app_audio` ticks).
3. **XIP read cost for table data**: sequential vs random, with and without a pool copy.
4. **LCD strip send time**: 240x96 and 240x48 strips, and whether the DMA lets the main loop continue.
5. **Flash erase/write time for one 4 KB sector at 0xE5000** (settings sector: Dinghy comments "tens of ms").
6. **Interference from the 10 kHz timer5 ISR and USB** on the audio ISR (`Felucca audio.c t5_nested_ticks`).
7. **Aliasing/noise** of the mip scheme at audio level (host spectrum test first, then record via USB audio).
8. Whether `HALF_FRAMES 128` (Felucca) can be used in Dinghy's platform for lower key latency.

### 4.6 Decisions needed from the user

1. **Controller assignment**: ALGORITHM = table, SELECT = position, PRESETS = preset (my proposal), or SELECT =
   menu navigation as in the stock firmware?
2. **Voices**: 8 (Felucca/SLOOP convention, ~15-25 % CPU **[estimate]**) or 12 (stock count)?
3. **Table content**: how many factory tables (6 x 64 vs 12 x 32 frames), and sources. AKWF is licensed CC BY 3.0
   per its repo (attribution; verify upstream). PPG ROM data (WvTable-logue, usynth, avr-ppg) has unclear
   rights: recommend generating our own (additive, FM, formant, wave-folding, spectral morphs).
4. **v1 scope**: include one warp mode (BEND) in v1 or leave all warp to v2?
5. **User tables**: is on-device upload (needs a platform SysEx path: the platform owns SysEx today per
   `docs/API.md`) wanted, or is build-time import enough for v1?
6. **Second core**: stay on one core (my advice); using cpu1 means starting it ourselves (lunar docs/08).
7. **Name/identity** for the firmware (`docs/MAKE-IT-YOURS.md`) before any release.

### 4.7 Open questions (pictures/data that would help)

- Photo of the black-key silk-screen (OP1-OP6 and the labels on the other black keys) and of the encoder
  legends: which encoders have a push switch.
- The official manual PDF page of the stock menu tree (for naming).
- A boot-time dump of `CLK_CON*` / measured loop timing to confirm 240 MHz.
- Which existing tables/ranges of flash a release package can actually carry (can `app.bin` grow past 568 KB, or
  must table data be written by a separate updater step?).

---------------------------------------------------------------------------------------------------------------

## Sources

- `pichenettes/eurorack`@d8f4739: `braids/digital_oscillator.cc` (RenderWavetables, RenderWaveMap, wavetable_definitions), `braids/makefile` (72 MHz), `braids/braids.cc:141` (96 kHz), `plaits/dsp/engine/wavetable_engine.cc`, `plaits/resources/wavetables.py:278-302`, README (MIT); `pichenettes/stmlib`@d18def8 `utils/dsp.h` (Interpolate824, Crossfade).
- `pichenettes/shruthi-1`@56bfe78 `shruthi/oscillator.cc` (RenderInterpolatedWavetable), README (GPL3).
- `plinkysynth/wavetable`@c956a83 `wavetable.cpp:95,266-310`, `LICENSE`.
- `Erbubar23/bubba-machine`@be5f512 `core/bm_engines.c:640-760`, `core/bm_rom.h:13-28`, README.
- `vuki/WvTable-logue`@e33b6d0 `src/wtgen.h`, `README.md`, `LICENSE.md`.
- `dukesrg/logue-osc`@8d9db38 `inc/wavebank.h`.
- `4ms/SWN`@3f833dd `src/oscillator.c`, `inc/sphere.h`, `LICENSE`.
- `Jacajack/usynth`@3f44717, `Jacajack/avr-ppg`@ab19d9d READMEs.
- `raybellis/PicoSynth`@5a52efc `README.md`, `src/voice.cxx`.
- `sensorium/Mozzi`@7f1e5b4 `Oscil.h`; `korginc/logue-sdk`@e848993 `platform/nts-1_mkii/common/osc_api.h`.
- `AndrewBelt/WaveEdit`@31d5fb2 `src/WaveEdit.hpp:123,181`, `src/bank.cpp:79-110`.
- `DatanoiseTV/AKWF_WaveForms`@ba4819c `License.md`.
- `mtytel/vital` README (GPLv3); https://vital.audio
- Serum `clm ` / 2048: https://www.kvraudio.com/forum/viewtopic.php?p=7291246
- FM-1: `AL-255/FM-1-RE`@ec832f2 `docs/architecture.md`, `docs/io/01-boot.md`, `docs/io/04-synth-engine.md`;
  `ip2k/lunar-modulator`@621490b `docs/01-hardware.md`, `docs/05-open-source-feasibility.md`;
  `hugelton/Felucca`@ef1f561 (`README.md`, `LICENSING.md`, `firmware/src/{audio,voice,dsp,eng_analog,eng_phase,lcd}.c`,
  `docs/controls.jpg`); `isod89/sloop-fm1`@cb6dedc (`SLOOP.md`, `GUIDE.md`, `firmware/hal/fm1_flash.h`,
  `firmware/src/{voice,project,eng_sample}.c`); `cicloid/awesome-fm-1`, `aroum/fm1-custom-fw` READMEs;
  https://synthanatomy.com/2026/07/m-vave-fm-1-review-low-budget-pocket-fm-ynthesizer-with-iconic-sounds.html
- This repo: `docs/RULES.md`, `docs/CODEMAP.md`, `docs/API.md`, `docs/SYNTH-DESIGN.md`, `firmware/app.ld`,
  `firmware/hal/fm1_flash.h`, `firmware/src/platform.c`.
