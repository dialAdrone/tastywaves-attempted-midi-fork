/* SPDX-License-Identifier: GPL-3.0-only */
/* The user wavetable slots as the engine and the panel see them. The writer and the computer link are wtlink.h (device
 * only); on a computer (host mode) tests/host.c can fill these in. Table index 0..11 = the factory tables, 12..19 = user
 * slot 0..7 (UT_FIRST + k). */
#pragma once
#include <stdint.h>

#define UT_SLOTS 8u
#define UT_FIRST 12u
#define UT_NTABLES (UT_FIRST + UT_SLOTS)

enum { UT_UNCHECKED = 0, UT_OK = 1, UT_BAD = 2 };   /* data CRC32 not checked yet / matches the header / does not */

typedef struct {
    uint8_t valid;                              /* the header passed every check (an empty slot is valid = 0) */
    uint8_t state;                              /* UT_*: only UT_OK tables are ever read by the engine */
    uint8_t frames, levels;
    char name[9];                               /* printable ASCII, NUL ended */
    uint32_t bytes, crc;                        /* data size, CRC32 of the data (from the header) */
} wt_ent_t;

static wt_ent_t wt_dir[UT_SLOTS];
/* The engine's view: sample 0 of frame 0 level 0 minus the guards, in the engine's own layout (firmware/gen/wt_bench.h:
 * 32 frames x 557 int16), read in place through the flash window. Set (by the main loop) only for a slot whose data CRC32
 * matched, 0 otherwise: the engine then plays table 0 (never silence, never a crash). Cleared before a slot is touched. */
static const int16_t *volatile eng_user_base[UT_SLOTS];
/* Counts every change of what a user table index plays (a slot checked, dropped, replaced): the app repaints the table
 * pictures when it moves, else a table drawn as the fallback table 0 (before its CRC check) would stay on screen. */
static volatile uint32_t wt_gen;
