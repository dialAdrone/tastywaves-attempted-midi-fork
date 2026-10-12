# SPDX-License-Identifier: GPL-3.0-only
"""Host spectrum test of the wavetable mip scheme (task T15, docs/WAVETABLE_DESIGN.md section 5 item 5).

An integer-exact model of the planned oscillator, reading the generated tables (build/wavetables.bin):
    inc   = INC_C9[n % 12] >> (10 - n / 12)                 (as vavewave.c)
    L     = smallest L in 0..5 with inc < 2^(25 + L)         ((64 >> L) * f0 < 22050, no divide); clamp at 5
    idx   = ph >> (24 + L), frac = (ph >> (8 + L)) & 0xFFFF; s = a + (((b - a) * frac) >> 16)
    pos   = 12-bit, f = pos >> 7, x = (pos & 127) << 8 (Q15, 32 frames); out = s0 + (((s1 - s0) * x) >> 15)
For every table, at several positions and MIDI notes 41..100 (+ extremes), render 1 s at 44.1 kHz, window
(4-term Blackman-Harris), FFT, mask the wanted harmonics k * f0 (k <= 64 >> L, below Nyquist) and report the
strongest remaining component (alias of a harmonic, or an interpolation image) in dB below the fundamental
(dBf) and below the strongest wanted harmonic (dBc).

    .venv/bin/python tests/wavetable_spectrum.py              # baseline (the shipped format), table + build/ files
    .venv/bin/python tests/wavetable_spectrum.py --variants   # also the candidate fixes (slower)
Writes build/wavetable_spectrum.txt and build/wavetable_spectrum.png.
"""
import argparse
import struct
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'tools'))
import gen_wavetables as gw  # noqa: E402

FS = 44100
NS = 44100                                     # 1 s
INC_C9 = [815363807, 863847862, 915214929, 969636441, 1027294024, 1088380105,
          1153098554, 1221665363, 1294309365, 1371273005, 1452813141, 1539201906]
NOTES = list(range(41, 101))                   # F3 -2 oct .. G5 +2 oct: the keyboard's playable range
EXTREME = [24, 30, 36, 105, 110, 116, 122, 127]
POSITIONS = [0, 15 << 7, (10 << 7) + 64, 31 << 7]   # frames 0, 15, 10.5 (morph), 31
PASS_DB = 60.0                                 # target: every spur at least 60 dB below the fundamental
WARN_DB = 50.0
GUARD_BINS = 6                                 # BH4 main lobe is +/-4 bins at 1 Hz resolution
WIN = None


