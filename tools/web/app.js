// SPDX-License-Identifier: GPL-3.0-only
// The page of tools/web/index.html: Web MIDI transport, slot list, file drop, conversion preview, send / save / erase.
import { VwLink, LinkError, slotImage, parseImage, cleanName, decodeBody, tableRefs, parseBackup, cleanPatchName } from './vwlink.js';
import { buildTable, naturalCompare } from './convert.js';

const $ = (id) => document.getElementById(id);
const PORT_RE = /fm-1|felucca|ota|composite|sinco|usb-midi/i;     // the same ports the installer would look at
let access = null, vw = null, dir = [], busy = false, fw = null;
let files = [], built = null, cache = new Map();                                     // built: {img, name, raw, report}

function status(text, cls = 'mute') { const s = $('status'); s.textContent = text; s.className = cls; }
function setBusy(b) { busy = b; for (const el of document.querySelectorAll('button')) el.disabled = b || (el.dataset.need === 'built' && !built); $('bar').classList.toggle('hide', !b); }

// ---- Web MIDI transport for VwLink ----
class MidiTransport {
  constructor(out, inp) { this.out = out; this.q = []; this.waiter = null; inp.onmidimessage = (e) => { if (e.data[0] === 0xF0) this.push(e.data); }; this.inp = inp; }
  push(d) { if (this.waiter) { const w = this.waiter; this.waiter = null; clearTimeout(w.t); w.r(d); } else this.q.push(d); }
  async send(pkt) { try { this.out.send(pkt); return true; } catch (e) { return false; } }
  recv(ms) {
    if (this.q.length) return Promise.resolve(this.q.shift());
    return new Promise((r) => { this.waiter = { r, t: setTimeout(() => { this.waiter = null; r(null); }, ms) }; });
  }
  drain() { this.q.length = 0; }
}

async function connectMidi() {
  if (!navigator.requestMIDIAccess) { status('This browser has no Web MIDI. Use Chrome or Edge.', 'bad'); return; }
  try { access = await navigator.requestMIDIAccess({ sysex: true }); }
  catch (e) { status('MIDI access with SysEx was refused: ' + e.message + ' (allow it in the browser\'s site settings).', 'bad'); return; }
  access.onstatechange = fillPorts;
  fillPorts();
  $('portLab').classList.remove('hide'); $('btnConnect').classList.remove('hide');
  status('Pick the FM-1 port and press Connect FM-1.');
}
function fillPorts() {
  const sel = $('port'), old = sel.value, outs = [...access.outputs.values()];
  sel.replaceChildren(...outs.map((o) => new Option(o.name, o.id)));
  const want = outs.find((o) => o.id === old) || outs.find((o) => PORT_RE.test(o.name));
  if (want) sel.value = want.id;
  if (!outs.length) status('No MIDI output found. Is the FM-1 plugged in?', 'warn');
}

async function connectFm1() {
  const out = access.outputs.get($('port').value);
  if (!out) { status('No port selected.', 'bad'); return; }
  const inp = [...access.inputs.values()].find((i) => i.name === out.name);
  if (!inp) { status('No matching MIDI input for ' + out.name, 'bad'); return; }
  vw = new VwLink(new MidiTransport(out, inp));
  setBusy(true);
  try {
    status('Asking the FM-1…');
    fw = await vw.hello();
    $('main').classList.remove('hide');
    await refresh();
    await refreshPatches();
    status(`Connected: ${fw.id}, VW link v${fw.proto}, firmware stage ${fw.stage}.`);
  } catch (e) { vw = null; $('main').classList.add('hide'); status(e.message, 'bad'); }
  setBusy(false);
}

async function refresh() {
  dir = await vw.directory(0, fw.slots);
  const box = $('slots'), tgt = $('target'), keep = tgt.value;
  box.replaceChildren(...dir.map((e) => {
    const d = document.createElement('div'); d.className = 'card slot'; d.id = 'slot' + e.slot;
    d.innerHTML = `<h3>SLOT ${e.slot}</h3><div class="name${e.valid ? '' : ' empty'}"></div><div class="crc"></div><div class="row"></div>`;
    d.querySelector('.name').textContent = e.valid ? e.name : 'empty';
    d.querySelector('.crc').textContent = e.valid ? `crc ${e.crc.toString(16).padStart(8, '0')}` : ' ';
    if (e.valid) {
      const s = Object.assign(document.createElement('button'), { textContent: 'Save…' }); s.onclick = () => saveSlot(e.slot);
      const x = Object.assign(document.createElement('button'), { textContent: 'Erase' }); x.onclick = () => eraseSlot(e.slot);
      d.querySelector('.row').append(s, x);
    }
    return d;
  }));
  tgt.replaceChildren(...dir.map((e) => new Option(`${e.slot}: ${e.valid ? e.name : 'empty'}`, e.slot)));
  tgt.value = keep || (dir.find((e) => !e.valid) || dir[0]).slot;
  $('fw').textContent = `${dir.filter((e) => e.valid).length} of ${dir.length} slots in use`;
}

