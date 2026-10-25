// SPDX-License-Identifier: GPL-3.0-only
// Wavetable WAV / Surge .wt -> TastyWaves user table, in the browser: a port of tools/fm1_wavetable.py (reader, frame
// detection, harmonics 1..64, 32-frame mapping) with tools/gen_wavetables.py (band-limiter, gain, quantiser) and
// tools/gen_bench_tables.py relayout (the engine's native 7-level layout). tests/webtool_check.mjs checks the output
// against the Python tool on the same files. The factory-table generator is not here: only conversion of user files.
import { LinkError } from './vwlink.js';

const FRAMES = 32, HMAX = 64, TOP = 256, PEAK = 0.9, LEVELS_IN = 6, LEVELS = 7, PRE = 3, POST = 4;
const FRAME_CHOICES = [2048, 1024, 512, 256];
const LEN = Array.from({ length: LEVELS }, (_, L) => TOP >> L);
const OFF = LEN.map((_, L) => LEN.slice(0, L).reduce((s, n) => s + n + PRE + POST, 0));
const FLEN = OFF[LEVELS - 1] + LEN[LEVELS - 1] + PRE + POST;      // 557
export const DATA_SAMPLES = FRAMES * FLEN;

export class WavError extends LinkError {}

export function roundEven(x) {                  // numpy's round: halves go to the even neighbour
  const r = Math.round(x);
  return (Math.abs(x % 1) === 0.5 && r % 2 !== 0) ? r - 1 : r;
}

// ---- readers ----
const f32 = (b, o, n) => new Float32Array(b.buffer.slice(b.byteOffset + o, b.byteOffset + o + 4 * n));

function readWt(b) {                            // Surge 'vawt'
  if (b.length < 12) throw new WavError('truncated .wt header');
  const dv = new DataView(b.buffer, b.byteOffset, b.length);
  const size = dv.getUint32(4, true), count = dv.getUint16(8, true), flags = dv.getUint16(10, true);
  if (!(size >= 2 && size <= 4096) || count < 1 || count > 512) throw new WavError(`.wt wave_size ${size} / wave_count ${count} out of range`);
  if (flags & 1) throw new WavError('.wt is flagged "is sample" (a recording, not single cycles)');
  const n = size * count; let x, bits;
  if (flags & 4) {
    if (b.length < 12 + 2 * n) throw new WavError('truncated .wt data');
    const k = flags & 8 ? 32768 : 16384; x = new Float64Array(n); bits = 16;
    for (let i = 0; i < n; i++) x[i] = dv.getInt16(12 + 2 * i, true) / k;
  } else {
    if (b.length < 12 + 4 * n) throw new WavError('truncated .wt data');
    x = Float64Array.from(f32(b, 12, n)); bits = 32;
  }
  return { x: x.map((v) => Number.isFinite(v) ? v : 0), rate: 0, info: { clm: null, channels: 1, bits, frame: size } };
}

export function readWav(b) {                    // b: Uint8Array
  if (b.length >= 4 && String.fromCharCode(b[0], b[1], b[2], b[3]) === 'vawt') return readWt(b);
  const dv = new DataView(b.buffer, b.byteOffset, b.length), tag4 = (o) => String.fromCharCode(b[o], b[o + 1], b[o + 2], b[o + 3]);
  if (b.length < 12 || tag4(0) !== 'RIFF' || tag4(8) !== 'WAVE') throw new WavError('not a RIFF/WAVE file');
  let pos = 12, fmt = null, data = null, clm = null;
  while (pos + 8 <= b.length) {
    const id = tag4(pos), size = dv.getUint32(pos + 4, true), body = b.subarray(pos + 8, pos + 8 + size);
    if (id === 'fmt ') fmt = body; else if (id === 'data') data = body;
    else if (id === 'clm ') clm = String.fromCharCode(...body);
    pos += 8 + size + (size & 1);
  }
  if (!fmt || !data) throw new WavError('missing fmt or data chunk');
  const fv = new DataView(fmt.buffer, fmt.byteOffset, fmt.length);
  let tag = fv.getUint16(0, true); const ch = fv.getUint16(2, true), rate = fv.getUint32(4, true), bits = fv.getUint16(14, true);
  if (tag === 0xFFFE && fmt.length >= 26) tag = fv.getUint16(24, true);
  if (ch < 1 || ch > 8) throw new WavError(`${ch} channels`);
  const d = new DataView(data.buffer, data.byteOffset, data.length); let x;
  const per = bits / 8;
  if (tag === 1 && ![8, 16, 24, 32].includes(bits)) throw new WavError(`${bits}-bit PCM not supported`);
  if (tag === 3 && ![32, 64].includes(bits)) throw new WavError(`${bits}-bit float not supported`);
  if (tag !== 1 && tag !== 3) throw new WavError(`format tag ${tag} not supported (PCM or float only)`);
  const n = Math.floor(data.length / per); x = new Float64Array(n);
  for (let i = 0; i < n; i++) {
    const o = i * per;
    if (tag === 1) {
      x[i] = bits === 8 ? (d.getUint8(o) - 128) / 128 : bits === 16 ? d.getInt16(o, true) / 32768
        : bits === 24 ? (((d.getUint8(o) | d.getUint8(o + 1) << 8 | d.getUint8(o + 2) << 16) << 8) >> 8) / 8388608
        : d.getInt32(o, true) / 2147483648;
    } else x[i] = bits === 32 ? d.getFloat32(o, true) : d.getFloat64(o, true);
  }
  const m = Math.floor(n / ch), mono = new Float64Array(m);
  for (let i = 0; i < m; i++) { let s = 0; for (let c = 0; c < ch; c++) s += x[i * ch + c]; mono[i] = s / ch; }
  return { x: mono.map((v) => Number.isFinite(v) ? v : 0), rate, info: { clm, channels: ch, bits } };
}

