---
name: fm1-build-test
description: Build the FM-1 firmware and test it on the computer. Use after any change in firmware/src, or when asked to build, test, listen, or screenshot the synth.
---
# Build and host-test

From repo root:

1. **Host (fast, no chip):** `tests/run_host.sh` → `build/host/tastywaves.wav`, `build/host/tastywaves-1000.png`,
   `-3000.png`; stdout lists MIDI out and settings saves. Custom script / film:
   `build/host/tastywaves build/host/out my_script.txt [FPS]`  (script verbs: `T keys MASK`, `T btn ID`,
   `T midi ST D1 D2`, `T end`; ms times, `#` comments). `APP_VER=x tests/run_host.sh` sets the version text.
2. **Inspect the result:** read the PNGs with the Read tool; for the WAV, analyse in the scratchpad with
   `python3 -I` (peak, RMS, clipping count, dominant frequency via FFT) rather than guessing.
3. **Chip build:** `./build.sh` (macOS: Docker must run). Expect `ok` lines + `package build/tastywaves.fwsc`.
   Report image/RAM/pool numbers. `FAIL` lines: see `docs/RULES.md`.
4. A host pass does not prove the chip build: float / 64-bit divide / ROM constants fail only in step 3.

Gaps in host mode (encoders, LEDs, ADC, flash persistence, CPU timing) are listed in `docs/CODEMAP.md`.
Known WIP: `build.sh` currently sources `.venv/bin/activte` (typo for `activate`).
