# SPDX-License-Identifier: GPL-3.0-only
"""fm1_wavetable.py: turn wavetable WAV files (Serum, WaveEdit, Ableton, Surge exports) into a VAVEWave user table.

    build  NAME OUT.vwt INPUT...  (WAV or Surge .wt) [--frame N] [--format auto|serum|waveedit|ableton|surge] [--pick WHICH]
    inspect INPUT...              what each file looks like and how it would be split (no output file)
    info [--port NAME]            the connected FM-1: firmware, and which of the 8 user slots hold a table
    get SLOT OUT.vwt [--port NAME]  copy a slot off the FM-1 into a file (backup; checked against its CRC)

INPUT is a WAV or Surge .wt file or a folder of them (natural name order, 'a2' before 'a10'). Several inputs are
joined in order: a file of single cycles from the Serum "Frames" collection becomes one frame, a wavetable file
several. --pick chooses source frames before the table is made: even (default), first, last, odd, A:B (a slice,
0-based, end excluded) or a comma list like 0,4,8,12.

The output is exactly what the firmware stores in one user slot (docs/UPLOAD-DESIGN.md sections 3 and 7): a 64-byte
header (magic VWT1, version, fmt 0, frames 32, levels 6, name[8], data bytes, data CRC32, header CRC32, 0xFF pad)
followed by 32 frames x 510 int16 (6 mip levels each) little endian = 32,640 bytes, made by the same band-limiter and
quantiser as the factory tables (tools/gen_wavetables.py). `info` and `get` only read (firmware stage 0, VW link, see
docs/UPLOAD-DESIGN.md section 4); `put` and `erase` come with the write stages of the firmware.

Input rules: mono or stereo (mixed to mono), PCM 8/16/24/32 or float 32/64 WAV, any sample rate (a frame is one
cycle, the rate does not matter). The frame length comes from the format: Serum writes it in a `clm ` chunk
("<!>2048 ..."), WaveEdit is 64 frames of 256, Ableton and Surge WAV exports use 2048, and a Surge .wt file carries its wave_size; without any of these the
largest of 2048/1024/512/256 that divides the length is ASSUMED and printed as such, and --frame overrides it. Frames beyond 32 are picked evenly, fewer are
interpolated. Only harmonics 1..64 are kept (a 256-sample cycle at 44.1 kHz); DC is removed.
"""
import argparse
import re
import struct
import sys
import time
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
def read_wt(b):
    """Surge .wt ('vawt', docs: Surge XT's "WT fileformat.txt"): u32 wave_size, u16 wave_count, u16 flags (1 sample,
    2 loop, 4 int16 data, 8 int16 full range), then wave_count x wave_size values. info['frame'] is wave_size."""
    if len(b) < 12:
        raise WavError('truncated .wt header')
    size, count, flags = struct.unpack_from('<IHH', b, 4)
    if not (2 <= size <= 4096) or count < 1 or count > 512:
        raise WavError(f'.wt wave_size {size} / wave_count {count} out of range')
    if flags & 1:
        raise WavError('.wt is flagged "is sample" (a recording, not single cycles)')
    n = size * count
    if flags & 4:
        if len(b) < 12 + 2 * n:
            raise WavError('truncated .wt data')
        x = np.frombuffer(b[12:12 + 2 * n], '<i2').astype(np.float64) / (32768.0 if flags & 8 else 16384.0)
        bits = 16
    else:
        if len(b) < 12 + 4 * n:
            raise WavError('truncated .wt data')
        x = np.frombuffer(b[12:12 + 4 * n], '<f4').astype(np.float64)
        bits = 32
    x = np.nan_to_num(x, nan=0.0, posinf=0.0, neginf=0.0)
    return x, 0, {'clm': None, 'channels': 1, 'bits': bits, 'frame': size}


def read_wav(path):
    """Returns (mono float array in -1..1, rate, info dict with 'clm' text or None, 'channels', 'bits').
    A Surge .wt file is read here too (rate 0, info['frame'] set)."""
    b = Path(path).read_bytes()
    if b[:4] == b'vawt':
        return read_wt(b)
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
    """(frame length, why): a .wt header, the explicit format, Serum's clm chunk, then the usual sizes (ASSUMED)."""
    n = len(x)
    if info.get('frame'):
        return info['frame'], 'wave_size in the .wt header'
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