// ---- frame length ----
export function detectFrame(x, info, fmt = 'auto') {
  const n = x.length;
  if (info.frame) return [info.frame, 'wave_size in the .wt header'];
  if (fmt === 'waveedit') return [256, 'format waveedit'];
  if (fmt === 'ableton' || fmt === 'surge') return [2048, 'format ' + fmt];
  if (info.clm) { const m = /^<!>(\d+)/.exec(info.clm.trim()); if (m && +m[1] >= 64 && +m[1] <= 8192) return [+m[1], 'clm chunk']; }
  if (fmt === 'serum') return [2048, 'format serum (no clm chunk)'];
  if (n === 16384) return [256, 'ASSUMED: 16384 samples is the WaveEdit layout (64 x 256); choose the frame length to change'];
  for (const L of FRAME_CHOICES) if (n % L === 0) return [L, `ASSUMED: no clm chunk, ${n} samples divide by ${L}; choose the frame length or format to change`];
  throw new WavError(`${n} samples fit no frame length of ${FRAME_CHOICES}; choose one`);
}

// ---- spectra: {re, im} Float64Array(64), x(t) = sum Im(c_k e^{ikt}) ----
const TW = new Map();                            // frame length -> cos / sin of 2 pi j / n
function twiddle(n) {
  if (!TW.has(n)) TW.set(n, { c: Float64Array.from({ length: n }, (_, j) => Math.cos(2 * Math.PI * j / n)), s: Float64Array.from({ length: n }, (_, j) => Math.sin(2 * Math.PI * j / n)) });
  return TW.get(n);
}
export function frameSpectrum(fr) {
  const n = fr.length, m = Math.min(HMAX, n >> 1), re = new Float64Array(HMAX), im = new Float64Array(HMAX), s = 2 / n, { c, s: sn } = twiddle(n);
  for (let k = 1; k <= m; k++) {
    let a = 0, b = 0;
    for (let j = 0, q = 0; j < n; j++, q += k) { if (q >= n) q %= n; a += fr[j] * c[q]; b -= fr[j] * sn[q]; }
    re[k - 1] = -b * s; im[k - 1] = a * s;             // c = i * X
  }
  return { re, im };
}
// one function per frame, computed on first use and kept: a frame that the pick leaves out is never transformed
export function spectraOf(x, frame) {
  const n = Math.floor(x.length / frame);
  if (n < 1) throw new WavError('file shorter than one frame');
  return Array.from({ length: n }, (_, i) => { let v = null; return () => v || (v = frameSpectrum(x.subarray(i * frame, (i + 1) * frame))); });
}

export function select(n, how) {
  const range = (a, b) => Array.from({ length: Math.max(0, b - a) }, (_, i) => a + i);
  if (!how || how === 'even') return range(0, n);
  if (how === 'first') return range(0, Math.min(n, FRAMES));
  if (how === 'last') return range(Math.max(0, n - FRAMES), n);
  if (how === 'odd') { const r = range(0, n).filter((i) => i % 2); return r.length ? r : [0]; }
  let idx;
  if (how.includes(':')) {
    const [lo, hi] = how.split(':', 2);
    if (!/^\d*$/.test(lo) || !/^\d*$/.test(hi)) throw new WavError(`pick ${how}: use even, first, last, odd, A:B or a comma list`);
    idx = range(lo ? +lo : 0, hi ? +hi : n);
  } else {
    idx = how.split(',').map((t) => t.trim());
    if (!idx.every((t) => /^\d+$/.test(t))) throw new WavError(`pick ${how}: use even, first, last, odd, A:B or a comma list`);
    idx = idx.map(Number);
  }
  idx = idx.filter((i) => i >= 0 && i < n);
  if (!idx.length) throw new WavError(`pick ${how} selects none of the ${n} frames`);
  return idx;
}

