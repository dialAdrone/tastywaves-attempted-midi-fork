# Task queue

Controller-owned. One line per item: `[ ]` open, `[~]` in progress, `[x]` done (add date + commit). Keep items
small enough for one agent in one pass. Newest decisions go in the Log at the bottom.

## Gate 0 — get a working build (everything else waits on this)

- [x] **T1** (2026-10-11) Fix `build.sh`: `.venv/bin/activte` typo → `activate`; confirm `PY=python` resolves to the venv and
      Pillow is installed in it. Owner: `fm1-build-verifier`. (Uncommitted WIP on branch `add_uv`.)
- [x] **T2** (2026-10-11, setup + verify; python pinned 3.12 via .python-version) Make `tools/install.sh` a complete, idempotent uv setup (currently the toolchain, SDK and Pillow
      steps are commented out and it ends by running build + host test). Decide: setup only vs setup + verify.
- [x] **T3** (2026-10-11) First green `./build.sh` and `tests/run_host.sh`; record image / RAM / pool numbers in
      `docs/CODEMAP.md` as the baseline. Needs Docker running, toolchain in `~/.jieli/toolchain`, SDK in
      `~/fw-AC79_AIoT_SDK`.

## Gate 1 — tooling gaps found in the first review

- [x] **T4** (2026-10-12, `enc` verb, selftest in tests/host_selftest.c) Host mode: add `enc` script verb (`T enc ID DELTA`) so `fm1_enc_take` returns events; document in
      CODEMAP. Owner: `panel-ui`. Blocks all encoder-driven UI testing.
- [~] **T5** (LED log done in T4) Host mode: remaining: stub
      `fm1_adc_read`, `fm1_ticks`/`FM1_TICKS_PER_US` so audio-timing code compiles on host.
- [ ] **T6** Host mode: persist settings across runs (file) or document that it does not.
- [~] **T7** (.gitignore done; stray .swp files remain, close editors) Add `*.swp` to `.gitignore`; delete stray `tools/.install.sh.swp`, `docs/.MAKE-IT-YOURS.md.swp`
      (check no editor is still open on them first).
- [x] **T8** (2026-10-12; engine.h / ui.h / wtbench.h, main loop in vavewave.c) Split `dinghy.c` into headers (voice engine / UI / patch) so `synth-dsp` and `panel-ui` can work in
      parallel without colliding. Do after T3 so the build proves the split.
- [ ] **T9** Add a host-side WAV analysis helper (peak, RMS, clip count, dominant freq) under `tools/` or `tests/`
      so DSP changes are checked by numbers, not by ear.
- [x] **T10** (2026-10-12; LAST/MAX us and % of 5805 us block on screen, verified on device) Hardware timing: a debug readout of `app_audio` cost (ticks per block, max) on screen, to replace
      guesses about CPU headroom.

## Gate 2 — the synth itself (not yet specified)

- [ ] **T11** Decide the target synth: voice architecture (subtractive vs FM; the panel has ALGORITHM, 4 knobs,
      ENV/LFO/FX buttons), voice count, preset storage. Needs user input. Owner: controller + user.
- [x] **T12** (2026-10-12) Identity `FM-1_950`, name VAVEWave: APP_NAME, vavewave.c, vavewave.fwsc, host output names. Logo/README still Dinghy's.
- [x] **T13** (2026-10-11) Decisions 1-6 made (12 voices, 12x32 own tables, BEND in v1, upload wanted, one core, map as proposed); name (7) still open.
- [~] **T14** (2026-10-12: first bench numbers in Log; SEL 0/1/4/8 and FX table readings pending) Measure on device: CPU clock, per-voice cost, XIP read cost (needs T10). Owner: `synth-dsp`.
- [x] **T15** (2026-10-12; FINDING: linear interpolation fails 60 dB, worst 19.6 dB; see docs/research/wavetable-spectrum.md; decision pending) `tools/gen_wavetables.py` + host spectrum test of the mip scheme. Owner: `synth-dsp`.
- [ ] **T16** Engine v1 in the ISR: osc/mip/morph, SVF, envs, LFO, drive, delay; event ring + derived struct.
- [ ] **T17** Parameter table, patch struct, preset log at 0xE5000 (widen FL_RANGE_OK), MIDI CC map. Owner: `panel-ui`.
- [ ] **T18** UI pages, waterfall screen, LED behaviour per WAVETABLE_DESIGN.md section 4. Owner: `panel-ui`.
- [~] **T19** (prior art done: docs/research/sample-transfer.md; protocol proposal in WAVETABLE_DESIGN.md 9a) Design the upload protocol and storage layout (WAVETABLE_DESIGN.md section 9): SysEx ids/commands,
      region, header + CRC, behaviour while playing. Owner: `researcher` for prior art (Felucca/SLOOP sample upload),
      then `synth-dsp`; `rules-reviewer` signs off.
