/* SPDX-License-Identifier: GPL-3.0-only */
/* User wavetable slots, "VW link" v1 (docs/UPLOAD-DESIGN.md). STAGE 3: uploads into slots 0..7 (BEGIN / DATA / COMMIT / ABORT / ERASE), SELFTEST of one spare sector.
 *
 * What this file does: finds out which of the 8 slots hold a valid header (a RAM directory, made once on the first
 * main-loop pass) and answers HELLO, DIR and READ from the computer. The engine does not use the slots yet.
 * Stage 1 added SELFTEST (cmd 10). Stage 2 adds the upload: BEGIN erases the slot's header sector (the slot reads as
 * empty from then on), DATA writes 256 B chunks in order (a sector is erased when its first chunk arrives, every chunk
 * is read back and compared), COMMIT checks the CRC32 of everything stored and writes the header LAST. A power cut,
 * cable pull, timeout (15 s without a request) or any error therefore leaves an empty slot, never a half table.
 * wt_flash_erase / wt_flash_prog are the ONLY calls to the flash writer in this file. They are limited to the spare
 * sector WT_TEST_AT and the writable slots (WT_W_LO..WT_W_HI, the 8 slots) by their own check and again by
 * FL_RANGE_OK (platform.c). A later stage widens the window to the slot region and nowhere else.
 * The app's loop calls wt_ui_poll(): while an upload runs it owns the screen and the app ignores keys, knobs, MIDI.
 *
 * Wire (SysEx on the USB-MIDI port, 7D = the non-commercial manufacturer id):
 *   request  F0 pack7( 7D 56 57 cmd seq len_lo len_hi payload[len] sum ) F7
 *   reply    F0 pack7( 7D 56 57 cmd rc seq len_lo len_hi payload[len] sum ) F7
 *   sum = ~(sum of every byte from cmd up to the one before sum) & 0xFF. A request with a bad sum or length is
 *   dropped without a reply (the tool retries). Frames of the stock updater (00 59 ...) are left for ota_service().
 *
 * Needs (platform.c supplies them before this file): ota_pack7 / ota_unpack7 / ota_wire_send / ota_frame_get /
 * ota_frame_done / ota_dec (ota.c), st_read, flash_ok, crc32, FELUCCA_ID. */
#include "usertab.h"                             /* wt_ent_t, wt_dir, eng_user_base, UT_* (shared with the engine and the panel) */
#define WT_SLOTS UT_SLOTS
#define WT_BASE 0x00097000u                      /* slot k: WT_BASE + k * WT_SLOT_SIZE */
#define WT_SLOT_SIZE 0x9000u                     /* 1 header sector + 8 data sectors */
#define WT_HDR_LEN 64u
#define WT_HDR_AT 0x8F00u                        /* the header: last page of the slot's last sector (layout v2) */
#define WT_HDR_SECTOR 0x8000u                    /* ... and that sector, erased at BEGIN: the slot is empty from then on */
#define WT_FRAMES_N 32u
#define WT_LEVELS_N 7u                           /* fmt 1: the engine's native layout, 7 mip levels with guards */
#define WT_DATA_BYTES 35648u                     /* 32 frames x 557 int16 (firmware/gen/wt_bench.h layout), at slot offset 0 */
#define WT_DATA_CHUNKS 140u                      /* of 256 B, the last one 64 B */
#define WT_STAGE 6u                              /* 0 read-only, 1 test sector, 2.. writer (docs/UPLOAD-DESIGN.md 8.2) */

enum { WT_CMD_HELLO = 1, WT_CMD_DIR = 2, WT_CMD_BEGIN = 3, WT_CMD_DATA = 4, WT_CMD_COMMIT = 5, WT_CMD_ABORT = 6,
       WT_CMD_ERASE = 7, WT_CMD_READ = 8, WT_CMD_AUDITION = 9, WT_CMD_SELFTEST = 10, WT_CMD_XIPCHECK = 11,
       WT_CMD_PDIR = 12, WT_CMD_PGET = 13, WT_CMD_PPUT = 14, WT_CMD_PDEL = 15 };      /* 12..15: the patch commands (patchlink.h), stage 5; stage 6: patch companions (ids 32..63) */
enum { WT_OK = 0, WT_E_ARGS = 1, WT_E_RANGE = 2, WT_E_BUSY = 7, WT_E_NOFLASH = 9 };

static uint8_t wt_scanned;

static uint32_t wt_slot_at(uint32_t k) { return WT_BASE + k * WT_SLOT_SIZE; }

