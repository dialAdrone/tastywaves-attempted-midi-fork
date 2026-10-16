# SPDX-License-Identifier: GPL-3.0-only
"""Checks tools/fm1_wavetable.py on synthetic files in the layouts of Serum, WaveEdit, Ableton and Surge exports.
Run: .venv/bin/python tests/wavetable_tool_check.py   (exit 0 = all pass). Writes only to a temp directory."""
import struct
import sys
import tempfile
import zlib
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'tools'))
import fm1_wavetable as wt  # noqa: E402
import gen_wavetables as gw  # noqa: E402

fails = 0


def check(ok, what):
    global fails
    print(('ok    ' if ok else 'FAIL  ') + what)
    fails += 0 if ok else 1


def chunk(cid, body):
    return cid + struct.pack('<I', len(body)) + body + (b'\0' if len(body) & 1 else b'')


def write_wav(path, x, bits=16, float_=False, ch=1, rate=44100, clm=None, extensible=False):
    x = np.asarray(x, dtype=float)
    if ch == 2:
        x = np.repeat(x[:, None], 2, axis=1).reshape(-1)
    if float_:
        raw = x.astype('<f4' if bits == 32 else '<f8').tobytes()
        tag = 3
    elif bits == 8:
        raw = np.clip(np.round(x * 127) + 128, 0, 255).astype(np.uint8).tobytes()
        tag = 1
    elif bits == 16:
        raw = np.clip(np.round(x * 32767), -32768, 32767).astype('<i2').tobytes()
        tag = 1
    elif bits == 24:
        v = np.clip(np.round(x * 8388607), -8388608, 8388607).astype(np.int32) & 0xFFFFFF
        raw = np.stack([v & 255, v >> 8 & 255, v >> 16 & 255], axis=1).astype(np.uint8).tobytes()
        tag = 1
    else:
        raise ValueError
    if extensible:
        fmt = struct.pack('<HHIIHHHHIH14s', 0xFFFE, ch, rate, rate * ch * bits // 8, ch * bits // 8, bits, 22, bits, 0,
                          tag, b'\x00\x00\x00\x00\x10\x00\x80\x00\x00\xaa\x00\x38\x9b\x71')
    else:
        fmt = struct.pack('<HHIIHH', tag, ch, rate, rate * ch * bits // 8, ch * bits // 8, bits)
    body = b'WAVE' + chunk(b'fmt ', fmt)
    if clm:
        body += chunk(b'clm ', clm.encode('ascii'))
    body += chunk(b'data', raw)
    Path(path).write_bytes(b'RIFF' + struct.pack('<I', len(body)) + body)


def cycle(n, harm):
    t = np.arange(n) * 2 * np.pi / n
    return sum(a * np.sin(k * t) for k, a in harm)


def morph(n, frames):
    """sine -> saw over `frames` single cycles of n samples; returns the concatenated table."""
    out = []
    for f in range(frames):
        u = f / max(frames - 1, 1)
        h = [(1, 1.0)] + [(k, u / k) for k in range(2, 40)]
        c = cycle(n, h)
        out.append(c / np.max(np.abs(c)) * 0.8)
    return np.concatenate(out)


def build(td, name, wav, **kw):
    out = Path(td) / (name + '.vwt')
    wavs = wav if isinstance(wav, (list, tuple)) else [wav]
    argv = ['build', name, str(out)] + [str(w) for w in wavs]
    for k, v in kw.items():
        argv += [f'--{k}', str(v)]
    rc = wt.main(argv)
    return rc, out


def level0(img_data, f):
    base = f * gw.FRAME_LEN
    return img_data[base:base + 256].astype(float)


with tempfile.TemporaryDirectory() as td:
    td = Path(td)
    # Serum-like: 2048 x 16, float32, clm chunk, 32-bit; the chunk decides the frame length
    write_wav(td / 'serum.wav', morph(2048, 16), bits=32, float_=True, clm='<!>2048 10000000 wavetable (www.xferrecords.com)')
    x, _, info = wt.read_wav(td / 'serum.wav')
    check(wt.detect_frame(x, info)[0] == 2048 and 'clm' in wt.detect_frame(x, info)[1], 'serum: frame 2048 from the clm chunk')
    rc, out = build(td, 'SERUM', td / 'serum.wav')
    im = wt.parse_image(out.read_bytes())
    check(rc == 0 and im['name'] == 'SERUM' and len(out.read_bytes()) == 64 + 32640, 'serum: image parses, 64 + 32,640 bytes')
    d0, d31 = level0(im['data'], 0), level0(im['data'], 31)
    ref0 = cycle(256, [(1, 1.0)])
    ref31 = cycle(256, [(1, 1.0)] + [(k, 1.0 / k) for k in range(2, 65)])
    check(np.corrcoef(d0, ref0)[0, 1] > 0.9999, 'serum: frame 0 is a sine')
    check(np.corrcoef(d31, ref31)[0, 1] > 0.995, 'serum: frame 31 is the band-limited saw (harmonics <= 64)')
    check(np.max(np.abs(im['data'])) <= 0.9 * 32767 + 1, 'serum: peak at 0.9 full scale')
    # mip levels: level 1 of the saw has no content above harmonic 32
    lv1 = im['data'][31 * gw.FRAME_LEN + gw.OFF[1]:31 * gw.FRAME_LEN + gw.OFF[1] + 128].astype(float)
    sp = np.abs(np.fft.rfft(lv1))
    check(sp[33:].max() < 2e-4 * sp.max(), f'serum: mip level 1 keeps harmonics <= 32 only (above: {sp[33:].max():.1f}, fundamental {sp.max():.0f})')

    # WaveEdit-like: 64 x 256, 16-bit, no clm: 16384 samples = the WaveEdit layout
    write_wav(td / 'we.wav', morph(256, 64), bits=16)
    x, _, info = wt.read_wav(td / 'we.wav')
    check(wt.detect_frame(x, info)[0] == 256, 'waveedit: 16384 samples -> frame 256')
    rc, out = build(td, 'WE', td / 'we.wav')
    im = wt.parse_image(out.read_bytes())
    check(rc == 0 and im['frames'] == 32, 'waveedit: builds (64 frames picked down to 32)')
    # the 64 -> 32 pick takes source frames 0, 2.03, ...: frame 31 must be the last source frame (full saw)
    check(np.corrcoef(level0(im['data'], 31), ref31)[0, 1] > 0.995, 'waveedit: last frame kept')

    # Ableton-like: 2048 x 10, 24-bit stereo, extensible, no clm: closed cycles at 2048 (auto) and --format ableton
    write_wav(td / 'ab.wav', morph(2048, 10), bits=24, ch=2, extensible=True)
    x, _, info = wt.read_wav(td / 'ab.wav')
    check(info['channels'] == 2 and len(x) == 20480, 'ableton: stereo extensible 24-bit read, mixed to mono')
    check(wt.detect_frame(x, info)[0] == 2048, 'ableton: no clm chunk, 20480 samples -> 2048 (assumed, printed as ASSUMED)')
    rc, out = build(td, 'AB', td / 'ab.wav', format='ableton')
    im = wt.parse_image(out.read_bytes())
    check(rc == 0 and np.corrcoef(level0(im['data'], 31), ref31)[0, 1] > 0.995, 'ableton: 10 frames interpolated to 32')
    # interpolation: frame 16 sits between source frames 4 and 5 (4.84 .. ): the amplitude of harmonic 2 grows with f
    h2 = [abs(np.fft.rfft(level0(im['data'], f))[2]) for f in range(32)]
    check(all(h2[i] <= h2[i + 1] + 1e-6 for i in range(31)), 'ableton: harmonic 2 grows monotonically across the table')

    # Surge-like export: 2048 x 8 samples as a WAV with --format surge
    write_wav(td / 'sg.wav', morph(2048, 8), bits=16)
    rc, out = build(td, 'SURGE', td / 'sg.wav', format='surge')
    check(rc == 0 and wt.parse_image(out.read_bytes())['frames'] == 32, 'surge: 2048-frame WAV via --format surge')

    # 8-bit and float64 read
    write_wav(td / 'b8.wav', morph(256, 8), bits=8)
    write_wav(td / 'f64.wav', morph(256, 8), bits=64, float_=True)
    check(wt.main(['inspect', str(td / 'b8.wav'), '--frame', '256']) == 0 and
          wt.main(['inspect', str(td / 'f64.wav'), '--frame', '256']) == 0, 'inspect: 8-bit and float64 files')

    # a single frame is held for all 32
    write_wav(td / 'one.wav', cycle(2048, [(1, 0.5), (3, 0.3)]), bits=16)
    rc, out = build(td, 'ONE', td / 'one.wav', frame=2048)
    im = wt.parse_image(out.read_bytes())
    check(rc == 0 and np.array_equal(level0(im['data'], 0), level0(im['data'], 31)), 'one frame: all 32 frames equal')

    # Surge .wt: float32, int16 (legacy 2^15 range and full 2^16 range), the 'is sample' flag is refused
    def write_wt(path, x, size, count, flags):
        x = np.asarray(x, dtype=float)
        if flags & 4:
            scale = 32767.0 if flags & 8 else 16383.0
            raw = np.round(x * scale).astype('<i2').tobytes()
        else:
            raw = x.astype('<f4').tobytes()
        Path(path).write_bytes(b'vawt' + struct.pack('<IHH', size, count, flags) + raw)
    m = morph(1024, 16)
    for nm, fl in (('wtf', 0), ('wt16', 4), ('wt16f', 4 | 8)):
        write_wt(td / f'{nm}.wt', m, 1024, 16, fl)
        rc, out = build(td, nm.upper(), td / f'{nm}.wt')
        im = wt.parse_image(out.read_bytes())
        check(rc == 0 and np.corrcoef(level0(im['data'], 31), ref31)[0, 1] > 0.995, f'surge .wt flags {fl}: 16 x 1024 read, last frame is the saw')
        check(np.max(np.abs(im['data'])) <= 0.9 * 32767 + 1, f'surge .wt flags {fl}: normalised')
    write_wt(td / 'smp.wt', m, 1024, 16, 1)
    rc, _ = build(td, 'X', td / 'smp.wt')
    check(rc == 1, 'surge .wt: "is sample" flag is refused')
    (td / 'short.wt').write_bytes(b'vawt' + struct.pack('<IHH', 2048, 64, 4) + b'\0' * 100)
    rc, _ = build(td, 'X', td / 'short.wt')
    check(rc == 1, 'surge .wt: truncated data is refused')

    # --pick on a 64-frame set: first / last / odd / slice / list, and the default takes whole frames (no blends)
    rc, out = build(td, 'FIRST', td / 'we.wav', pick='first')
    f_first = wt.parse_image(out.read_bytes())['data']
    rc2, out2 = build(td, 'LAST', td / 'we.wav', pick='last')
    f_last = wt.parse_image(out2.read_bytes())['data']
    x_we, _, _ = wt.read_wav(td / 'we.wav')
    src = [wt.frame_spectrum(x_we[i * 256:(i + 1) * 256]) for i in range(64)]
    ref_first = np.array([np.fft.irfft(np.r_[0, -1j * c * 128, np.zeros(1)][:129], 256) for c in src[:1]])[0]
    check(rc == 0 and rc2 == 0 and not np.array_equal(f_first, f_last), 'pick: first and last differ')
    check(np.corrcoef(level0(f_first, 0), ref_first)[0, 1] > 0.9999, 'pick first: table frame 0 is source frame 0')
    check(np.corrcoef(level0(f_first, 31), np.fft.irfft(np.r_[0, -1j * src[31] * 128, np.zeros(1)][:129], 256))[0, 1] > 0.9999,
          'pick first: table frame 31 is source frame 31 (no blending)')
    rc, out = build(td, 'ODD', td / 'we.wav', pick='1,2,3')
    im = wt.parse_image(out.read_bytes())
    check(rc == 0 and im['frames'] == 32, 'pick: a comma list of 3 frames interpolates to 32')
    for bad in ('99', 'x', '5:5'):
        rc, _ = build(td, 'X', td / 'we.wav', pick=bad)
        check(rc == 1, f'pick: {bad!r} is refused')

    # several files / a folder: joined in natural name order (a2 before a10), one frame per single-cycle file
    sc = td / 'cycles'
    sc.mkdir()
    for i, k in enumerate((10, 2, 1, 3)):                       # names a10, a2, a1, a3 -> natural order a1 a2 a3 a10
        c = cycle(2048, [(1, 1.0)] + [(h, 0.5) for h in range(2, 2 + k)])
        write_wav(sc / f'a{k}.wav', c / np.max(np.abs(c)) * 0.8, bits=16)
    rc, out = build(td, 'CYC', sc, frame=2048)
    im = wt.parse_image(out.read_bytes())
    # four frames in order a1 a2 a3 a10 -> harmonic 3 amplitude grows with the number of added harmonics: frame 0
    # (a1: harmonics 1,2) has none, frame 31 (a10) the most
    h3 = [abs(np.fft.rfft(level0(im['data'], f))[3]) for f in range(32)]
    check(rc == 0 and h3[0] < 1e-3 * h3[31] and h3[31] > 0, 'folder: files joined in natural order a1 a2 a3 a10')
    rc, out = build(td, 'TWO', [td / 'serum.wav', td / 'we.wav'])
    check(rc == 0, 'two files with different frame lengths join (2048 and 256)')

    # failures are errors, not crashes
    (td / 'junk.wav').write_bytes(b'not a wav at all')
    write_wav(td / 'odd.wav', np.zeros(1000) + 0.1 * np.sin(np.arange(1000)), bits=16)
    write_wav(td / 'sil.wav', np.zeros(2048), bits=16)
    for nm, kw in (('junk', {}), ('odd', {}), ('sil', {'frame': 2048})):
        rc, _ = build(td, 'X', td / (nm + '.wav'), **kw)
        check(rc == 1, f'error path: {nm}.wav is refused with exit 1')

    # image integrity: any single flipped byte is caught by parse_image
    img = bytearray(Path(td / 'SERUM.vwt').read_bytes())
    bad = 0
    for pos in (0, 5, 10, 20, 27, 64, 5000, len(img) - 1):
        t = bytearray(img)
        t[pos] ^= 0x01
        try:
            wt.parse_image(bytes(t))
            bad += 1
        except ValueError:
            pass
    check(bad == 0, 'image: a flipped bit in header or data is detected')
    check(wt.clean_name('wARM pad!!') == 'WARM PAD' and wt.clean_name('') == 'USER' and len(wt.clean_name('x' * 20)) == 8,
          'names: upper case, 8 chars, safe set')

print(f'{fails} FAIL')
sys.exit(1 if fails else 0)
