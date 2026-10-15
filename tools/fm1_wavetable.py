# SPDX-License-Identifier: GPL-3.0-only
"""fm1_wavetable.py: turn wavetable WAV files (Serum, WaveEdit, Ableton, Surge exports) into a VAVEWave user table.

    build  NAME OUT.vwt INPUT.wav [--frame N] [--format auto|serum|waveedit|ableton|surge] [--gain-only]
    inspect INPUT.wav             what the file looks like and how it would be split (no output file)

The output is exactly what the firmware stores in one user slot (docs/UPLOAD-DESIGN.md sections 3 and 7): a 64-byte
header (magic VWT1, version, fmt 0, frames 32, levels 6, name[8], data bytes, data CRC32, header CRC32, 0xFF pad)
followed by 32 frames x 510 int16 (6 mip levels each) little endian = 32,640 bytes, made by the same band-limiter and
quantiser as the factory tables (tools/gen_wavetables.py). Sending it to the device (put / get / info / erase) comes
with the firmware receive path (T20); this tool has no device commands yet.

Input rules: mono or stereo (mixed to mono), PCM 8/16/24/32 or float 32/64 WAV, any sample rate (a frame is one
cycle, the rate does not matter). The frame length comes from the format: Serum writes it in a `clm ` chunk
("<!>2048 ..."), WaveEdit is 64 frames of 256, Ableton and Surge exports use 2048; without any of these the
largest of 2048/1024/512/256 that divides the length is ASSUMED and printed as such, and --frame overrides it. Frames beyond 32 are picked evenly, fewer are
interpolated. Only harmonics 1..64 are kept (a 256-sample cycle at 44.1 kHz); DC is removed.
"""
import argparse
import re
import struct
import sys
import zlib
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import gen_wavetables as gw  # noqa: E402  (the factory-table band-limiter and quantiser)

MAGIC = b'VWT1'
VERSION = 1
HEADER_LEN = 64
DATA_BYTES = gw.FRAMES * gw.FRAME_LEN * 2          # 32,640
FRAME_CHOICES = (2048, 1024, 512, 256)
FORMATS = ('auto', 'serum', 'waveedit', 'ableton', 'surge')


class WavError(Exception):
    pass


