/* SPDX-License-Identifier: GPL-3.0-only */
/* User wavetable slots, "VW link" v1 (docs/UPLOAD-DESIGN.md). STAGE 1: read-only plus a self-test of one spare sector.
 *
 * What this file does: finds out which of the 8 slots hold a valid header (a RAM directory, made once on the first
 * main-loop pass) and answers HELLO, DIR and READ from the computer. The engine does not use the slots yet.
 * Commands that would write a slot (BEGIN, DATA, COMMIT, ABORT, ERASE) are answered "refused" (rc 7).
 * Stage 1 adds SELFTEST (cmd 10): wt_flash_erase / wt_flash_prog, the ONLY calls to the flash writer in this file, are
 * limited to the spare sector WT_TEST_AT (0xDF000..0xDFFFF) by their own check and again by FL_RANGE_OK (platform.c).
 * Later stages widen the window in wt_flash_ok() to the slot region and nowhere else.
 *
 * Wire (SysEx on the USB-MIDI port, 7D = the non-commercial manufacturer id):
 *   request  F0 pack7( 7D 56 57 cmd seq len_lo len_hi payload[len] sum ) F7
 *   reply    F0 pack7( 7D 56 57 cmd rc seq len_lo len_hi payload[len] sum ) F7
 *   sum = ~(sum of every byte from cmd up to the one before sum) & 0xFF. A request with a bad sum or length is
 *   dropped without a reply (the tool retries). Frames of the stock updater (00 59 ...) are left for ota_service().
 *
 * Needs (platform.c supplies them before this file): ota_pack7 / ota_unpack7 / ota_wire_send / ota_frame_get /
 * ota_frame_done / ota_dec (ota.c), st_read, flash_ok, crc32, FELUCCA_ID. */
#define WT_SLOTS 8u
#define WT_BASE 0x00097000u                      /* slot k: WT_BASE + k * WT_SLOT_SIZE */
#define WT_SLOT_SIZE 0x9000u                     /* 1 header sector + 8 data sectors */
#define WT_HDR_LEN 64u
#define WT_FRAME_BYTES 1020u                     /* fmt 0: one frame = 510 samples (6 mip levels) x 2 bytes */
#define WT_STAGE 1u                              /* 0 read-only, 1 test sector, 2.. writer (docs/UPLOAD-DESIGN.md 8.2) */

enum { WT_CMD_HELLO = 1, WT_CMD_DIR = 2, WT_CMD_BEGIN = 3, WT_CMD_DATA = 4, WT_CMD_COMMIT = 5, WT_CMD_ABORT = 6,
       WT_CMD_ERASE = 7, WT_CMD_READ = 8, WT_CMD_AUDITION = 9, WT_CMD_SELFTEST = 10 };
enum { WT_OK = 0, WT_E_ARGS = 1, WT_E_RANGE = 2, WT_E_BUSY = 7, WT_E_NOFLASH = 9 };

typedef struct {
    uint8_t valid;                               /* 1: header passed every check */
    uint8_t frames, levels;
    char name[9];                                /* printable ASCII, NUL ended */
    uint32_t bytes, crc;                         /* data size, CRC32 of the data (checked by COMMIT / the tool, not here) */
} wt_ent_t;

static wt_ent_t wt_dir[WT_SLOTS];
static uint8_t wt_scanned;

static uint32_t wt_slot_at(uint32_t k) { return WT_BASE + k * WT_SLOT_SIZE; }

/* Header sector content -> entry. Anything that does not check out is an empty slot, never a crash, never trusted.
 * Layout (little endian): "VWT1", u16 version = 1, u8 fmt = 0, u8 frames, u8 levels, u8 0, char name[8], u32 data
 * bytes, u32 data CRC32, u32 CRC32 of bytes 0..25. */
static void wt_parse(const uint8_t *h, wt_ent_t *e)
{
    uint32_t i, bytes;
    e->valid = 0;
    e->frames = e->levels = 0;
    for (i = 0; i < sizeof e->name; i++)
        e->name[i] = 0;                          /* the whole name: no bytes of an older one survive a rescan */
    e->bytes = e->crc = 0;
    if (h[0] != 'V' || h[1] != 'W' || h[2] != 'T' || h[3] != '1')
        return;
    if (ota_rd16(h + 4) != 1u || h[6] != 0u)     /* version 1, fmt 0 */
        return;
    if (h[7] < 1u || h[7] > 32u || h[8] != 6u)   /* frames 1..32, 6 mip levels */
        return;
    bytes = ota_rd32(h + 18);
    if (bytes != (uint32_t)h[7] * WT_FRAME_BYTES || bytes > 8u * 0x1000u)
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
        wt_dir[k].valid = 0;
        if (flash_ok && !wt_read(wt_slot_at(k), h, WT_HDR_LEN))
            wt_parse(h, &wt_dir[k]);
        fm1_wdt_feed();
    }
}

static int quiet(void);                          /* the app: nothing sounds (engine.h) */

/* ---- the one flash writer. Stage 1 window: the spare sector only. */
#define WT_TEST_LEN 0x1000u
static int wt_flash_ok(uint32_t off, uint32_t n)
{
    return off >= WT_TEST_AT && off <= WT_TEST_AT + WT_TEST_LEN && n <= WT_TEST_AT + WT_TEST_LEN - off;
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
        n = wt_selftest(out);
        wt_reply(cmd, WT_OK, seq, out, n);
    } else if ((cmd >= WT_CMD_BEGIN && cmd <= WT_CMD_ERASE) || cmd == WT_CMD_AUDITION) {
        wt_reply(cmd, WT_E_BUSY, seq, out, 0);   /* no slot writes before stage 2 */
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
    }
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
