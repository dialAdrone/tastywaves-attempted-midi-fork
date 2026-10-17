#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# The patch log through the real panel (panel.h + patchlog.h on the NOR mock of tests/host.c): save, replace, pick a target,
# confirm a replace, delete; then a second run on the log left behind (a power cycle). Checks the flash image with an
# independent decoder of the record format (docs/PATCH-STORAGE-DESIGN.md section 4).
set -e
cd "$(dirname "$0")/.."
mkdir -p build/host
cc -O2 -Wall -Wno-unused-function -DDINGHY_HOST -DAPP_VER='"dev"' -Ifirmware/src -Ifirmware/gen -o build/host/vavewave firmware/src/vavewave.c
PY="${PYTHON:-$( [ -x .venv/bin/python ] && echo .venv/bin/python || echo python3 )}"
F=build/host/patches.bin
rm -f $F build/host/patches-*.ppm build/host/patches-*.png
DINGHY_PATCHES=$F build/host/vavewave build/host/patches tests/scripts/patches1.txt > build/host/patches1.log
"$PY" tests/patches_check.py $F run1
DINGHY_PATCHES=$F build/host/vavewave build/host/patches tests/scripts/patches2.txt > build/host/patches2.log
"$PY" tests/patches_check.py $F run2
"$PY" - <<'PY'
from pathlib import Path
from PIL import Image
for p in sorted(Path("build/host").glob("patches-*.ppm")):
    Image.open(p).resize((480, 480), Image.NEAREST).save(p.with_suffix(".png"))
    p.unlink()
PY
