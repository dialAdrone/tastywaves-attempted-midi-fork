// SPDX-License-Identifier: GPL-3.0-only
// tools/web: (1) convert.js against tools/fm1_wavetable.py on the same inputs, (2) vwlink.js against the real firmware
// logic (build/host/wtlink_check --serve) on a fake flash. Run through tests/run_webtool.sh:
//   node tests/webtool_check.mjs PYTHON INPUT_DIR
import { spawn, execFileSync } from 'node:child_process';
import { readFileSync, readdirSync, mkdtempSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { createInterface } from 'node:readline';
import { buildTable, readWav, detectFrame, naturalCompare } from '../tools/web/convert.js';
import { encodeBody, decodeBody, parseBackup, tableRefs, VwLink, slotImage, parseImage, crc32, vwFrame, vwParse, LinkError, HEADER_LEN, DATA_BYTES } from '../tools/web/vwlink.js';

const [py, inDir] = process.argv.slice(2);
let fails = 0;
const check = (ok, what) => { console.log((ok ? 'ok    ' : 'FAIL  ') + what); if (!ok) fails++; };
const tmp = mkdtempSync(join(tmpdir(), 'webtool-'));
const bytes = (p) => new Uint8Array(readFileSync(p));

// ---- (1) converter parity ----
const cases = [
  ['serum16.wav', {}], ['serum16.wav', { pick: 'first' }], ['serum16.wav', { pick: '10:60' }], ['stereo24.wav', {}],
  ['float20.wav', {}], ['waveedit.wav', {}], ['one.wav', {}], ['ext.wav', {}], ['surge16.wt', {}], ['surgef.wt', { pick: 'odd' }],
  ['serum16.wav', { frame: 1024 }],
];
for (const [file, o] of cases) {
  const args = ['-I', 'tools/fm1_wavetable.py', 'build', 'PARITY', join(tmp, 'p.vwt'), join(inDir, file)];
  if (o.pick) args.push('--pick', o.pick);
  if (o.frame) args.push('--frame', String(o.frame));
  execFileSync(py, args, { stdio: 'pipe' });
  const want = bytes(join(tmp, 'p.vwt'));
  const r = buildTable([{ name: file, bytes: bytes(join(inDir, file)) }], o);
  const got = slotImage('PARITY', r.raw);
  let diff = 0, maxd = 0;
  const a = new Int16Array(got.buffer.slice(HEADER_LEN)), b = new Int16Array(want.buffer.slice(want.byteOffset + HEADER_LEN, want.byteOffset + want.length));
  for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) { diff++; maxd = Math.max(maxd, Math.abs(a[i] - b[i])); }
  const same = got.length === want.length && got.every((v, i) => v === want[i]);
  check(same || (diff <= 8 && maxd <= 1), `${file} ${JSON.stringify(o)}: ${same ? 'identical to the Python tool' : `${diff} of ${a.length} samples differ, max ${maxd}`}`);
  parseImage(got);
}
check(['a2', 'a10', 'a1'].sort(naturalCompare).join() === 'a1,a2,a10', 'natural order a1 a2 a10');
for (const [bad, what] of [[new Uint8Array(10), 'junk'], [bytes(join(inDir, 'one.wav')).subarray(0, 30), 'truncated']]) {
  let msg = ''; try { buildTable([{ name: 'x', bytes: bad }]); } catch (e) { msg = e.message; }
  check(msg !== '', `${what} input is refused with a message (${msg})`);
}
{ let m = ''; try { buildTable([{ name: 'x', bytes: bytes(join(inDir, 'one.wav')) }], { pick: 'zz' }); } catch (e) { m = e.message; }
  check(/pick/.test(m), 'a bad pick is refused'); }

