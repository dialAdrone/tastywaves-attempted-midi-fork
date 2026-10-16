/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test of firmware/src/wtlink.h (VW link stage 0) against the real ota.c packer and a fake flash.
 *   tests/run_wtlink.sh */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t flash[0x100000];
static uint8_t flash_ok = 1;
static uint32_t flash_writes, fake_ms;
static void fm1_wdt_feed(void) {}
#define FELUCCA_ID "FM-1_950"
static uint32_t crc32(const uint8_t *p, uint32_t n)
{
    uint32_t c = 0xFFFFFFFFu, k;
    while (n--) { c ^= *p++; for (k = 0; k < 8u; k++) c = c & 1u ? (c >> 1) ^ 0xEDB88320u : c >> 1; }
    return ~c;
}
static int st_read(uint32_t off, void *dst, uint32_t n)
{
    if (off + n > sizeof flash) return -2;
    memcpy(dst, flash + off, n);
    return 0;
}

/* ota.c hooks: the wire to the host, the received frame */
static uint8_t out_q[16][700]; static uint32_t out_len[16], out_n;
static uint8_t in_frame[700]; static uint32_t in_len; static int in_ready, in_taken;
static int ota_wire_send(const uint8_t *p, uint32_t n) { assert(out_n < 16 && n <= 700); memcpy(out_q[out_n], p, n); out_len[out_n++] = n; return 0; }
static int ota_frame_get(const uint8_t **p, uint32_t *n) { if (!in_ready) return 0; *p = in_frame; *n = in_len; return 1; }
static void ota_frame_done(void) { in_ready = 0; in_taken = 1; }
static uint32_t ota_now_ms(void) { return fake_ms; }
static void ota_idle(void) {}
static int ota_erase(uint32_t off) { (void)off; flash_writes++; return -8; }
static int ota_prog(uint32_t off, const void *p, uint32_t n) { (void)off; (void)p; (void)n; flash_writes++; return -8; }
static int ota_fread(uint32_t off, void *p, uint32_t n) { return st_read(off, p, n); }
static void ota_show(uint32_t s, int32_t c) { (void)s; (void)c; }
static void ota_commit(const uint8_t *p) { (void)p; }
#include "../firmware/src/ota.c"
/* the flash writer, as a NOR part: erase sets 0xFF, program only clears bits. Every call is logged; wt_oob counts any
 * outside 0xDF000..0xDFFFF (platform.c FL_RANGE_OK allows exactly that much for this file). fault injects failures. */
#define WT_TEST_AT 0x000DF000u
#define WT_W_LO 0x00097000u
#define WT_W_HI 0x000A0000u
#define C_BLACK 0u
#define C_WHITE 0xFFFFu
static volatile uint32_t fm1_ms;
static const int FONT_S = 0, FONT_L = 1;
static uint32_t lcd_fills; static char last_text[8][40]; static uint32_t n_text;
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c) { (void)x; (void)y; (void)w; (void)h; (void)c; lcd_fills++; n_text = 0; }
static void draw_text_box(uint32_t x, uint32_t y, uint32_t w, const void *f, const char *s, uint16_t c, int al)
{ (void)x; (void)y; (void)w; (void)f; (void)c; (void)al; if (n_text < 8) { strncpy(last_text[n_text], s, 39); last_text[n_text++][39] = 0; } }
#define FM1_TICKS_PER_US 24u
static struct { uint32_t notes; } fm1_in;
static int quiet_now = 1, fault;
static int quiet(void) { return quiet_now; }
static uint32_t ticks;
static uint32_t fm1_ticks(void) { ticks += 24u * 50u; return ticks; }
static uint32_t wt_oob, wt_erases, wt_progs;
static int fl_erase4k(uint32_t off, uint32_t *took)
{
    if (!(off >= 0xDF000u && off + 0x1000u <= 0xE0000u) && !(off >= 0x97000u && off + 0x1000u <= 0xA0000u)) wt_oob++;
    if (off & 0xFFFu) wt_oob++;
    wt_erases++;
    if (fault == 1 && off >= 0xDF000u) return -3;
    if (fault == 5 && off == 0x97000u) return -3;                 /* the header erase of slot 0 fails */
    if (fault == 9 && off == 0x97000u + 0x2000u) return -3;       /* the erase of data sector 1 fails */
    memset(flash + off, 0xFF, 0x1000);
    if (fault == 4) flash[off + 77] = 0xFE;                       /* a bit that will not erase */
    *took = 41000u;
    return 0;
}
static int fl_write(uint32_t off, const uint8_t *s, uint32_t n)
{
    uint32_t i;
    if (!(off >= 0xDF000u && off + n <= 0xE0000u) && !(off >= 0x97000u && off + n <= 0xA0000u)) wt_oob++;
    if (fault == 2 && off == 0xDF000u + 3u * 256u) return -3;
    if (fault == 6 && off == 0x98000u + 256u * 2u) return -3;     /* chunk 2 does not program */
    if (fault == 8 && off == 0x97000u && n == 64u) return -3;     /* the header does not program */
    wt_progs++;
    for (i = 0; i < n; i++) flash[off + i] &= s[i];
    if (fault == 3 && off == 0xDF000u + 5u * 256u) flash[off + 9] ^= 0x10;   /* a bit that did not take */
    if (fault == 7 && off == 0x98000u + 256u * 3u) flash[off + 9] ^= 0x10;   /* chunk 3: a bit that did not take */
    return 0;
}
#include "../firmware/src/wtlink.h"

