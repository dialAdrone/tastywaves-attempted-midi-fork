# SPDX-License-Identifier: GPL-3.0-only
"""WAV checks for the v1 engine (T16), in the style of tests/wavetable_spectrum.py.

Reads a host-mode WAV (build/host/<name>.wav, tests/host.c) and the script that made it. Check lines in the script
are comments, so tests/host.c ignores them:

    #! check NAME T0 T1 KIND [note=N] [bend=V range=R] [semi=S] [min=DB] [max=DB]

T0..T1 in ms (the analysed window, Blackman-Harris 4-term). KIND:
    alias   osc 1 alone, static: worst non-harmonic component below the strongest harmonic (dBc). The harmonics
            k * f0 (f0 from the exact integer phase step, as the firmware) are masked +-6 bins, as in
            wavetable_spectrum.py. min= the pass mark (default 50), warn below min + 10.
    pitch   fundamental vs the expected f0 (note, bend, semi): error in cents, |err| <= 3 passes
    level   peak (dBFS) and hard clips (3+ samples in a row at full scale): fails on more clips than clips= (default
            0), or a peak above max=
    silent  peak below max= (default -80 dBFS)
Every window also reports peak, RMS and clip count. Exit status 1 if any check fails.

    .venv/bin/python -I tests/engine_check.py build/host/engine_alias.wav tests/scripts/engine_alias.txt
"""
import math
import sys
import wave
from pathlib import Path

import numpy as np

FS = 44100
INC_C9 = [815363807, 863847862, 915214929, 969636441, 1027294024, 1088380105,
          1153098554, 1221665363, 1294309365, 1371273005, 1452813141, 1539201906]
GUARD = 6


def fine16(i):
    return round((2.0 ** (i / 3072.0) - 1.0) * 65536)


def pitch_inc(p):                                # as synth.h pitch_inc (1/256 semitone)
    p = max(0, min(131 * 256 + 255, p))
    n = p >> 8
    base = INC_C9[n % 12] >> (10 - n // 12)
    return base + ((base * fine16(p & 255)) >> 16)


def f0_of(note, bend=0, rng=2, semi=0):
    return pitch_inc(note * 256 + ((bend * rng) >> 5) + semi * 256) * FS / 2 ** 32


def bh4(n):
    a = [0.35875, 0.48829, 0.14128, 0.01168]
    k = np.arange(n) / n * 2 * np.pi
    return a[0] - a[1] * np.cos(k) + a[2] * np.cos(2 * k) - a[3] * np.cos(3 * k)


def read_wav(path):
    with wave.open(str(path)) as w:
        assert w.getframerate() == FS and w.getsampwidth() == 2
        x = np.frombuffer(w.readframes(w.getnframes()), dtype='<i2').reshape(-1, w.getnchannels())
    return x[:, 0].astype(np.float64)


def db(v):
    return 20 * math.log10(max(v, 1e-12))


def spectrum(seg):
    n = len(seg)
    X = np.abs(np.fft.rfft(seg * bh4(n)))
    return X, FS / n


def alias_dbc(seg, f0):
    X, res = spectrum(seg)
    mask = np.zeros(len(X), bool)
    mask[:int(25 / res) + 1] = True               # DC and below 25 Hz
    k = 1
    href = 0.0
    while k * f0 < FS / 2:
        c = int(round(k * f0 / res))
        lo, hi = max(0, c - GUARD), min(len(X), c + GUARD + 1)
        mask[lo:hi] = True
        href = max(href, X[lo:hi].max())
        k += 1
    rest = np.where(mask, 0.0, X)
    i = int(rest.argmax())
    return db(href / max(rest[i], 1e-12)), i * res


def measured_f0(seg, f0):
    X, res = spectrum(seg)
    lo, hi = int(f0 * 0.94 / res), int(f0 * 1.06 / res) + 1
    i = lo + int(X[lo:hi].argmax())
    a, b, c = (np.log(X[j] + 1e-12) for j in (i - 1, i, i + 1))
    d = 0.5 * (a - c) / (a - 2 * b + c) if (a - 2 * b + c) != 0 else 0.0
    return (i + d) * res


def hard_clips(seg):
    full = np.abs(seg) >= 32767
    n, run = 0, 0
    for f in full:
        run = run + 1 if f else 0
        if run == 3:
            n += 1
    return n


def parse(script):
    checks = []
    for ln in Path(script).read_text().splitlines():
        if not ln.startswith('#! check'):
            continue
        t = ln.split()[2:]
        name, t0, t1, kind = t[0], int(t[1]), int(t[2]), t[3]
        opt = dict(kv.split('=') for kv in t[4:])
        checks.append((name, t0, t1, kind, opt))
    return checks


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    wav, script = Path(sys.argv[1]), Path(sys.argv[2])
    x = read_wav(wav)
    rows, fails, warns = [], 0, 0
    for name, t0, t1, kind, o in parse(script):
        seg = x[int(t0 * FS / 1000):int(t1 * FS / 1000)]
        if len(seg) < 1024:
            rows.append(f'{name:<16} {kind:<6} window outside the WAV  FAIL')
            fails += 1
            continue
        peak = np.abs(seg).max()
        rms = math.sqrt(float(np.mean(seg ** 2)))
        clips = hard_clips(seg)
        info = f'peak {db(peak / 32767):6.1f} dBFS  rms {db(rms / 32767):6.1f}  clips {clips}'
        verdict, extra = 'ok', ''
        note = int(o.get('note', 0))
        f0 = f0_of(note, int(o.get('bend', 0)), int(o.get('range', 2)), int(o.get('semi', 0))) if note else 0
        if kind == 'alias':
            dbc, at = alias_dbc(seg, f0)
            mn = float(o.get('min', 50))
            extra = f'note {note:3d} f0 {f0:8.1f} Hz  worst spur {dbc:5.1f} dBc at {at:7.0f} Hz (pass >= {mn:g})'
            verdict = 'FAIL' if dbc < mn else 'warn' if dbc < mn + 10 else 'ok'
        elif kind == 'pitch':
            fm = measured_f0(seg, f0)
            cents = 1200 * math.log2(fm / f0)
            extra = f'note {note:3d} expect {f0:8.2f} Hz  got {fm:8.2f} Hz  {cents:+5.2f} cents'
            verdict = 'FAIL' if abs(cents) > 3 else 'ok'
        elif kind == 'level':
            mx, ok_clips = float(o.get('max', 0)), int(o.get('clips', 0))
            extra = f'(max {mx:g} dBFS, hard clips <= {ok_clips})'
            verdict = 'FAIL' if clips > ok_clips or db(peak / 32767) > mx else 'ok'
        elif kind == 'silent':
            mx = float(o.get('max', -80))
            extra = f'(silent below {mx:g} dBFS)'
            verdict = 'FAIL' if db(peak / 32767) > mx else 'ok'
        fails += verdict == 'FAIL'
        warns += verdict == 'warn'
        rows.append(f'{name:<16} {kind:<6} {t0:6d}-{t1:<6d} {info}  {extra}  {verdict}')
    out = [f'{wav} ({len(x) / FS:.1f} s) checked against {script}'] + rows + \
          [f'{len(rows)} checks: {fails} FAIL, {warns} warn']
    text = '\n'.join(out)
    print(text)
    rep = wav.with_suffix('.check.txt')
    rep.write_text(text + '\n')
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
