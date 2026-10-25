// SPDX-License-Identifier: GPL-3.0-only
// VW link v1 (docs/UPLOAD-DESIGN.md section 4) for the browser and for node: the same wire format and the same
// sequences as tools/fm1_wavetable.py. No DOM and no Web MIDI in here: a transport object is passed in
//   transport.send(Uint8Array)          -> Promise<boolean>   (false = could not send)
//   transport.recv(timeoutMs)           -> Promise<Uint8Array|null>   (one whole F0..F7 message, or null on timeout)
//   transport.drain()                   drop anything received so far

export const SLOTS = 8, HEADER_AT = 0x8F00, HEADER_LEN = 64, DATA_BYTES = 35648, FMT = 1, FRAMES = 32, LEVELS = 7;
export const CMD = { HELLO: 1, DIR: 2, BEGIN: 3, DATA: 4, COMMIT: 5, ABORT: 6, ERASE: 7, READ: 8, SELFTEST: 10, XIPCHECK: 11, PDIR: 12, PGET: 13, PPUT: 14, PDEL: 15 };
export const RC_TEXT = {
  1: 'bad arguments', 2: 'range or alignment', 3: 'erase failed', 4: 'program failed', 5: 'verify failed',
  6: 'no BEGIN or timeout', 7: 'refused (keys down, sound playing, or this firmware cannot do that)', 8: 'CRC mismatch', 9: 'no flash', 10: 'no such patch', 11: 'the patch store is full (delete a patch first)', 12: 'that stored patch is damaged' };

export class LinkError extends Error {}

// ---- CRC32 (zlib) ----
const CRC_T = (() => { const t = new Uint32Array(256); for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1; t[n] = c >>> 0; } return t; })();
export function crc32(b, c = 0) {
  c = ~c >>> 0;
  for (let i = 0; i < b.length; i++) c = CRC_T[(c ^ b[i]) & 0xFF] ^ (c >>> 8);
  return ~c >>> 0;
}

// ---- 7-bit packing of SysEx (the same packer as the installer's: low bits first) ----
export function pack7(data) {
  const out = []; let acc = 0, nb = 0;
  for (const b of data) { acc |= b << nb; nb += 8; while (nb >= 7) { out.push(acc & 0x7F); acc >>>= 7; nb -= 7; } }
  if (nb) out.push(acc & 0x7F);
  return Uint8Array.from(out);
}
export function unpack7(s) {
  const out = []; let acc = 0, nb = 0;
  for (const b of s) { acc |= (b & 0x7F) << nb; nb += 7; while (nb >= 8) { out.push(acc & 0xFF); acc >>>= 8; nb -= 8; } }
  return Uint8Array.from(out);
}

export function vwFrame(cmd, seq, payload = new Uint8Array(0)) {
  const body = new Uint8Array(8 + payload.length);
  body.set([0x7D, 0x56, 0x57, cmd, seq, payload.length & 255, payload.length >> 8]);
  body.set(payload, 7);
  let s = 0; for (let i = 3; i < body.length - 1; i++) s += body[i];
  body[body.length - 1] = ~s & 0xFF;
  const p = pack7(body), out = new Uint8Array(p.length + 2);
  out[0] = 0xF0; out.set(p, 1); out[out.length - 1] = 0xF7;
  return out;
}

// reply frame -> {cmd, rc, seq, payload} or null if it is not a well-formed VW reply
export function vwParse(pkt) {
  if (!pkt || pkt.length < 2 || pkt[0] !== 0xF0 || pkt[pkt.length - 1] !== 0xF7) return null;
  const d = unpack7(pkt.subarray(1, pkt.length - 1));
  if (d.length < 9 || d[0] !== 0x7D || d[1] !== 0x56 || d[2] !== 0x57 || (d[6] | d[7] << 8) + 9 !== d.length) return null;
  let s = 0; for (let i = 3; i < d.length - 1; i++) s += d[i];
  if ((~s & 0xFF) !== d[d.length - 1]) return null;
  return { cmd: d[3], rc: d[4], seq: d[5], payload: d.subarray(8, d.length - 1) };
}