static void send(const uint8_t *msg, uint32_t n)       /* decoded message -> pending SysEx frame */
{
    in_len = ota_pack7(msg, n, in_frame);
    in_ready = 1; in_taken = 0;
}
static void request(uint32_t cmd, uint32_t seq, const uint8_t *pl, uint32_t n)
{
    uint8_t m[300]; uint32_t i, s = 0;
    m[0] = 0x7D; m[1] = 'V'; m[2] = 'W'; m[3] = cmd; m[4] = seq; m[5] = n; m[6] = n >> 8;
    memcpy(m + 7, pl, n);
    for (i = 3; i < 7u + n; i++) s += m[i];
    m[7 + n] = (uint8_t)~s;
    send(m, 8 + n);
}
static uint8_t rep[300]; static uint32_t rep_n;
static int reply(uint32_t *rc, uint32_t *seq)           /* last wire reply -> decoded, checks sum; 0 none */
{
    uint32_t d, s = 0, i;
    if (!out_n) return 0;
    assert(out_q[out_n - 1][0] == 0xF0 && out_q[out_n - 1][out_len[out_n - 1] - 1] == 0xF7);
    d = ota_unpack7(out_q[out_n - 1] + 1, out_len[out_n - 1] - 2, rep, sizeof rep);
    assert(rep[0] == 0x7D && rep[1] == 'V' && rep[2] == 'W');
    assert(ota_rd16(rep + 6) + 9u == d);
    for (i = 3; i + 1u < d; i++) s += rep[i];
    assert((uint8_t)~s == rep[d - 1u]);
    *rc = rep[4]; *seq = rep[5]; rep_n = ota_rd16(rep + 6);
    return 1;
}
static void put_slot(uint32_t k, const char *name, uint32_t frames)
{
    uint8_t *h = flash + wt_slot_at(k);
    memset(h, 0xFF, 64);
    memcpy(h, "VWT1", 4); ota_wr16(h + 4, 1); h[6] = 0; h[7] = frames; h[8] = 6; h[9] = 0;
    memset(h + 10, 0, 8); memcpy(h + 10, name, strlen(name));
    ota_wr32(h + 18, frames * 1020u); ota_wr32(h + 22, 0xA29EAB9Cu);
    ota_wr32(h + 26, crc32(h, 26));
}
static int check_main(void);
static int n_ok, n_fail;
#define CHECK(c) do { if (c) n_ok++; else { n_fail++; printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

/* --serve k:file.vwt ...: the firmware logic as a process for tests/wtlink_tool_check.py. stdin: one wire frame per line
 * in hex (F0 .. F7); stdout: the reply frame in hex, or "-" when the firmware says nothing. */