def select(n, how):
    """Source frame indices kept by --pick (before the 32-frame mapping)."""
    if how in (None, '', 'even'):
        return list(range(n))
    if how == 'first':
        return list(range(min(n, gw.FRAMES)))
    if how == 'last':
        return list(range(max(0, n - gw.FRAMES), n))
    if how == 'odd':
        return list(range(1, n, 2)) or [0]
    try:
        if ':' in how:
            lo, hi = how.split(':', 1)
            idx = list(range(int(lo or 0), int(hi) if hi else n))
        else:
            idx = [int(t) for t in how.split(',')]
    except ValueError:
        raise WavError(f'--pick {how!r}: use even, first, last, odd, A:B or a comma list')
    idx = [i for i in idx if 0 <= i < n]
    if not idx:
        raise WavError(f'--pick {how!r} selects none of the {n} frames')
    return idx


def pick_32(spectra):
    """32 spectra from any count: 32 or more are picked at evenly spaced frames (a set of distinct waves is not
    blended), fewer are linearly interpolated (a crossfade between neighbours)."""
    n = len(spectra)
    if n == 1:
        return [spectra[0]] * gw.FRAMES
    if n >= gw.FRAMES:
        return [spectra[int(round(i * (n - 1) / (gw.FRAMES - 1)))] for i in range(gw.FRAMES)]
    out = []
    for i in range(gw.FRAMES):
        p = i * (n - 1) / (gw.FRAMES - 1)
        a = int(np.floor(p))
        b = min(a + 1, n - 1)
        t = p - a
        out.append(spectra[a] * (1.0 - t) + spectra[b] * t)
    return out


def spectra_of(x, frame):
    n = len(x) // frame
    if n < 1:
        raise WavError('file shorter than one frame')
    return [frame_spectrum(x[i * frame:(i + 1) * frame]) for i in range(n)]


def convert(sources, how=None):
    """int16 table data, source frame count, kept count, gain from a list of per-frame spectra."""
    keep = [sources[i] for i in select(len(sources), how)]
    sp = pick_32(keep)
    if all(np.max(np.abs(c)) < 1e-9 for c in sp):
        raise WavError('the input is silent (or has no energy in harmonics 1..64)')
    gain = gw.table_gain(sp)
    return gw.quantise(sp, gain), len(keep), gain


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
def natural_key(p):
    return [int(t) if t.isdigit() else t.lower() for t in re.split(r'(\d+)', str(p))]


def expand(inputs):
    """Files as given; a folder becomes its .wav files in natural order."""
    out = []
    for i in inputs:
        p = Path(i)
        if p.is_dir():
            fs = sorted((f for f in p.iterdir() if f.suffix.lower() in ('.wav', '.wt') and not f.name.startswith('.')),
                        key=natural_key)
            if not fs:
                raise WavError(f'{p}: no .wav or .wt files')
            out += fs
        else:
            out.append(p)
    return out


def describe(path, fmt, frame):
    x, rate, info = read_wav(path)
    if frame:
        L, why = frame, '--frame'
    else:
        L, why = detect_frame(x, info, fmt)
    n = len(x) // L
    return x, rate, info, L, why, n


def cmd_inspect(a):
    for path in expand(a.input):
        x, rate, info, L, why, n = describe(path, a.format, a.frame)
        print(f'{path}: {len(x)} samples, {info["channels"]} ch, {info["bits"]}-bit, {rate} Hz')
        print(f'  frame length {L} ({why}); {n} frame(s)')
        if len(x) % L:
            print(f'  note: {len(x) % L} trailing samples ignored')
        if info.get('clm'):
            print(f'  clm chunk: {info["clm"].strip()!r}')
        peak = float(np.max(np.abs(x))) if len(x) else 0.0
        print(f'  peak {peak:.3f}; harmonics kept 1..{min(gw.HMAX, L // 2)} of {L // 2}')
        if L > 4096 or n > 256:
            print('  warning: unusually large; is this a cycle table or a recording?')
    return 0


