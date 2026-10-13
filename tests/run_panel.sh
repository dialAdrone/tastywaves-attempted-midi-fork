#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# The real panel on this computer: vavewave.c (panel.h + the v1 engine) with tests/scripts/panel_ui.txt
# -> build/host/panel-<name>.png (480x480 = 2x), build/host/panel.wav, and the LED / settings log in panel.log.
set -e
cd "$(dirname "$0")/.."
mkdir -p build/host
cc -O2 -Wall -Wno-unused-function -DDINGHY_HOST -DAPP_VER="\"${APP_VER:-dev}\"" -Ifirmware/src -Ifirmware/gen -o build/host/vavewave firmware/src/vavewave.c
rm -f build/host/panel-*.png build/host/panel-*.ppm
build/host/vavewave build/host/panel tests/scripts/panel_ui.txt > build/host/panel.log
"${PYTHON:-$( [ -x .venv/bin/python ] && echo .venv/bin/python || echo python3 )}" - <<'PY'
from pathlib import Path
from PIL import Image
for p in sorted(Path("build/host").glob("panel-*.ppm")):
    Image.open(p).resize((480, 480), Image.NEAREST).save(p.with_suffix(".png"))
    p.unlink()
    print("screen ->", p.with_suffix(".png"))
PY
