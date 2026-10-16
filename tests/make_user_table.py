# SPDX-License-Identifier: GPL-3.0-only
"""Writes a small user table file (.vwt) for the host engine test (tests/run_engine.sh): every frame is the 2nd harmonic
alone (a sine at twice the played pitch), so a user table is told from table 0 (a sine at the played pitch) by pitch.
    .venv/bin/python -I tests/make_user_table.py OUT.vwt NAME"""
import struct
import sys
import tempfile
import wave
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / 'tools'))
import fm1_wavetable as wt  # noqa: E402

out, name = sys.argv[1], sys.argv[2]
x = np.tile(np.sin(2 * np.pi * 2 * np.arange(2048) / 2048) * 0.5, 32)
with tempfile.TemporaryDirectory() as td:
    w = Path(td) / 'in.wav'
    with wave.open(str(w), 'wb') as f:
        f.setnchannels(1); f.setsampwidth(2); f.setframerate(44100)
        f.writeframes(np.round(x * 32767).astype('<i2').tobytes())
    sys.exit(wt.main(['build', name, out, str(w), '--frame', '2048']))