/* Header sector content -> entry. Anything that does not check out is an empty slot, never a crash, never trusted.
 * Layout (little endian): "VWT1", u16 version = 2, u8 fmt = 1, u8 frames = 32, u8 levels = 7, u8 0, char name[8], u32 data
 * bytes = 35648, u32 data CRC32, u32 CRC32 of bytes 0..25. */
static void wt_parse(const uint8_t *h, wt_ent_t *e)
{
    uint32_t i, bytes;
    e->valid = 0;
    e->state = UT_UNCHECKED;
    e->frames = e->levels = 0;
    for (i = 0; i < sizeof e->name; i++)
        e->name[i] = 0;                          /* the whole name: no bytes of an older one survive a rescan */
    e->bytes = e->crc = 0;
    if (h[0] != 'V' || h[1] != 'W' || h[2] != 'T' || h[3] != '1')
        return;
    if (ota_rd16(h + 4) != 2u || h[6] != 1u)     /* layout version 2, fmt 1 */
        return;
    if (h[7] != WT_FRAMES_N || h[8] != WT_LEVELS_N)   /* 32 frames, 7 mip levels */
        return;
    bytes = ota_rd32(h + 18);
    if (bytes != WT_DATA_BYTES)
        return;
    if (crc32(h, 26) != ota_rd32(h + 26))
        return;
    e->frames = h[7];
    e->levels = h[8];
    e->bytes = bytes;
    e->crc = ota_rd32(h + 22);
    for (i = 0; i < 8u && h[10 + i]; i++)
        e->name[i] = h[10 + i] >= 0x20u && h[10 + i] < 0x7Fu ? (char)h[10 + i] : '?';
    e->name[i] = 0;
    e->valid = 1;
}

/* flash -> dst in pieces of 64 B: the interrupts are off for each piece (about 0.2 ms), not for 256 */
static int wt_read(uint32_t off, uint8_t *dst, uint32_t n)
{
    while (n) {
        uint32_t k = n > 64u ? 64u : n;
        int rc = st_read(off, dst, k);
        if (rc)
            return rc;
        off += k;
        dst += k;
        n -= k;
    }
    return 0;
}

static void wt_scan(void)
{
    uint8_t h[WT_HDR_LEN];
    uint32_t k;
    for (k = 0; k < WT_SLOTS; k++) {
        eng_user_base[k] = 0;
        wt_dir[k].valid = 0;
        if (flash_ok && !wt_read(wt_slot_at(k) + WT_HDR_AT, h, WT_HDR_LEN))
            wt_parse(h, &wt_dir[k]);
        fm1_wdt_feed();
    }
}

static int quiet(void);                          /* the app: nothing sounds (engine.h) */

/* ---- the one flash writer. Window: the spare sector and the writable slots (WT_WRITABLE slots from slot 0). */
#define WT_TEST_LEN 0x1000u
#define WT_WRITABLE 8u                           /* slots 0 .. WT_WRITABLE - 1 may be written (stage 3: all 8) */
typedef char wt_window_matches[(WT_W_HI - WT_W_LO == WT_WRITABLE * WT_SLOT_SIZE && WT_W_LO == WT_BASE) ? 1 : -1];
static int wt_flash_ok(uint32_t off, uint32_t n)
{
    return (off >= WT_TEST_AT && off <= WT_TEST_AT + WT_TEST_LEN && n <= WT_TEST_AT + WT_TEST_LEN - off) ||
           (off >= WT_W_LO && off <= WT_W_HI && n <= WT_W_HI - off);
}
static int wt_flash_erase(uint32_t off, uint32_t *us)       /* one 4 KiB sector, 0 = ok; *us = how long it took */
{
    uint32_t took = 0;
    int rc;
    *us = 0;
    if (!flash_ok || (off & 0xFFFu) || !wt_flash_ok(off, 0x1000u))
        return -8;
    rc = fl_erase4k(off, &took);                /* took: how long the part was busy, from the driver */
    *us = took;
    return rc;
}
static int wt_flash_prog(uint32_t off, const uint8_t *src_ram, uint32_t n)   /* one page of at most 256 B */
{
    if (!flash_ok || n == 0u || n > 256u || !wt_flash_ok(off, n))
        return -8;
    return fl_write(off, src_ram, n);
}

/* SELFTEST: erase the spare sector, check it reads 0xFF, program 16 pages of a pattern, read back and compare,
 * erase again and check. result: 0 pass, 3 erase failed, 4 program failed, 5 verify failed, 7 refused (keys down,
 * sound, no flash). Times in microseconds. */
