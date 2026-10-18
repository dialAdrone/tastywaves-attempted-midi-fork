#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# The effects chain (fx.h) on the fake FM-1: a held note with no effect, chorus and flanger (feedback at its maximum) in
# slot 1. P_FX1_TYPE is parameter 57 (A 58, B 59, MIX 60; patch.h), P_CUTOFF 15.
set -e
cd "$(dirname "$0")/.."
mkdir -p build/host
cc -O2 -Wall -Wno-unused-function -DDINGHY_HOST -DAPP_VER='"dev"' -Ifirmware/src -Ifirmware/gen -o build/host/vavewave firmware/src/vavewave.c
run() { printf "%s\n" "$2" "50 patch 15 100" "100 keys 0x1" "3000 keys 0" "3500 end" > build/host/fx_$1.txt; build/host/vavewave build/host/fx_$1 build/host/fx_$1.txt > /dev/null; }
run dry "1 patch 57 0"
run cho "1 patch 57 1
2 patch 58 40
3 patch 59 100
4 patch 60 127"
run fla "1 patch 57 2
2 patch 58 30
3 patch 59 127
4 patch 60 127"
run off "1 patch 57 2
2 patch 60 127
3 patch 57 0"
PY="${PYTHON:-$( [ -x .venv/bin/python ] && echo .venv/bin/python || echo python3 )}"
"$PY" - <<'PY'
import struct, wave
fails = 0
def check(ok, what):
    global fails
    print(('ok    ' if ok else 'FAIL  ') + what)
    fails += 0 if ok else 1
def load(n):
    w = wave.open('build/host/fx_%s.wav' % n); d = w.readframes(w.getnframes())
    a = struct.unpack('<%dh' % (len(d) // 2), d); return a[0::2], a[1::2]
dry, _ = load('dry'); cho, chor = load('cho'); fla, _ = load('fla'); off, _ = load('off')
pk = lambda x: max(map(abs, x))
check(pk(cho) < 1.5 * pk(dry) and pk(cho) > 0.3 * pk(dry), 'chorus: level stays near the dry level')
check(sum(abs(x - y) for x, y in zip(dry, cho)) / len(dry) > 50, 'chorus: the sound differs from the dry one')
check(any(abs(a - b) > 100 for a, b in zip(cho, chor)), 'chorus: left and right differ (the quarter cycle LFO)')
check(pk(fla) < 3 * pk(dry), 'flanger with maximum feedback: bounded')
check(pk(fla[-4410:]) < 1000 and pk(cho[-4410:]) < 1000, 'flanger / chorus: silent a while after the note ends')
check(max(abs(a - b) for a, b in zip(dry[8000:], off[8000:])) < 50, 'a slot set back to OFF passes the signal through')
print('fx: %d failures' % fails)
raise SystemExit(1 if fails else 0)
PY
