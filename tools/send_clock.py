#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Send MIDI clock (24 per quarter note) to the FM-1, to try the arp's external clock without a DAW.
   .venv/bin/python tools/send_clock.py [BPM] [seconds]     (default 120 BPM, 10 s; Ctrl-C stops)
The synth shows up as the MIDI port "Felucca" (the USB name of the platform it is built on)."""
import sys, time
import mido

bpm = float(sys.argv[1]) if len(sys.argv) > 1 else 120.0
secs = float(sys.argv[2]) if len(sys.argv) > 2 else 10.0
names = [n for n in mido.get_output_names() if 'felucca' in n.lower() or 'fm-1' in n.lower() or 'vave' in n.lower()]
if not names:
    sys.exit('no FM-1 MIDI output found; ports: %s' % mido.get_output_names())
out = mido.open_output(names[0])
print('clock to %s at %g BPM for %g s' % (names[0], bpm, secs))
period = 60.0 / bpm / 24.0
out.send(mido.Message('start'))
t0 = nxt = time.perf_counter()
try:
    while time.perf_counter() - t0 < secs:
        out.send(mido.Message('clock'))
        nxt += period
        while time.perf_counter() < nxt:
            pass
except KeyboardInterrupt:
    pass
out.send(mido.Message('stop'))
