---
name: fm1-release-install
description: Cut a versioned release build and install a .fwsc on a connected M-VAVE FM-1 over USB-MIDI. Use when asked to flash, install, release, or check what firmware is on the device.
---
# Release and install

**Hardware write — get explicit user confirmation before step 3.**

1. `./build.sh --release X.Y` (one digit each) → `build/vavewave-X.Y.fwsc`, identity `FM-1_95Y`. Without
   `--release`: `build/vavewave.fwsc`, identity `FM-1_950`. Own firmware → pick own identity in `tools/build.py`
   (see `docs/MAKE-IT-YOURS.md`; check the taken list).
2. `.venv/bin/python tools/fm1_install.py --info` — what runs on the connected FM-1 (needs `mido python-rtmidi`).
3. `.venv/bin/python tools/fm1_install.py build/<file>.fwsc` — it asks before writing; FM-1 restarts into the new
   firmware. Exit codes: 3 no FM-1, 4 connection lost, 5 timeout, 6 wrong model / identity mismatch.
4. Verify: `--info` again; screen shows the name and version.
5. Publishing (tag, GitHub release with .fwsc, hub listing) is outward-facing: confirm each step with the user.
Fallback if broken: OCT- at power-on (rescue) → install a good build; or the Firmware Hub for the official firmware.
