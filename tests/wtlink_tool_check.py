# SPDX-License-Identifier: GPL-3.0-only
"""tools/fm1_wavetable.py `info` and `get` against the real firmware logic (firmware/src/wtlink.h, compiled by
tests/run_wtlink.sh as build/host/wtlink_check --serve) on a fake flash: the wire format of both sides must agree.
Run through tests/run_wtlink.sh."""
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'tools'))
import fm1_wavetable as wt  # noqa: E402

SERVER = ROOT / 'build' / 'host' / 'wtlink_check'
fails = 0


def check(ok, what):
    global fails
    print(('ok    ' if ok else 'FAIL  ') + what)
    fails += 0 if ok else 1


class FakeLink:
    """what MidoLink offers, backed by the firmware process; drop=N loses the first N replies (a busy USB port)"""
    def __init__(self, args, drop=0):
        self.p = subprocess.Popen([str(SERVER), '--serve', *args], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
        self.q, self.drop, self.sent = [], drop, 0

    def send(self, pkt):
        self.sent += 1
        self.p.stdin.write(pkt.hex() + '\n')
        self.p.stdin.flush()
        r = self.p.stdout.readline().strip()
        if r != '-' and self.drop:
            self.drop -= 1
        elif r != '-':
            self.q.append(bytes.fromhex(r))
        return True

    def read(self, timeout):
        return self.q.pop(0) if self.q else None

    def drain(self):
        self.q.clear()

    def close(self):
        self.p.stdin.close()
        self.p.wait()


class Dev:
    def __init__(self, link):
        self.link, self.name = link, 'fake'


def fast(link):
    v = wt.VwLink(link, tries=4, timeout=0.01)
    return v


with tempfile.TemporaryDirectory() as t:
    td = Path(t)
    x = np.array([np.sin(np.arange(2048) * 2 * np.pi * (1 + f % 4) / 2048) for f in range(8)]).reshape(-1)
    sources = wt.spectra_of(x, 2048)
    data, _, _ = wt.convert(sources, None)
    img = wt.slot_image('PROLOGFA', data)
    (td / 'a.vwt').write_bytes(img)

    link = FakeLink([f'2:{td / "a.vwt"}'])
    vw = fast(link)
    h = vw.hello()
    check(h['proto'] == 1 and h['stage'] == 1 and h['slots'] == 8 and h['slot_size'] == 0x9000 and h['id'] == 'FM-1_950',
          'HELLO: protocol, stage 1, 8 slots, identity')
    d = vw.directory()
    check([e['valid'] for e in d] == [False, False, True, False, False, False, False, False], 'DIR: only slot 2 holds a table')
    check(d[2]['name'] == 'PROLOGFA' and d[2]['frames'] == 32 and d[2]['levels'] == 6, 'DIR: name, frames, levels')
    check(d[2]['crc'] == wt.parse_image(img)['crc'], 'DIR: data CRC equals the tool\'s')
    check(vw.read(2, 0, 64) == img[:64] and vw.read(2, 0x1000, 32640) == img[64:], 'READ: header and all data come back exact')
    try:
        vw.request(wt.CMD_READ, bytes([8, 0, 0, 1, 0]))
        check(False, 'READ slot 8 is refused')
    except wt.LinkError as e:
        check('bad arguments' in str(e), 'READ slot 8 is refused with a clear message')
    try:
        vw.request(3, b'')
        check(False, 'a slot write command is refused at stage 1')
    except wt.LinkError as e:
        check('refused' in str(e), 'BEGIN is refused at stage 1')
    link.close()

    # a lost reply is asked again
    link = FakeLink([f'2:{td / "a.vwt"}'], drop=2)
    vw = fast(link)
    check(vw.hello()['slots'] == 8 and link.sent == 3, 'a lost reply: the request is repeated (3rd try answered)')
    link.close()
    link = FakeLink([], drop=99)
    try:
        fast(link).hello()
        check(False, 'silence is an error')
    except wt.LinkError as e:
        check('no answer' in str(e) and link.sent == 4, 'no answer: 4 tries, then an error')
    link.close()

    # the commands as the user runs them
    class Backend:                      # what fm1_install.Updater.find() needs
        def __init__(self, link): self.link = link
        def input_names(self): return ['FM-1']
        def output_names(self): return ['FM-1']
        def open(self, i, o): return self.link

    class FakeId:
        loader = False
        text = 'FM-1_950'

    import fm1_install as fi
    saved = (fi.handshake, wt.VwLink.__init__)
    fi.handshake = lambda link, tries=3: FakeId()
    orig_init = wt.VwLink.__init__
    wt.VwLink.__init__ = lambda self, link, tries=4, timeout=0.01: orig_init(self, link, tries, timeout)
    try:
        link = FakeLink([f'2:{td / "a.vwt"}'])
        rc = wt.main(['get', '2', str(td / 'back.vwt')], Backend(link))
        check(rc == 0 and (td / 'back.vwt').read_bytes() == img, 'get: the saved file equals what was uploaded')
        link = FakeLink([f'2:{td / "a.vwt"}'])
        rc = wt.main(['get', '1', str(td / 'none.vwt')], Backend(link))
        check(rc == 1 and not (td / 'none.vwt').exists(), 'get: an empty slot is an error and writes nothing')
        link = FakeLink([f'2:{td / "a.vwt"}'])
        check(wt.main(['info'], Backend(link)) == 0, 'info runs')
        link = FakeLink([])
        check(wt.main(['selftest', '--yes'], Backend(link)) == 0, 'selftest: passes on a healthy flash')
        link = FakeLink([])
        import io, contextlib
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            wt.cmd_selftest(type('A', (), {'yes': False, 'port': None})(), Backend(link), ask=lambda p: 'n')
        check('cancelled' in buf.getvalue(), 'selftest: asks first, "n" cancels without sending the command')
        link = FakeLink([f'2:{td / "a.vwt"}'])
        check(wt.main(['get', '9', str(td / 'x.vwt')], Backend(link)) == 1, 'get: slot 9 is an error')
        link.close()
    finally:
        fi.handshake, wt.VwLink.__init__ = saved

print(f'{fails} FAIL')
sys.exit(1 if fails else 0)
