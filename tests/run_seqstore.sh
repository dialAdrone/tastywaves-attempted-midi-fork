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
print('seqstore: %d failures' % fails)
raise SystemExit(1 if fails else 0)
PY
