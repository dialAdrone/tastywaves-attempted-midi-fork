/* SPDX-License-Identifier: GPL-3.0-only */
/* The patch commands of the VW link (T17 stage P2, docs/PATCH-STORAGE-DESIGN.md section 12): list, read, write and delete the
 * user patches of the log (patchlog.h) from the computer: the web tool's patch library (backup, restore, rename, reorder).
 * wtlink.h's wt_handle passes cmd 12..14 here. Nothing in this file touches flash: every write goes through pl_put / pl_del.
 *
 * A patch body (PGET reply, PPUT payload after the id): name[12] (zero padded) | n | pw | int16 v[n] | int16 pattern[pw], little
 * endian: what a log record holds without its header and CRC. The values are not interpreted here (the panel clamps every
 * value on load), only the shape is checked.
 *   PDIR  start, count (1..16)        -> per id 16 B: id, present, n, pw, name[12]
 *   PGET  id                          -> the body (rc 10: no patch with that id)
 *   PPUT  id, body                    -> writes the patch (a new record; the old one of the id is replaced); rc 11 store full
 *   PDEL  id                          -> deletes it (rc 0 also when there was none)
 * Return codes: 1 bad arguments, 4 the write failed (flash), 7 busy (a table upload runs), 9 no flash, 10 no such patch, 11 store full.
 * The app learns which ids changed from pl_link_dirty (bit per id), to refresh the names it shows. */
#pragma once
enum { PLK_CMD_PDIR = 12, PLK_CMD_PGET = 13, PLK_CMD_PPUT = 14, PLK_CMD_PDEL = 15 };
enum { PLK_E_ARGS = 1, PLK_E_FLASH = 4, PLK_E_BUSY = 7, PLK_E_NOFLASH = 9, PLK_E_NONE = 10, PLK_E_FULL = 11 };
static volatile uint32_t pl_link_dirty;

static uint32_t plk_rc(int rc)
{
    return rc == PL_OK ? 0u : rc == PL_E_FULL ? PLK_E_FULL : rc == PL_E_NONE ? PLK_E_NONE : rc == PL_E_NOFLASH ? PLK_E_NOFLASH :
           rc == PL_E_ARGS ? PLK_E_ARGS : PLK_E_FLASH;
}

/* a: payload, len: its length, out: 256 B reply buffer, *n: reply length. Returns the rc. */
static uint32_t pl_link(uint32_t cmd, const uint8_t *a, uint32_t len, uint8_t *out, uint32_t *n)
{
    static pl_patch_t p;
    uint32_t i, k, id;
    *n = 0;
    if (!pl.ready || !flash_ok) return PLK_E_NOFLASH;
    if (cmd == PLK_CMD_PDIR) {
        if (len != 2u || a[0] >= PL_IDS || a[1] == 0u || a[1] > 16u || a[0] + a[1] > PL_IDS) return PLK_E_ARGS;
        for (id = a[0]; id < (uint32_t)a[0] + a[1]; id++) {
            int rc = pl_present() >> id & 1u ? pl_get(id, &p) : PL_E_NONE;
            out[(*n)++] = (uint8_t)id;
            out[(*n)++] = rc == PL_OK;
            out[(*n)++] = rc == PL_OK ? p.n : 0u;
            out[(*n)++] = rc == PL_OK ? p.pw : 0u;
            for (i = 0; i < PL_NAME; i++) out[(*n)++] = rc == PL_OK ? (uint8_t)p.name[i] : 0u;
        }
        return 0;
    }
    if (cmd == PLK_CMD_PGET) {
        int rc;
        if (len != 1u || a[0] >= PL_IDS) return PLK_E_ARGS;
        rc = pl_get(a[0], &p);
        if (rc) return plk_rc(rc);
        for (i = 0; i < PL_NAME; i++) out[(*n)++] = (uint8_t)p.name[i];
        out[(*n)++] = p.n;
        out[(*n)++] = p.pw;
        for (i = 0; i < p.n; i++) { out[(*n)++] = (uint8_t)p.v[i]; out[(*n)++] = (uint8_t)((uint16_t)p.v[i] >> 8); }
        for (i = 0; i < p.pw; i++) { out[(*n)++] = (uint8_t)p.pv[i]; out[(*n)++] = (uint8_t)((uint16_t)p.pv[i] >> 8); }
        return 0;
    }
    if (cmd == PLK_CMD_PPUT) {
        uint32_t pn, pw, o;
        int rc;
        if (len < 1u + PL_NAME + 2u || a[0] >= PL_IDS) return PLK_E_ARGS;
        pn = a[1u + PL_NAME];
        pw = a[2u + PL_NAME];
        if (pn > PL_MAXP || pw > PL_MAXPW || len != 1u + PL_NAME + 2u + 2u * pn + 2u * pw) return PLK_E_ARGS;
        for (i = 0; i < PL_NAME; i++) {
            uint8_t c = a[1u + i];
            if (c && (c < 32u || c > 126u)) return PLK_E_ARGS;       /* names are printable ASCII, zero padded */
            p.name[i] = (char)c;
        }
        p.name[PL_NAME] = 0;
        p.n = (uint8_t)pn;
        p.pw = (uint8_t)pw;
        o = 3u + PL_NAME;
        for (k = 0; k < pn; k++, o += 2u) p.v[k] = (int16_t)(a[o] | a[o + 1u] << 8);
        for (k = 0; k < pw; k++, o += 2u) p.pv[k] = (int16_t)(a[o] | a[o + 1u] << 8);
        rc = pl_put(a[0], &p);
        if (rc == PL_OK) pl_link_dirty |= 1u << a[0];
        return plk_rc(rc);
    }
    if (cmd == PLK_CMD_PDEL) {
        int rc;
        if (len != 1u || a[0] >= PL_IDS) return PLK_E_ARGS;
        rc = pl_del(a[0]);
        if (rc == PL_OK) pl_link_dirty |= 1u << a[0];
        return plk_rc(rc);
    }
    return PLK_E_ARGS;
}