// ---- the patch library (VW link stage 5): list, rename, reorder, delete, backup, restore ----
let patches = [], pdir = [];
// the factory patches of the PRESETS list, in order (firmware/src/panel.h PRESET[]): built in, not on the link
const FACTORY = ['BRIGHT PAD', 'GLASS BELL', 'WARM VOWEL', 'FOLD LEAD', 'SUB BASS', 'ORGAN KEYS', 'FM PLUCK', 'REED SOLO'];
async function refreshPatches() {
  const box = $('patches');
  $('patchSec').classList.toggle('hide', fw.stage < 5);
  if (fw.stage < 5) return;
  const fixed = FACTORY.map((n, k) => {
    const d = document.createElement('div'); d.className = 'card prow fixed';
    d.innerHTML = '<span class="pid"></span><span class="pname"></span><span class="pinfo mute">factory patch, cannot be changed</span><span class="row"></span>';
    d.querySelector('.pid').textContent = String(k + 1);
    d.querySelector('.pname').textContent = n;
    return d;
  });
  pdir = await vw.patchDirectory();
  patches = [];
  for (const e of pdir) if (e.present) {
    try {
      const { body, seq2 } = await vw.getPatch(e.id);
      patches.push({ id: e.id, name: e.name, ...decodeBody(body), seq2: seq2 ? decodeBody(seq2) : null });
    } catch (x) { patches.push({ id: e.id, name: e.name || '(damaged)', params: [], pattern: [], seq2: null, damaged: true }); }
  }
  const rows = patches.map((p, k) => {
    const d = document.createElement('div'); d.className = 'card prow';
    const tr = tableRefs(p.params), steps = p.pattern.length ? p.pattern[0] : 0, steps2 = p.seq2 && p.seq2.pattern.length ? p.seq2.pattern[0] : 0;
    const pat = steps && steps2 ? `SEQ1 ${steps} + SEQ2 ${steps2} steps` : steps ? `${steps} step pattern` : steps2 ? `SEQ2 ${steps2} steps` : '';
    const info = p.damaged ? 'damaged: delete it' : [tr.length ? 'user table slot ' + tr.join(', ') : 'factory tables', pat].filter(Boolean).join(' · ');
    d.innerHTML = '<span class="pid"></span><span class="pname"></span><span class="pinfo mute"></span><span class="row"></span>';
    d.querySelector('.pid').textContent = 'U' + String(p.id + 1).padStart(2, '0');
    d.querySelector('.pname').textContent = p.name;
    d.querySelector('.pinfo').textContent = info;
    const mkb = (t, f, off) => { const b = Object.assign(document.createElement('button'), { textContent: t }); b.onclick = f; b.disabled = !!off; return b; };
    d.querySelector('.row').append(mkb('Rename…', () => renamePatch(p)), mkb('▲', () => movePatch(k, -1), k === 0), mkb('▼', () => movePatch(k, 1), k === patches.length - 1), mkb('Delete', () => deletePatch(p)));
    return d;
  });
  box.replaceChildren(...fixed, ...(rows.length ? rows : [Object.assign(document.createElement('div'), { className: 'mute', textContent: 'No user patches saved yet (SAVE on the FM-1 makes one).' })]));     // one list, in PRESETS order
  $('pcount').textContent = `${patches.length} of 32 patches saved`;
}
async function renamePatch(p) {
  const n = prompt('New name (up to 12 characters, plain ASCII)', p.name);
  if (n === null) return;
  await guarded('Rename', async () => { await vw.renamePatch(p.id, cleanPatchName(n)); await refreshPatches(); status(`U${String(p.id + 1).padStart(2, '0')} is now "${cleanPatchName(n)}".`); });
}
async function movePatch(k, dir) {
  const a = patches[k], b = patches[k + dir];
  if (!b) return;
  await guarded('Move', async () => {
    const how = await vw.swapPatches(a.id, b.id);
    await refreshPatches();
    status(`"${a.name}" and "${b.name}" changed places.`);
  });
}
async function deletePatch(p) {
  if (!confirm(`Delete patch U${String(p.id + 1).padStart(2, '0')} "${p.name}" from the FM-1?\nUse Backup first if you may want it again.`)) return;
  await guarded('Delete', async () => { await vw.deletePatch(p.id); await refreshPatches(); status(`Patch "${p.name}" deleted.`); });
}
async function backupPatches() {
  await guarded('Backup', async () => {
    status('Reading the patches…');
    const bk = await vw.backup();
    download(`vavewave-patches-${new Date().toISOString().slice(0, 10)}.json`, JSON.stringify(bk));
    status(`${bk.patches.length} patches saved to a file.`);
  });
}
async function restorePatches(file) {
  if (!file) return;
  let list;
  try { list = parseBackup(JSON.parse(await file.text())); } catch (e) { status('Restore: ' + (e instanceof LinkError ? e.message : 'the file is not a patch backup'), 'bad'); return; }
  pdir = await vw.patchDirectory();                       // fresh, for the replace warning
  const clash = list.filter((p) => pdir[p.id] && pdir[p.id].present);
  if (!confirm(`Restore ${list.length} patches from "${file.name}"?` + (clash.length ? `\n${clash.length} of them REPLACE patches now on the FM-1 (same number): ${clash.map((p) => 'U' + String(p.id + 1).padStart(2, '0')).join(' ')}. Patches not in the file are kept.` : '') +
    '\nKeep the FM-1 quiet (no keys, no arp or latch) and plugged in while it runs.')) return;
  await guarded('Restore', async () => {
    let k = 0;
    for (const p of list) { status(`Writing ${p.name}…`); await vw.putPatch(p.id, p); progress(++k / list.length); }
    await refreshPatches();
    status(`${list.length} patches restored and read back.`);
  });
}

