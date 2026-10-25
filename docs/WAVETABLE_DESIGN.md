# Wavetable engine: design summary (branch `wavetable_synth`)

Status: **decisions 1-6 made by the user 2026-10-11** (section 6); name still open. Distilled from `docs/research/wavetable-engine.md` (14 projects read, with sources
and commit ids there). Nothing here is measured on the device: **[estimate]** = inference, **[measure]** = needs
hardware. Items under "Decisions" need the user.

## 1. Headline findings

- The chip has far more headroom than `docs/RULES.md` suggests: pi32v2 at about **240 MHz** (source: FM-1-RE and
  lunar-modulator docs, unconfirmed under Dinghy), so the budget is about **5,400 cycles per output frame**. The stock
  firmware runs 12 six-operator FM voices on a second core. **[measure the clock first]**
- No morphing wavetable engine for the FM-1 exists yet (Felucca: phase distortion; Bubba Machine: a 16-wave bank,
  not run on hardware).
- Proven recipe for small integer targets (Braids, Shruthi-1, Plinky): short single-cycle frames, linear
  interpolation in phase and in position, band-limited per-octave mip levels, tables generated on a PC and stored
  `const` in flash (zero RAM).

## 2. Audio engine

| | v1 | v2 |
| --- | --- | --- |
| Voices | **12** (poly / mono / legato, glide) | unison |
| Oscillators | 2 wavetable + sub + noise, **BEND warp** | more warps (sync, PM/FM, fold) |
| Frame | 256 x int16, **12 factory tables x 32 frames**, 6 mip levels (256..8 samples) | on-device upload (section 9) is wanted early; 2048-sample import on the PC |
| Filter | one SVF (LP/BP/HP), trapezoidal, Q12 | ladder, formant |
| Mod | 2 ADSR, 1 LFO, fixed routings | 2 LFO, S&H, 4-slot matrix |
| FX | soft-clip drive, stereo delay (.pool) | chorus, reverb (Felucca fx.c is GPL-3, reusable) |
| Arp | none | 5 modes on ARP |

- **Storage:** about 1 KB per frame including mips, 64 KB per 64-frame table. Flash headroom is about 526 KB over the
  baseline; reserve about 120 KB for code, so about **400 KB of tables = 12 x 32 frames (about 384 KB)**, as decided.
- **Reading:** `idx = ph >> (24+L)`, 16-bit fraction, a guard sample so no masking; position is 12-bit, frame lerp in
  Q15. Mip level per voice per block from `inc` (no 64-bit divide; 1/4-octave hysteresis).
- **Build step:** `tools/gen_wavetables.py` (PC, float allowed): .wav in (WaveEdit 64x256, 2048-sample Serum/Vital,
  AKWF cycles) -> FFT, per-level harmonic limit, normalise per table -> `firmware/gen/wavetables.h` with a directory.
- **Cost [estimate]:** about 100-150 cycles per voice-frame, so 12 voices ≈ 22-33 % of the block (8 would be 15-22 %).
- **Rules that still bind:** integer only; ISR has no flash/screen/poll; envelope/filter/LFO per 32 frames; Bubba's
  SVF uses an int64 divide and must not be copied.

## 3. Backend

- **Parameters:** a `const` descriptor array (name, min, max, default, format) shared by UI, MIDI CC map and
  preset (de)serialiser; about 56 values in v1.
- **Patch:** 64-byte struct, version in byte 0 (tables, 12-bit positions, 52 params, 5-char name).
- **Presets:** 48 factory patches `const` in flash; user patches as an **append-only log** of 64-byte records in the
  sector at `0xE5000` (64 slots, erase only when full), which needs `FL_RANGE_OK` widened to exactly that sector.
  The 256-byte settings sector keeps the current patch plus globals.
- **ISR sharing:** main writes `patch.p[]`, calls `derive()` into the inactive of two `derived` structs and flips a
  pointer; notes go through a 32-entry single-producer ring and **voice allocation moves into the ISR** (single
  writer for voice state). ISR returns `pos_now`, `peak`, `cpu_q8`, `voices_busy` as `volatile` taps.
- **MIDI CCs** follow Felucca's convention (7 level, 71 resonance, 72/73/75 R/A/D, 74 cutoff, 91/93 sends) and add
  20-29 for table, osc2, mix, sub, noise, LFO, mod env, drive; PB ±2 st; PC = preset.
- **Gotcha:** user data above flash 0x93000 is encrypted-flash territory: read it with `st_read` or call
  `fl_plain_window_init()` at boot. **[verify on device]**

## 4. UI on the FM-1 controls (proposal)

