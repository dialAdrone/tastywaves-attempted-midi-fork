#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# The note pattern is saved with the patch (panel.h pn_pat_*): run 1 records two steps and SAVEs; run 2 (a power cycle, the log
# kept) loads that patch and the sequencer plays them; run 3 replaces a playing pattern by loading it, and a factory patch
# after it leaves the pattern alone.
set -e
cd "$(dirname "$0")/.."
mkdir -p build/host
cc -O2 -Wall -Wno-unused-function -DDINGHY_HOST -DAPP_VER='"dev"' -Ifirmware/src -Ifirmware/gen -o build/host/vavewave firmware/src/vavewave.c
F=build/host/seqstore.bin
rm -f $F
for n in 1 2 3; do DINGHY_PATCHES=$F build/host/vavewave build/host/ss$n tests/scripts/seqstore$n.txt > build/host/ss$n.log; done
G=build/host/seq45.bin
rm -f $G
DINGHY_PATCHES=$G build/host/vavewave build/host/s4 tests/scripts/seq4.txt > build/host/s4.log
DINGHY_PATCHES=$G build/host/vavewave build/host/s5 tests/scripts/seq5.txt > build/host/s5.log
PY="${PYTHON:-$( [ -x .venv/bin/python ] && echo .venv/bin/python || echo python3 )}"
"$PY" - <<'PY'
import re
fails = 0
def check(ok, what):
    global fails
    print(('ok    ' if ok else 'FAIL  ') + what)
    fails += 0 if ok else 1
def ons(f):
    return [(int(m[1]), int(m[2])) for l in open(f) for m in [re.match(r'\s*(\d+) ms  ARP on (\d+)', l)] if m]
d = open('build/host/seqstore.bin', 'rb').read()
rec = [d[s*4096:(s+1)*4096] for s in range(4)]
r = next(x for x in rec if x[0:2] != b'\xff\xff')
check(r[22] == 3, 'the record holds 3 pattern words (length + 2 steps)')
b = ons('build/host/ss2.log')
check(len(b) >= 4 and [n for t, n in b[:4]] == [53, 57, 53, 57] and b[0][0] > 1800, 'run 2: loading the patch starts the saved two-step pattern')
c = ons('build/host/ss3.log')
before = [n for t, n in c if t < 1400]
after = [n for t, n in c if 2300 < t < 3000]
late = [n for t, n in c if t > 3600]
check(before and all(n == 60 for n in before), 'run 3: the one-step pattern plays first')
check(len(after) >= 4 and set(after) == {53, 57}, 'loading the patch replaces the pattern')
check(len(late) >= 4 and set(late) == {53, 57}, 'a factory patch afterwards leaves the pattern alone')
e = ons('build/host/s4.log')
w = lambda a, b, ev=e: [n for t, n in ev if a <= t < b]
one, two = [n for n in w(1690, 3690) if n in (53, 60)], [n for n in w(1690, 3690) if n in (57, 65)]
check(set(w(1690, 3690)) == {53, 60, 57, 65} and 14 <= len(one) <= 17 and 7 <= len(two) <= 9, 'two sequencers: SEQ1 at 1/16 (%d notes), SEQ2 at 1/8 (%d) in 2 s' % (len(one), len(two)))
check(w(3750, 4690) and set(w(3750, 4690)) <= {53, 60}, 'SEQ2 LEVEL 0: only SEQ1 sounds')
check({61, 69} <= set(w(4750, 6690)) and {53, 60} <= set(w(4750, 6690)), 'SEQ2 with its own arp UP + CHORD MAJ (A3: 57 61, F4: 65 69), SEQ1 plain under it')
f = ons('build/host/s5.log')
check({53, 60, 57, 61, 65, 69} <= set(n for t, n in f if t > 2550), 'after a power cycle, loading U01 brings both patterns and SEQ2\'s rate, arp and chord back')
print('seqstore: %d failures' % fails)
raise SystemExit(1 if fails else 0)
PY