function progress(f) { $('bar').value = f; }
function download(name, bytes) {
  const a = Object.assign(document.createElement('a'), { href: URL.createObjectURL(new Blob([bytes])), download: name });
  document.body.append(a); a.click(); a.remove(); setTimeout(() => URL.revokeObjectURL(a.href), 1000);
}

async function guarded(label, fn) {
  setBusy(true); progress(0);
  try { await fn(); } catch (e) { status(`${label}: ${e instanceof LinkError ? e.message : e}`, 'bad'); try { await refresh(); } catch (_) { /* link is gone */ } setBusy(false); return; }
  setBusy(false);
}

async function saveSlot(n) {
  await guarded('Save', async () => {
    status(`Reading slot ${n}…`);
    const img = await vw.get(n, progress), t = parseImage(img);
    download(`${t.name.trim() || 'table'}.vwt`, img);
    status(`Slot ${n} (${t.name}) saved as a .vwt file (checked against its CRC).`, 'mute');
  });
}
async function eraseSlot(n) {
  const e = dir[n];
  if (!confirm(`Empty slot ${n} (${e.name}, crc ${e.crc.toString(16)})?\nUse Save first if you may want it again.`)) return;
  await guarded('Erase', async () => { await vw.erase(n); await refresh(); status(`Slot ${n} emptied.`); });
}
async function sendTable() {
  const n = +$('target').value, cur = dir[n];
  const what = cur.valid ? `This REPLACES "${cur.name}" (crc ${cur.crc.toString(16)}) in slot ${n}. Use Save on that slot first if you may want it.` : `It goes into the empty slot ${n}.`;
  if (!confirm(`Send "${built.name}" to the FM-1?\n${what}\nKeys up and no sound; do not unplug the FM-1 while it runs (about 5 seconds).`)) return;
  await guarded('Send', async () => {
    status(`Writing slot ${n}… do not unplug.`);
    const img = slotImage($('name').value || built.name, built.raw);
    const t = await vw.put(n, img, progress);
    await refresh();
    status(`Slot ${n}: "${t.name}" written, read back and checked (crc ${t.crc.toString(16).padStart(8, '0')}).`);
  });
}

