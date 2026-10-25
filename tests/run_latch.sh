#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# LATCH on the real panel (tastywaves.c on the fake FM-1): the notes sent out as MIDI show what the keys did.
set -e
cd "$(dirname "$0")/.."
mkdir -p build/host
cc -O2 -Wall -Wno-unused-function -DDINGHY_HOST -DAPP_VER='"dev"' -Ifirmware/src -Ifirmware/gen -o build/host/tastywaves firmware/src/tastywaves.c
build/host/tastywaves build/host/latch tests/scripts/latch.txt > build/host/latch.log
PY="${PYTHON:-$( [ -x .venv/bin/python ] && echo .venv/bin/python || echo python3 )}"
"$PY" - <<'PY'
import re
ev = []
for l in open('build/host/latch.log'):
    m = re.match(r'\s*(\d+) ms  MIDI out ([0-9A-F]+) (\d+) (\d+)', l)
    if m:
        t, st, n, v = int(m[1]), int(m[2], 16), int(m[3]), int(m[4])
        ev.append((t, 'on' if st & 0xF0 == 0x90 else 'off', n))
fails = 0
def at(lo, hi): return [(k, n) for t, k, n in ev if lo <= t < hi]
def check(ok, what):
    global fails
    print(('ok    ' if ok else 'FAIL  ') + what)
    fails += 0 if ok else 1
a = at(100, 300)
check(len(a) == 2 and a[0][0] == 'on' and a[1] == ('off', a[0][1]), 'normal: on at the press, off at the release')
n0 = a[0][1]
check(at(300, 500) == [('on', n0)] and at(500, 600) == [], 'latched: on at the press, nothing at the release')
n1 = n0 + 1
check(at(600, 800) == [('on', n1)], 'a second key latches too')
check(at(800, 1000) == [('off', n0)], 'the same key again lets that note go')
check(at(1000, 1100) == [('off', n1)], 'latch off releases the rest')
n2 = n0 + 2
check(at(1100, 1200) == [('on', n2)] and at(1200, 1400) == [], 'a key held when latch goes on is latched, its release does nothing')
check(at(1400, 1500) == [('off', n2)], 'latch off lets it go')
check(at(1500, 1800) == [('on', n1)], 'latched again')
check(at(1800, 1900) == [('off', n1), ('on', n1 + 12)], 'OCT+: the latched note moves up an octave')
check(at(1900, 2100) == [('off', n1 + 12)], 'the key at the new octave is that note: pressing it lets it go')
check(at(2100, 2200) == [] and at(2200, 2400) == [('on', n1)], 'OCT- with nothing latched changes nothing; the key is the lower note again')
check(at(2500, 2800) == [('on', n1)], 'after MIDI all-notes-off the latch is clear: the key starts a note again')
raise SystemExit(1 if fails else 0)
PY