def cmd_build(a):
    files = expand(a.input)
    sources, lens = [], set()
    for path in files:
        x, rate, info, L, why, n = describe(path, a.format, a.frame)
        sources += spectra_of(x, L)
        lens.add(L)
        print(f'{path.name}: frame {L} ({why}), {n} frame(s)')
    if len(lens) > 1:
        print(f'note: frame lengths differ ({sorted(lens)}); each file was read at its own length')
    total = len(sources)
    data, kept, gain = convert(sources, a.pick)
    img = slot_image(a.name, data)
    Path(a.out).write_bytes(img)
    how = f'{total} frame(s)' + (f', --pick {a.pick} keeps {kept}' if kept != total else '')
    print(f'{len(files)} file(s): {how} -> 32, gain {gain:.3f}')
    print(f'{a.out}: {len(img)} bytes, name {clean_name(a.name)!r}, data crc32 {zlib.crc32(img[HEADER_LEN:]):08x}')
    return 0

# ---- the FM-1 over USB-MIDI ("VW link" v1, docs/UPLOAD-DESIGN.md section 4) ----
import fm1_install as fi  # noqa: E402  (pack7, the MIDI backend and the port search of the installer)

SLOTS, SLOT_SIZE, DATA_AT = 8, 0x9000, 0x1000
CMD_HELLO, CMD_DIR, CMD_READ = 1, 2, 8
RC_TEXT = {1: 'bad arguments', 2: 'range or alignment', 3: 'erase failed', 4: 'program failed', 5: 'verify failed',
           6: 'no BEGIN or timeout', 7: 'refused (this firmware stage cannot do that)', 8: 'CRC mismatch',
           9: 'no flash'}


class LinkError(Exception):
    pass


def vw_frame(cmd, seq, payload=b''):
    body = bytes([0x7D, 0x56, 0x57, cmd, seq]) + len(payload).to_bytes(2, 'little') + bytes(payload)
    body += bytes([~sum(body[3:]) & 0xFF])
    return b'\xF0' + fi.pack7(body) + b'\xF7'


def vw_parse(pkt):
    """reply frame -> (cmd, rc, seq, payload), or None if it is not a well-formed VW reply"""
    if len(pkt) < 2 or pkt[0] != 0xF0 or pkt[-1] != 0xF7:
        return None
    d = fi.unpack7(pkt[1:-1])
    if len(d) < 9 or d[:3] != b'\x7D\x56\x57' or int.from_bytes(d[6:8], 'little') + 9 != len(d):
        return None
    if (~sum(d[3:-1])) & 0xFF != d[-1]:
        return None
    return d[3], d[4], d[5], d[8:-1]


class VwLink:
    def __init__(self, link, tries=4, timeout=1.5):
        self.link, self.tries, self.timeout, self.seq = link, tries, timeout, 0

    def request(self, cmd, payload=b''):
        """send, wait for the matching reply (the same request is repeated if none comes); -> payload, raises LinkError"""
        self.seq = self.seq % 255 + 1
        pkt = vw_frame(cmd, self.seq, payload)
        for _ in range(self.tries):
            self.link.drain()
            if not self.link.send(pkt):
                raise LinkError('could not send to the FM-1 (unplugged?)')
            end = time.monotonic() + self.timeout
            while (left := end - time.monotonic()) > 0:
                r = vw_parse(self.link.read(left) or b'')
                if r and r[0] == cmd and r[2] == self.seq:
                    if r[1]:
                        raise LinkError(f'FM-1 answered: {RC_TEXT.get(r[1], "error %d" % r[1])}')
                    return r[3]
        raise LinkError('no answer from the FM-1 (is the firmware new enough? VAVEWave with the VW link is needed)')

    def hello(self):
        d = self.request(CMD_HELLO)
        if len(d) != 16:
            raise LinkError('unexpected HELLO reply')
        return {'proto': d[0], 'stage': d[1], 'slots': d[2], 'hdr': d[3], 'slot_size': int.from_bytes(d[4:8], 'little'),
                'id': d[8:16].rstrip(b'\0').decode('latin-1')}

    def directory(self, start=0, count=SLOTS):
        d = self.request(CMD_DIR, bytes([start, count]))
        if len(d) != 16 * count:
            raise LinkError('unexpected DIR reply')
        out = []
        for i in range(count):
            e = d[16 * i:16 * i + 16]
            out.append({'slot': e[0], 'valid': bool(e[1]), 'frames': e[2], 'levels': e[3],
                        'name': e[4:12].rstrip(b'\0').decode('latin-1'), 'crc': int.from_bytes(e[12:16], 'little')})
        return out

    def read(self, slot, off, n):
        out = b''
        while n:
            k = min(n, 256)
            d = self.request(CMD_READ, bytes([slot]) + off.to_bytes(2, 'little') + k.to_bytes(2, 'little'))
            if len(d) != k:
                raise LinkError('short READ reply')
            out, off, n = out + d, off + k, n - k
        return out


