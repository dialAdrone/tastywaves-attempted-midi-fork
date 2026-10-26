// SPDX-License-Identifier: GPL-3.0-only
// A fake Web MIDI device for tests/web_smoke.sh: answers HELLO and DIR (slot 3 holds TESTTAB), READ with zeros.
import { vwFrame, unpack7 } from './vwlink.js';
const enc = new TextEncoder();
const input = { name: 'FM-1 Composite', id: 'i1', onmidimessage: null };
const reply = (cmd, seq, payload) => {
  // the device's reply carries rc and a different layout: cmd rc seq len payload sum (docs/UPLOAD-DESIGN.md section 4)
  const body = new Uint8Array(9 + payload.length); body.set([0x7D, 0x56, 0x57, cmd, 0, seq, payload.length & 255, payload.length >> 8]); body.set(payload, 8);
  let s = 0; for (let i = 3; i < body.length - 1; i++) s += body[i]; body[body.length - 1] = ~s & 0xFF;
  return body;
};
const pack = (b) => { const o = []; let a = 0, n = 0; for (const x of b) { a |= x << n; n += 8; while (n >= 7) { o.push(a & 127); a >>>= 7; n -= 7; } } if (n) o.push(a & 127); return Uint8Array.from([0xF0, ...o, 0xF7]); };
const output = {
  name: 'FM-1 Composite', id: 'o1',
  send(pkt) {
    const d = unpack7(pkt.subarray(1, pkt.length - 1)), cmd = d[3], seq = d[4], pl = d.subarray(7, d.length - 1);
    let p = new Uint8Array(0);
    if (cmd === 1) { p = new Uint8Array(16); p.set([1, 5, 8, 64, 0, 0x90, 0, 0]); p.set(enc.encode('FM-1_937'), 8); }
    else if (cmd === 2) {
      const start = pl[0], n = pl[1]; p = new Uint8Array(16 * n);
      for (let i = 0; i < n; i++) { const e = p.subarray(16 * i, 16 * i + 16); e[0] = start + i; if (start + i === 3) { e[1] = 1; e[2] = 32; e[3] = 7; e.set(enc.encode('TESTTAB'), 4); e.set([0x78, 0x56, 0x34, 0x12], 12); } }
    } else if (cmd === 12) {                           // PDIR: patch id 2 is "SHIM PAD" with a 16 step pattern
      const start = pl[0]; p = new Uint8Array(256);
      for (let i = 0; i < 16; i++) { const e = p.subarray(16 * i, 16 * i + 16); e[0] = start + i; if (start + i === 2) { e[1] = 1; e[2] = 73; e[3] = 17; e.set(enc.encode('SHIM PAD'), 4); } }
    } else if (cmd === 13) {                           // PGET: osc 1 plays user table slot 2 (value 14)
      p = new Uint8Array(12 + 2 + 146 + 34); p.set(enc.encode('SHIM PAD'), 0); p[12] = 73; p[13] = 17; p[14] = 14; p[14 + 146] = 16;
    } else if (cmd === 8) p = new Uint8Array(pl[3] | pl[4] << 8);
    setTimeout(() => input.onmidimessage && input.onmidimessage({ data: pack(reply(cmd, seq, p)) }), 1);
  },
};
navigator.requestMIDIAccess = async () => ({ inputs: new Map([['i1', input]]), outputs: new Map([['o1', output]]), onstatechange: null });