def inc_of(n):
    return INC_C9[n % 12] >> (10 - n // 12)


def level_of(inc, levels=6):
    for L in range(levels):
        if inc < (1 << (25 + L)):
            return L
    return levels - 1


# ---- tables: dict(len=[N_L], data=[array (32, N_L + guards)], harm=[H_L], pre=guards before sample 0) ----
def load_bin(path):
    b = path.read_bytes()
    assert b[:4] == b'WTB1'
    nt, nf, nl, flen = struct.unpack_from('<4H', b, 4)
    p = 12
    dirs = []
    for _ in range(nt):
        name, frames, _, off = struct.unpack_from('<6sBBI', b, p)
        dirs.append((name.rstrip(b'\0').decode(), frames, off))
        p += 12
    data = np.frombuffer(b, dtype='<i2', offset=p).astype(np.int64)
    out = []
    for name, frames, off in dirs:
        fr = data[off:off + frames * flen].reshape(frames, flen)
        out.append((name, dict(len=gw.LEN, harm=[gw.HMAX >> L for L in range(nl)], pre=0,
                               data=[fr[:, gw.OFF[L]:gw.OFF[L] + gw.LEN[L] + 1] for L in range(nl)])))
    return out


def make_variant(lengths, pre=0, post=1, taper=False):
    """Tables re-generated with other level lengths (harmonics per level unchanged), int16, per-table gain.
    taper: raised-cosine fade of the top octave of every level (harmonics H/2..H)."""
    out = []
    for name, frames in gw.spectra():
        lv = []
        for L, n in enumerate(lengths):
            H = gw.HMAX >> L
            k = np.arange(1, gw.HMAX + 1)
            w = np.clip((H - k) / (H / 2), 0, 1) if taper else np.ones(gw.HMAX)
            w = 0.5 - 0.5 * np.cos(np.pi * w) if taper else w
            if taper and H <= 2:
                w = np.where(k == 1, 1.0, 0.0)
            lv.append(np.array([gw.mip_frame(c * w, L, n) for c in frames]))
        peak = max(np.max(np.abs(x)) for x in lv)
        g = gw.PEAK * 32767 / peak
        data = []
        for x in lv:
            q = np.clip(np.round(x * g), -32767, 32767).astype(np.int64)
            data.append(np.concatenate([q[:, -pre:] if pre else q[:, :0], q, q[:, :post]], axis=1))
        out.append((name, dict(len=list(lengths), harm=[gw.HMAX >> L for L in range(len(lengths))], pre=pre,
                               data=data)))
    return out


# ---- the oscillator model ----
def render(tab, n, pos, interp='linear'):
    inc = inc_of(n)
    L = level_of(inc, len(tab['len']))
    N = tab['len'][L]
    sh = 32 - (N.bit_length() - 1)
    ph = (np.arange(NS, dtype=np.uint64) * np.uint64(inc)) & np.uint64(0xFFFFFFFF)
    idx = (ph >> np.uint64(sh)).astype(np.int64) + tab['pre']
    frac = ((ph >> np.uint64(sh - 16)) & np.uint64(0xFFFF)).astype(np.int64)
    f, x = pos >> 7, (pos & 127) << 8
    f1 = min(f + 1, gw.FRAMES - 1)
    outs = []
    for fr in (f, f1):
        t = tab['data'][L][fr]
        if interp == 'linear':
            a, b = t[idx], t[idx + 1]
            outs.append(a + (((b - a) * frac) >> 16))            # arithmetic shift, as C on int32
        elif interp == 'hermite':                                # 4-point Catmull-Rom (float model)
            ym, y0, y1, y2 = t[idx - 1], t[idx], t[idx + 1], t[idx + 2]
            u = frac / 65536.0
            c1 = 0.5 * (y1 - ym)
            c2 = ym - 2.5 * y0 + 2 * y1 - 0.5 * y2
            c3 = 0.5 * (y2 - ym) + 1.5 * (y0 - y1)
            outs.append(np.round(((c3 * u + c2) * u + c1) * u + y0).astype(np.int64))
        elif interp.startswith('sinc8q'):                        # integer polyphase: P phases, Q15 int16 taps
            P = int(interp[6:])
            co = sinc8_coefs(P)
            ph_i = (frac * P) >> 16
            acc = np.zeros(NS, dtype=np.int64)
            for j in range(-3, 5):
                acc += co[ph_i, j + 3] * t[idx + j]
            outs.append((acc + (1 << 14)) >> 15)
        else:                                                    # 8-tap Kaiser(6)-windowed sinc, taps -3..+4
            u = frac / 65536.0
            acc = np.zeros(NS)
            for j in range(-3, 5):
                d = u - j
                w = np.sinc(d) * np.i0(6 * np.sqrt(np.clip(1 - (d / 4) ** 2, 0, None))) / np.i0(6)
                acc += w * t[idx + j]
            outs.append(np.round(acc).astype(np.int64))
    s0, s1 = outs
    return s0 + (((s1 - s0) * x) >> 15), L, inc


_COEFS = {}


def sinc8_coefs(P):
    """Polyphase table [P][8] of Q15 taps (-3..+4) of the Kaiser(6) 8-tap sinc, each phase summing to 32768."""
    if P not in _COEFS:
        u = np.arange(P)[:, None] / P
        d = u - np.arange(-3, 5)[None, :]
        w = np.sinc(d) * np.i0(6 * np.sqrt(np.clip(1 - (d / 4) ** 2, 0, None))) / np.i0(6)
        w = w / w.sum(axis=1, keepdims=True)
        q = np.round(w * 32768).astype(np.int64)
        q[:, 3] += 32768 - q.sum(axis=1)                         # exact unity DC gain
        _COEFS[P] = q
    return _COEFS[P]


def analyse(y, inc, H):
    global WIN
    if WIN is None:
        k = np.arange(NS) / NS * 2 * np.pi
        WIN = 0.35875 - 0.48829 * np.cos(k) + 0.14128 * np.cos(2 * k) - 0.01168 * np.cos(3 * k)
    S = np.abs(np.fft.rfft(y * WIN)) / (np.sum(WIN) / 2)        # a full-scale sine reads 32767
    f0 = inc * FS / 2.0 ** 32
    mask = np.zeros(len(S), bool)
    mask[:20] = True                                             # DC
    hs = []
    for k in range(1, H + 1):
        fk = k * f0
        if fk >= FS / 2:
            break
        c = int(round(fk))
        lo, hi = max(0, c - GUARD_BINS), min(len(S), c + GUARD_BINS + 1)
        mask[lo:hi] = True
        hs.append(S[lo:hi].max())
    if not hs or max(hs) < 1.0:                                  # nothing wanted below Nyquist (or silent): no reference
        return None
    fund, strongest = hs[0], max(hs)
    rest = np.where(mask, 0.0, S)
    j = int(np.argmax(rest))
    spur = max(rest[j], 1e-3)
    dbf, dbc = 20 * np.log10(max(fund, 1e-3) / spur), 20 * np.log10(strongest / spur)
    score = dbf if fund * 10 >= strongest else dbc               # weak or absent fundamental: judge on dBc
    return dbf, dbc, j, score


def run(tables, notes, positions, interp='linear', progress=''):
    res = {}                                     # (table, note, pos) -> (dBf, dBc, spur_hz, L, score)
    for name, tab in tables:
        for n in notes:
            for p in positions:
                y, L, inc = render(tab, n, p, interp)
                r = analyse(y.astype(float), inc, tab['harm'][L])
                if r is not None:
                    res[(name, n, p)] = (r[0], r[1], r[2], L, r[3])
        if progress:
            print(f'  {progress} {name} done', file=sys.stderr)
    return res


def worst(res, name=None, notes=None, key=4):
    items = [(v[key], k, v) for k, v in res.items() if (name is None or k[0] == name) and (notes is None or k[1] in notes)]
    return min(items) if items else (float('nan'), ('-', 0, 0), (float('nan'),) * 3 + (0, float('nan')))


def mipname(n):
    return 'C C#D D#E F F#G G#A A#B '[2 * (n % 12):2 * (n % 12) + 2].strip() + str(n // 12 - 1)


def int32_hazard(tables):
    """Largest |b - a| * 65535 against int32 (the 16-bit fraction lerp in C)."""
    m = 0
    for _, tab in tables:
        for d in tab['data']:
            m = max(m, int(np.max(np.abs(np.diff(d, axis=1)))))
    return m, m * 65535 > 0x7FFFFFFF


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--variants', action='store_true', help='also measure the candidate fixes')
    a = ap.parse_args()
    binp = ROOT / 'build' / 'wavetables.bin'
    if not binp.exists():
        sys.exit('no build/wavetables.bin: run .venv/bin/python tools/gen_wavetables.py')
    base = load_bin(binp)
    allnotes = sorted(set(NOTES + EXTREME))
    lines = []

    def out(s=''):
        print(s)
        lines.append(s)

    res = run(base, allnotes, POSITIONS, progress='baseline')
    vres = []
    out(f'Wavetable mip scheme, integer model, 1 s at {FS} Hz, BH4 window; spur = strongest component that is not a '
        f'wanted harmonic (k <= 64>>L).')
    out(f'dBf = spur below the fundamental, dBc = below the strongest wanted harmonic. Score = dBf, or dBc (*) where')
    out(f'the fundamental is more than 20 dB under the strongest harmonic. PASS >= {PASS_DB:.0f}, warn >= {WARN_DB:.0f}, else FAIL.')
    out(f'Positions: {", ".join(f"{p / 128:.1f}" for p in POSITIONS)} (frames). Playable = MIDI 41..100.')
    out()
    out('Per table, worst score over positions and notes:')
    out(f'{"table":6s} | {"playable 41..100":^37s} | {"all notes " + str(EXTREME[0]) + ".." + str(EXTREME[-1]):^37s}')
    hdr = f'{"score":>6s} {"dBf":>6s} {"dBc":>5s} {"note":>4s} {"L":>1s} {"spur Hz":>7s} {"":4s}'
    out(f'{"":6s} | {hdr} | {hdr}')
    for name, _ in base:
        row = f'{name:6s} |'
        for ns in (NOTES, allnotes):
            sc, (_, n, p), (dbf, dbc, hz, L, _) = worst(res, name, ns)
            verdict = 'PASS' if sc >= PASS_DB else ('warn' if sc >= WARN_DB else 'FAIL')
            star = '*' if sc != dbf else ' '
            row += f' {sc:5.1f}{star} {dbf:6.1f} {dbc:5.1f} {mipname(n):>4s} {L:1d} {hz:7d} {verdict:4s} |'
        out(row)
    out()
    out('Worst score per MIDI note over all tables and positions (L = mip level):')
    for i in range(0, len(allnotes), 6):
        chunk = allnotes[i:i + 6]
        out('  ' + '   '.join(f'{n:3d} {mipname(n):>4s} L{worst(res, None, [n])[2][3]} {worst(res, None, [n])[0]:5.1f}'
                              for n in chunk))
    out()
    out('Worst per mip level (playable range):')
    for L in range(6):
        ns = [n for n in NOTES if level_of(inc_of(n)) == L]
        if ns:
            sc, k, v = worst(res, None, ns)
            out(f'  L{L} ({256 >> L:3d} samples, {64 >> L:2d} harmonics, MIDI {ns[0]}..{ns[-1]}): score {sc:5.1f}, '
                f'worst dBc {worst(res, None, ns, 1)[0]:5.1f}  (worst score: {k[0]} {mipname(k[1])} pos {k[2] / 128:.1f})')
    m, bad = int32_hazard(base)
    out()
    out(f'Largest adjacent-sample step in the shipped data: {m}; (b - a) * frac (16-bit) '
        f'{"OVERFLOWS int32" if bad else "fits int32"} (max {m * 65535}).')

    if a.variants:
        out()
        out('Candidate fixes: worst dBc (spur below the strongest wanted harmonic) over all tables and positions:')
        variants = [
            ('A linear (baseline)', base, 'linear', 1),
            ('B Catmull-Rom 4-pt', make_variant(gw.LEN, pre=1, post=2), 'hermite', 3),
            ('C 8-tap windowed sinc', make_variant(gw.LEN, pre=3, post=4), 'sinc8', 7),
            ('C2 8-tap int, 256 phases', make_variant(gw.LEN, pre=3, post=4), 'sinc8q256', 7),
            ('C3 8-tap int, 1024 phases', make_variant(gw.LEN, pre=3, post=4), 'sinc8q1024', 7),
            ('D linear, top-octave taper', make_variant(gw.LEN, taper=True), 'linear', 1),
            ('E Catmull-Rom + taper', make_variant(gw.LEN, pre=1, post=2, taper=True), 'hermite', 3),
            ('F linear, 2x longer levels', make_variant([512, 256, 128, 64, 32, 16]), 'linear', 1),
            ('G linear, 256 at every level', make_variant([256] * 6), 'linear', 1),
        ]
        out(f'  {"variant":30s} {"score":>5s} {"41..100":>7s} {"all":>6s}   ' + '  '.join(f'{"L" + str(L):>5s}' for L in range(6))
            + '   flash (12 x 32 frames)')
        for title, tabs, interp, guards in variants:
            r = res if tabs is base else run(tabs, allnotes, POSITIONS, interp, progress=title)
            vres.append((title, r))
            per = []
            for L in range(6):
                ns = [n for n in allnotes if level_of(inc_of(n)) == L]
                per.append(f'{worst(r, None, ns, 1)[0]:5.1f}')
            flash = sum(n + guards for n in tabs[0][1]['len']) * 2 * gw.FRAMES * len(tabs)
            out(f'  {title:30s} {worst(r, None, NOTES)[0]:5.1f} {worst(r, None, NOTES, 1)[0]:7.1f} {worst(r, None, allnotes, 1)[0]:6.1f}   '
                + '  '.join(per) + f'   {flash // 1024} KiB')
        out('  (score = worst over 41..100 as in the table above; the rest are worst dBc over all tables and positions;')
        out('   L columns over all notes at that level; flash includes the guard samples each interpolator needs.)')

    (ROOT / 'build').mkdir(exist_ok=True)
    (ROOT / 'build' / 'wavetable_spectrum.txt').write_text('\n'.join(lines) + '\n')
    try:
        import matplotlib
        matplotlib.use('Agg')
        import matplotlib.pyplot as plt
        fig, axs = plt.subplots(2 if vres else 1, 1, figsize=(11, 10 if vres else 5.5), squeeze=False)
        ax = axs[0][0]
        for i, (name, _) in enumerate(base):
            ax.plot(allnotes, [worst(res, name, [n])[0] for n in allnotes], label=name, lw=1, ls='-' if i < 10 else '--')
        ax.set_title('Shipped scheme (linear, 256..8): worst score per note over 4 positions')
        panels = [(ax, 'score (dB below fundamental, or dBc *)')]
        if vres:
            ax2 = axs[1][0]
            for title, r in vres:
                ax2.plot(allnotes, [worst(r, None, [n], 1)[0] for n in allnotes], label=title, lw=1)
            ax2.set_title('Candidate fixes: worst dBc per note over all tables and positions')
            panels.append((ax2, 'dB below strongest harmonic'))
        for a_, yl in panels:
            a_.axhline(PASS_DB, color='k', ls='--', lw=0.8)
            a_.axvspan(41, 100, color='0.92', zorder=0)
            a_.set_xlabel('MIDI note')
            a_.set_ylabel(yl)
            a_.set_ylim(-10, 110)
            a_.legend(ncol=4, fontsize=8)
            a_.grid(alpha=0.3)
        fig.tight_layout()
        fig.savefig(ROOT / 'build' / 'wavetable_spectrum.png', dpi=100)
    except ImportError:
        pass
    return 0


if __name__ == '__main__':
    sys.exit(main())