// ---- files and conversion ----
async function loadFiles(list) {
  const arr = [...list].sort((a, b) => naturalCompare(a.name, b.name));
  if (!arr.length) return;
  files = []; cache = new Map();
  for (const f of arr) files.push({ name: f.name, bytes: new Uint8Array(await f.arrayBuffer()) });
  const vwt = files.length === 1 && /\.vwt$/i.test(files[0].name);
  for (const id of ['frame', 'format', 'pick']) $(id).disabled = vwt;
  $('name').value = cleanName(files[0].name.replace(/\.[^.]*$/, ''));
  $('conv').classList.remove('hide');
  await rebuild();
}
let building = 0;
async function rebuild() {
  const my = ++building;
  built = null; $('btnSend').disabled = $('btnSave').disabled = true;
  $('report').textContent = 'Converting…';
  await new Promise((r) => setTimeout(r, 20));
  try {
    if (files.length === 1 && /\.vwt$/i.test(files[0].name)) {
      const t = parseImage(files[0].bytes);
      built = { name: t.name, raw: t.data.slice(), report: [`${files[0].name}: a table file, ${t.frames} frames, crc ${t.crc.toString(16).padStart(8, '0')} (checked)`] };
    } else {
      const frame = +$('frame').value;
      const r = buildTable(files, { frame, format: $('format').value, pick: $('pick').value }, cache);
      built = { name: $('name').value, raw: r.raw, report: [...r.report, `${r.total} frame(s)` + (r.kept !== r.total ? `, ${r.kept} kept` : '') + ' → 32, gain ' + r.gain.toFixed(3)] };
    }
    if (my !== building) return;
    $('report').textContent = built.report.join('\n');
    $('report').className = built.report.some((l) => l.includes('ASSUMED')) ? 'warn' : '';
    draw(built.raw);
  } catch (e) {
    $('report').textContent = e.message; $('report').className = 'bad';
    const c = $('wave'); c.getContext('2d').clearRect(0, 0, c.width, c.height);
  }
  setBusy(false);
}
function draw(raw) {                                  // 32 frames, level 0 (256 samples after 3 guards), stacked
  const s = new Int16Array(raw.buffer.slice(raw.byteOffset, raw.byteOffset + raw.length)), FLEN = 557;
  const c = $('wave'), g = c.getContext('2d'), W = c.width, H = c.height;
  g.clearRect(0, 0, W, H);
  const css = getComputedStyle(document.body), fg = css.color;
  g.strokeStyle = fg; g.lineWidth = 2;
  for (let f = 31; f >= 0; f--) {
    const y0 = 70 + f * (H - 90) / 31, a = 0.25 + 0.75 * (1 - f / 31);
    g.globalAlpha = a; g.beginPath();
    for (let i = 0; i <= 256; i++) {
      const v = s[f * FLEN + 3 + (i % 256)] / 32768, x = 20 + i * (W - 40) / 256, y = y0 - v * 55;
      i ? g.lineTo(x, y) : g.moveTo(x, y);
    }
    g.stroke();
  }
  g.globalAlpha = 1;
}

// ---- wiring ----
$('btnAccess').onclick = connectMidi;
$('btnConnect').onclick = connectFm1;
$('btnRefresh').onclick = () => guarded('Refresh', async () => { await refresh(); await refreshPatches(); status('Refreshed.'); });
$('btnBackup').onclick = backupPatches;
$('btnRestore').onclick = () => $('restoreFile').click();
$('restoreFile').onchange = (e) => { restorePatches(e.target.files[0]); e.target.value = ''; };
$('btnFiles').onclick = () => $('files').click();
$('files').onchange = (e) => loadFiles(e.target.files);
$('btnSend').dataset.need = $('btnSave').dataset.need = 'built';
$('btnSend').onclick = sendTable;
$('btnSave').onclick = () => { const img = slotImage($('name').value || built.name, built.raw); download(`${cleanName($('name').value || built.name)}.vwt`, img); };
for (const id of ['frame', 'format', 'pick']) $(id).onchange = rebuild;
$('name').oninput = () => { if (built) built.name = $('name').value; };
const drop = $('drop');
drop.ondragover = (e) => { e.preventDefault(); drop.classList.add('over'); };
drop.ondragleave = () => drop.classList.remove('over');
drop.ondrop = (e) => { e.preventDefault(); drop.classList.remove('over'); loadFiles(e.dataTransfer.files); };
status('Use Chrome or Edge (Safari has no Web MIDI). Plug the FM-1 in by USB with VAVEWave running.');
