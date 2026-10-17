# SPDX-License-Identifier: GPL-3.0-only
"""Decodes the patch log window written by tests/host.c (DINGHY_PATCHES file, 16 KiB = 4 sectors) with its own reader of
the record format (docs/PATCH-STORAGE-DESIGN.md section 4) and checks what tests/scripts/patches1.txt must have left."""
import re
import struct
import sys
import zlib

data = open(sys.argv[1], 'rb').read()
assert len(data) == 0x4000, len(data)
fails = 0


def check(ok, what):
    global fails
    print(('ok    ' if ok else 'FAIL  ') + what)
    fails += 0 if ok else 1


recs = []                                          # (seq, type, id, name, values, sector)
for s in range(4):
    sec = data[s * 4096:(s + 1) * 4096]
    pos = 0
    while pos + 28 <= 4096:
        ln = struct.unpack_from('<H', sec, pos)[0]
        if ln != 0xFFFF and 28 <= ln <= 256 and ln % 4 == 0 and pos + ln <= 4096:
            body = sec[pos:pos + ln]
            if zlib.crc32(body[:-4]) == struct.unpack_from('<I', body, ln - 4)[0]:
                typ, idn = body[2], body[3]
                seq = struct.unpack_from('<I', body, 4)[0]
                ver, n = body[8], body[9]
                name = body[10:22].split(b'\0')[0].decode()
                vals = list(struct.unpack_from('<%dh' % n, body, 24))
                assert ver == 1 and body[22:24] == b'\0\0' and (typ == 1 and ln == (24 + 2 * n + 4 + 3) // 4 * 4 or typ == 2 and n == 0)
                recs.append((seq, typ, idn, name, vals, s))
                pos += ln
                continue
        pos += 4
live = {}
for seq, typ, idn, name, vals, s in sorted(recs):
    if typ == 1:
        live[idn] = (name, vals)
    else:
        live.pop(idn, None)
seqs = [r[0] for r in recs]
check(len(seqs) == len(set(seqs)), 'sequence numbers are unique')
print('      records:', len(recs), 'live ids:', sorted(live))
P_CUTOFF, P_RESO, P_DRIVE = 15, 16, 40
src = open(__file__.rsplit('/tests/', 1)[0] + '/firmware/src/patch.h').read()
NP = len(re.findall(r'\{"[A-Z0-9]+", ', src.split('PDESC[P_COUNT]')[1].split('};')[0]))      # PDESC rows = P_COUNT
print('      P_COUNT', NP)

if sys.argv[2] == 'run1':
    check(sorted(live) == [0, 6], 'ids U01 and U07 hold patches (U02 saved, replaced, then deleted)')
    check(len(live.get(0, ('', []))[1]) == 47, 'U01 has 47 parameters (trailing defaults are not stored; the arp ones are defaults)')
    u1 = live[0][1]
    check(live[0][0] == 'BRIGHT PAD' and u1[P_CUTOFF] == 55, 'U01: named after BRIGHT PAD, cutoff 55 (the replace in place won)')
    u5 = live[6]
    check(u5[0] == 'REED SOLO' and u5[1][P_DRIVE] == 70, 'U07: named after REED SOLO (factory 8), drive 70')
    check(any(r[1] == 2 and r[2] == 1 for r in recs), 'a tombstone for U02 exists')
    check(sum(1 for r in recs if r[1] == 1 and r[2] == 1) == 2, 'U02 was written twice (save, confirmed replace)')
    u2s = [r for r in sorted(recs) if r[1] == 1 and r[2] == 1]
    check(u2s[0][4][P_RESO] == 99 and u2s[0][4][P_DRIVE] == 0 and u2s[1][4][P_DRIVE] == 20 and u2s[1][3] == 'REED SOLO',
          'U02 versions: reso 99 (from ORGAN KEYS); then the REED SOLO edit with drive 20 copied over it')
sys.exit(1 if fails else 0)