# ---- WAV reading (own reader: wave.* refuses float and extensible files) ----
def read_wav(path):
    """Returns (mono float array in -1..1, rate, info dict with 'clm' text or None, 'channels', 'bits')."""
    b = Path(path).read_bytes()
    if len(b) < 12 or b[:4] != b'RIFF' or b[8:12] != b'WAVE':
        raise WavError('not a RIFF/WAVE file')
    pos, fmt, data, clm = 12, None, None, None
    while pos + 8 <= len(b):
        cid, size = b[pos:pos + 4], struct.unpack_from('<I', b, pos + 4)[0]
        body = b[pos + 8:pos + 8 + size]
        if cid == b'fmt ':
            fmt = body
        elif cid == b'data':
            data = body
        elif cid == b'clm ':
            clm = body.decode('latin-1', 'replace')
        pos += 8 + size + (size & 1)
    if fmt is None or data is None:
        raise WavError('missing fmt or data chunk')
    tag, ch, rate, _, _, bits = struct.unpack_from('<HHIIHH', fmt, 0)
    if tag == 0xFFFE and len(fmt) >= 26:
        tag = struct.unpack_from('<H', fmt, 24)[0]      # extensible: the sub-format GUID starts with the tag
    if ch < 1 or ch > 8:
        raise WavError(f'{ch} channels')
    if tag == 1:
        if bits == 8:
            x = (np.frombuffer(data, np.uint8).astype(np.float64) - 128.0) / 128.0
        elif bits == 16:
            x = np.frombuffer(data[:len(data) // 2 * 2], '<i2').astype(np.float64) / 32768.0
        elif bits == 24:
            n = len(data) // 3
            r = np.frombuffer(data[:n * 3], np.uint8).reshape(n, 3).astype(np.int32)
            v = r[:, 0] | r[:, 1] << 8 | r[:, 2] << 16
            x = np.where(v & 0x800000, v - 0x1000000, v).astype(np.float64) / 8388608.0
        elif bits == 32:
            x = np.frombuffer(data[:len(data) // 4 * 4], '<i4').astype(np.float64) / 2147483648.0
        else:
            raise WavError(f'{bits}-bit PCM not supported')
    elif tag == 3:
        if bits == 32:
            x = np.frombuffer(data[:len(data) // 4 * 4], '<f4').astype(np.float64)
        elif bits == 64:
            x = np.frombuffer(data[:len(data) // 8 * 8], '<f8')
        else:
            raise WavError(f'{bits}-bit float not supported')
    else:
        raise WavError(f'format tag {tag} not supported (PCM or float only)')
    n = len(x) // ch * ch
    x = x[:n].reshape(-1, ch).mean(axis=1)
    x = np.nan_to_num(x, nan=0.0, posinf=0.0, neginf=0.0)
    return x, rate, {'clm': clm, 'channels': ch, 'bits': bits}


# ---- frame length ----
def detect_frame(x, info, fmt='auto'):
    """(frame length, why). The explicit format wins, then Serum's clm chunk, then a periodicity guess."""
    n = len(x)
    if fmt == 'waveedit':
        return 256, 'format waveedit'
    if fmt in ('ableton', 'surge'):
        return 2048, f'format {fmt}'
    if info.get('clm'):
        m = re.match(r'<!>(\d+)', info['clm'].strip())
        if m and 64 <= int(m.group(1)) <= 8192:
            return int(m.group(1)), 'clm chunk'
    if fmt == 'serum':
        return 2048, 'format serum (no clm chunk)'
    if n == 16384:
        return 256, 'ASSUMED: 16384 samples is the WaveEdit layout (64 x 256); use --frame to change'
    for L in FRAME_CHOICES:                     # statistics cannot tell cycles from pieces of cycles: take the usual
        if n % L == 0:                          # Serum / Ableton / Surge size, and say so
            return L, f'ASSUMED: no clm chunk, {n} samples divide by {L}; use --frame or --format to change'
    raise WavError(f'{n} samples fit no frame length of {FRAME_CHOICES}; give --frame')


# ---- frames -> spectra ----
def frame_spectrum(fr):
    """Harmonics 1..64 of one cycle of any length, in gen_wavetables' convention (x = sum Im(c_k e^{ikt}))."""
    n = len(fr)
    X = np.fft.rfft(fr) / (n / 2)
    c = np.zeros(gw.HMAX, dtype=complex)
    m = min(gw.HMAX, len(X) - 1)
    c[:m] = 1j * X[1:m + 1]
    return c


def pick_32(spectra):
    """32 spectra from any count: evenly picked when there are more, linearly interpolated when fewer."""
    n = len(spectra)
    if n == 1:
        return [spectra[0]] * gw.FRAMES
    out = []
    for i in range(gw.FRAMES):
        p = i * (n - 1) / (gw.FRAMES - 1)
        a = int(np.floor(p))
        b = min(a + 1, n - 1)
        t = p - a
        out.append(spectra[a] * (1.0 - t) + spectra[b] * t)
    return out


def convert(x, frame):
    n = len(x) // frame
    if n < 1:
        raise WavError('file shorter than one frame')
    frames = [frame_spectrum(x[i * frame:(i + 1) * frame]) for i in range(n)]
    sp = pick_32(frames)
    if all(np.max(np.abs(c)) < 1e-9 for c in sp):
        raise WavError('the file is silent (or has no energy in harmonics 1..64)')
    gain = gw.table_gain(sp)
    return gw.quantise(sp, gain), n, gain


# ---- the slot image ----
def clean_name(name):
    s = re.sub(r'[^A-Z0-9 ._-]', '', name.upper())[:8].strip()
    return s or 'USER'


def slot_image(name, data):
    """64-byte header + data; the exact bytes of one user slot (header sector content, then the data sectors)."""
    raw = data.astype('<i2').tobytes()
    assert len(raw) == DATA_BYTES
    nm = clean_name(name).encode('ascii').ljust(8, b'\0')
    h = struct.pack('<4sHBBBB8sII', MAGIC, VERSION, 0, gw.FRAMES, gw.LEVELS, 0, nm, len(raw), zlib.crc32(raw))
    h += struct.pack('<I', zlib.crc32(h))
    return h.ljust(HEADER_LEN, b'\xff') + raw


def parse_image(img):
    """Inverse of slot_image for tests and `inspect`: dict or raises ValueError."""
    if len(img) != HEADER_LEN + DATA_BYTES:
        raise ValueError('size')
    magic, ver, fmt, frames, levels, _, nm, nbytes, dcrc = struct.unpack_from('<4sHBBBB8sII', img, 0)
    hcrc = struct.unpack_from('<I', img, 26)[0]
    if magic != MAGIC or ver != VERSION or zlib.crc32(img[:26]) != hcrc:
        raise ValueError('header')
    if frames != gw.FRAMES or levels != gw.LEVELS or nbytes != DATA_BYTES or zlib.crc32(img[HEADER_LEN:]) != dcrc:
        raise ValueError('data')
    return {'name': nm.rstrip(b'\0').decode(), 'frames': frames, 'levels': levels, 'crc': dcrc,
            'data': np.frombuffer(img[HEADER_LEN:], '<i2')}


# ---- commands ----
def describe(path, fmt, frame):
    x, rate, info = read_wav(path)
    if frame:
        L, why = frame, '--frame'
    else:
        L, why = detect_frame(x, info, fmt)
    n = len(x) // L
    return x, rate, info, L, why, n


def cmd_inspect(a):
    x, rate, info, L, why, n = describe(a.input, a.format, a.frame)
    print(f'{a.input}: {len(x)} samples, {info["channels"]} ch, {info["bits"]}-bit, {rate} Hz')
    print(f'frame length {L} ({why}); {n} frame(s) of the file -> 32')
    if len(x) % L:
        print(f'note: {len(x) % L} trailing samples ignored')
    if info.get('clm'):
        print(f'clm chunk: {info["clm"].strip()!r}')
    peak = float(np.max(np.abs(x))) if len(x) else 0.0
    print(f'peak {peak:.3f}; harmonics kept 1..{min(gw.HMAX, L // 2)} of {L // 2}')
    if L > 4096 or n > 256:
        print('warning: unusually large; is this a cycle table or a recording?')
    return 0


def cmd_build(a):
    x, rate, info, L, why, n = describe(a.input, a.format, a.frame)
    data, nsrc, gain = convert(x, L)
    img = slot_image(a.name, data)
    Path(a.out).write_bytes(img)
    print(f'{a.input}: frame {L} ({why}), {nsrc} frame(s) -> 32, gain {gain:.3f}')
    print(f'{a.out}: {len(img)} bytes, name {clean_name(a.name)!r}, data crc32 {zlib.crc32(img[HEADER_LEN:]):08x}')
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    sub = ap.add_subparsers(dest='cmd', required=True)
    for name in ('inspect', 'build'):
        p = sub.add_parser(name)
        if name == 'build':
            p.add_argument('name')
            p.add_argument('out')
        p.add_argument('input')
        p.add_argument('--frame', type=int, help='samples per frame (overrides detection)')
        p.add_argument('--format', choices=FORMATS, default='auto')
    a = ap.parse_args(argv)
    try:
        return {'inspect': cmd_inspect, 'build': cmd_build}[a.cmd](a)
    except (WavError, OSError) as e:
        print(f'error: {e}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
