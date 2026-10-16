#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# The v1 engine on this computer (T16): builds the host fake with UBSan (aborts on any undefined behaviour), renders
# (with ASan too: user tables are read through a heap copy) the engine scripts and checks the WAVs (tests/engine_check.py: peak, clipping, pitch, worst alias component).
#   tests/run_engine.sh            -> build/host/engine_{alias,patch}.wav + .check.txt; exit 1 on a FAIL
# Needs BENCH 0 in firmware/src/vavewave.c. Host timing is the computer's: CPU cost is measured on the FM-1 only.
set -e
cd "$(dirname "$0")/.."
mkdir -p build/host
PY="${PYTHON:-$( [ -x .venv/bin/python ] && echo .venv/bin/python || echo python3 )}"
cc -O1 -g -Wall -Wno-unused-function -fsanitize=address,undefined -fno-sanitize-recover=undefined -DDINGHY_HOST \
   -DAPP_VER="\"${APP_VER:-dev}\"" -Ifirmware/src -Ifirmware/gen -o build/host/vavewave_ubsan firmware/src/vavewave.c
"$PY" -I tests/make_user_table.py build/host/ut0.vwt USERT > /dev/null
"$PY" -I tests/make_hostile_table.py build/host/ut_hostile.vwt
export DINGHY_USERTAB="0:build/host/ut0.vwt;1:build/host/ut0.vwt:bad;2:build/host/ut0.vwt:unchecked;3:build/host/ut_hostile.vwt"
rc=0
for s in engine_alias engine_patch engine_user engine_hostile; do
    build/host/vavewave_ubsan "build/host/$s" "tests/scripts/$s.txt" > "build/host/$s.log"
    tail -1 "build/host/$s.log"
    "$PY" -I tests/engine_check.py "build/host/$s.wav" "tests/scripts/$s.txt" > /dev/null || rc=1
    tail -1 "build/host/$s.check.txt"; grep FAIL "build/host/$s.check.txt" | grep -v "checks:" || true
done
rm -f build/host/engine_*.ppm
exit $rc