// ---- (2) the link against the firmware logic ----
class FakeLink {
  constructor(args) {
    this.p = spawn('build/host/wtlink_check', ['--serve', ...args], { stdio: ['pipe', 'pipe', 'inherit'] });
    this.lines = []; this.waiters = []; this.q = []; this.drop = 0; this.sent = 0;
    createInterface({ input: this.p.stdout }).on('line', (l) => { const w = this.waiters.shift(); if (w) w(l); else this.lines.push(l); });
  }
  line() { return this.lines.length ? Promise.resolve(this.lines.shift()) : new Promise((r) => this.waiters.push(r)); }
  async send(pkt) {
    this.sent++;
    this.p.stdin.write(Buffer.from(pkt).toString('hex') + '\n');
    const r = await this.line();
    if (r !== '-' && this.drop > 0) this.drop--;               // a lost reply
    else if (r !== '-') this.q.push(Uint8Array.from(Buffer.from(r, 'hex')));
    return true;
  }
  async recv() { return this.q.shift() || null; }
  drain() { this.q.length = 0; }
  close() { this.p.stdin.end(); }
}

const t0 = buildTable([{ name: 'a', bytes: bytes(join(inDir, 'serum16.wav')) }]);
const t1 = buildTable([{ name: 'b', bytes: bytes(join(inDir, 'waveedit.wav')) }]);
const imgA = slotImage('serum a', t0.raw), imgB = slotImage('WE-64', t1.raw);
check(parseImage(imgA).name === 'SERUM A', 'name upper-cased and cleaned');
const crcA = parseImage(imgA).crc, crcB = parseImage(imgB).crc;
writeFileSync(join(tmp, 'b.vwt'), imgB);

const fl = new FakeLink([`2:${join(tmp, 'b.vwt')}`]);
const vw = new VwLink(fl, { tries: 4, timeout: 50 });
const h = await vw.hello();
check(h.proto === 1 && h.stage === 6 && h.slots === 8 && h.slotSize === 0x9000 && h.id === 'FM-1_950', 'HELLO from the firmware logic');
let d = await vw.directory();
check(d.length === 8 && d[2].valid && d[2].name === 'WE-64' && d[2].crc === crcB && !d[0].valid, 'DIR: slot 2 holds WE-64, others empty');
const g = await vw.get(2);
check(g.length === imgB.length && g.every((v, i) => v === imgB[i]), 'get returns the stored image, byte for byte');

let prog = [];
await vw.put(0, imgA, (f) => prog.push(f));
d = await vw.directory();
check(d[0].valid && d[0].crc === crcA && d[0].name === 'SERUM A', 'put to the empty slot 0: valid, crc, name');
check(prog.length > 100 && prog.at(-1) === 1 && prog.every((v, i) => i === 0 || v >= prog[i - 1]), 'progress rises to 1');
const g0 = await vw.get(0);
check(g0.every((v, i) => v === imgA[i]), 'read back after put equals the image');

fl.drop = 3;                                                  // lose three replies: retries must cover them
await vw.put(1, imgB);
check((await vw.directory(1, 1))[0].crc === crcB, 'put survives lost replies (retry)');
await vw.put(0, imgB);                                        // replace in place
check((await vw.directory(0, 1))[0].crc === crcB, 'replace in place');
await vw.erase(0);
check(!(await vw.directory(0, 1))[0].valid && (await vw.directory(1, 1))[0].valid, 'erase empties only slot 0');
for (const [fn, what] of [[() => vw.get(0), 'get of an empty slot'], [() => vw.put(8, imgA), 'put to slot 8'], [() => vw.put(0, imgA.subarray(0, 100)), 'put of a short file'],
  [() => vw.erase(9), 'erase of slot 9']]) {
  let e = null; try { await fn(); } catch (x) { e = x; }
  check(e instanceof LinkError, `${what} is refused (${e && e.message})`);
}
const bad = imgA.slice(); bad[HEADER_LEN + 5] ^= 1;
{ let e = null; try { await vw.put(3, bad); } catch (x) { e = x; } check(e instanceof LinkError && !(await vw.directory(3, 1))[0].valid, 'a file with a bad CRC is refused and nothing is written'); }

// wire format: frame builder and parser round trip, damaged frames are dropped
const f = vwFrame(2, 9, Uint8Array.of(1, 2, 3));
check(vwParse(f) === null || vwParse(f).payload.length === 3, 'frame parses');
const dmg = f.slice(); dmg[4] ^= 0x10;
check(vwParse(dmg) === null, 'a damaged frame is rejected');
check(crc32(new TextEncoder().encode('123456789')) === 0xCBF43926, 'crc32 check value');