static uint8_t wt_page[256];
static uint32_t wt_mismatch(uint32_t off, uint32_t n, int pattern)   /* bytes of [off, off + n) that differ from 0xFF or the pattern */
{
    uint8_t b[64];
    uint32_t i, k, bad = 0;
    for (k = 0; k < n; k += 64u) {
        if (wt_read(off + k, b, 64u))
            return n;
        for (i = 0; i < 64u; i++) {
            uint32_t at = k + i;
            uint8_t want = pattern ? (uint8_t)(at * 7u + (at >> 8) * 31u + 0x5Au) : 0xFFu;
            bad += b[i] != want;
        }
        fm1_wdt_feed();
    }
    return bad;
}
static uint32_t wt_selftest(uint8_t *o)         /* -> payload length (18) */
{
    uint32_t e1 = 0, e2 = 0, prog = 0, bad = 0, p, i, t0;
    int rc, detail = 0, result = 0;
    if (!flash_ok || !quiet() || fm1_in.notes) {
        o[0] = 7; o[1] = 0;
        for (i = 2; i < 18u; i++) o[i] = 0;
        return 18;
    }
    if ((rc = wt_flash_erase(WT_TEST_AT, &e1)) != 0) { result = 3; detail = rc; }
    else if ((bad = wt_mismatch(WT_TEST_AT, WT_TEST_LEN, 0)) != 0) result = 3;
    else {
        t0 = fm1_ticks();
        for (p = 0; p < 16u && !result; p++) {
            for (i = 0; i < 256u; i++)
                wt_page[i] = (uint8_t)((p * 256u + i) * 7u + p * 31u + 0x5Au);
            if ((rc = wt_flash_prog(WT_TEST_AT + p * 256u, wt_page, 256u)) != 0) { result = 4; detail = rc; }
            fm1_wdt_feed();
        }
        prog = (fm1_ticks() - t0) / FM1_TICKS_PER_US;
        if (!result && (bad = wt_mismatch(WT_TEST_AT, WT_TEST_LEN, 1)) != 0) result = 5;
        if (!result) {
            if ((rc = wt_flash_erase(WT_TEST_AT, &e2)) != 0) { result = 3; detail = rc; }
            else if ((bad = wt_mismatch(WT_TEST_AT, WT_TEST_LEN, 0)) != 0) result = 3;
        }
    }
    o[0] = (uint8_t)result;
    o[1] = (uint8_t)detail;
    ota_wr32(o + 2, e1);
    ota_wr32(o + 6, prog);
    ota_wr32(o + 10, e2);
    ota_wr32(o + 14, bad);
    return 18;
}

static void wt_xip_init(void);
static void wt_drop(uint32_t k)                 /* the engine must not read slot k from here on (before it is touched) */
{
    eng_user_base[k] = 0;
    wt_dir[k].state = UT_UNCHECKED;
    wt_gen++;
}

/* ---- stage 2: the upload session (one at a time) ---- */
#define WT_SESSION_MS 15000u                     /* no request for this long: the upload is dropped, the slot stays empty */
#define WT_SHOW_MS 1500u
enum { WT_R_NONE = 0, WT_R_OK = 1, WT_R_ABORT = 10 };   /* wt.result otherwise: the rc (3 erase, 4 program, 5 verify, 6 timeout, 8 CRC) */
static struct {
    uint8_t busy, slot, frames, result, shown_pct, redraw;
    uint32_t bytes, crc, next, t_last, show_until, t_draw;
    char name[9];
} wt;

static uint32_t wt_crc_upd(uint32_t c, const uint8_t *p, uint32_t n)   /* c starts at 0xFFFFFFFF; the CRC is ~c (as crc32()) */
{
    uint32_t k;
    while (n--) {
        c ^= *p++;
        for (k = 0; k < 8u; k++)
            c = c & 1u ? (c >> 1) ^ 0xEDB88320u : c >> 1;
    }
    return c;
}
static uint32_t wt_chunks(void) { return (wt.bytes + 255u) / 256u; }   /* 140 */
static void wt_end(uint32_t result)             /* the upload is over, ok or not: the screen shows it for 1.5 s */
{
    wt.busy = 0;
    wt.result = (uint8_t)result;
    wt.show_until = fm1_ms + WT_SHOW_MS;
    wt.redraw = 1;
}
static int wt_same(uint32_t off, const uint8_t *ref, uint32_t n)   /* flash == ref (RAM)? 1 yes */
{
    uint8_t b[64];
    uint32_t i, k;
    for (k = 0; k < n; k += 64u) {
        uint32_t m = n - k > 64u ? 64u : n - k;
        if (wt_read(off + k, b, m))
            return 0;
        for (i = 0; i < m; i++)
            if (b[i] != ref[k + i])
                return 0;
    }
    return 1;
}
static uint32_t wt_may_write(uint32_t slot)     /* a slot the writer may touch right now */
{
    return slot < WT_WRITABLE && flash_ok && quiet() && !fm1_in.notes;
}

