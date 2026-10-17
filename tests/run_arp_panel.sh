#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# The arp on the real panel (vavewave.c on the fake FM-1): ARP button, held keys, latch, page; the "ARP on/off n" lines
# of the host log show what the engine played (the engine itself is tested by tests/run_arp.sh).
set -e
cd "$(dirname "$0")/.."
mkdir -p build/host
cc -O2 -Wall -Wno-unused-function -DDINGHY_HOST -DAPP_VER='"dev"' -Ifirmware/src -Ifirmware/gen -o build/host/vavewave firmware/src/vavewave.c
build/host/vavewave build/host/arp tests/scripts/arp.txt > build/host/arp.log
build/host/vavewave build/host/arp2 tests/scripts/arp2.txt > build/host/arp2.log
build/host/vavewave build/host/arp3 tests/scripts/arp3.txt > build/host/arp3.log
PY="${PYTHON:-$( [ -x .venv/bin/python ] && echo .venv/bin/python || echo python3 )}"
"$PY" - <<'PY'
import re
def load(f):
    ev = []
    for l in open(f):
        m = re.match(r'\s*(\d+) ms  ARP (on|off) (\d+)', l)
        if m: ev.append((int(m[1]), m[2], int(m[3])))
    return ev
fails = 0
def check(ok, what):
    global fails
    print(('ok    ' if ok else 'FAIL  ') + what)
    fails += 0 if ok else 1
a = load('build/host/arp.log')
ons = [(t, n) for t, k, n in a if k == 'on']
check(not any(t < 700 for t, k, n in a), 'arp off: a key is a plain note (nothing from the arp)')
check(len(ons) >= 5 and [n for t, n in ons[:3]] == sorted(n for t, n in ons[:3]), 'ARP pressed, three keys: notes arrive in ascending order')
gaps = [ons[i + 1][0] - ons[i][0] for i in range(min(len(ons), 6) - 1)]
check(all(110 <= g <= 140 for g in gaps), '120 BPM 1/16: steps about 125 ms apart (%s)' % gaps)
check(len({n for t, n in ons}) == 3, 'three different notes')
check(not any(t > 1900 and k == 'on' for t, k, n in a), 'keys up: the arp stops')
b = load('build/host/arp2.log')
bo = [(t, n) for t, k, n in b if k == 'on']
check(any(t < 1400 for t, n in bo) and any(1000 < t < 1400 for t, n in bo), 'latched notes keep the arp going after the keys are up')
check(len({n for t, n in bo if t < 1400}) == 2, 'two latched notes: both are played')
check(not any(1500 < t < 1800 and k == 'on' for t, k, n in b), 'PLAY again lets them go: the arp stops')
check(not any(t > 2700 and k == 'on' for t, k, n in b), 'nothing plays after the arp is switched off again')
c = [t for t, k, n in load('build/host/arp3.log') if k == 'on']
g = [c[i + 1] - c[i] for i in range(len(c) - 1)]
check(len(c) >= 8 and all(140 <= x <= 160 for x in g[2:]), 'MIDI clock at 100 BPM: steps 150 ms apart (%s)' % g[:8])
print('arp panel: %d failures' % fails)
raise SystemExit(1 if fails else 0)
PY