| Control | Function |
| --- | --- |
| Keys / OCT± | play; octave -2..+2. OCT- at power-on stays the platform rescue |
| ALGORITHM | wavetable select (hold + KNOB: osc 2) |
| SELECT | wavetable position from any page (menu scroll inside menus) |
| PRESETS | browse presets, auto-load after 400 ms idle |
| KNOB1-4 | the four values of the current page |
| EDIT | OSC pages (table/pos, mix/sub/noise/detune; v2 warp) |
| FX | filter page, then env amt/drive/delay |
| ENV / LFO | amp + mod ADSR, then destinations / LFO rate, shape, depth, per-destination depth |
| SEL | voice mode, glide, unison detune, bend range |
| GLO / HOME | globals (tune, MIDI ch, velocity curve) / home, hold = system (CPU readout) |
| SAVE | slot via KNOB1, OCT+ saves, OCT- cancels; LED blinks when modified |
| ARP / SEQ / PLAY / REC | v2; dark in v1 |
| MASTER | volume (platform) |

**Screen 240x240:** header (name, page, 8 voice dots) / **waterfall of 12 frames** with the current frame
highlighted / live waveform of the current frame / four value cards. Draw only on change, waterfall at ≤ 20 fps in two
240x48 strips (the canvas limit is 240x64), a 700 ms value overlay after edits.

## 5. Measure before committing (in this order)