/* BEGIN: slot, fmt, frames, levels, u32 bytes, u32 crc32, name[8]. The header sector is erased first: the slot is empty. */
static uint32_t wt_begin(const uint8_t *a, uint32_t len)
{
    uint32_t i, us, bytes;
    if (len != 20u || a[0] >= WT_SLOTS)
        return WT_E_ARGS;
    if (a[1] != 1u || a[2] != WT_FRAMES_N || a[3] != WT_LEVELS_N)
        return WT_E_ARGS;
    bytes = ota_rd32(a + 4);
    if (bytes != WT_DATA_BYTES)
        return WT_E_ARGS;
    if (!wt_may_write(a[0]))
        return WT_E_BUSY;
    wt.busy = 0;                                 /* a BEGIN over a running upload starts again */
    wt_drop(a[0]);                               /* the engine stops reading the slot BEFORE it is erased */
    wt_dir[a[0]].valid = 0;
    if (wt_flash_erase(wt_slot_at(a[0]) + WT_HDR_SECTOR, &us)) {      /* the header's sector: empty from here on */
        wt_end(3);
        return 3;
    }
    wt.slot = a[0];
    wt.frames = a[2];
    wt.bytes = bytes;
    wt.crc = ota_rd32(a + 8);
    wt.next = 0;
    for (i = 0; i < 8u; i++)
        wt.name[i] = a[12 + i] == 0u ? 0 : a[12 + i] >= 0x20u && a[12 + i] < 0x7Fu ? (char)a[12 + i] : '?';
    wt.name[8] = 0;
    wt.t_last = fm1_ms;
    wt.shown_pct = 255;
    wt.result = WT_R_NONE;
    wt.show_until = 0;
    wt.redraw = 1;
    wt.busy = 1;
    return WT_OK;
}

/* DATA: slot, u16 chunk index, data (256 B; the last chunk the rest). In order; the previous chunk again (a lost reply)
 * is only compared. A sector is erased when its first chunk arrives; every chunk is read back. */
static uint32_t wt_data(const uint8_t *a, uint32_t len, uint32_t *next)
{
    uint32_t idx, n, off, at, us, nch;
    if (!wt.busy || len < 4u || a[0] != wt.slot)
        return 6;
    idx = ota_rd16(a + 1);
    n = len - 3u;
    nch = wt_chunks();
    if (idx >= nch)
        return WT_E_RANGE;
    off = idx * 256u;
    if (n != (idx + 1u == nch ? wt.bytes - off : 256u))
        return WT_E_ARGS;
    at = wt_slot_at(wt.slot) + off;
    if (idx + 1u == wt.next) {                   /* the one before: a retry */
        if (!wt_same(at, a + 3, n)) {
            wt_end(5);
            return 5;
        }
    } else if (idx != wt.next)
        return WT_E_RANGE;                       /* out of order: the upload stays open, the 15 s keep running */
    else {
        if (!(off & 0xFFFu) && off < WT_HDR_SECTOR && wt_flash_erase(at, &us)) {   /* sector 8 was erased by BEGIN */
            wt_end(3);
            return 3;
        }
        if (wt_flash_prog(at, a + 3, n)) {
            wt_end(4);
            return 4;
        }
        if (!wt_same(at, a + 3, n)) {
            wt_end(5);
            return 5;
        }
        wt.next = idx + 1u;
    }
    wt.t_last = fm1_ms;                          /* only a chunk that was taken (or the benign retry) keeps the upload alive */
    *next = wt.next;
    return WT_OK;
}

