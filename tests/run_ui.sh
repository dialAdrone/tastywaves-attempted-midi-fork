#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# UI design mock-ups (T18): tests/ui_demo.c + firmware/src/ui_screens.h on the fake FM-1 -> build/host/ui-<name>.png
set -e
cd "$(dirname "$0")/.."
mkdir -p build/host
cc -O2 -Wall -Wno-unused-function -DDINGHY_HOST -Ifirmware/src -Ifirmware/gen -o build/host/ui_demo tests/ui_demo.c
rm -f build/host/ui-*.png build/host/ui-*.ppm
build/host/ui_demo build/host/ui tests/scripts/ui_demo.txt
"${PYTHON:-$( [ -x .venv/bin/python ] && echo .venv/bin/python || echo python3 )}" - <<'PY'
from pathlib import Path
from PIL import Image
for p in sorted(Path("build/host").glob("ui-*.ppm")):
    Image.open(p).resize((480, 480), Image.NEAREST).save(p.with_suffix(".png"))
    p.unlink()
    print("screen ->", p.with_suffix(".png"))
PY
