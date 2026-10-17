/* SPDX-License-Identifier: GPL-3.0-only */
/* patchlog.h: the user patch store (T17, docs/PATCH-STORAGE-DESIGN.md): an append-only log of records over 4 flash
 * sectors. No UI, no engine; the app maps patch.p[] to and from pl_patch_t.
 *
 *   pl_init()              boot scan (reads only): which ids hold a patch, where the log can be appended
 *   pl_present()           bit mask of the ids (0..31) that hold a patch
 *   pl_get(id, &p)         read and check one patch
 *   pl_put(id, &p)         append a new record (replaces the id); main loop only, when quiet(); may compact (up to
 *                          about 0.3 s, 3 sector erases of ~17 ms with interrupts off each): feeds the watchdog
 *   pl_del(id)             append a tombstone
 *
 * Record (little endian, 4-byte aligned): u16 length | u8 type (1 patch, 2 tombstone) | u8 id | u32 sequence |
 * u8 version | u8 n | char name[12] | u16 0 | int16 v[n] | pad | u32 crc32 of everything before it.
 * A record is programmed from byte 2 on first and its 2 length bytes last: until then the position still reads as
 * erased, so a power cut leaves either nothing or a whole record. A record is valid only with sane length and CRC.
 * The newest (highest sequence) valid record of an id wins; a tombstone newer than every patch record deletes it.
 *
 * Flash writer: pl_erase / pl_prog are the ONLY calls to fl_erase4k / fl_write in this file, limited to the window
 * PL_AT .. PL_HI (platform.c FL_RANGE_OK allows exactly that much for this file).
 *
 * Space: the head sector takes appends. When a new sector is needed and fewer than 2 erased ones remain, compaction
 * copies every live record (fresh sequence numbers) into an erased sector, verifies each copy, and only then erases
 * the sectors that hold nothing live, oldest first and stopping at a failed erase (so a deleted patch can never come back). Live data is
 * capped at one sector (PL_SSZ bytes of records), so the copy always fits.
 * Needs from the platform: st_read, fl_erase4k, fl_write, flash_ok, crc32, fm1_wdt_feed. */
#pragma once
#include <stdint.h>

#define PL_AT 0x000E5000u
#define PL_SECT 4u
#define PL_SSZ 0x1000u
#define PL_HI (PL_AT + PL_SECT * PL_SSZ)
#define PL_IDS 32u
#define PL_NAME 12u
#define PL_MAXP 100u                             /* parameters per patch (record <= 256 B) */
#define PL_HDR 24u
#define PL_MAXREC 256u
#define PL_T_PATCH 1u
#define PL_T_DEL 2u
#define PL_VER 1u
#define PL_NONE 0xFFu

enum { PL_OK = 0, PL_E_ARGS = -1, PL_E_NONE = -2, PL_E_BAD = -3, PL_E_FULL = -4, PL_E_FLASH = -5, PL_E_NOFLASH = -6 };

typedef struct {
    char name[PL_NAME + 1];                      /* printable ASCII, zero terminated */
    uint8_t n;                                   /* parameters in v */
    int16_t v[PL_MAXP];
} pl_patch_t;

typedef struct { uint32_t off, seq; uint16_t len; uint8_t type; } pl_ent_t;   /* type 0: none */
static struct {
    pl_ent_t ent[PL_IDS];                        /* newest record per id (patch or tombstone) */
    uint16_t free_from[PL_SECT];                 /* from here to the sector's end every byte is 0xFF (0 = erased) */
    uint32_t maxseq[PL_SECT];                    /* highest sequence of a valid record in the sector (0 = none) */
    uint32_t seq, live_bytes;
    uint8_t head, ready;                         /* sector taking appends, or PL_NONE */
    uint32_t erases, progs, gcs;                 /* counters (tests, diagnostics) */
} pl;
static uint8_t pl_rec[PL_MAXREC], pl_chk[PL_MAXREC];