function pick32(sp) {
  const n = sp.length;
  if (n === 1) return Array(FRAMES).fill(sp[0]());
  if (n >= FRAMES) return Array.from({ length: FRAMES }, (_, i) => sp[roundEven(i * (n - 1) / (FRAMES - 1))]());
  return Array.from({ length: FRAMES }, (_, i) => {
    const p = i * (n - 1) / (FRAMES - 1), a = Math.floor(p), b = Math.min(a + 1, n - 1), t = p - a;
    const re = new Float64Array(HMAX), im = new Float64Array(HMAX);
    const A = sp[a](), B = sp[b]();
    for (let k = 0; k < HMAX; k++) { re[k] = A.re[k] * (1 - t) + B.re[k] * t; im[k] = A.im[k] * (1 - t) + B.im[k] * t; }
    return { re, im };
  });
}

// ---- band-limit, gain, quantise, native layout ----
const COS = Float64Array.from({ length: TOP }, (_, i) => Math.cos(2 * Math.PI * i / TOP));
const SIN = Float64Array.from({ length: TOP }, (_, i) => Math.sin(2 * Math.PI * i / TOP));

function mipFrame(c, level) {                   // float samples at one mip level: Im(c_k e^{ikt}), k <= 64 >> level
  const n = LEN[level], h = HMAX >> level, st = TOP / n, out = new Float64Array(n);
  for (let j = 0; j < n; j++) {
    let s = 0;
    for (let k = 1; k <= h; k++) { const q = ((j * k) % n) * st; s += c.im[k - 1] * COS[q] + c.re[k - 1] * SIN[q]; }
    out[j] = s;
  }
  return out;
}

export function convert(sources, how) {         // -> {raw: Uint8Array (35,648 B native), kept, gain}
  const keep = select(sources.length, how).map((i) => sources[i]), sp = pick32(keep);
  if (sp.every((c) => Math.max(...c.re.map(Math.abs), ...c.im.map(Math.abs)) < 1e-9)) throw new WavError('the input is silent (or has no energy in harmonics 1..64)');
  const mips = sp.map((c) => Array.from({ length: LEVELS_IN }, (_, L) => mipFrame(c, L)));
  let peak = 0; for (const f of mips) for (const m of f) for (const v of m) peak = Math.max(peak, Math.abs(v));
  const gain = PEAK * 32767 / peak, out = new Int16Array(DATA_SAMPLES);
  mips.forEach((f, fi) => {
    const base = fi * FLEN, q = f.map((m) => Int16Array.from(m, (v) => Math.max(-32767, Math.min(32767, roundEven(v * gain)))));
    for (let L = 0; L < LEVELS; L++) {
      const n = LEN[L]; let lv;
      if (L < LEVELS_IN) lv = q[L];
      else {                                    // level 6: the fundamental of the stored level 0, 4 samples
        let a = 0, b = 0; for (let j = 0; j < TOP; j++) { a += q[0][j] * COS[j]; b -= q[0][j] * SIN[j]; }
        a *= 2 / TOP; b *= 2 / TOP;
        lv = Int16Array.from({ length: n }, (_, i) => Math.max(-32768, Math.min(32767, roundEven(a * Math.cos(2 * Math.PI * i / n) - b * Math.sin(2 * Math.PI * i / n)))));
      }
      const o = base + OFF[L];
      for (let i = 0; i < PRE; i++) out[o + i] = lv[(n - PRE + i) % n];
      out.set(lv, o + PRE);
      for (let i = 0; i < POST; i++) out[o + PRE + n + i] = lv[i % n];
    }
  });
  return { raw: new Uint8Array(out.buffer), kept: keep.length, gain };
}

export function naturalKey(s) { return s.toLowerCase().split(/(\d+)/).map((t, i) => i % 2 ? +t : t); }
export function naturalCompare(a, b) {
  const x = naturalKey(a), y = naturalKey(b);
  for (let i = 0; i < Math.min(x.length, y.length); i++) if (x[i] !== y[i]) return x[i] < y[i] ? -1 : 1;
  return x.length - y.length;
}

// files: [{name, bytes}] in the order wanted -> {raw, report: [text], total, kept, gain}
export function buildTable(files, { frame = 0, format = 'auto', pick = 'even' } = {}, cache = new Map()) {
  const sources = [], report = [], lens = new Set();
  for (const f of files) {
    if (!cache.has(f)) cache.set(f, { wav: readWav(f.bytes), by: new Map() });      // cache: reading and spectra per file
    const c = cache.get(f), { x, info } = c.wav;
    const [L, why] = frame ? [frame, 'chosen'] : detectFrame(x, info, format);
    if (!c.by.has(L)) c.by.set(L, spectraOf(x, L));
    const sp = c.by.get(L); sources.push(...sp); lens.add(L);
    report.push(`${f.name}: frame ${L} (${why}), ${sp.length} frame(s)`);
  }
  if (lens.size > 1) report.push(`note: frame lengths differ (${[...lens].sort((a, b) => a - b)}); each file was read at its own length`);
  const total = sources.length, r = convert(sources, pick);
  return { raw: r.raw, report, total, kept: r.kept, gain: r.gain };
}
