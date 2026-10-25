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
build/host/vavewave build/host/arp4 tests/scripts/arp4.txt > build/host/arp4.log
build/host/vavewave build/host/arp5 tests/scripts/arp5.txt > build/host/arp5.log
build/host/vavewave build/host/arp6 tests/scripts/arp6.txt > build/host/arp6.log
build/host/vavewave build/host/seq tests/scripts/seq.txt > build/host/seq.log
build/host/vavewave build/host/seq2 tests/scripts/seq2.txt > build/host/seq2.log
build/host/vavewave build/host/seq3 tests/scripts/seq3.txt > build/host/seq3.log
build/host/vavewave build/host/fxbyp tests/scripts/fxbyp.txt > build/host/fxbyp.log
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
d = [t for t, k, n in load('build/host/arp4.log') if k == 'on']
check(any(1000 < t < 1500 for t in d) and any(2000 < t < 2500 for t in d) and any(3000 < t < 3500 for t in d), 'arp + latch: the arp goes on playing through two patch changes')
e = load('build/host/arp5.log')
check(not any(1300 < t < 2000 and k == 'on' for t, k, n in e if n), 'ARP off over latched notes: no arp steps after the handover')
check(sum(1 for t, k, n in e if 2050 < t < 3000 and k == 'on') >= 5, 'ARP on again: the latched notes arpeggiate again')
h = load('build/host/arp6.log')
check(sum(1 for t, k, n in h if 350 < t < 1200 and k == 'on') >= 5, 'held keys + ARP: arpeggio')
check(not any(1300 < t < 2000 and k == 'on' for t, k, n in h), 'ARP off: the held keys are plain notes')
check(sum(1 for t, k, n in h if 2050 < t < 2900 and k == 'on') >= 4, 'ARP on again with the keys still down: arpeggio again')
check(not any(t > 3000 and k == 'on' for t, k, n in h), 'keys up: silence')
sl = open('build/host/seq.log').read().splitlines()
mo = [int(re.match(r'\s*\d+ ms  MIDI out 90 (\d+)', l)[1]) for l in sl if re.match(r'\s*\d+ ms  MIDI out 90 ', l)]
n0, n4, n7 = mo[0], mo[1], mo[2]
q = load('build/host/seq.log')
qo = [(t, n) for t, k, n in q if k == 'on' and 1090 < t < 3000]
check(len(mo) >= 3 and (n4 - n0, n7 - n0) == (4, 7), 'step input: the keys sound as plain notes while recording')
check([n for t, n in qo[:6]] == [n0, n4, n7, n0, n4, n7], 'SEQ plays the 5 steps: note note (rest) note (tie): %s' % [n for t, n in qo[:7]])
gp = [qo[i + 1][0] - qo[i][0] for i in range(5)]
check(all(abs(a - b) <= 8 for a, b in zip(gp, [125, 250, 250, 125, 250])), 'timing: 125 / 250 (rest) / 250 (tie) ms (%s)' % gp)
off7 = [t for t, k, n in q if k == 'off' and n == n7 and t > 1150 and t not in [x for x, y in qo]]
tr = [(t, n) for t, k, n in q if k == 'on' and 3200 < t < 4300]
d = n0 + 1 - 60
check(len(tr) >= 4 and all(n - d in (n0, n4, n7) for t, n in tr[:4]), 'a held key transposes the pattern by (key - 60 = %d)' % d)
check(not any(t > 4500 and t < 5100 and False for t, k, n in q) and any(4400 < t < 5200 and k == 'on' and n in (n0, n4, n7) for t, k, n in q), 'key released: the pattern plays as recorded again')
check(any(5900 < t < 6200 and k == 'on' for t, k, n in q), 'REC held alone only asks: the pattern still plays')
check(not any(t > 6300 and k == 'on' for t, k, n in q), 'REC held, then REC again, clears the pattern: silence')
q2 = load('build/host/seq2.log')
r13 = [int(m[1]) for m in (re.match(r'\s*(\d+) ms  LED 13 on', l) for l in open('build/host/seq2.log')) if m]
check(r13 and r13[0] > 150, 'REC on HOME does nothing; on the SEQ page it records (REC LED at %s ms)' % (r13[:1],))
check(not any(750 < t < 1000 and k == 'on' for t, k, n in q2), 'REC ends: the pattern waits, nothing plays')
check(any(1000 < t < 1900 and k == 'on' for t, k, n in q2), 'PLAY on the SEQ page starts the sequencer')
check(not any(2150 < t < 2950 and k == 'on' for t, k, n in q2), 'PLAY on the SEQ page stops it')
check(any(3000 < t < 3500 and k == 'on' for t, k, n in q2), 'PLAY starts it again')
check(not any(3700 < t < 4100 and k == 'on' for t, k, n in q2), 'on another page PLAY stops it too')
check(any(4100 < t < 4600 and k == 'on' for t, k, n in q2), 'SEQ still lit: PLAY on another page starts it again')
check(not any(5200 < t < 6500 and k == 'on' for t, k, n in q2), 'hold SEQ: off and stopped; PLAY is LATCH then and does not start it')
q3 = [(t, n) for t, k, n in load('build/host/seq3.log') if k == 'on']
w = lambda a, b: [n for t, n in q3 if a <= t < b]
check(w(990, 1490) == [53, 57, 60, 53] and w(1490, 1990) == [60, 64, 67, 60], 'SEQ + ARP UP + CHORD MAJ: a step a beat, its triad arpeggiated at 1/16 (%s %s)' % (w(990, 1490), w(1490, 1990)))
check(w(2995, 3990) and set(w(2995, 3990)) <= {53, 60} and len(w(2995, 3490)) == 4, 'SEQ + ARP, CHORD OFF: the step note alone, repeated (%s)' % w(2995, 3990))
grp = {}
for t, n in q3:
    if 4000 <= t < 4990: grp.setdefault(t, []).append(n)
check(len(grp) >= 6 and all(sorted(g) in ([53, 57, 60], [60, 64, 67]) for g in grp.values()), 'SEQ, ARP off, CHORD MAJ: block chords a 1/16 (%d)' % len(grp))
check(w(5000, 5500)[:3] == [46, 50, 53] and set(w(5000, 6490)) <= {46, 50, 53, 57, 60}, 'CHORD HELD (F3 A3 C4): the lowest key transposes (-7), the others give the shape (%s)' % w(5000, 6000))
check(w(6550, 7000) and set(w(6550, 7000)) <= {53, 60}, 'keys up: HELD with no keys plays the note alone (%s)' % w(6550, 7000))
import wave, struct, math
wv = wave.open('build/host/fxbyp.wav'); fr = wv.getframerate(); smp = struct.unpack('<%dh' % (wv.getnframes() * 2), wv.readframes(wv.getnframes()))
rms = lambda a, b: math.sqrt(sum(x * x for x in smp[a * fr // 500:b * fr // 500]) / max(1, (b - a) * fr // 500))
e1, e2, e3 = rms(600, 1400), rms(2200, 2900), rms(3700, 4400)
check(e1 > 30 and e2 < 2 and e3 > 5, 'hold FX bypasses the chain (delay echoes %.0f, bypassed %.0f), hold again: the echoes, still running, return (%.0f)' % (e1, e2, e3))
print('arp panel: %d failures' % fails)
raise SystemExit(1 if fails else 0)
PY
