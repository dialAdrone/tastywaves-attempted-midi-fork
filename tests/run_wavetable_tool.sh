#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# tools/fm1_wavetable.py on synthetic Serum / WaveEdit / Ableton / Surge layouts (no device).
set -e
cd "$(dirname "$0")/.."
exec "${PYTHON:-$( [ -x .venv/bin/python ] && echo .venv/bin/python || echo python3 )}" tests/wavetable_tool_check.py