/* COMMIT: slot. The CRC32 of what is stored must be BEGIN's; then the header is written, last, and read back. */
static uint32_t wt_commit(const uint8_t *a, uint32_t len, uint32_t *crc)
{
    uint8_t h[WT_HDR_LEN], r[WT_HDR_LEN], b[64];
    uint32_t k, c = 0xFFFFFFFFu, i;
    wt_ent_t e;
    if (!wt.busy || len != 1u || a[0] != wt.slot || wt.next != wt_chunks())
        return 6;
    for (k = 0; k < wt.bytes; k += 64u) {
        uint32_t m = wt.bytes - k > 64u ? 64u : wt.bytes - k;
        if (wt_read(wt_slot_at(wt.slot) + k, b, m)) {
            wt_end(5);
            return 5;
        }
        c = wt_crc_upd(c, b, m);
        fm1_wdt_feed();
    }
    c = ~c;
    if (c != wt.crc) {
        wt_end(8);
        return 8;
    }
    for (i = 0; i < WT_HDR_LEN; i++)
        h[i] = 0xFF;
    h[0] = 'V'; h[1] = 'W'; h[2] = 'T'; h[3] = '1';
    ota_wr16(h + 4, 2u);
    h[6] = 1; h[7] = WT_FRAMES_N; h[8] = WT_LEVELS_N; h[9] = 0;
    for (i = 0; i < 8u; i++)
        h[10 + i] = (uint8_t)wt.name[i];
    ota_wr32(h + 18, wt.bytes);
    ota_wr32(h + 22, c);
    ota_wr32(h + 26, crc32(h, 26));
    if (wt_flash_prog(wt_slot_at(wt.slot) + WT_HDR_AT, h, WT_HDR_LEN)) {
        wt_end(4);
        return 4;
    }
    if (wt_read(wt_slot_at(wt.slot) + WT_HDR_AT, r, WT_HDR_LEN) || !wt_same(wt_slot_at(wt.slot) + WT_HDR_AT, h, WT_HDR_LEN)) {
        wt_end(5);
        return 5;
    }
    wt_parse(r, &e);
    if (!e.valid) {
        wt_end(5);
        return 5;
    }
    wt_dir[wt.slot] = e;
    wt_dir[wt.slot].state = UT_OK;               /* the CRC32 of all 35,648 stored bytes was just checked */
    wt_xip_init();
    eng_user_base[wt.slot] = fl_xip_i16(wt_slot_at(wt.slot));
    wt_gen++;
    *crc = c;
    wt_end(WT_R_OK);
    return WT_OK;
}

/* the upload screen; the app calls this every pass. 1: the upload owns the display (the app ignores everything else),
 * 2: it just ended (the app redraws its own screen), 0: not involved */
static void wt_u2s(char *o, uint32_t v)
{
    char t[11];
    uint32_t n = 0, i = 0;
    do { t[n++] = (char)('0' + v % 10u); v /= 10u; } while (v && n < 10u);
    while (n) o[i++] = t[--n];
    o[i] = 0;
}
static uint32_t wt_ui_poll(void)
{
    char b[16];
    if (wt.busy) {
        uint32_t pct = wt.next * 25600u / wt.bytes;
        if (pct > 100u) pct = 100u;
        if (wt.redraw) {
            wt.redraw = 0;
            lcd_fill(0, 0, 240, 240, C_BLACK);
            draw_text_box(0, 40, 240, &FONT_S, "UPLOADING", C_WHITE, 1);
            draw_text_box(0, 80, 240, &FONT_L, wt.name, C_WHITE, 1);
            draw_text_box(0, 200, 240, &FONT_S, "DO NOT POWER OFF", C_WHITE, 1);
        }
        if (pct != wt.shown_pct && (wt.shown_pct == 255u || fm1_ms - wt.t_draw >= 80u)) {
            wt.shown_pct = (uint8_t)pct;
            wt.t_draw = fm1_ms;
            wt_u2s(b, pct);
            b[pct < 10u ? 1 : pct < 100u ? 2 : 3] = ' ';
            b[pct < 10u ? 2 : pct < 100u ? 3 : 4] = '%';
            b[pct < 10u ? 3 : pct < 100u ? 4 : 5] = 0;
            draw_text_box(0, 130, 240, &FONT_L, b, C_WHITE, 1);
        }
        return 1;
    }
    if (wt.show_until) {
        if (wt.redraw) {
            const char *l1 = wt.result == WT_R_OK ? "TABLE SAVED" : wt.result == 6u ? "TIMED OUT" :
                             wt.result == WT_R_ABORT ? "ABORTED" : "UPLOAD FAILED";
            wt.redraw = 0;
            lcd_fill(0, 0, 240, 240, C_BLACK);
            draw_text_box(0, 70, 240, &FONT_L, l1, C_WHITE, 1);
            draw_text_box(0, 120, 240, &FONT_S, wt.name, C_WHITE, 1);
            if (wt.result != WT_R_OK && wt.result != WT_R_ABORT && wt.result != 6u) {
                b[0] = 'C'; b[1] = 'O'; b[2] = 'D'; b[3] = 'E'; b[4] = ' ';
                wt_u2s(b + 5, wt.result);
                draw_text_box(0, 150, 240, &FONT_S, b, C_WHITE, 1);
            }
        }
        if ((int32_t)(fm1_ms - wt.show_until) < 0)
            return 1;
        wt.show_until = 0;
        wt.result = WT_R_NONE;
        return 2;
    }
    return 0;
}

