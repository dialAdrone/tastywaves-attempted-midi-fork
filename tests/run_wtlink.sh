#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Host tests of the VW link stage 0: firmware/src/wtlink.h with the real packer from ota.c and a fake flash, then
# tools/fm1_wavetable.py info / get talking to that same firmware code.
set -e
cd "$(dirname "$0")/.."
mkdir -p build/host
cc -O1 -g -Wall -Wno-unused-function -fsanitize=address,undefined -Ifirmware/src -o build/host/wtlink_check tests/wtlink_check.c
build/host/wtlink_check
"${PYTHON:-$( [ -x .venv/bin/python ] && echo .venv/bin/python || echo python3 )}" tests/wtlink_tool_check.py