1. CPU clock under Dinghy and cycles per op (loop timed with `fm1_ticks()`).
2. Per-voice ISR cost at 1/4/8/12 voices (needs T10's on-screen readout).
3. XIP read cost for tables, random vs sequential (mitigation: copy the active table to `.pool`).
4. LCD strip send time; flash erase time for the preset sector; timer5/USB interference.
5. Aliasing of the mip scheme: **host spectrum test first** (render each table over all octaves).

## 6. Decisions (user, 2026-10-11)

| # | Question | Decision |
| --- | --- | --- |
| 1 | Controller map | **As proposed**: ALGORITHM = table, SELECT = position, PRESETS = preset |
| 2 | Voices | **12** to start (estimated 22-33 % of the block; keep overload shedding; measure) |
| 3 | Factory tables | **12 x 32 frames, generated by us** (additive, FM, formant, folding, spectral morphs; no third-party data) |
| 4 | Warp in v1 | **Yes**: BEND in v1 |
| 5 | On-device table upload | **Very desirable**: needs a platform SysEx path, a storage region and a PC uploader (section 9) |
| 6 | Cores | **One core** |
| 7 | Firmware name | open (identity is `FM-1_950`) |

## 7. Prerequisites in the task queue

T4 (encoder events in host mode), T5 (LED log, ticks, ADC stubs), T9 (WAV analysis, plus a table spectrum test),
T10 (ISR timing readout), T8 (split `tastywaves.c` into headers before the engine lands). See `docs/TASKS.md`.

## 8. Answered by the photo (2026-10-11)

- **Black keys carry silk-screened functions:** OP1 OP2 OP3 | OP4 OP5 | OP6 PIT GLO | MONO POLY | (one unlabelled,
  the top F#). By position these are F#3 G#3 A#3 | C#4 D#4 | F#4 G#4 A#4 | C#5 D#5 | F#5. In the stock firmware they
  are the operator / pitch / glide / mono / poly keys (how they double as notes is not known: check the manual).
  Proposal: keep them playable as notes, and use them as function keys only while EDIT is held (`fm1_in.notes` plus
  `fm1_in.buttons`): OP1/OP2 = select osc 1 / osc 2 for the edit pages, OP3 = sub, OP4 = noise, OP5 = filter,
  OP6 = amp, PIT = pitch/bend, GLO = glide, MONO / POLY = voice mode directly.
- **Stock screen layout:** four icons with a value each in a row at the top, over KNOB1-4's values; a big waveform
  area in the middle; patch number and name ("001 PIANO 1") and a battery icon along the bottom. Our four value cards
  should sit at the **top**, as on the stock screen, with the preset name at the bottom.
- **Controls:** MASTER and SELECT top left, PRESETS and ALGORITHM below them, OCT-/OCT+ below those; KNOB1-4 top
  right; PLAY/STOP is one button, REC is red. No push switch is visible on any encoder: assume none.
- Still unknown: the manual's menu tree, and whether a package can carry table data beyond the image.

## 9. On-device table upload (decision 5, to be designed)

Not yet designed; everything here is a proposal to verify.
- **Transport:** a SysEx message set on the USB-MIDI port (the platform owns SysEx today: updater handshake in
  `ota.c` / `usb.c`, in the timer5 ISR). Needs a new, non-colliding manufacturer-id + command set (begin table,
  frame chunk, commit, abort), chunks small enough for the existing ring, data queued to the main loop.
- **Writes:** the main loop does the flash erase/program (stalls audio about 50 ms per sector, so only while quiet
  or with voices muted, and with a visible "UPLOADING" screen), after `FL_RANGE_OK` is widened to exactly the
  user-table region.
- **Region:** the family's free flash is 0x97000..0xDFFFF (about 292 KB) plus 0xE5000..0xFBFFF (about 92 KB) minus the
  preset sector 0xE5000, so room for about 9 user tables of 32 frames if the 256-sample + mips layout is kept
  (about 32 KB each), or twice that if only the top level is stored and mips are derived in `.pool`.
- **Format:** the same frame/mip layout as the factory tables, produced on the PC (`tools/fm1_wavetable_upload.py`
  does the FFT, the band-limiting and the SysEx), plus a header (name, frames, CRC32) so the firmware validates before
  using. Reads through `st_read` or the plain window (encrypted-flash caveat in section 3).
- **Safety:** the biggest brick risk of the whole project is a bug in this write path. The rescue (OCT-) and
  OTA staging must stay out of the writable range; `rules-reviewer` must review it before any device install.
- **Interaction with v1 scope:** the factory engine must not depend on it: ship tables-in-image first (T16), then
  add upload (T19-T21).

## 10. Resampling and RAM-backed wavetable features (v2 direction, user 2026-10-12)

**Dropped:** using the stock FM engine on the second core as a table source. It is part of the closed stock image that
Dinghy replaces; Baud Girl's way of retaining it is closed source and not ours to build on. Building our own FM engine
for this is not wanted: there are many FM variants on the hub already. Decision 6 (one core) stands.

**Direction: resample our own engine into tables.**
- **Bounce:** render the engine offline with a note whose period is exactly 256 samples (44100/256 ≈ 172.27 Hz), so
  each cycle is one frame without pitch detection. Modulate during the render (filter sweep, envelope, LFO, position
  scan, BEND) so the evolution becomes the 32 frames; band-limit with the factory mip generator; store in a user slot.
- **Cost [estimate]:** a few ms per frame, run sliced from the main loop between `platform_poll()` calls; audio ISR
  unaffected, so one can keep playing.
- **Known limits:** the table cannot carry pitch-dependent behaviour (key-tracked filter, key-scaled envelope);
  delay/drive can be included or excluded from the capture.
- **Unknown:** whether the FM-1 has any audio input worth sampling; research before designing for it.

**Spare pool (336 KB; v1 uses about 120 KB: delay 88 KB + active table copies), about 200 KB free:**
- user / resampled slots: about 6 full-mip tables (32 KB each), or about 12 storing only the top level and deriving
  the mips on load;
- spectral editing per frame (harmonic add/cut/tilt via DFT, re-synthesised);
- table morph: spectral crossfade of two tables into a third slot, or a live A<->B morph;
- sample import: slice a sample into cycles (shares code with upload);
- longer delay / reverb tails if room remains.

### 9a. Prior art result (docs/research/sample-transfer.md, 2026-10-12)

- SLOOP, zp12, sloopDX, Felucca and the Hub's web tools are all GPL-3.0-only (code reusable with attribution;
  Felucca's icons/panel image/sample pack are all-rights-reserved: do not copy). Nobody samples on the device: zp12
  records in the browser and uploads by SysEx; SLOOP's punch-in is a live effect on a 0.74 s ring, never stored.
- Transport: USB-MIDI SysEx via Web MIDI or mido. USB is polled in the timer5 ISR into one 640 B frame buffer; the
  main loop handles and acks one frame at a time (about 10 ms per 256 B piece, pack7 8/7).
- Proposal: `F0 pack7(7D 'V' 'W' cmd seq len payload ~sum) F7`; commands HELLO, DIR, BEGIN, DATA (256 B,
  erase on sector start, read-back verify), COMMIT (CRC32, header written last), ABORT, ERASE, READ, AUDITION.
  A slot is 9 sectors (header + 8 data) for one 32,640 B full-mip table: 8 slots in 0x97000..0xDFFFF, about 10 with
  the second region. PC tool `tools/fm1_wavetable.py` (Python + mido).
- Platform gaps: `ota_take()` (ota.c) discards every non-updater frame, so a new service must run before
  `ota_service()`; `FL_RANGE_OK` (platform.c:71) must be widened to exactly the slots; nothing calls
  `fl_plain_window_init()` yet; validate stored headers defensively (Felucca #61: boot-time divide by zero).
- To verify on hardware: erase time and endurance of the flash part, plain-window reads of user data, that
  0x97000..0xDFFFF is unused, frame loss/throughput, USB loss during a ~45 ms IRQ-off erase while playing.