const u32 = (b, o) => (b[o] | b[o + 1] << 8 | b[o + 2] << 16 | b[o + 3] << 24) >>> 0;
const le32 = (v) => [v & 255, v >>> 8 & 255, v >>> 16 & 255, v >>> 24 & 255];
const latin1 = (b) => String.fromCharCode(...b).replace(/\0+$/, '');
const cat = (...a) => { const o = new Uint8Array(a.reduce((n, x) => n + x.length, 0)); let p = 0; for (const x of a) { o.set(x, p); p += x.length; } return o; };

// ---- the 64-byte slot header + data: what one user slot holds (tools/fm1_wavetable.py slot_image) ----
export function cleanName(name) {
  const s = name.toUpperCase().replace(/[^A-Z0-9 ._-]/g, '').slice(0, 8).trim();
  return s || 'USER';
}
export function slotImage(name, raw) {
  if (raw.length !== DATA_BYTES) throw new LinkError('table data must be ' + DATA_BYTES + ' bytes');
  const h = new Uint8Array(HEADER_LEN).fill(0xFF), dv = new DataView(h.buffer);
  h.set([0x56, 0x57, 0x54, 0x31], 0);            // VWT1
  dv.setUint16(4, 2, true); h[6] = FMT; h[7] = FRAMES; h[8] = LEVELS; h[9] = 0;
  h.fill(0, 10, 18);
  for (let i = 0, n = cleanName(name); i < n.length; i++) h[10 + i] = n.charCodeAt(i);
  dv.setUint32(18, raw.length, true); dv.setUint32(22, crc32(raw), true);
  dv.setUint32(26, crc32(h.subarray(0, 26)), true);
  return cat(h, raw);
}
// inverse for loading a .vwt file: {name, crc, data} or throws
export function parseImage(img) {
  const dv = new DataView(img.buffer, img.byteOffset, img.length);
  if (img.length !== HEADER_LEN + DATA_BYTES) throw new LinkError('wrong size');
  if (latin1(img.subarray(0, 4)) !== 'VWT1' || dv.getUint16(4, true) !== 2 || crc32(img.subarray(0, 26)) !== dv.getUint32(26, true)) throw new LinkError('header');
  if (img[6] !== FMT || img[7] !== FRAMES || img[8] !== LEVELS || dv.getUint32(18, true) !== DATA_BYTES || crc32(img.subarray(HEADER_LEN)) !== dv.getUint32(22, true)) throw new LinkError('data');
  return { name: latin1(img.subarray(10, 18)), frames: img[7], levels: img[8], crc: dv.getUint32(22, true), data: img.subarray(HEADER_LEN) };
}

// ---- patches (VW link stage 5, firmware/src/patchlink.h): a body is name[12] | n | pw | int16 v[n] | int16 pattern[pw], little endian ----
export const PATCH_IDS = 32, PATCH_NAME = 12, MAX_PARAMS = 80, MAX_PATTERN = 33, P_TABLE1 = 0, P_TABLE2 = 5;
// stage 6: each patch id k may have a companion at id 32 + k (the second sequencer's settings and pattern, the same body shape).
// It belongs to the patch only while it is the newer record, so it is always written after the patch, and again after any
// rewrite of the patch (rename, move, restore).
export const SEQ2_STAGE = 6;
export function cleanPatchName(name) { return name.replace(/[^\x20-\x7e]/g, '').slice(0, PATCH_NAME).replace(/\s+$/, '') || 'PATCH'; }
export function encodeBody({ name, params, pattern = [] }) {
  if (params.length > MAX_PARAMS || pattern.length > MAX_PATTERN) throw new LinkError('patch too large');
  const b = new Uint8Array(PATCH_NAME + 2 + 2 * params.length + 2 * pattern.length), dv = new DataView(b.buffer);
  const nm = cleanPatchName(name);
  for (let i = 0; i < nm.length; i++) b[i] = nm.charCodeAt(i);
  b[PATCH_NAME] = params.length; b[PATCH_NAME + 1] = pattern.length;
  let o = PATCH_NAME + 2;
  for (const v of params) { dv.setInt16(o, v, true); o += 2; }
  for (const v of pattern) { dv.setInt16(o, v, true); o += 2; }
  return b;
}
export function decodeBody(b) {
  if (b.length < PATCH_NAME + 2) throw new LinkError('patch body too short');
  const n = b[PATCH_NAME], pw = b[PATCH_NAME + 1];
  if (n > MAX_PARAMS || pw > MAX_PATTERN || b.length !== PATCH_NAME + 2 + 2 * n + 2 * pw) throw new LinkError('patch body has the wrong size');
  const dv = new DataView(b.buffer, b.byteOffset, b.length), params = [], pattern = [];
  let o = PATCH_NAME + 2;
  for (let i = 0; i < n; i++, o += 2) params.push(dv.getInt16(o, true));
  for (let i = 0; i < pw; i++, o += 2) pattern.push(dv.getInt16(o, true));
  return { name: latin1(b.subarray(0, PATCH_NAME)), params, pattern };
}
// which user table slots a patch plays (a table value of 12 or more is user slot value - 12; missing parameters are the defaults, table 0)
export function tableRefs(params) {
  return [params[P_TABLE1] || 0, params[P_TABLE2] || 0].filter((t) => t >= 12).map((t) => t - 12);
}
const sameBytes = (a, b) => a.length === b.length && a.every((v, i) => v === b[i]);