/* XIPCHECK (cmd 11, stage 4a): the engine will read user tables through the plain XIP window, so prove on the device that
 * it works before it matters. The window is set up here, once, on the first XIPCHECK (not at boot: a bad setup can then
 * only follow an explicit command, never a boot loop). Compares the 35,648 data bytes and the header page of one slot read
 * through XIP with the same bytes read over SPI (st_read). Reply (16 B, rc 0): result (0 ok, 7 refused), u32 differing bytes,
 * u32 offset of the first difference (0xFFFFFFFF none), u32 microseconds to read the data through XIP once. */
static uint8_t wt_xip;
static void wt_xip_init(void)
{
    if (!wt_xip) {
        fl_plain_window_init();                  /* hal/fm1_flash.h: SFC plain window from 0x93000, then the cache is dropped */
        wt_xip = 1;
    }
}
static uint32_t wt_xipcheck(uint32_t slot, uint8_t *o)
{
    uint32_t i, k, bad = 0, first = 0xFFFFFFFFu, t0, sum = 0, base;
    uint8_t b[64];
    if (slot >= WT_SLOTS || !flash_ok || wt.busy || !quiet() || fm1_in.notes) {
        o[0] = 7;
        for (i = 1; i < 16u; i++) o[i] = 0;
        return 16;
    }
    wt_xip_init();
    base = wt_slot_at(slot);
    t0 = fm1_ticks();
    for (i = 0; i < WT_DATA_BYTES; i++)
        sum += fl_xip_vptr(base)[i];      /* time one pass over the data */
    t0 = (fm1_ticks() - t0) / FM1_TICKS_PER_US;
    (void)sum;
    for (k = 0; k < WT_DATA_BYTES + 64u; k += 64u) {             /* data, then (past it) the header page's first 64 B */
        uint32_t at = k < WT_DATA_BYTES ? k : WT_HDR_AT;
        const volatile uint8_t *x = fl_xip_vptr(base + at);
        uint32_t m = k < WT_DATA_BYTES ? (WT_DATA_BYTES - k > 64u ? 64u : WT_DATA_BYTES - k) : 64u;
        if (wt_read(base + at, b, m)) { bad += m; continue; }
        for (i = 0; i < m; i++)
            if (x[i] != b[i]) {
                bad++;
                if (first == 0xFFFFFFFFu) first = at + i;
            }
        fm1_wdt_feed();
        if (k >= WT_DATA_BYTES) break;
    }
    o[0] = 0; o[1] = o[2] = o[3] = 0;
    ota_wr32(o + 4, bad);
    ota_wr32(o + 8, first);
    ota_wr32(o + 12, t0);
    return 16;
}

/* The engine plays a user table only after its data CRC32 matched the header's (a header alone can sit over damaged data:
 * a power cut inside BEGIN's erase of the header sector, docs/UPLOAD-DESIGN.md 12). The check reads the 35,648 bytes over SPI
 * (about 0.15 s, the main loop only; the audio runs on). Returns 1 for UT_OK. */
static uint32_t wt_verify(uint32_t k)
{
    uint8_t b[64];
    uint32_t i, c = 0xFFFFFFFFu;
    eng_user_base[k] = 0;
    for (i = 0; i < WT_DATA_BYTES; i += 64u) {
        if (wt_read(wt_slot_at(k) + i, b, 64u))
            return 0;                            /* a failed read proves nothing: stays UNCHECKED, tried again */
        c = wt_crc_upd(c, b, 64u);
        fm1_wdt_feed();
    }
    if (~c != wt_dir[k].crc) {
        wt_dir[k].state = UT_BAD;                /* the data is not what the header says: never played, not tried again */
        wt_gen++;
        return 0;
    }
    wt_xip_init();
    wt_dir[k].state = UT_OK;
    eng_user_base[k] = fl_xip_i16(wt_slot_at(k));
    wt_gen++;
    return 1;
}
/* main loop, every pass: the user tables the patch refers to (table index t1 / t2, 12.. = slot 0..) are checked once;
 * until then the engine plays table 0 */
