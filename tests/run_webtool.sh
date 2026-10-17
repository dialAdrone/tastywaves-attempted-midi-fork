#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# tools/web (browser upload tool) under node: converter parity with tools/fm1_wavetable.py, link against the firmware logic.
set -e
cd "$(dirname "$0")/.."
mkdir -p build/host
cc -O1 -g -Wall -Wno-unused-function -fsanitize=address,undefined -Ifirmware/src -o build/host/wtlink_check tests/wtlink_check.c
PY="${PYTHON:-$( [ -x .venv/bin/python ] && echo .venv/bin/python || echo python3 )}"
D=$(mktemp -d)
"$PY" tests/make_web_inputs.py "$D"
node tests/webtool_check.mjs "$PY" "$D"
rm -rf "$D"