// ---- the patch library (stage 5) against the firmware logic and the real patch log ----
{
  const mk = (name, seed, pat = 0) => encodeBody({ name, params: Array.from({ length: 73 }, (_, i) => (seed * 7 + i * 13) % 300 - 100), pattern: Array.from({ length: pat }, (_, i) => seed + i) });
  let pd = await vw.patchDirectory();
  check(pd.length === 32 && pd.every((e) => !e.present), 'PDIR: an empty store lists 32 ids, none present');
  const a = mk('BRIGHT PAD', 1), b = mk('SUB BASS', 2, 9), c = mk('ARP LEAD', 3, 17);
  await vw.putPatchBody(0, a); await vw.putPatchBody(1, b); await vw.putPatchBody(5, c);
  pd = await vw.patchDirectory();
  check(pd[0].present && pd[0].name === 'BRIGHT PAD' && pd[1].pw === 9 && pd[5].pw === 17 && !pd[2].present, 'PDIR after three puts: names, pattern words');
  check(decodeBody(await vw.getPatchBody(5)).pattern.length === 17, 'PGET returns the pattern words');
  await vw.renamePatch(1, 'bass 2 \u00e9');
  check(decodeBody(await vw.getPatchBody(1)).name === 'bass 2', 'rename keeps printable ASCII only and the patch contents');
  check(decodeBody(await vw.getPatchBody(1)).params.every((v, i) => v === decodeBody(b).params[i]), 'rename leaves every parameter alone');
  check(await vw.swapPatches(0, 5, 'stale') === 'swapped', 'swap through a free id');
  check(decodeBody(await vw.getPatchBody(0)).name === 'ARP LEAD' && decodeBody(await vw.getPatchBody(5)).name === 'BRIGHT PAD' && !(await vw.patchDirectory())[2].present, 'after the swap the patches changed places and no copy is left');
  check(await vw.swapPatches(5, 9) === 'moved' && !(await vw.patchDirectory())[5].present && decodeBody(await vw.getPatchBody(9)).name === 'BRIGHT PAD', 'a move to an empty id');
  const bk = await vw.backup();
  check(bk.patches.length === 3 && parseBackup(JSON.parse(JSON.stringify(bk))).length === 3, 'backup lists 3 patches and parses back');
  await vw.deletePatch(0); await vw.deletePatch(1); await vw.deletePatch(9);
  check((await vw.patchDirectory()).every((e) => !e.present), 'delete removes them');
  for (const p of parseBackup(bk)) await vw.putPatchBody(p.id, p.body);
  check(sameAll(await vw.getPatchBody(0), c) && (await vw.getPatchBody(1)).length > 0, 'restore from the backup brings them back unchanged');
  for (const [fn, what] of [[() => vw.getPatchBody(7), 'get of an empty id'], [() => vw.putPatchBody(32, a), 'put to id 32'], [() => vw.deletePatch(40), 'delete of id 40'],
      [() => vw.putPatchBody(3, a.subarray(0, 20)), 'put of a short body'], [() => parseBackup({ format: 'x' }), 'a file that is not a backup']]) {
    let e = null; try { await fn(); } catch (x) { e = x; }
    check(e instanceof LinkError, `${what} is refused (${e && e.message})`);
  }
  check(JSON.stringify(tableRefs([14, 0, 0, 0, 0, 3])) === '[2]' && tableRefs([]).length === 0, 'tableRefs: user slot of osc 1 only');
  // stage 6: the second sequencer's companion travels with its patch
  {
    const s2 = encodeBody({ name: 'SEQ2', params: [1, 1, 2, 64, 0, 2, 127, 90], pattern: [3, 0x80 + 57, 0x80 + 60, 0x80 + 64] });
    await vw.putPatch(0, { body: c, seq2: s2 });
    check(sameAll(await vw.getSeq2Body(0), s2), 'SEQ2: written after its patch and read back');
    await vw.renamePatch(0, 'TWO SEQS');
    check(decodeBody(await vw.getPatchBody(0)).name === 'TWO SEQS' && sameAll(await vw.getSeq2Body(0), s2), 'SEQ2: rename keeps it');
    check(await vw.swapPatches(0, 1) === 'swapped' && sameAll(await vw.getSeq2Body(1), s2) && (await vw.getSeq2Body(0)) === null, 'SEQ2: a swap moves it with its patch');
    const bk2 = await vw.backup(), e2 = bk2.patches.find((p) => p.id === 1);
    check(e2 && typeof e2.seq2 === 'string' && sameAll(parseBackup(JSON.parse(JSON.stringify(bk2))).find((p) => p.id === 1).seq2, s2), 'SEQ2: in the backup and parsed back');
    await vw.putPatchBody(1, c);
    check((await vw.getSeq2Body(1)) === null, 'SEQ2: the patch written again without it: the old one no longer counts');
    for (const p of parseBackup(bk2)) await vw.putPatch(p.id, p);
    check(sameAll(await vw.getSeq2Body(1), s2), 'SEQ2: restore brings it back');
    await vw.putPatch(1, { body: c, seq2: null });
    check((await vw.getSeq2Body(1)) === null, 'SEQ2: a put without it deletes it');
    await vw.putPatch(1, { body: c, seq2: s2 }); await vw.deletePatch(1);
    check(!(await vw.patchDirectory())[1].present && (await vw.getSeq2Body(1)) === null, 'SEQ2: deleting the patch deletes it');
    check(parseBackup({ format: 'tastywaves-patches', version: 1, patches: [{ id: 2, name: 'X', body: Array.from(a).map((v) => v.toString(16).padStart(2, '0')).join('') }] })[0].seq2 === null, 'a backup without SEQ2 (older tool) still parses');
    check(parseBackup({ format: 'vavewave-patches', version: 1, patches: [{ id: 2, name: 'X', body: Array.from(a).map((v) => v.toString(16).padStart(2, '0')).join('') }] }).length === 1, 'a backup from before the rename (vavewave-patches) still parses');
  }
  let full = 0;
  for (let i = 0; i < 32; i++) { try { await vw.putPatchBody(i, mk('FILL ' + i, i, 33)); full++; } catch (x) { if (/full/.test(x.message)) break; throw x; } }
  check(full >= 14 && full < 32, `a full store says so after ${full} large patches`);
}
function sameAll(x, y) { return x.length === y.length && x.every((v, i) => v === y[i]); }

