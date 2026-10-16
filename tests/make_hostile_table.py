# SPDX-License-Identifier: GPL-3.0-only
"""Writes a table file whose 35,648 data bytes are full-scale int16 noise and square steps (not a band-limited table at all):
what a hand-made or damaged-but-CRC-valid slot could hold. For the host engine test (tests/run_engine.sh): the engine must
stay inside the table and inside int32 arithmetic whatever the samples are.
    .venv/bin/python -I tests/make_hostile_table.py OUT.vwt"""
import sys

import numpy as np

rng = np.random.default_rng(7)
n = 35648 // 2
x = rng.integers(-32768, 32768, n).astype('<i2')
x[::3] = 32767
x[1::3] = -32768
open(sys.argv[1], 'wb').write(b'VWT1' + bytes(60) + x.tobytes())
