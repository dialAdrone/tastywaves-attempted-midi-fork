# SPDX-License-Identifier: GPL-3.0-only
"""Factory wavetables for the wavetable engine (docs/WAVETABLE_DESIGN.md, task T15).

Synthesises 12 original tables x 32 frames from additive / spectral / waveshaping recipes (no third-party
table data), band-limits every frame into 6 mip levels and writes
    firmware/gen/wavetables.h   static const int16_t data + a directory (not included by firmware yet)
    build/wavetables.bin        the same data, little-endian, with a small header (for uploaders / tests)

Format (per frame, frame-major, all levels of one frame contiguous):
    level L = 0..5 holds N_L = 256 >> L samples + 1 guard sample (copy of sample 0), int16;
    level L keeps harmonics 1 .. (64 >> L) (DC and everything above are zero), made by IFFT to N_L points.
    frame = 257+129+65+33+17+9 = 510 int16 = 1020 bytes; table = 32 frames = 32,640 bytes.
Reader: idx = ph >> (24 + L), frac = (ph >> (8 + L)) & 0xFFFF, t[idx], t[idx + 1] (guard, no mask).
Level rule (no divide): the smallest L with (64 >> L) * f0 < fs/2  <=>  inc < 2^(25 + L).

Normalisation is per table (one gain for all frames and levels), peak = PEAK * 32767.

    .venv/bin/python tools/gen_wavetables.py            # writes both files, prints a size summary
Other scripts import this module (tests/wavetable_spectrum.py uses spectra() and mip_frame()).
"""
import struct
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent.parent
FRAMES = 32
LEVELS = 6
TOP = 256                      # level-0 samples per cycle
HMAX = 64                      # harmonics kept at level 0
PEAK = 0.9                     # per-table peak, fraction of int16 full scale
HIRES = 8192                   # time-domain recipes are drawn at this resolution, then FFT'd
LEN = [TOP >> L for L in range(LEVELS)]
OFF = [sum(n + 1 for n in LEN[:L]) for L in range(LEVELS)]   # level offsets inside a frame
FRAME_LEN = OFF[-1] + LEN[-1] + 1                             # 510

K = np.arange(1, HMAX + 1)     # harmonic numbers 1..64
TH = np.arange(HIRES) * (2 * np.pi / HIRES)                   # one cycle at HIRES


# ---- helpers: every recipe returns a complex spectrum c[1..64] for x(t) = sum Im(c_k e^{i k t}) ----
def from_sine_amps(a):
    """Sine-phase additive: x = sum a_k sin(k t)."""
    return np.asarray(a, dtype=complex)


def from_wave(x):
    """Harmonics 1..64 of one cycle drawn at HIRES points (content above 64 is discarded = band-limited)."""
    X = np.fft.rfft(np.asarray(x, dtype=float)) / (HIRES / 2)
    # rfft bin k = A e^{i phi} for A cos(k t + phi); in sine convention c_k with x = Im(c e^{ikt}) -> c = i * X
    return 1j * X[1:HMAX + 1]


def unit_rms(c):
    """Scale a spectrum to unit RMS (equal loudness across frames before the per-table gain)."""
    e = np.sqrt(np.sum(np.abs(c) ** 2) / 2)
    return c / e if e > 0 else c


SCHROEDER = np.exp(1j * np.pi * K * (K - 1) / HMAX)          # low-crest phases for dense harmonic bands


def ramp(f):
    return f / (FRAMES - 1)


def lerp(a, b, t):
    return a + (b - a) * t


# ---- the 12 recipes (frame index f = 0..31) ----
def t_sawsq(f):                # saw -> square: the even harmonics fade out
    t = ramp(f)
    return from_sine_amps(np.where(K % 2 == 1, 1.0, 1.0 - t) / K)


def t_pwm(f):                  # pulse width 50 % -> 4 %
    d = lerp(0.5, 0.04, ramp(f))
    return from_wave(np.where(TH < 2 * np.pi * d, 1.0, -1.0))


def t_harm(f):                 # a log-gaussian band of harmonics sweeping from 1 to 48, over a weak fundamental
    c = np.exp(lerp(np.log(1.0), np.log(48.0), ramp(f)))
    a = np.exp(-0.5 * (np.log(K / c) / 0.35) ** 2)
    a = unit_rms(a * SCHROEDER)
    a[0] += 0.35                                              # the fundamental stays audible
    return a