static void wt_user_poll(uint32_t t1, uint32_t t2)
{
    uint32_t i, t;
    if (!wt_scanned || wt.busy || !flash_ok)
        return;
    for (i = 0; i < 2u; i++) {
        t = i ? t2 : t1;
        if (t >= UT_FIRST && t < UT_NTABLES && wt_dir[t - UT_FIRST].valid && wt_dir[t - UT_FIRST].state == UT_UNCHECKED) {
            wt_verify(t - UT_FIRST);
            return;                              /* one slot a pass */
        }
    }
}

static uint8_t wt_msg[9 + 256 + 1], wt_wire[2 + (9 + 256 + 1) * 8 / 7 + 2];

static void wt_reply(uint32_t cmd, uint32_t rc, uint32_t seq, const uint8_t *pl, uint32_t n)   /* n <= 256 */
{
    uint32_t i, s = 0, w;
    wt_msg[0] = 0x7D; wt_msg[1] = 'V'; wt_msg[2] = 'W';
    wt_msg[3] = (uint8_t)cmd; wt_msg[4] = (uint8_t)rc; wt_msg[5] = (uint8_t)seq;
    wt_msg[6] = (uint8_t)n; wt_msg[7] = (uint8_t)(n >> 8);
    for (i = 0; i < n; i++)
        wt_msg[8 + i] = pl[i];
    for (i = 3; i < 8u + n; i++)
        s += wt_msg[i];
    wt_msg[8 + n] = (uint8_t)~s;
    wt_wire[0] = 0xF0;
    w = 1u + ota_pack7(wt_msg, 9u + n, wt_wire + 1);
    wt_wire[w++] = 0xF7;
    ota_wire_send(wt_wire, w);
}

