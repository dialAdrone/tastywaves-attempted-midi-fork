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

- [ ] **T4** Host mode: add `enc` script verb (`T enc ID DELTA`) so `fm1_enc_take` returns events; document in
      CODEMAP. Owner: `panel-ui`. Blocks all encoder-driven UI testing.
- [ ] **T5** Host mode: record LED state (`fm1_led_key`) and optionally print on change; stub
      `fm1_adc_read`, `fm1_ticks`/`FM1_TICKS_PER_US` so audio-timing code compiles on host.
- [ ] **T6** Host mode: persist settings across runs (file) or document that it does not.
- [~] **T7** (.gitignore done; stray .swp files remain, close editors) Add `*.swp` to `.gitignore`; delete stray `tools/.install.sh.swp`, `docs/.MAKE-IT-YOURS.md.swp`
      (check no editor is still open on them first).
- [ ] **T8** Split `dinghy.c` into headers (voice engine / UI / patch) so `synth-dsp` and `panel-ui` can work in
      parallel without colliding. Do after T3 so the build proves the split.
- [ ] **T9** Add a host-side WAV analysis helper (peak, RMS, clip count, dominant freq) under `tools/` or `tests/`
      so DSP changes are checked by numbers, not by ear.
- [ ] **T10** Hardware timing: a debug readout of `app_audio` cost (ticks per block, max) on screen, to replace
      guesses about CPU headroom.

## Gate 2 — the synth itself (not yet specified)

- [ ] **T11** Decide the target synth: voice architecture (subtractive vs FM; the panel has ALGORITHM, 4 knobs,
      ENV/LFO/FX buttons), voice count, preset storage. Needs user input. Owner: controller + user.
- [~] **T12** Identity done 2026-10-11: `FM-1_950` (release X.Y -> `FM-1_95Y`), build.py + platform.c. App name / file names still open; see `docs/MAKE-IT-YOURS.md`.

## Log

- 2026-10-11 Queue created from the initial review. Order: T1 → T3 first, then Gate 1, then Gate 2.
- 2026-10-11 Gate 0 done: `tools/install.sh` green. Baseline: image 55076 B of 581564, RAM 12368 B of 98304, pool 30720 B of 344064, identity FM-1_990. gfx.c `.pool` guarded for host (Mach-O); venv pinned to Python 3.12 because python-rtmidi has no cp313 macOS wheel.
- 2026-10-11 FM-1 connected over USB-C, `fm1_install.py --info` reports FM-1_093 [running]. User chose to HOLD OFF on installing build/dinghy.fwsc; no device write done. Ask again before any install.
- 2026-10-11 Identity set to FM-1_95N (user said yes to my proposal of 95N; not yet checked against the hub's list, no registry). Still no device install.