VOWELS = {                     # formant centre (Hz) and bandwidth (Hz): F1, F2, F3
    'a': ((730, 90), (1090, 110), (2440, 160)),
    'e': ((530, 70), (1840, 100), (2480, 160)),
    'i': ((270, 60), (2290, 100), (3010, 170)),
    'o': ((570, 80), (840, 90), (2410, 160)),
    'u': ((300, 60), (870, 90), (2240, 160)),
}
VOWEL_PATH = 'aeiou'
VOWEL_F0 = 110.0               # the table's formants sit where they belong for a 110 Hz voice


def t_vowel(f):                # formant morph a-e-i-o-u over a glottal-ish 1/k source
    p = ramp(f) * (len(VOWEL_PATH) - 1)
    i = min(int(p), len(VOWEL_PATH) - 2)
    t = p - i
    va, vb = VOWELS[VOWEL_PATH[i]], VOWELS[VOWEL_PATH[i + 1]]
    hz = K * VOWEL_F0
    env = np.zeros(HMAX)
    for (fa, ba), (fb, bb), g in zip(va, vb, (1.0, 0.6, 0.35)):
        fc, bw = lerp(fa, fb, t), max(lerp(ba, bb, t), 1.5 * VOWEL_F0)   # >= 1.5 harmonics wide: smooth morph
        env += g / (1.0 + ((hz - fc) / (bw / 2)) ** 2)        # Lorentzian resonance
    return unit_rms(from_sine_amps(env / K ** 0.7 + 0.02 / K))


def t_fm(f):                   # 2-op FM, ratio 1:1 then 1:2 mixed in, index 0 -> 7
    t = ramp(f)
    idx = 7.0 * t * t
    x = np.sin(TH + idx * np.sin(TH)) * (1 - 0.4 * t) + 0.4 * t * np.sin(TH + 0.6 * idx * np.sin(2 * TH))
    return from_wave(x)


def t_fold(f):                 # sine wavefolder, drive 1 -> 9, with a little offset for even harmonics
    g = lerp(1.0, 9.0, ramp(f) ** 1.3)
    return from_wave(np.sin(g * np.sin(TH) + 0.25 * ramp(f)))


DRAWBARS = np.array([1, 3, 2, 4, 6, 8, 10, 12, 16])          # 16' 5 1/3' 8' 4' 2 2/3' 2' 1 3/5' 1 1/3' 1'
REGISTRATIONS = [              # classic drawbar settings 0..8
    (8, 8, 8, 0, 0, 0, 0, 0, 0),
    (8, 0, 8, 0, 0, 0, 0, 0, 0),
    (8, 8, 8, 8, 0, 0, 0, 0, 0),
    (8, 6, 8, 8, 0, 0, 0, 0, 8),
    (6, 8, 8, 6, 0, 0, 0, 0, 0),
    (8, 8, 8, 8, 8, 8, 8, 8, 8),
    (0, 0, 8, 8, 8, 0, 0, 0, 0),
    (0, 0, 6, 8, 0, 4, 0, 6, 8),
]


def t_organ(f):                # drawbar registrations, morphed in order
    p = ramp(f) * (len(REGISTRATIONS) - 1)
    i = min(int(p), len(REGISTRATIONS) - 2)
    r = lerp(np.array(REGISTRATIONS[i], float), np.array(REGISTRATIONS[i + 1], float), p - i)
    a = np.zeros(HMAX)
    for h, lvl in zip(DRAWBARS, r):
        a[h - 1] += (10 ** ((lvl - 8) * 3 / 20)) if lvl > 0 else 0.0  # 3 dB per drawbar step
    return unit_rms(from_sine_amps(a))


BELL = [(1, 0.35), (2, 1.0), (4, 0.7), (5, 0.55), (6, 0.45), (8, 0.6), (11, 0.4), (13, 0.3),
        (17, 0.28), (21, 0.22), (27, 0.18), (33, 0.15), (41, 0.12), (52, 0.1)]


def t_bell(f):                 # church-bell partials (hum, prime, tierce, quint, nominal, ...) folded to harmonics;
    t = ramp(f)                # bright strike at frame 0, upper partials decay faster towards frame 31
    a = np.zeros(HMAX)
    ph = np.zeros(HMAX)
    for h, g in BELL:
        a[h - 1] = g * np.exp(-t * 0.18 * h)
        ph[h - 1] = 0.7 * h                                   # fixed spread phases: lower crest factor
    return a * np.exp(1j * ph)


