# TastyWaves Tables (browser)

Sends wavetables to the 8 user slots of an FM-1 running TastyWaves. Same protocol and the same converter as
`tools/fm1_wavetable.py` (the converter output is byte-identical to the Python tool's; `tests/run_webtool.sh` checks it).

Run it (Chrome or Edge; Safari has no Web MIDI; Chrome blocks ES modules from `file://`, so serve the folder):

    .venv/bin/python -m http.server 8000 --bind 127.0.0.1 --directory tools/web
    # open http://localhost:8000 , press "Connect to MIDI" and allow SysEx

Then pick the FM-1 port, Connect FM-1, drop files (WAV from Serum / WaveEdit / Ableton, Surge `.wt`, or a `.vwt`
made earlier), check the preview, pick the slot, Send. Keys up and no sound while it writes; a cut cable or power
loss leaves the slot empty. Sending into a used slot replaces it; use Save on that slot first for a `.vwt` backup.

Nothing is sent anywhere but to the MIDI port. No tables are shipped with this page.

## Patches

With firmware stage 5 the page also lists the user patches (U01..U32): rename, move up or down (the order of the PRESETS list),
delete, **Backup all** (a JSON file with every parameter and pattern) and **Restore from a file**. Restore puts each patch back
under its own number and keeps the others. With firmware stage 6 (TastyWaves 1.0) the second sequencer (SEQ2: its settings and pattern) travels with each patch through all of these. A patch refers to a user table by slot number; back the tables up with Save on each slot.

## Install button and GitHub Pages

The **Install TastyWaves** card puts the release bundled in `firmware/` (the package and `release.json`) on the FM-1,
over the FM-1's own USB update, with Felucca's installer: `fm1ota.js` and `fm1pkg.js` are Felucca's `web/` files at
commit 1c0798e, unchanged (GPL-3.0, Leo Kuroshita / Hügelton Instruments). Without `firmware/release.json` the card hides.

A release: `./build.sh --release X.Y.Z`, then `tools/web_release.sh X.Y.Z` (copies the package, writes `release.json`),
commit and push. `.github/workflows/pages.yml` publishes this folder as https://tastysynth.com/tool/ (with the landing page,
`site/`, at the root: `tools/site_build.sh`).