export class VwLink {
  constructor(transport, { tries = 4, timeout = 1500 } = {}) { this.t = transport; this.tries = tries; this.timeout = timeout; this.seq = 0; }

  // send, wait for the matching reply (the same request is repeated if none comes) -> payload; throws LinkError
  async request(cmd, payload = new Uint8Array(0), { timeout, tries } = {}) {
    this.seq = this.seq % 255 + 1;
    const pkt = vwFrame(cmd, this.seq, payload);
    for (let k = 0; k < (tries || this.tries); k++) {
      this.t.drain();
      if (!await this.t.send(pkt)) throw new LinkError('could not send to the FM-1 (unplugged?)');
      const end = Date.now() + (timeout || this.timeout);
      for (let left = end - Date.now(); left > 0; left = end - Date.now()) {
        const r = vwParse(await this.t.recv(left));
        if (r && r.cmd === cmd && r.seq === this.seq) {
          if (r.rc) throw new LinkError('FM-1 answered: ' + (RC_TEXT[r.rc] || 'error ' + r.rc));
          return r.payload;
        }
      }
    }
    throw new LinkError('no answer from the FM-1 (cable pulled or switched off? or firmware without the VW link?). ' +
      'A table being written stays empty until it is complete; press Refresh to see.');
  }

  async hello() {
    const d = await this.request(CMD.HELLO);
    if (d.length !== 16) throw new LinkError('unexpected HELLO reply');
    this.stage = d[1];
    return { proto: d[0], stage: d[1], slots: d[2], hdr: d[3], slotSize: u32(d, 4), id: latin1(d.subarray(8, 16)) };
  }

  async directory(start = 0, count = SLOTS) {
    const d = await this.request(CMD.DIR, Uint8Array.of(start, count));
    if (d.length !== 16 * count) throw new LinkError('unexpected DIR reply');
    const out = [];
    for (let i = 0; i < count; i++) {
      const e = d.subarray(16 * i, 16 * i + 16);
      out.push({ slot: e[0], valid: !!e[1], frames: e[2], levels: e[3], name: latin1(e.subarray(4, 12)), crc: u32(e, 12) });
    }
    return out;
  }

  async read(slot, off, n, progress) {
    const out = new Uint8Array(n); let p = 0;
    while (p < n) {
      const k = Math.min(n - p, 256), o = off + p;
      const d = await this.request(CMD.READ, Uint8Array.of(slot, o & 255, o >> 8, k & 255, k >> 8));
      if (d.length !== k) throw new LinkError('short READ reply');
      out.set(d, p); p += k;
      if (progress) progress(p / n);
    }
    return out;
  }

  // the whole slot as a .vwt image (header + data), checked against its own CRCs
  async get(slot, progress) {
    const e = (await this.directory(slot, 1))[0];
    if (!e.valid) throw new LinkError('slot ' + slot + ' is empty (nothing to copy)');
    const img = cat(await this.read(slot, HEADER_AT, HEADER_LEN), await this.read(slot, 0, DATA_BYTES, progress));
    parseImage(img);
    return img;
  }

  async abort() { try { await this.request(CMD.ABORT, new Uint8Array(0), { tries: 1 }); } catch (e) { /* nothing to abort */ } }