def t_noise(f):                # noise-like: random phases, spectral centroid sweeping upwards, fundamental kept
    rng = np.random.default_rng(15)
    amp = rng.uniform(0.3, 1.0, HMAX)                         # same draws for every frame (smooth morph)
    ph = rng.uniform(0, 2 * np.pi, HMAX)
    tilt = lerp(-1.2, 1.0, ramp(f))                           # slope in log-amplitude per log-harmonic
    a = amp * (K / 8.0) ** tilt
    a = a / np.sqrt(np.sum(a ** 2))
    a[0] = max(a[0], 0.25)
    return a * np.exp(1j * ph)


def t_shape(f):                # sine -> triangle -> saw (spectral interpolation)
    sine = (K == 1).astype(float)
    tri = np.where(K % 2 == 1, (8 / np.pi ** 2) * (-1.0) ** ((K - 1) // 2) / K ** 2, 0.0)
    saw = (2 / np.pi) / K
    p = ramp(f) * 2
    return from_sine_amps(lerp(sine, tri, p) if p <= 1 else lerp(tri, saw, p - 1))


def t_reed(f):                 # clarinet-like: odd harmonics with a soft knee, brightness and evens grow
    t = ramp(f)
    knee = lerp(3.0, 18.0, t)
    odd = 1.0 / (1.0 + (K / knee) ** 3)
    a = np.where(K % 2 == 1, odd, lerp(0.02, 0.25, t) * odd) / K ** 0.5
    return from_sine_amps(a)


def t_stair(f):                # bit-crush stairs: sine quantised from 5 bits to 1 bit, then sample-rate decimation
    t = ramp(f)
    bits = lerp(5.0, 1.0, min(1.0, t * 1.6))
    q = 2 ** bits / 2
    x = np.sin(TH)
    if t > 0.5:                # second half: sample-and-hold, 64 -> 6 steps per cycle
        steps = int(round(np.exp(lerp(np.log(64.0), np.log(6.0), (t - 0.5) * 2))))
        n = np.arange(HIRES)
        x = np.sin(2 * np.pi * (np.floor(n * steps / HIRES) + 0.5) / steps)
    return from_wave(np.round(x * q) / q)


TABLES = [
    ('SAWSQ', t_sawsq), ('PWM', t_pwm), ('HARM', t_harm), ('VOWEL', t_vowel),
    ('FMIDX', t_fm), ('FOLD', t_fold), ('ORGAN', t_organ), ('BELL', t_bell),
    ('NOISE', t_noise), ('SHAPE', t_shape), ('REED', t_reed), ('STAIR', t_stair),
]


# ---- band-limiting and quantisation ----
def mip_frame(c, level, n=None):
    """Float samples of spectrum c (harmonics 1..64) at mip level `level`: keep 64 >> level harmonics,
    IFFT to n (default 256 >> level) points. No guard sample."""
    n = LEN[level] if n is None else n
    h = HMAX >> level
    X = np.zeros(n // 2 + 1, dtype=complex)
    X[1:h + 1] = -1j * c[:h] * (n / 2)                        # inverse of from_wave's convention
    return np.fft.irfft(X, n)


def spectra():
    """[(name, [32 complex spectra])] for all tables, before scaling."""
    return [(name, [np.asarray(fn(f), dtype=complex) for f in range(FRAMES)]) for name, fn in TABLES]


def table_gain(frames):
    peak = max(np.max(np.abs(mip_frame(c, L))) for c in frames for L in range(LEVELS))
    return PEAK * 32767.0 / peak


def quantise(frames, gain):
    """int16 array [FRAMES * FRAME_LEN] for one table."""
    out = np.zeros((FRAMES, FRAME_LEN), dtype=np.int16)
    for f, c in enumerate(frames):
        for L in range(LEVELS):
            s = np.clip(np.round(mip_frame(c, L) * gain), -32767, 32767).astype(np.int16)
            out[f, OFF[L]:OFF[L] + LEN[L]] = s
            out[f, OFF[L] + LEN[L]] = s[0]                    # guard
    return out.reshape(-1)


def build():
    """[(name, int16 data, gain)] for all 12 tables."""
    res = []
    for name, frames in spectra():
        g = table_gain(frames)
        res.append((name, quantise(frames, g), g))
    return res


# ---- writers ----
def write_header(tables, path):
    total = sum(len(d) for _, d, _ in tables)
    lines = [
        '/* generated by tools/gen_wavetables.py: 12 factory wavetables x 32 frames, synthesised (no third-party data).',
        ' * Do not edit; rerun the generator. Format: docs/WAVETABLE_DESIGN.md section 2.',
        ' * Per frame (frame-major): 6 mip levels, level L = 256 >> L samples + 1 guard (copy of sample 0),',
        ' * keeping harmonics 1 .. 64 >> L. Level L of frame f of table t starts at',
        ' *     WT_DATA[WT_DIR[t].offset + f * WT_FRAME_LEN + WT_LEVEL_OFF[L]].',
        ' * Level rule: the smallest L with inc < 2^(25 + L) (= (64 >> L) * f0 < fs / 2). */',
        '/* SPDX-License-Identifier: GPL-3.0-only */',
        '#pragma once',
        '#include <stdint.h>',
        '',
        f'#define WT_TABLES    {len(tables)}u',
        f'#define WT_FRAMES    {FRAMES}u',
        f'#define WT_LEVELS    {LEVELS}u',
        f'#define WT_TOP       {TOP}u          /* level-0 samples per cycle */',
        f'#define WT_FRAME_LEN {FRAME_LEN}u          /* int16 per frame, all levels and guards */',
        f'#define WT_DATA_LEN  {total}u     /* {total * 2} bytes */',
        '',
        'typedef struct {',
        '    char name[6];          /* up to 5 chars + NUL */',
        '    uint8_t frames;',
        '    uint32_t offset;       /* into WT_DATA, in int16 */',
        '} wt_dir_t;',
        '',
        f'static const uint16_t WT_LEVEL_OFF[{LEVELS}] = {{{", ".join(str(o) for o in OFF)}}};',
        '',
        f'static const int16_t WT_DATA[{total}] = {{',
    ]
    for ti, (name, d, _) in enumerate(tables):
        lines.append(f'    /* {ti}: {name} */')
        for i in range(0, len(d), 16):
            lines.append('    ' + ', '.join(str(int(v)) for v in d[i:i + 16]) + ',')
    lines.append('};')
    lines.append('')
    lines.append(f'static const wt_dir_t WT_DIR[{len(tables)}] = {{')
    off = 0
    for name, d, _ in tables:
        lines.append(f'    {{"{name}", {FRAMES}, {off}}},')
        off += len(d)
    lines.append('};')
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text('\n'.join(lines) + '\n')


def write_bin(tables, path):
    """'WTB1', u16 tables, frames, levels, frame_len; per table: name[6], u8 frames, u8 0, u32 offset; int16 LE data."""
    b = bytearray(b'WTB1')
    b += struct.pack('<4H', len(tables), FRAMES, LEVELS, FRAME_LEN)
    off = 0
    for name, d, _ in tables:
        b += struct.pack('<6sBBI', name.encode('ascii'), FRAMES, 0, off)
        off += len(d)
    for _, d, _ in tables:
        b += d.astype('<i2').tobytes()
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(bytes(b))
    return len(b)


def main():
    tables = build()
    for name, _, _ in tables:
        assert len(name) <= 5 and name.isascii()
    hdr = ROOT / 'firmware' / 'gen' / 'wavetables.h'
    write_header(tables, hdr)
    nbin = write_bin(tables, ROOT / 'build' / 'wavetables.bin')
    print(f'frame: {FRAME_LEN} int16 = {FRAME_LEN * 2} B (levels {LEN}, offsets {OFF})')
    total = 0
    for name, d, g in tables:
        pk = int(np.max(np.abs(d.astype(np.int32))))
        print(f'  {name:5s}  {FRAMES} frames  {len(d) * 2:6d} B  peak {pk:5d} ({pk / 32767:.2f} FS)  gain {g:9.1f}')
        total += len(d) * 2
    print(f'total table data: {total} B ({total / 1024:.1f} KiB) -> {hdr.relative_to(ROOT)}')
    print(f'build/wavetables.bin: {nbin} B')
    return 0


if __name__ == '__main__':
    sys.exit(main())