- [ ] **T20** Platform change: receive table SysEx, queue to main loop, widen FL_RANGE_OK to the user-table region only.
- [ ] **T21** `tools/fm1_wavetable_upload.py` (PC): FFT/band-limit, pack, send, verify.
- [ ] **T22** Confirm the black-key function layer with the stock manual; decide EDIT-held key layer (section 8).
- [ ] **T23** Design resample/bounce (WAVETABLE_DESIGN.md section 10): offline render at 256-sample period, which
      parameters can be swept, UI flow (ALGORITHM/SELECT/OP keys), slot storage. Depends on the v1 engine (T16).
- [ ] **T24** Spectral tools in firmware: integer DFT/IDFT for mip derivation, harmonic edit, table morph. Shares code
      with T15's generator logic; measure cost.
- [ ] **T25** Pool budget: define the map (delay, active tables, user slots) and enforce with a build check.
- [x] **T26** (2026-10-12, answered by user: no audio input jack; sampling is over USB via browser, see sample-transfer research) Research: does the FM-1 have an audio input worth sampling? (`researcher`)

## Log

- 2026-10-11 Queue created from the initial review. Order: T1 → T3 first, then Gate 1, then Gate 2.
- 2026-10-11 Gate 0 done: `tools/install.sh` green. Baseline: image 55076 B of 581564, RAM 12368 B of 98304, pool 30720 B of 344064, identity FM-1_990. gfx.c `.pool` guarded for host (Mach-O); venv pinned to Python 3.12 because python-rtmidi has no cp313 macOS wheel.
- 2026-10-11 FM-1 connected over USB-C, `fm1_install.py --info` reports FM-1_093 [running]. User chose to HOLD OFF on installing build/dinghy.fwsc; no device write done. Ask again before any install.
- 2026-10-11 Identity set to FM-1_95N (user said yes to my proposal of 95N; not yet checked against the hub's list, no registry). Still no device install.
- 2026-10-11 Branch wavetable_synth. `researcher` agent defined (.claude/agents/researcher.md; not registered until restart, run via general-purpose). Research: docs/research/wavetable-engine.md; summary: docs/WAVETABLE_DESIGN.md. Gate 3 added.
- 2026-10-11 User decisions: map as proposed, 12 voices, 12x32 own tables, BEND in v1, on-device upload desirable, one core. Photo of the panel received: black keys are silk-screened OP1-OP6/PIT/GLO/MONO/POLY. Name open.
- 2026-10-12 T4 done by panel-ui (host enc verb, LED log, note edges, selftest). Name chosen: VAVEWave (rename T12 pending until T15 agent finishes).
- 2026-10-12 FM-engine-as-table-source dropped (stock engine is closed, replaced by Dinghy). Direction: resample our own engine + RAM-backed spectral/morph features (T23-T26).
- 2026-10-12 Rename to VAVEWave done (build + host + selftest green; image 55080 B). T15 done: generator + spectrum test; linear interpolation FAILS the 60 dB target (19.6 dB worst); 8-tap sinc gets 63.3 dBc at higher CPU. Sample-transfer research (SLOOP/zp12) started.
- 2026-10-12 Sample-transfer research done: all firmwares GPL-3.0-only (SLOOP is not commercial); nobody samples on-device; SysEx upload protocol proposed.
- 2026-10-12 T8/T10 done; bench build (BENCH 1: 12 voices x 2 osc, no filter/env/FX, 12 tables) reviewed SAFE by rules-reviewer and installed on the FM-1 by the user (identity FM-1_950). Plays, keys respond. MAX of the audio block, 12 voices x 2 osc, 12 different tables: LINEAR 17.1 %, CUBIC 4-TAP 33.0 %, SINC 8-TAP 44.0 %, SHED 0. Per osc-sample that is about 39 / 75 / 100 cycles at 240 MHz, close to the instruction counts (37 / 71 / 84), so 240 MHz at ~1 IPC is consistent. SINC costs ~16 cycles more than its instruction count (multiply/load stalls, tap table).
