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
runk() { printf "%s\n" "$2" "50 patch 15 100" "100 keys 0x1" "400 keys 0" "3500 end" > build/host/fx_$1.txt; build/host/vavewave build/host/fx_$1 build/host/fx_$1.txt > /dev/null; }
run pha "1 patch 57 3
2 patch 58 40
3 patch 59 100
4 patch 60 127"
runk dly "1 patch 57 4
2 patch 58 20
3 patch 59 60
4 patch 60 127"
runk dly2 "1 patch 57 4
2 patch 58 20
3 patch 59 60
4 patch 60 127
5 patch 61 4
6 patch 62 100
7 patch 63 100
8 patch 64 127"
runk rev "1 patch 57 5
2 patch 58 64
3 patch 59 64
4 patch 60 127"
runk revdry "1 patch 57 0"
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
pha, phar = load('pha'); dly, _ = load('dly'); dly2, _ = load('dly2'); rev, revr = load('rev'); rd, _ = load('revdry')
check(pk(pha) < 2 * pk(dry) and sum(abs(x - y) for x, y in zip(dry, pha)) / len(dry) > 50, 'phaser: differs from the dry sound, level bounded')
check(any(abs(a - b) > 100 for a, b in zip(pha, phar)), 'phaser: left and right differ')
rms = lambda x: (sum(v * v for v in x) / max(len(x), 1)) ** .5
sr = 44100
check(rms(dly[int(.7 * sr):int(1.1 * sr)]) > 5 * rms(rd[int(.7 * sr):int(1.1 * sr)]) + 50, 'delay: echoes after the note has ended')
check(rms(dly[-4410:]) < 0.3 * rms(dly[int(.7 * sr):int(1.1 * sr)]) and pk(dly) < 3 * pk(dry), 'delay: the echoes die away (feedback 0.4), level bounded')
check(dly2 == dly, 'delay: a second delay in the chain does nothing (once only)')
check(rms(rev[int(.5 * sr):int(.9 * sr)]) > 5 * rms(rd[int(.5 * sr):int(.9 * sr)]) + 50, 'reverb: a tail after the note has ended')
check(rms(rev[-4410:]) < 0.3 * rms(rev[int(.5 * sr):int(.9 * sr)]) and pk(rev) < 3 * pk(dry), 'reverb: the tail decays, level bounded')
check(any(abs(a - b) > 100 for a, b in zip(rev, revr)), 'reverb: left and right differ')
print('fx: %d failures' % fails)
raise SystemExit(1 if fails else 0)
PY
