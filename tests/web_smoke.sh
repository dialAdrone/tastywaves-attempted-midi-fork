#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# tools/web/index.html in headless Chrome with a fake Web MIDI device (HELLO / DIR only): the page must connect, list the
# slots, convert a dropped WAV and draw it without a script error. Skipped when Chrome is not installed.
set -e
cd "$(dirname "$0")/.."
CHROME="${CHROME:-/Applications/Google Chrome.app/Contents/MacOS/Google Chrome}"
[ -x "$CHROME" ] || { echo "skip: no Chrome"; exit 0; }
PY="${PYTHON:-$( [ -x .venv/bin/python ] && echo "$PWD/.venv/bin/python" || echo python3 )}"
D=$(mktemp -d); trap 'kill $SRV 2>/dev/null; rm -rf "$D"' EXIT
for f in app.js convert.js vwlink.js logo.svg fm1ota.js fm1pkg.js firmware; do ln -s "$PWD/tools/web/$f" "$D/$f"; done
"$PY" tests/make_web_inputs.py "$D"
cp tests/web_smoke_shim.js "$D/shim.js"
sed 's|<script type="module" src="app.js"></script>|<script type="module" src="shim.js"></script><script type="module" src="app.js"></script><script type="module" src="auto.js"></script>|' tools/web/index.html > "$D/index.html"
cat > "$D/auto.js" <<'JS'
const $ = (i) => document.getElementById(i), wait = (ms) => new Promise((r) => setTimeout(r, ms));
const out = [];
window.onerror = (m) => out.push('ERROR ' + m);
window.addEventListener('unhandledrejection', (e) => out.push('REJECT ' + e.reason));
await wait(100);
$('btnAccess').click(); await wait(300);
$('btnConnect').click(); await wait(800);
out.push('patches=' + document.querySelectorAll('.prow:not(.fixed)').length, 'patch=' + document.querySelector('.prow:not(.fixed) .pname').textContent + ' / ' + document.querySelector('.prow:not(.fixed) .pinfo').textContent, 'factory=' + document.querySelectorAll('.prow.fixed').length);
out.push('install=' + $('fwVer').textContent + ' ' + !$('btnInstall').disabled + ' ' + $('fwDownload').getAttribute('download'));
out.push('slots=' + document.querySelectorAll('.slot:not(.fixed)').length, 'tables=' + document.querySelectorAll('.slot.fixed').length, 'slot3=' + document.querySelector('#slot3 .name').textContent, 'status=' + $('status').textContent);
const bytes = new Uint8Array(await (await fetch('serum16.wav')).arrayBuffer());
const dt = new DataTransfer(); dt.items.add(new File([bytes], 'Serum Test.wav'));
$('files').files = dt.files; $('files').dispatchEvent(new Event('change'));
for (let i = 0; i < 400 && (!$('report').textContent || $('report').textContent === 'Converting…'); i++) await wait(50);
await wait(200);
out.push('report=' + $('report').textContent.replace(/\n/g, ' | '), 'name=' + $('name').value, 'sendEnabled=' + !$('btnSend').disabled);
const c = $('wave'), px = c.getContext('2d').getImageData(0, 0, c.width, c.height).data; let ink = 0; for (let i = 3; i < px.length; i += 4) if (px[i]) ink++;
out.push('ink=' + (ink > 2000));
const r = document.createElement('pre'); r.id = 'result'; r.textContent = out.join('\n'); document.body.append(r);
JS
(cd "$D" && exec "$PY" -m http.server 8765 --bind 127.0.0.1 >/dev/null 2>&1) & SRV=$!
sleep 1
"$CHROME" --headless=new --disable-gpu --no-sandbox --user-data-dir="$D/profile" --virtual-time-budget=60000 --dump-dom http://127.0.0.1:8765/index.html 2>"$D/chrome.err" > "$D/dom.html" || true; grep -v CVDisplayLink "$D/chrome.err" | head -c 1500 >&2
R=$(sed -n '/<pre id="result">/,/<\/pre>/p' "$D/dom.html" | sed 's/<[^>]*>//g')
echo "$R"
V=$(sed -n 's/.*"version": "\([^"]*\)".*/\1/p' tools/web/firmware/release.json)
echo "$R" | grep -q "install=$V true tastywaves-$V.fwsc" && echo "$R" | grep -q "slots=8" && echo "$R" | grep -q "tables=12" && echo "$R" | grep -q "patches=1" && echo "$R" | grep -q "factory=8" && echo "$R" | grep -q "SHIM PAD / user table slot 2 · 16 step pattern" && echo "$R" | grep -q "slot3=TESTTAB" && echo "$R" | grep -q "sendEnabled=true" && echo "$R" | grep -q "ink=true" && ! echo "$R" | grep -q "ERROR\|REJECT" && echo "web smoke ok" || { echo "web smoke FAILED"; exit 1; }