  // write an image into a slot, then read it back and compare. The caller has asked the user first.
  async put(slot, img, progress) {
    if (!(slot >= 0 && slot < SLOTS)) throw new LinkError('slot must be 0..' + (SLOTS - 1));
    let t;
    try { t = parseImage(img); } catch (e) { throw new LinkError('not a valid table (' + e.message + ')'); }
    const h = await this.hello();
    if (h.stage < 2) throw new LinkError('this firmware cannot write slots yet (stage < 2)');
    const data = img.subarray(HEADER_LEN), nm = new Uint8Array(8);
    for (let i = 0; i < t.name.length; i++) nm[i] = t.name.charCodeAt(i);
    await this.request(CMD.BEGIN, cat(Uint8Array.of(slot, FMT, t.frames, t.levels), le32(data.length), le32(t.crc), nm));
    try {
      const n = Math.ceil(data.length / 256);
      for (let i = 0; i < n; i++) {
        await this.request(CMD.DATA, cat(Uint8Array.of(slot, i & 255, i >> 8), data.subarray(i * 256, i * 256 + 256)));
        if (progress) progress((i + 1) / n * 0.7);
      }
      try { await this.request(CMD.COMMIT, Uint8Array.of(slot), { timeout: 10000, tries: 1 }); }
      catch (e) {                                 // the reply may have been lost after a good commit: look
        const d = (await this.directory(slot, 1))[0];
        if (!(d.valid && d.crc === t.crc)) throw e;
      }
    } catch (e) { await this.abort(); throw e; }
    const d = (await this.directory(slot, 1))[0];
    if (!(d.valid && d.crc === t.crc)) throw new LinkError('the slot does not hold the table after COMMIT');
    const back = cat(await this.read(slot, HEADER_AT, HEADER_LEN), await this.read(slot, 0, DATA_BYTES, (f) => progress && progress(0.7 + f * 0.3)));
    if (back.length !== img.length || back.some((v, i) => v !== img[i]))
      throw new LinkError('read-back differs from the file (the slot was written but does not match: put again)');
    return t;
  }

  async erase(slot) {
    if (!(slot >= 0 && slot < SLOTS)) throw new LinkError('slot must be 0..' + (SLOTS - 1));
    await this.request(CMD.ERASE, Uint8Array.of(slot));
    if ((await this.directory(slot, 1))[0].valid) throw new LinkError('the slot still reads as valid');
  }