static uint32_t pl_u32(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static void pl_p32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static uint32_t pl_reclen(uint32_t n) { return (PL_HDR + 2u * n + 4u + 3u) & ~3u; }

/* ---- the one flash writer ---- */
static int pl_flash_ok(uint32_t off, uint32_t n) { return off >= PL_AT && off <= PL_HI && n <= PL_HI - off; }
static int pl_erase(uint32_t s)
{
    uint32_t took = 0;
    int rc;
    if (!flash_ok || s >= PL_SECT || !pl_flash_ok(PL_AT + s * PL_SSZ, PL_SSZ)) return PL_E_NOFLASH;
    pl.erases++;
    rc = fl_erase4k(PL_AT + s * PL_SSZ, &took);
    if (rc) { pl.free_from[s] = PL_SSZ; return PL_E_FLASH; }       /* unusable until a later erase works */
    pl.free_from[s] = 0;
    pl.maxseq[s] = 0;
    return PL_OK;
}
static int pl_prog(uint32_t off, const uint8_t *src, uint32_t n)
{
    if (!flash_ok || n == 0u || !pl_flash_ok(off, n)) return PL_E_NOFLASH;
    pl.progs++;
    return fl_write(off, src, n) ? PL_E_FLASH : PL_OK;
}

/* ---- records ---- */
/* r holds avail bytes; 1 when a whole valid record starts there (its length is returned) */
static int pl_valid(const uint8_t *r, uint32_t avail, uint32_t *len)
{
    uint32_t l = r[0] | (uint32_t)r[1] << 8, n;
    if (l < PL_HDR + 4u || l > PL_MAXREC || (l & 3u) || l > avail) return 0;
    n = r[9];
    if (r[3] >= PL_IDS || r[8] != PL_VER) return 0;
    if (r[2] == PL_T_PATCH) { if (n > PL_MAXP || l != pl_reclen(n)) return 0; }
    else if (r[2] == PL_T_DEL) { if (n != 0u || l != PL_HDR + 4u) return 0; }
    else return 0;
    if (crc32(r, l - 4u) != pl_u32(r + l - 4u)) return 0;
    *len = l;
    return 1;
}

static void pl_note(uint32_t s, uint32_t off, const uint8_t *r, uint32_t len)   /* a valid record was found / written */
{
    uint32_t id = r[3], seq = pl_u32(r + 4);
    if (seq > pl.maxseq[s]) pl.maxseq[s] = seq;
    if (seq > pl.seq) pl.seq = seq;
    if (!pl.ent[id].type || seq > pl.ent[id].seq) {
        pl.ent[id].off = PL_AT + s * PL_SSZ + off;
        pl.ent[id].seq = seq;
        pl.ent[id].len = (uint16_t)len;
        pl.ent[id].type = r[2];
    }
}

static void pl_scan(uint32_t s)
{
    static uint8_t win[512];
    uint32_t pos = 0, page = 0xFFFFFFFFu, lastend = 0, len;
    while (pos < PL_SSZ) {
        uint32_t p = pos >> 8, o = pos & 255u;
        if (p != page) {                         /* this page and the next (a record is at most 256 B) */
            page = p;
            if (st_read(PL_AT + s * PL_SSZ + p * 256u, win, 256u)) { lastend = PL_SSZ; break; }   /* unreadable: do not append here */
            if (p + 1u < PL_SSZ / 256u) { if (st_read(PL_AT + s * PL_SSZ + (p + 1u) * 256u, win + 256, 256u)) { lastend = PL_SSZ; break; } }
            else for (len = 0; len < 256u; len++) win[256u + len] = 0xFF;
            fm1_wdt_feed();
        }
        if (pl_valid(win + o, 512u - o, &len) && pos + len <= PL_SSZ) {
            pl_note(s, pos, win + o, len);
            pos += len;
            lastend = pos;
        } else {
            if (pl_u32(win + o) != 0xFFFFFFFFu) lastend = pos + 4u;
            pos += 4u;
        }
    }
    pl.free_from[s] = (uint16_t)lastend;
}

static int pl_init(void)
{
    uint32_t s, best = 0;
    uint8_t *z = (uint8_t *)&pl;
    uint32_t i;
    for (i = 0; i < sizeof pl; i++) z[i] = 0;
    pl.head = PL_NONE;
    if (!flash_ok) return PL_E_NOFLASH;
    pl.ready = 1;
    for (s = 0; s < PL_SECT; s++) pl_scan(s);
    for (s = 0; s < PL_SECT; s++)
        if (pl.maxseq[s] > best) { best = pl.maxseq[s]; pl.head = (uint8_t)s; }
    for (i = 0; i < PL_IDS; i++)
        if (pl.ent[i].type == PL_T_PATCH) pl.live_bytes += pl.ent[i].len;
    return PL_OK;
}

static uint32_t pl_present(void)
{
    uint32_t m = 0, i;
    for (i = 0; i < PL_IDS; i++)
        if (pl.ent[i].type == PL_T_PATCH) m |= 1u << i;
    return m;
}

static int pl_get(uint32_t id, pl_patch_t *out)
{
    uint32_t len, n, i;
    if (!pl.ready) return PL_E_NOFLASH;
    if (id >= PL_IDS || !out) return PL_E_ARGS;
    if (pl.ent[id].type != PL_T_PATCH) return PL_E_NONE;
    if (st_read(pl.ent[id].off, pl_rec, pl.ent[id].len)) return PL_E_FLASH;
    if (!pl_valid(pl_rec, pl.ent[id].len, &len) || pl_rec[2] != PL_T_PATCH || pl_rec[3] != id) return PL_E_BAD;
    n = pl_rec[9];
    out->n = (uint8_t)n;
    for (i = 0; i < PL_NAME; i++) {
        uint8_t c = pl_rec[10 + i];
        out->name[i] = c >= 32u && c < 127u ? (char)c : (c ? '?' : 0);
    }
    out->name[PL_NAME] = 0;
    for (i = 0; i < n; i++) out->v[i] = (int16_t)(pl_rec[PL_HDR + 2u * i] | pl_rec[PL_HDR + 2u * i + 1u] << 8);
    return PL_OK;
}

/* ---- writing ---- */
/* append the record in buf (seq and CRC are filled in here) at the head: body first, the 2 length bytes last, then read
 * back. Any failure makes the head sector "full" (nothing is ever programmed over bytes that may hold junk). */
static int pl_append(uint8_t *buf, uint32_t len)
{
    uint32_t s = pl.head, off, i;
    pl_p32(buf + 4, pl.seq + 1u);
    pl_p32(buf + len - 4u, crc32(buf, len - 4u));
    off = PL_AT + s * PL_SSZ + pl.free_from[s];
    if (pl_prog(off + 2u, buf + 2, len - 2u) || pl_prog(off, buf, 2u) || st_read(off, pl_chk, len)) {
        pl.free_from[s] = PL_SSZ;
        return PL_E_FLASH;
    }
    for (i = 0; i < len; i++)
        if (pl_chk[i] != buf[i]) { pl.free_from[s] = PL_SSZ; return PL_E_FLASH; }
    pl_note(s, pl.free_from[s], buf, len);
    pl.free_from[s] = (uint16_t)(pl.free_from[s] + len);
    return PL_OK;
}

static uint32_t pl_erased(void)                  /* erased sectors other than the head */
{
    uint32_t s, n = 0;
    for (s = 0; s < PL_SECT; s++)
        if (s != pl.head && pl.free_from[s] == 0u) n++;
    return n;
}
static int pl_first_erased(void)
{
    uint32_t s;
    for (s = 0; s < PL_SECT; s++)
        if (s != pl.head && pl.free_from[s] == 0u) return (int)s;
    return -1;
}
static int pl_has_live(uint32_t s)
{
    uint32_t i;
    for (i = 0; i < PL_IDS; i++)
        if (pl.ent[i].type == PL_T_PATCH && pl.ent[i].off >= PL_AT + s * PL_SSZ && pl.ent[i].off < PL_AT + (s + 1u) * PL_SSZ) return 1;
    return 0;
}

/* erase every sector except `keep` that holds nothing live, oldest (lowest highest-sequence) first */
static void pl_reclaim(uint32_t keep)
{
    uint32_t tried = 1u << keep, s, best, bs;
    for (;;) {
        best = 0xFFFFFFFFu;
        bs = PL_SECT;
        for (s = 0; s < PL_SECT; s++)
            if (!(tried >> s & 1u) && pl.free_from[s] != 0u && !pl_has_live(s) && pl.maxseq[s] < best) { best = pl.maxseq[s]; bs = s; }
        if (bs == PL_SECT) break;
        tried |= 1u << bs;
        fm1_wdt_feed();
        if (pl_erase(bs)) break;                 /* stop at a failure: never erase a newer sector (a tombstone) past an older one */
    }
}

static int pl_gc(void)
{
    uint32_t id, d, len, n;
    int e = pl_first_erased();
    pl.gcs++;
    if (e < 0) {                                 /* no erased sector (a cut during an earlier compaction): free a dead one */
        pl_reclaim(pl.head == PL_NONE ? PL_SECT : pl.head);
        e = pl_first_erased();
        if (e < 0) return PL_E_FLASH;
    }
    d = (uint32_t)e;
    pl.head = (uint8_t)d;
    for (id = 0; id < PL_IDS; id++) {
        if (pl.ent[id].type != PL_T_PATCH) continue;
        n = pl.ent[id].len;
        fm1_wdt_feed();
        if (st_read(pl.ent[id].off, pl_rec, n)) return PL_E_FLASH;
        if (!pl_valid(pl_rec, n, &len)) {        /* a live record that no longer checks out is lost either way */
            pl.live_bytes -= n;
            pl.ent[id].type = 0;
            continue;
        }
        if (pl.free_from[d] + len > PL_SSZ) return PL_E_FULL;
        {
            pl_ent_t old = pl.ent[id];
            int rc;
            pl.ent[id].type = 0;                 /* pl_note replaces it by the copy */
            rc = pl_append(pl_rec, len);
            if (rc) { pl.ent[id] = old; return rc; }
        }
    }
    pl_reclaim(d);
    for (id = 0; id < PL_IDS; id++)              /* a tombstone whose sector was erased is gone */
        if (pl.ent[id].type == PL_T_DEL && pl.free_from[(pl.ent[id].off - PL_AT) / PL_SSZ] == 0u) pl.ent[id].type = 0;
    return PL_OK;
}

static int pl_reserve(uint32_t len)
{
    int e, rc;
    if (pl.head != PL_NONE && pl.free_from[pl.head] + len <= PL_SSZ) return PL_OK;
    if (pl_erased() < 2u) {
        rc = pl_gc();
        if (rc) return rc;
        if (pl.free_from[pl.head] + len <= PL_SSZ) return PL_OK;
    }
    e = pl_first_erased();
    if (e < 0) return PL_E_FLASH;
    pl.head = (uint8_t)e;
    return PL_OK;
}

static int pl_put(uint32_t id, const pl_patch_t *in)
{
    uint32_t i, n, len, live;
    int rc;
    if (!pl.ready) return PL_E_NOFLASH;
    if (id >= PL_IDS || !in || in->n > PL_MAXP) return PL_E_ARGS;
    n = in->n;
    len = pl_reclen(n);
    live = pl.live_bytes - (pl.ent[id].type == PL_T_PATCH ? pl.ent[id].len : 0u) + len;
    if (live > PL_SSZ) return PL_E_FULL;
    if ((rc = pl_reserve(len)) != 0) return rc;       /* may compact: that uses pl_rec and changes pl.seq, so build the record after it */
    for (i = 0; i < len; i++) pl_rec[i] = 0;
    pl_rec[0] = (uint8_t)len; pl_rec[1] = (uint8_t)(len >> 8);
    pl_rec[2] = PL_T_PATCH; pl_rec[3] = (uint8_t)id; pl_rec[8] = PL_VER; pl_rec[9] = (uint8_t)n;
    for (i = 0; i < PL_NAME && in->name[i]; i++) {
        uint8_t c = (uint8_t)in->name[i];
        pl_rec[10 + i] = c >= 32u && c < 127u ? c : '?';
    }
    for (i = 0; i < n; i++) { pl_rec[PL_HDR + 2u * i] = (uint8_t)in->v[i]; pl_rec[PL_HDR + 2u * i + 1u] = (uint8_t)((uint16_t)in->v[i] >> 8); }
    if (pl.ent[id].type == PL_T_PATCH) pl.live_bytes -= pl.ent[id].len;
    {
        pl_ent_t old = pl.ent[id];
        rc = pl_append(pl_rec, len);
        if (rc) { pl.live_bytes += old.type == PL_T_PATCH ? old.len : 0u; return rc; }
    }
    pl.live_bytes += len;
    return PL_OK;
}

static int pl_del(uint32_t id)
{
    uint32_t i, len = PL_HDR + 4u;
    int rc;
    if (!pl.ready) return PL_E_NOFLASH;
    if (id >= PL_IDS) return PL_E_ARGS;
    if (pl.ent[id].type != PL_T_PATCH) return PL_OK;  /* nothing to delete */
    if ((rc = pl_reserve(len)) != 0) return rc;
    for (i = 0; i < len; i++) pl_rec[i] = 0;
    pl_rec[0] = (uint8_t)len;
    pl_rec[2] = PL_T_DEL; pl_rec[3] = (uint8_t)id; pl_rec[8] = PL_VER;
    {
        uint32_t old = pl.ent[id].len;
        if ((rc = pl_append(pl_rec, len)) != 0) return rc;
        pl.live_bytes -= old;
    }
    return PL_OK;
}