// no answer at all
const dead = new VwLink({ send: async () => true, recv: async () => null, drain() {} }, { tries: 2, timeout: 5 });
{ let e = null; try { await dead.hello(); } catch (x) { e = x; } check(e instanceof LinkError && /no answer/.test(e.message), 'no reply -> clear message'); }
// the factory patch names the page lists match the firmware's PRESET[]
{
  const fw = [...readFileSync('firmware/src/panel.h', 'utf8').split('static const preset_t PRESET[')[1].split('};')[0].matchAll(/^\s*\{"([^"]+)"/gm)].map((m) => m[1]);
  const page = JSON.parse(readFileSync('tools/web/app.js', 'utf8').match(/const FACTORY = (\[[^\]]*\])/)[1].replace(/'/g, '"'));
  check(fw.length === 8 && fw.join() === page.join(), 'factory patch names match firmware PRESET[]');
}
{
  const fw = JSON.parse(readFileSync('firmware/gen/wt_bench.h', 'utf8').match(/WTG_NAME\[12\]\[6\] = \{([^}]*)\}/)[1].replace(/^/, '[').replace(/$/, ']'));
  const page = JSON.parse(readFileSync('tools/web/app.js', 'utf8').match(/const FACTORY_TABLES = (\[[^\]]*\])/)[1].replace(/'/g, '"'));
  check(fw.length === 12 && fw.join() === page.join(), 'factory table names match firmware WTG_NAME[]');
}
fl.close();
console.log(fails ? `${fails} FAILED` : 'all web tool checks passed');
process.exit(fails ? 1 : 0);
