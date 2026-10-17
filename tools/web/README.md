# VAVEWave Tables (browser)

Sends wavetables to the 8 user slots of an FM-1 running VAVEWave. Same protocol and the same converter as
`tools/fm1_wavetable.py` (the converter output is byte-identical to the Python tool's; `tests/run_webtool.sh` checks it).

Run it (Chrome or Edge; Safari has no Web MIDI; Chrome blocks ES modules from `file://`, so serve the folder):

    .venv/bin/python -m http.server 8000 --bind 127.0.0.1 --directory tools/web
    # open http://localhost:8000 , press "Connect to MIDI" and allow SysEx

Then pick the FM-1 port, Connect FM-1, drop files (WAV from Serum / WaveEdit / Ableton, Surge `.wt`, or a `.vwt`
made earlier), check the preview, pick the slot, Send. Keys up and no sound while it writes; a cut cable or power
loss leaves the slot empty. Sending into a used slot replaces it; use Save on that slot first for a `.vwt` backup.

Nothing is sent anywhere but to the MIDI port. No tables are shipped with this page.