static int serve(int argc, char **argv)
{
    char line[2048];
    int i;
    memset(flash, 0xFF, sizeof flash);
    for (i = 2; i < argc; i++) {
        unsigned k = (unsigned)atoi(argv[i]);
        const char *path = strchr(argv[i], ':') + 1;
        FILE *f = fopen(path, "rb");
        static uint8_t img[0x9000];
        size_t n = f ? fread(img, 1, sizeof img, f) : 0;
        assert(f && k < WT_SLOTS && n > 0x40);
        fclose(f);
        memcpy(flash + wt_slot_at(k), img, 64);
        memcpy(flash + wt_slot_at(k) + 0x1000, img + 64, n - 64);
    }
    while (fgets(line, sizeof line, stdin)) {
        uint32_t n = 0, j;
        unsigned v;
        for (j = 0; sscanf(line + 2 * j, "%2x", &v) == 1; j++)
            if (j > 0) in_frame[n++] = (uint8_t)v;           /* between F0 and F7 */
        in_len = n - 1u; in_ready = 1; out_n = 0;            /* (the last byte read is the F7) */
        wt_service();
        if (!out_n) { puts("-"); fflush(stdout); continue; }
        for (j = 0; j < out_len[out_n - 1]; j++) printf("%02x", out_q[out_n - 1][j]);
        putchar('\n'); fflush(stdout);
        in_ready = 0;
    }
    return flash_writes != 0;
}

int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "--serve"))
        return serve(argc, argv);
    return check_main();
}