  // ---- patch library ----
  async patchDirectory() {
    const out = [];
    for (const start of [0, 16]) {
      const d = await this.request(CMD.PDIR, Uint8Array.of(start, 16));
      if (d.length !== 256) throw new LinkError('unexpected PDIR reply');
      for (let i = 0; i < 16; i++) {
        const e = d.subarray(16 * i, 16 * i + 16);
        out.push({ id: e[0], present: !!e[1], n: e[2], pw: e[3], name: latin1(e.subarray(4, 16)) });
      }
    }
    return out;
  }
  async getPatchBody(id) { return (await this.request(CMD.PGET, Uint8Array.of(id))).slice(); }
  // the companion of patch id (SEQ2), or null: none, or firmware before stage 6
  async getSeq2Body(id) {
    if (!(this.stage >= SEQ2_STAGE)) return null;
    try { return (await this.request(CMD.PGET, Uint8Array.of(PATCH_IDS + id))).slice(); }
    catch (e) { if (e instanceof LinkError && /no such patch/i.test(e.message)) return null; throw e; }
  }
  // write the companion (after the patch!) and read it back; null deletes it. Nothing on firmware before stage 6.
  async putSeq2Body(id, body) {
    if (!(id >= 0 && id < PATCH_IDS)) throw new LinkError('patch id must be 0..' + (PATCH_IDS - 1));
    if (!(this.stage >= SEQ2_STAGE)) return;
    if (!body) { await this.request(CMD.PDEL, Uint8Array.of(PATCH_IDS + id), { timeout: 5000 }); return; }
    decodeBody(body);
    await this.request(CMD.PPUT, cat(Uint8Array.of(PATCH_IDS + id), body), { timeout: 5000 });
    const back = await this.getSeq2Body(id);
    if (!back || !sameBytes(back, body)) throw new LinkError('read-back of the SEQ2 part of patch ' + (id + 1) + ' differs from what was sent');
  }
  // a whole patch: the body and its companion (null: none)
  async getPatch(id) { return { body: await this.getPatchBody(id), seq2: await this.getSeq2Body(id) }; }
  async putPatch(id, { body, seq2 = null }) { await this.putPatchBody(id, body); await this.putSeq2Body(id, seq2); }
  // write and read back; throws if the log does not hold exactly this body afterwards
  async putPatchBody(id, body) {
    if (!(id >= 0 && id < PATCH_IDS)) throw new LinkError('patch id must be 0..' + (PATCH_IDS - 1));
    decodeBody(body);
    await this.request(CMD.PPUT, cat(Uint8Array.of(id), body), { timeout: 5000 });
    if (!sameBytes(await this.getPatchBody(id), body)) throw new LinkError('read-back of patch ' + (id + 1) + ' differs from what was sent');
  }
  async deletePatch(id) {
    if (!(id >= 0 && id < PATCH_IDS)) throw new LinkError('patch id must be 0..' + (PATCH_IDS - 1));
    await this.request(CMD.PDEL, Uint8Array.of(id), { timeout: 5000 });
    if ((await this.patchDirectory())[id].present) throw new LinkError('the patch is still there after delete');
  }
  async renamePatch(id, name) {
    const { body, seq2 } = await this.getPatch(id);
    await this.putPatch(id, { body: encodeBody({ ...decodeBody(body), name }), seq2 });     // (the companion again: the patch is newer now)
  }
  // exchange two ids (the order of the list on the panel) through a free id as a parking place, so no patch is ever only in the
  // computer's memory. The parked copy is deleted only after both writes were read back.
  async swapPatches(a, b) {
    const d = await this.patchDirectory();                     // always fresh: the panel may have saved a patch since the page looked
    if (!d[a].present) throw new LinkError('patch ' + (a + 1) + ' is gone (Refresh)');
    const ba = await this.getPatch(a), bb = d[b].present ? await this.getPatch(b) : null;
    if (!bb) { await this.putPatch(b, ba); await this.deletePatch(a); return 'moved'; }
    const free = d.findIndex((e, i) => !e.present && i !== a && i !== b);
    if (free < 0) throw new LinkError('all 32 patch numbers are in use: delete one first (a swap needs one free number as a parking place so that no patch can be lost)');
    await this.putPatch(free, ba);                              // on any failure from here the parked copy stays: nothing is lost
    try {
      await this.putPatch(a, bb);
      await this.putPatch(b, ba);
    } catch (e) { throw new LinkError(e.message + ` (a copy of patch ${a + 1} is kept as patch ${free + 1}; nothing was lost)`); }
    await this.deletePatch(free);
    return 'swapped';
  }
  // backup of every patch as a JSON-able object; restore of one
  async backup() {
    const d = await this.patchDirectory(), patches = [];
    const hex = (b) => Array.from(b).map((v) => v.toString(16).padStart(2, '0')).join('');
    for (const e of d) if (e.present) {
      const { body, seq2 } = await this.getPatch(e.id);
      patches.push({ id: e.id, name: e.name, body: hex(body), ...(seq2 ? { seq2: hex(seq2) } : {}) });
    }
    return { format: 'tastywaves-patches', version: 1, patches };       // (seq2: optional, so older tools still read the file)
  }
}
export function parseBackup(obj) {
  if (!obj || (obj.format !== 'tastywaves-patches' && obj.format !== 'vavewave-patches') || obj.version !== 1 || !Array.isArray(obj.patches)) throw new LinkError('not a TastyWaves patch backup');     // (vavewave-patches: the same file from before the rename)
  const seen = new Set();
  return obj.patches.map((p) => {
    if (!Number.isInteger(p.id) || p.id < 0 || p.id >= PATCH_IDS || seen.has(p.id) || typeof p.body !== 'string' || !/^([0-9a-f]{2})+$/.test(p.body)) throw new LinkError('damaged entry in the backup');
    seen.add(p.id);
    const body = Uint8Array.from(p.body.match(/../g).map((h) => parseInt(h, 16)));
    const d = decodeBody(body);
    for (let i = 0; i < PATCH_NAME; i++) if (body[i] && (body[i] < 32 || body[i] > 126)) throw new LinkError('damaged name in the backup');
    let seq2 = null;
    if (p.seq2 !== undefined) {
      if (typeof p.seq2 !== 'string' || !/^([0-9a-f]{2})+$/.test(p.seq2)) throw new LinkError('damaged SEQ2 entry in the backup');
      seq2 = Uint8Array.from(p.seq2.match(/../g).map((h) => parseInt(h, 16)));
      decodeBody(seq2);
    }
    return { id: p.id, name: d.name, body, seq2 };
  });
}