static uint32_t pl_link(uint32_t cmd, const uint8_t *a, uint32_t len, uint8_t *out, uint32_t *n);   /* patchlink.h, after the log */
static void wt_handle(const uint8_t *r)         /* r: a checked request, r[3] cmd, r[4] seq, r[5..6] len, payload at r + 7 */
{
    uint32_t cmd = r[3], seq = r[4], len = ota_rd16(r + 5);
    const uint8_t *a = r + 7;
    static uint8_t out[256];
    uint32_t i, k, n = 0;
    if (cmd == WT_CMD_HELLO) {
        static const char ID[] = FELUCCA_ID;
        out[0] = 1;                              /* protocol */
        out[1] = WT_STAGE;
        out[2] = WT_SLOTS;
        out[3] = WT_HDR_LEN;
        ota_wr32(out + 4, WT_SLOT_SIZE);
        for (i = 0; i < 8u; i++)
            out[8 + i] = i < sizeof ID - 1u ? (uint8_t)ID[i] : 0;
        wt_reply(cmd, WT_OK, seq, out, 16);
    } else if (cmd == WT_CMD_DIR) {              /* start, count -> 16 B per slot */
        if (len != 2u || a[0] >= WT_SLOTS || a[1] == 0u || a[1] > WT_SLOTS || a[0] + a[1] > WT_SLOTS) {
            wt_reply(cmd, WT_E_ARGS, seq, out, 0);
            return;
        }
        if (!flash_ok) {
            wt_reply(cmd, WT_E_NOFLASH, seq, out, 0);
            return;
        }
        for (k = a[0]; k < (uint32_t)a[0] + a[1]; k++) {
            const wt_ent_t *e = &wt_dir[k];
            out[n++] = (uint8_t)k;
            out[n++] = e->valid;
            out[n++] = e->frames;
            out[n++] = e->levels;
            for (i = 0; i < 8u; i++)
                out[n++] = e->valid ? (uint8_t)e->name[i] : 0;
            ota_wr32(out + n, e->crc);
            n += 4u;
        }
        wt_reply(cmd, WT_OK, seq, out, n);
    } else if (cmd == WT_CMD_READ) {             /* slot, offset (u16 LE), length (u16 LE, <= 256) */
        uint32_t off, cnt;
        if (len != 5u || a[0] >= WT_SLOTS) {
            wt_reply(cmd, WT_E_ARGS, seq, out, 0);
            return;
        }
        off = ota_rd16(a + 1);
        cnt = ota_rd16(a + 3);
        if (cnt == 0u || cnt > 256u || off + cnt > WT_SLOT_SIZE) {
            wt_reply(cmd, WT_E_RANGE, seq, out, 0);
            return;
        }
        if (!flash_ok) {
            wt_reply(cmd, WT_E_NOFLASH, seq, out, 0);
            return;
        }
        if (wt_read(wt_slot_at(a[0]) + off, out, cnt)) {
            wt_reply(cmd, WT_E_NOFLASH, seq, out, 0);
            return;
        }
        wt_reply(cmd, WT_OK, seq, out, cnt);
    } else if (cmd == WT_CMD_SELFTEST) {         /* no payload; the reply comes after about 0.2 s */
        if (len != 0u) {
            wt_reply(cmd, WT_E_ARGS, seq, out, 0);
            return;
        }
        if (wt.busy) {                           /* not in the middle of an upload */
            wt_reply(cmd, WT_E_BUSY, seq, out, 0);
            return;
        }
        n = wt_selftest(out);
        wt_reply(cmd, WT_OK, seq, out, n);
    } else if (cmd == WT_CMD_BEGIN) {
        wt_reply(cmd, wt_begin(a, len), seq, out, 0);
    } else if (cmd == WT_CMD_DATA) {
        uint32_t nx = 0, rc = wt_data(a, len, &nx);
        ota_wr16(out, nx);                       /* the next chunk the firmware wants */
        wt_reply(cmd, rc, seq, out, rc ? 0u : 2u);
    } else if (cmd == WT_CMD_COMMIT) {
        uint32_t crc = 0, rc = wt_commit(a, len, &crc);
        ota_wr32(out, crc);
        wt_reply(cmd, rc, seq, out, rc ? 0u : 4u);
    } else if (cmd == WT_CMD_ABORT) {            /* the slot stays empty (BEGIN erased its header) */
        if (wt.busy)
            wt_end(WT_R_ABORT);
        wt_reply(cmd, WT_OK, seq, out, 0);
    } else if (cmd == WT_CMD_ERASE) {            /* slot: erase the header sector only; the slot is empty */
        uint32_t us = 0, rc = WT_OK;
        if (len != 1u || a[0] >= WT_SLOTS)
            rc = WT_E_ARGS;
        else if (wt.busy || !wt_may_write(a[0]))
            rc = WT_E_BUSY;
        else {
            wt_drop(a[0]);
            wt_dir[a[0]].valid = 0;
            if (wt_flash_erase(wt_slot_at(a[0]) + WT_HDR_SECTOR, &us))
                rc = 3;
        }
        wt_reply(cmd, rc, seq, out, 0);
    } else if (cmd == WT_CMD_XIPCHECK) {         /* slot */
        if (len != 1u) {
            wt_reply(cmd, WT_E_ARGS, seq, out, 0);
            return;
        }
        n = wt_xipcheck(a[0], out);
        wt_reply(cmd, WT_OK, seq, out, n);
    } else if (cmd >= WT_CMD_PDIR && cmd <= WT_CMD_PDEL) {    /* the patch library: not while a table upload runs */
        uint32_t rc = wt.busy ? WT_E_BUSY : pl_link(cmd, a, len, out, &n);
        wt_reply(cmd, rc, seq, out, rc ? 0u : n);
    } else if (cmd == WT_CMD_AUDITION) {
        wt_reply(cmd, WT_E_BUSY, seq, out, 0);   /* stage 4 */
    } else {
        wt_reply(cmd, WT_E_ARGS, seq, out, 0);
    }
}

/* main loop, before ota_service(): takes a pending frame only if it is a VW link frame; the updater's stay */
static void wt_service(void)
{
    const uint8_t *p;
    uint32_t n, d, s = 0, i;
    if (!wt_scanned) {
        wt_scanned = 1;
        wt_scan();
        wt_xip_init();                           /* the plain flash window for the engine's reads (proven by XIPCHECK, 4a) */
    }
    if (wt.busy && (int32_t)(fm1_ms - wt.t_last) > (int32_t)WT_SESSION_MS)
        wt_end(6);                               /* the tool went away: the slot stays empty */
    if (!ota_frame_get(&p, &n))
        return;
    d = ota_unpack7(p, n, ota_dec, sizeof ota_dec);
    if (d < 8u || d > sizeof ota_dec || ota_dec[0] != 0x7D || ota_dec[1] != 'V' || ota_dec[2] != 'W') {
        if (d >= 3u && ota_dec[0] == 0x7D && ota_dec[1] == 'V' && ota_dec[2] == 'W')
            ota_frame_done();                    /* ours but too short or too long: drop */
        return;                                  /* not ours: ota_service() takes it */
    }
    ota_frame_done();
    if (ota_rd16(ota_dec + 5) + 8u != d)
        return;
    for (i = 3; i + 1u < d; i++)
        s += ota_dec[i];
    if ((uint8_t)~s != ota_dec[d - 1u])
        return;
    wt_handle(ota_dec);
}