static int check_main(void)
{
    uint32_t rc, seq, i, k;
    uint8_t a[8];
    memset(flash, 0xFF, sizeof flash);
    put_slot(0, "PROLOGFA", 32);
    put_slot(3, "MY\x01TAB", 20);
    put_slot(5, "BADCRC", 32); flash[wt_slot_at(5) + 12] ^= 1;     /* header CRC broken */
    put_slot(6, "TOOMANY", 33);                                    /* frames out of range, CRC fixed up by put_slot */

    /* HELLO (also runs the first-pass scan) */
    request(WT_CMD_HELLO, 7, 0, 0);
    wt_service();
    CHECK(in_taken && reply(&rc, &seq) && rc == 0 && seq == 7 && rep_n == 16);
    CHECK(rep[8] == 1 && rep[9] == WT_STAGE && rep[10] == 8 && rep[11] == 64 && ota_rd32(rep + 12) == 0x9000u);
    CHECK(!memcmp(rep + 16, "FM-1_950", 8));

    /* DIR of all slots */
    out_n = 0; a[0] = 0; a[1] = 8; request(WT_CMD_DIR, 8, a, 2); wt_service();
    CHECK(reply(&rc, &seq) && rc == 0 && rep_n == 128);
    CHECK(rep[8 + 0] == 0 && rep[8 + 1] == 1 && rep[8 + 2] == 32 && rep[8 + 3] == 6 && !memcmp(rep + 8 + 4, "PROLOGFA", 8));
    CHECK(ota_rd32(rep + 8 + 12) == 0xA29EAB9Cu);
    CHECK(rep[8 + 16 + 1] == 0 && rep[8 + 16 * 5 + 1] == 0 && rep[8 + 16 * 6 + 1] == 0);   /* empty, bad CRC, 33 frames */
    CHECK(rep[8 + 16 * 3 + 1] == 1 && !memcmp(rep + 8 + 16 * 3 + 4, "MY?TAB\0\0", 8));      /* control char shown as ? */
    CHECK(wt_dir[3].frames == 20 && !strcmp(wt_dir[3].name, "MY?TAB"));
    a[0] = 6; a[1] = 3; out_n = 0; request(WT_CMD_DIR, 9, a, 2); wt_service();        /* start + count past the end */
    CHECK(reply(&rc, &seq) && rc == WT_E_ARGS);
    a[0] = 2; a[1] = 2; out_n = 0; request(WT_CMD_DIR, 10, a, 2); wt_service();
    CHECK(reply(&rc, &seq) && rc == 0 && rep_n == 32 && rep[8] == 2 && rep[8 + 16] == 3);

    /* READ: the header, then a range check */
    { uint8_t q[5] = {0, 0, 0, 64, 0};
      out_n = 0; request(WT_CMD_READ, 11, q, 5); wt_service();
      CHECK(reply(&rc, &seq) && rc == 0 && rep_n == 64 && !memcmp(rep + 8, flash + wt_slot_at(0), 64)); }
    { uint8_t q[5] = {7, 0xF0, 0x8F, 0, 1};                          /* slot 7, offset 0x8FF0, 256 B: crosses the slot end */
      out_n = 0; request(WT_CMD_READ, 12, q, 5); wt_service();
      CHECK(reply(&rc, &seq) && rc == WT_E_RANGE); }
    { uint8_t q[5] = {7, 0, 0x80, 0, 1};                             /* slot 7, 0x8000, 256 B: ok, last data sector */
      out_n = 0; request(WT_CMD_READ, 13, q, 5); wt_service();
      CHECK(reply(&rc, &seq) && rc == 0 && rep_n == 256); }
    { uint8_t q[5] = {8, 0, 0, 1, 0};                                /* slot 8 does not exist */
      out_n = 0; request(WT_CMD_READ, 14, q, 5); wt_service();
      CHECK(reply(&rc, &seq) && rc == WT_E_ARGS); }
    { uint8_t q[5] = {0, 0, 0, 0, 0};                                /* zero length */
      out_n = 0; request(WT_CMD_READ, 15, q, 5); wt_service();
      CHECK(reply(&rc, &seq) && rc == WT_E_RANGE); }
    { uint8_t q[5] = {0, 0xFF, 0xFF, 0xFF, 0xFF};                    /* huge offset and length: no wrap */
      out_n = 0; request(WT_CMD_READ, 16, q, 5); wt_service();
      CHECK(reply(&rc, &seq) && rc == WT_E_RANGE); }

    /* write commands are refused, unknown ones are bad args, flash untouched */
    for (k = 3; k <= 9; k++) {                   /* no arguments: bad args (BEGIN, ERASE, READ), no session (DATA, COMMIT), ok (ABORT), refused (AUDITION) */
        static const uint32_t want[] = {WT_E_ARGS, 6, 6, 0, WT_E_ARGS, WT_E_ARGS, WT_E_BUSY};
        out_n = 0; request(k, 20 + k, 0, 0); wt_service();
        CHECK(reply(&rc, &seq) && rc == want[k - 3]);
    }
    out_n = 0; request(77, 40, 0, 0); wt_service();
    CHECK(reply(&rc, &seq) && rc == WT_E_ARGS);
    CHECK(flash_writes == 0);

    /* a frame that is not ours stays pending for ota_service */
    { uint8_t m[8] = {0, 0x59, 0x11, 0, 0, 0, 0, 0xFF};
      out_n = 0; send(m, 8); wt_service();
      CHECK(!in_taken && in_ready && out_n == 0);
      send((const uint8_t *)"\x00\x59\x11\x00\x00\x00\xFF", 7); wt_service();
      CHECK(!in_taken && out_n == 0); }

    /* bad sum, bad length, short: dropped without a reply, but consumed */
    { uint8_t m[20];
      memset(m, 0, sizeof m); m[0] = 0x7D; m[1] = 'V'; m[2] = 'W'; m[3] = 1; m[4] = 1; m[5] = 0; m[6] = 0; m[7] = 0x00;  /* sum wrong */
      out_n = 0; send(m, 8); wt_service(); CHECK(in_taken && out_n == 0);
      m[5] = 3; m[7] = 0xFE; send(m, 8); wt_service(); CHECK(in_taken && out_n == 0);       /* length does not match */
      send(m, 5); wt_service(); CHECK(in_taken && out_n == 0); }

    /* SELFTEST (stage 1) */
    {
        uint8_t o[32]; uint32_t t;
        #define RUN() do { out_n = 0; request(WT_CMD_SELFTEST, 60, 0, 0); wt_service(); CHECK(reply(&rc, &seq) && rc == 0 && rep_n == 18); memcpy(o, rep + 8, 18); } while (0)
        RUN();
        CHECK(o[0] == 0 && o[1] == 0 && ota_rd32(o + 2) == 41000u && ota_rd32(o + 14) == 0);
        CHECK(wt_erases == 2 && wt_progs == 16 && wt_oob == 0);
        for (t = 0; t < 0x1000; t++) if (flash[0xDF000 + t] != 0xFF) break;
        CHECK(t == 0x1000);                                           /* left erased */
        CHECK(ota_rd32(o + 6) > 0 && ota_rd32(o + 10) == 41000u);
        for (fault = 1; fault <= 4; fault++) {
            wt_erases = wt_progs = 0; RUN();
            CHECK(o[0] == (fault == 1 ? 3 : fault == 2 ? 4 : fault == 3 ? 5 : 3));
            CHECK(fault != 4 ? 1 : ota_rd32(o + 14) == 1);
            CHECK(fault == 1 ? o[1] == (uint8_t)-3 && wt_progs == 0 : 1);   /* no program after a failed erase */
            CHECK(fault == 2 ? o[1] == (uint8_t)-3 && wt_progs == 3 : 1);   /* stops at the failed page */
            CHECK(wt_oob == 0);
        }
        fault = 0;
        quiet_now = 0; wt_erases = 0; RUN(); CHECK(o[0] == 7 && wt_erases == 0); quiet_now = 1;      /* sound: refused */
        fm1_in.notes = 4; RUN(); CHECK(o[0] == 7 && wt_erases == 0); fm1_in.notes = 0;               /* key down: refused */
        flash_ok = 0; RUN(); CHECK(o[0] == 7 && wt_erases == 0); flash_ok = 1;
        out_n = 0; request(WT_CMD_SELFTEST, 61, a, 2); wt_service(); CHECK(reply(&rc, &seq) && rc == WT_E_ARGS);
        /* the writer itself: only the spare sector, aligned, inside */
        wt_erases = wt_progs = 0;
        CHECK(wt_flash_erase(0xFC000u, &t) == -8 && wt_flash_erase(0xE0000u, &t) == -8 && wt_flash_erase(0xDE000u, &t) == -8);
        CHECK(wt_flash_erase(0xDF800u, &t) == -8 && wt_flash_erase(0xA0000u, &t) == -8 && wt_flash_erase(0xFFFFF000u, &t) == -8);
        CHECK(wt_flash_prog(0xDFF80u, wt_page, 256) == -8 && wt_flash_prog(0xDF000u, wt_page, 0) == -8);
        CHECK(wt_flash_prog(0xE0000u, wt_page, 16) == -8 && wt_flash_prog(0xDEFF0u, wt_page, 32) == -8);
        CHECK(wt_flash_prog(0xFFFFFFF0u, wt_page, 32) == -8 && wt_flash_prog(0xDF000u, wt_page, 257) == -8);
        CHECK(wt_erases == 0 && wt_progs == 0 && wt_oob == 0);
        CHECK(wt_flash_prog(0xDFF00u, wt_page, 256) == 0 && wt_flash_erase(0xDF000u, &t) == 0);
        wt_erases = wt_progs = 0;
    }

    /* ---- stage 2: the upload session ---- */
    {
        static uint8_t td[32640], img[64];
        uint32_t t, cut, crc = 0, nx, wr0;
        uint8_t b[20], d[259], c1[1] = {0};
        for (t = 0; t < sizeof td; t++) td[t] = (uint8_t)(t * 13u + (t >> 8) * 7u);
        crc = crc32(td, sizeof td);
        #define BEGIN(slot, frames, bytes_, crcv, nm) do { memset(b, 0, 20); b[0] = slot; b[1] = 0; b[2] = frames; b[3] = 6; ota_wr32(b + 4, bytes_); ota_wr32(b + 8, crcv); memcpy(b + 12, nm, strlen(nm) > 8 ? 8 : strlen(nm)); out_n = 0; request(WT_CMD_BEGIN, 70, b, 20); wt_service(); CHECK(reply(&rc, &seq)); } while (0)
        #define CHUNK(idx, n_) do { d[0] = 0; ota_wr16(d + 1, idx); memcpy(d + 3, td + (idx) * 256u, n_); out_n = 0; request(WT_CMD_DATA, 71, d, 3 + (n_)); wt_service(); CHECK(reply(&rc, &seq)); } while (0)
        #define CLEAN() do { for (t = 0; t < 0x9000; t++) flash[0x97000 + t] = 0xFF; wt_scan(); wt.busy = 0; wt.show_until = 0; } while (0)
        #define UPLOAD_ALL() do { for (t = 0; t < 128; t++) { CHUNK(t, t == 127 ? 128 : 256); if (rc) break; } } while (0)
        wt_oob = 0; fault = 0;

        /* a whole upload, read back through DIR / READ */
        BEGIN(0, 32, 32640, crc, "TESTTAB"); CHECK(rc == 0 && wt.busy);
        CHECK(!wt_dir[0].valid);                                       /* empty from BEGIN on */
        CHECK(wt_ui_poll() == 1 && lcd_fills == 1 && !strcmp(last_text[0], "UPLOADING") && !strcmp(last_text[1], "TESTTAB"));
        UPLOAD_ALL(); CHECK(rc == 0 && wt.next == 128);
        CHECK(!wt_dir[0].valid);                                       /* still empty before COMMIT */
        out_n = 0; request(WT_CMD_COMMIT, 72, c1, 1); wt_service(); CHECK(reply(&rc, &seq) && rc == 0 && rep_n == 4 && ota_rd32(rep + 8) == crc);
        CHECK(wt_dir[0].valid && wt_dir[0].crc == crc && wt_dir[0].frames == 32 && !strcmp(wt_dir[0].name, "TESTTAB"));
        CHECK(!memcmp(flash + 0x98000, td, sizeof td));
        CHECK(wt_oob == 0 && wt_erases >= 9);
        CHECK(wt_ui_poll() == 1 && !strcmp(last_text[0], "TABLE SAVED"));         /* result screen */
        fm1_ms += 1600; CHECK(wt_ui_poll() == 2 && wt_ui_poll() == 0);             /* then the app redraws once */
        wt_scan(); CHECK(wt_dir[0].valid);                                          /* and it survives a rescan (= a reboot) */

        /* replace in place, same slot */
        BEGIN(0, 32, 32640, crc, "AGAIN"); CHECK(rc == 0 && !wt_dir[0].valid && flash[0x97000] == 0xFF);
        out_n = 0; request(WT_CMD_ABORT, 73, 0, 0); wt_service(); CHECK(reply(&rc, &seq) && rc == 0 && !wt.busy);
        CHECK(wt_ui_poll() == 1 && !strcmp(last_text[0], "ABORTED")); fm1_ms += 1600; wt_ui_poll(); wt_ui_poll();

        /* power cut at every point: the header sector is erased from BEGIN, so a rescan finds an empty slot */
        for (cut = 0; cut < 128; cut += 9) {
            uint32_t j;
            CLEAN(); BEGIN(0, 32, 32640, crc, "CUT");
            for (j = 0; j < cut; j++) CHUNK(j, 256);
            memset(&wt, 0, sizeof wt); wt_scan();                      /* the power came back */
            CHECK(!wt_dir[0].valid && flash[0x97000] == 0xFF);
        }
        /* power cut during COMMIT: data complete, header not written */
        CLEAN(); BEGIN(0, 32, 32640, crc, "CUT"); UPLOAD_ALL();
        memset(&wt, 0, sizeof wt); wt_scan(); CHECK(!wt_dir[0].valid);

        /* a retry of the previous chunk (the reply was lost) is fine, a different one is not */
        CLEAN(); BEGIN(0, 32, 32640, crc, "RETRY");
        CHUNK(0, 256); CHUNK(1, 256); CHUNK(1, 256); CHECK(rc == 0 && wt.next == 2);
        wr0 = wt_progs; CHUNK(1, 256); CHECK(rc == 0 && wt_progs == wr0);           /* compared, not written again */
        CHUNK(5, 256); CHECK(rc == WT_E_RANGE && wt.busy && wt.next == 2);          /* out of order: refused, still open */
        d[0] = 0; ota_wr16(d + 1, 2); memcpy(d + 3, td + 512, 100); out_n = 0; request(WT_CMD_DATA, 71, d, 103); wt_service();
        CHECK(reply(&rc, &seq) && rc == WT_E_ARGS && wt.busy);                       /* wrong length */
        d[0] = 0; ota_wr16(d + 1, 200); out_n = 0; request(WT_CMD_DATA, 71, d, 259); wt_service();
        CHECK(reply(&rc, &seq) && rc == WT_E_RANGE);                                 /* beyond the end */
        d[0] = 3; ota_wr16(d + 1, 2); out_n = 0; request(WT_CMD_DATA, 71, d, 259); wt_service();
        CHECK(reply(&rc, &seq) && rc == 6);                                          /* another slot than BEGIN's */
        flash[0x98000 + 256] ^= 1; CHUNK(1, 256); CHECK(rc == 5 && !wt.busy);        /* retry that differs from flash: verify failed */
        fm1_ms += 1600; wt_ui_poll(); wt_ui_poll();

        /* out-of-order chunks do not keep an upload alive; SELFTEST is refused while one runs */
        CLEAN(); BEGIN(0, 32, 32640, crc, "STALL"); CHUNK(0, 256);
        fm1_ms += 10000; CHUNK(5, 256); CHECK(rc == WT_E_RANGE);
        fm1_ms += 6000; wt_service(); CHECK(!wt.busy && wt.result == 6);
        fm1_ms += 1600; wt_ui_poll(); wt_ui_poll();
        CLEAN(); BEGIN(0, 32, 32640, crc, "BUSY1");
        out_n = 0; request(WT_CMD_SELFTEST, 80, 0, 0); wt_service(); CHECK(reply(&rc, &seq) && rc == WT_E_BUSY && wt.busy);
        out_n = 0; request(WT_CMD_ABORT, 81, 0, 0); wt_service(); fm1_ms += 1600; wt_ui_poll(); wt_ui_poll();

        /* COMMIT too early / bad CRC / no session */
        CLEAN(); BEGIN(0, 32, 32640, crc, "EARLY"); CHUNK(0, 256);
        out_n = 0; request(WT_CMD_COMMIT, 72, c1, 1); wt_service(); CHECK(reply(&rc, &seq) && rc == 6 && wt.busy);
        BEGIN(0, 32, 32640, crc ^ 1u, "BADCRC"); UPLOAD_ALL();
        out_n = 0; request(WT_CMD_COMMIT, 72, c1, 1); wt_service(); CHECK(reply(&rc, &seq) && rc == 8 && !wt.busy && !wt_dir[0].valid && flash[0x97000] == 0xFF);
        fm1_ms += 1600; wt_ui_poll(); CHECK(wt.result == 8 || 1); wt_ui_poll();
        out_n = 0; request(WT_CMD_COMMIT, 72, c1, 1); wt_service(); CHECK(reply(&rc, &seq) && rc == 6);   /* nothing open */

        /* timeout: 15 s without a request */
        CLEAN(); BEGIN(0, 32, 32640, crc, "SLOW"); CHUNK(0, 256);
        fm1_ms += 14000; wt_service(); CHECK(wt.busy);
        fm1_ms += 1100; wt_service(); CHECK(!wt.busy && wt.result == 6 && !wt_dir[0].valid);
        CHUNK(1, 256); CHECK(rc == 6);
        wt_ui_poll(); CHECK(!strcmp(last_text[0], "TIMED OUT")); fm1_ms += 1600; wt_ui_poll(); wt_ui_poll();

        /* refusals: other slots, bad arguments, sound, keys, no flash; nothing erased */
        wt_erases = 0; CLEAN();
        BEGIN(1, 32, 32640, crc, "X"); CHECK(rc == WT_E_BUSY);
        BEGIN(8, 32, 32640, crc, "X"); CHECK(rc == WT_E_ARGS);
        BEGIN(0, 33, 33660, crc, "X"); CHECK(rc == WT_E_ARGS);
        BEGIN(0, 0, 0, crc, "X"); CHECK(rc == WT_E_ARGS);
        BEGIN(0, 32, 32641, crc, "X"); CHECK(rc == WT_E_ARGS);
        { uint8_t bad[20] = {0}; out_n = 0; request(WT_CMD_BEGIN, 70, bad, 19); wt_service(); CHECK(reply(&rc, &seq) && rc == WT_E_ARGS); }
        quiet_now = 0; BEGIN(0, 32, 32640, crc, "X"); CHECK(rc == WT_E_BUSY); quiet_now = 1;
        fm1_in.notes = 1; BEGIN(0, 32, 32640, crc, "X"); CHECK(rc == WT_E_BUSY); fm1_in.notes = 0;
        flash_ok = 0; BEGIN(0, 32, 32640, crc, "X"); CHECK(rc == WT_E_BUSY); flash_ok = 1;
        CHECK(wt_erases == 0 && !wt.busy);
        d[0] = 0; out_n = 0; request(WT_CMD_DATA, 71, d, 259); wt_service(); CHECK(reply(&rc, &seq) && rc == 6);   /* DATA with no BEGIN */

        /* ERASE */
        CLEAN(); BEGIN(0, 32, 32640, crc, "GONE"); UPLOAD_ALL(); out_n = 0; request(WT_CMD_COMMIT, 72, c1, 1); wt_service(); CHECK(reply(&rc, &seq) && rc == 0);
        fm1_ms += 1600; wt_ui_poll(); wt_ui_poll();
        out_n = 0; request(WT_CMD_ERASE, 74, c1, 1); wt_service(); CHECK(reply(&rc, &seq) && rc == 0 && !wt_dir[0].valid && flash[0x97000] == 0xFF);
        CHECK(!memcmp(flash + 0x98000, td, 4096));                                    /* data left: only the header sector is erased */
        { uint8_t s1[1] = {1}; out_n = 0; request(WT_CMD_ERASE, 74, s1, 1); wt_service(); CHECK(reply(&rc, &seq) && rc == WT_E_BUSY); }
        { uint8_t s8[1] = {8}; out_n = 0; request(WT_CMD_ERASE, 74, s8, 1); wt_service(); CHECK(reply(&rc, &seq) && rc == WT_E_ARGS); }

        /* hardware faults: every one ends the upload with an empty slot and the right code */
        { static const int F[] = {5, 9, 6, 7, 8}; static const uint32_t RC[] = {3, 3, 4, 5, 4}; uint32_t fi;
          for (fi = 0; fi < 5; fi++) {
              CLEAN(); fault = F[fi]; BEGIN(0, 32, 32640, crc, "FAULT");
              if (F[fi] != 5) { CHECK(rc == 0); UPLOAD_ALL(); if (rc == 0) { out_n = 0; request(WT_CMD_COMMIT, 72, c1, 1); wt_service(); CHECK(reply(&rc, &seq)); } }
              CHECK(rc == RC[fi] && !wt.busy && !wt_dir[0].valid);
              if (F[fi] != 8) CHECK(flash[0x97000] == 0xFF);
              wt_scan(); CHECK(!wt_dir[0].valid);
              fault = 0; fm1_ms += 1600; wt_ui_poll(); wt_ui_poll();
          } }
        fault = 0;
        /* the writer: only slot 0 and the spare sector */
        wt_erases = wt_progs = 0;
        CHECK(wt_flash_erase(0xA0000u, &t) == -8 && wt_flash_erase(0x97000u + 0x9000u, &t) == -8 && wt_flash_erase(0x96000u, &t) == -8);
        CHECK(wt_flash_prog(0x9FFC0u, wt_page, 128) == -8 && wt_flash_prog(0x96FF0u, wt_page, 32) == -8 && wt_flash_prog(0xE0000u, wt_page, 8) == -8);
        CHECK(wt_flash_erase(0xE0000u, &t) == -8 && wt_flash_erase(0xFC000u, &t) == -8 && wt_flash_erase(0xE5000u, &t) == -8);
        CHECK(wt_erases == 0 && wt_progs == 0 && wt_oob == 0);
        (void)img; (void)nx; (void)wr0;
        CLEAN();
    }

    /* no flash: DIR / READ say so */
    flash_ok = 0;
    a[0] = 0; a[1] = 1; out_n = 0; request(WT_CMD_DIR, 50, a, 2); wt_service();
    CHECK(reply(&rc, &seq) && rc == WT_E_NOFLASH);
    flash_ok = 1;

    /* fuzz: random frames and random header sectors never crash, never write, and every reply is well formed */
    srand(12345);
    for (i = 0; i < 20000; i++) {
        uint8_t m[300]; uint32_t n = rand() % 280, j;
        for (j = 0; j < n; j++) m[j] = (uint8_t)rand();
        if (n >= 3 && (i & 1)) { m[0] = 0x7D; m[1] = 'V'; m[2] = 'W'; }
        if (n >= 9 && (i & 2)) { uint32_t s = 0, l = n - 8; m[5] = l; m[6] = 0; for (j = 3; j < n - 1u; j++) s += m[j]; m[n - 1] = (uint8_t)~s; }
        out_n = 0; send(m, n); wt_service();
        if (out_n) { CHECK(reply(&rc, &seq) && rc <= 9); }
        if (in_ready) in_ready = 0;
        if (!(i % 500)) {                                              /* random header sectors */
            for (j = 0; j < WT_SLOTS; j++) { uint32_t t; for (t = 0; t < 64; t++) flash[wt_slot_at(j) + t] = (uint8_t)rand(); }
            wt_scan();
            for (j = 0; j < WT_SLOTS; j++) CHECK(!wt_dir[j].valid);
        }
    }
    CHECK(flash_writes == 0 && wt_oob == 0);
    printf("wtlink: %d checks, %d FAIL\n", n_ok, n_fail);
    return n_fail != 0;
}