def connect(a, backend=None):
    """-> (VwLink, device name); the FM-1 is found with the installer's port search"""
    if backend is None:
        try:
            backend = fi.MidoBackend()
        except ImportError:
            raise LinkError('needs mido and python-rtmidi (pip install mido python-rtmidi)')
    up = fi.Updater(backend, a.port)
    dev = up.find()
    if not dev:
        raise LinkError('no FM-1 found (plugged in? another program using its MIDI port?)')
    if dev.id.loader:
        raise LinkError(f'the FM-1 is in update mode ({dev.id.text}); finish the update first (fm1_install.py)')
    return VwLink(dev.link), dev


def cmd_info(a, backend=None):
    vw, dev = connect(a, backend)
    try:
        h = vw.hello()
        print(f'{h["id"]}  port {dev.name}  VW link v{h["proto"]}, firmware stage {h["stage"]} '
              f'({"read only" if h["stage"] == 0 else "can write"}), {h["slots"]} slots')
        for e in vw.directory(0, h['slots']):
            if e['valid']:
                print(f'  slot {e["slot"]}: {e["name"]!r}  {e["frames"]} frames, {e["levels"]} levels, data crc32 {e["crc"]:08x}')
            else:
                print(f'  slot {e["slot"]}: empty')
    finally:
        dev.link.close()
    return 0


def cmd_get(a, backend=None):
    if not 0 <= a.slot < SLOTS:
        raise LinkError(f'slot must be 0..{SLOTS - 1}')
    vw, dev = connect(a, backend)
    try:
        e = vw.directory(a.slot, 1)[0]
        if not e['valid']:
            raise LinkError(f'slot {a.slot} is empty (nothing to copy)')
        img = vw.read(a.slot, 0, HEADER_LEN) + vw.read(a.slot, DATA_AT, DATA_BYTES)
        parse_image(img)                             # header and data CRC: a bad copy is never written out
    finally:
        dev.link.close()
    Path(a.out).write_bytes(img)
    print(f'slot {a.slot} {e["name"]!r}: {len(img)} bytes -> {a.out}, data crc32 {e["crc"]:08x} (checked)')
    return 0


def main(argv=None, backend=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    sub = ap.add_subparsers(dest='cmd', required=True)
    for name in ('info', 'get'):
        p = sub.add_parser(name)
        if name == 'get':
            p.add_argument('slot', type=int)
            p.add_argument('out')
        p.add_argument('--port', metavar='NAME', help='MIDI port to use (part of its name)')
    for name in ('inspect', 'build'):
        p = sub.add_parser(name)
        if name == 'build':
            p.add_argument('name')
            p.add_argument('out')
            p.add_argument('--pick', help='even (default), first, last, odd, A:B or a comma list of source frames')
        p.add_argument('input', nargs='+', help='WAV files and / or folders of WAV files, joined in order')
        p.add_argument('--frame', type=int, help='samples per frame (overrides detection)')
        p.add_argument('--format', choices=FORMATS, default='auto')
    a = ap.parse_args(argv)
    try:
        if a.cmd in ('info', 'get'):
            return {'info': cmd_info, 'get': cmd_get}[a.cmd](a, backend)
        return {'inspect': cmd_inspect, 'build': cmd_build}[a.cmd](a)
    except (WavError, LinkError, OSError) as e:
        print(f'error: {e}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
