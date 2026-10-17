# SPDX-License-Identifier: GPL-3.0-only
"""Writes the WAV / .wt inputs of tests/webtool_check.mjs into the directory given as argv[1] (synthetic, ours):
a 2048-frame 16-bit WAV with a Serum clm chunk, a 24-bit stereo one, a float32 file with 20 frames of 1024, a WaveEdit
style 64 x 256 file, a one-frame file, a Surge .wt in int16 and in float, and a file with extensible float format."""
import struct
import sys
from pathlib import Path

import numpy as np

out = Path(sys.argv[1])
rng = np.random.default_rng(7)


def frames(n, L, kind):
    t = np.arange(L) / L * 2 * np.pi
    f = []
    for i in range(n):
        u = i / max(1, n - 1)
        if kind == 'mix':
            w = sum(np.sin((k + 1) * t + k * u) / (k + 1) ** (1 + u) for k in range(1, 24)) + 0.3 * np.sin(t)
        else:
            w = np.sin(t) + u * np.sin(3 * t) + 0.2 * rng.standard_normal(L) * u
        f.append(w)
    return np.concatenate(f)


def wav(path, x, bits=16, ch=1, tag=1, clm=None, ext=False):
    x = np.clip(x / (np.max(np.abs(x)) * 1.05), -1, 1)
    if ch > 1:
        x = np.stack([x, x * 0.5] + [x] * (ch - 2), axis=1).reshape(-1)
    if tag == 3:
        data = x.astype('<f4').tobytes() if bits == 32 else x.astype('<f8').tobytes()
    elif bits == 16:
        data = np.round(x * 32767).astype('<i2').tobytes()
    elif bits == 24:
        v = np.round(x * 8388607).astype(np.int32) & 0xFFFFFF
        data = np.stack([v & 255, v >> 8 & 255, v >> 16 & 255], axis=1).astype(np.uint8).tobytes()
    else:
        raise ValueError(bits)
    fmt = struct.pack('<HHIIHH', 0xFFFE if ext else tag, ch, 44100, 44100 * ch * bits // 8, ch * bits // 8, bits)
    if ext:
        fmt += struct.pack('<HHI', 22, bits, 3) + struct.pack('<H', tag) + b'\x00\x00\x00\x00\x10\x00\x80\x00\x00\xaa\x00\x38\x9b\x71'
    chunks = b'fmt ' + struct.pack('<I', len(fmt)) + fmt
    if clm:
        c = clm.encode('latin-1')
        chunks += b'clm ' + struct.pack('<I', len(c)) + c + (b'\0' if len(c) & 1 else b'')
    chunks += b'data' + struct.pack('<I', len(data)) + data
    path.write_bytes(b'RIFF' + struct.pack('<I', 4 + len(chunks)) + b'WAVE' + chunks)


wav(out / 'serum16.wav', frames(256, 2048, 'mix'), 16, clm='<!>2048 10000000 wavetable (www.xferrecords.com)')
wav(out / 'stereo24.wav', frames(40, 512, 'mix'), 24, ch=2, clm='<!>512 00000000')
wav(out / 'float20.wav', frames(20, 1024, 'noise'), 32, tag=3)
wav(out / 'waveedit.wav', frames(64, 256, 'mix'), 16)
wav(out / 'one.wav', frames(1, 2048, 'mix'), 16)
wav(out / 'ext.wav', frames(12, 2048, 'mix'), 32, tag=3, ext=True)
x = frames(50, 1024, 'mix')
x = x / np.max(np.abs(x)) * 0.9
(out / 'surge16.wt').write_bytes(b'vawt' + struct.pack('<IHH', 1024, 50, 4 | 8) + np.round(x * 32767).astype('<i2').tobytes())
(out / 'surgef.wt').write_bytes(b'vawt' + struct.pack('<IHH', 1024, 50, 0) + x.astype('<f4').tobytes())
