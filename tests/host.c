/* SPDX-License-Identifier: GPL-3.0-only */
/* A fake FM-1 on a computer: the app (firmware/src/tastywaves.c) runs unchanged on it. platform.c includes
 * this file instead of the hardware when DINGHY_HOST is set. A script presses keys over time; out come
 * the screen (PPM, at the times SHOT asks) and the sound (WAV, as the FM-1 sends it over USB).
 *
 *   mkdir -p build/host
 *   cc -O2 -Wall -DDINGHY_HOST -Ifirmware/src -Ifirmware/gen -o build/host/tastywaves firmware/src/tastywaves.c
 *   build/host/tastywaves build/host/tastywaves        (-> build/host/tastywaves.wav, build/host/tastywaves-<ms>.ppm)
 *   build/host/tastywaves OUT script.txt [FPS]    (your own script; with FPS: OUT-NNNNN.ppm, a frame every 1/FPS s:
 *                                             a film of your firmware, e.g. ffmpeg -r 30 -i OUT-%05d.ppm ...)
 *
 * Script lines (ms first): keys MASK | btn ID | bdown ID | bup ID | enc ID DELTA | midi ST D1 D2 | end. enc ID is 0..6 (ENC_SELECT ..
 * ENC_PRESETS), DELTA signed detents, + = clockwise; fm1_enc_take() returns what has accumulated, then 0, as on the
 * device. LED changes are logged as "LED id on/off". Change SCRIPT0 below to try your own app. */
#include <stdint.h>
#include <stdio.h>
#include "usertab.h"
#include <stdlib.h>
#include <string.h>

#ifndef APP_NAME
#define APP_NAME "TASTYWAVES"
#endif
#ifndef APP_VER
#define APP_VER "0.0"
#endif
#define FS 44100u
#define HALF_FRAMES 256u

/* fm1_ticks: on the FM-1 a free-running 24 MHz counter (hal/fm1_time.h). Here: the host's monotonic clock at the
 * same rate, so code that times itself (app_audio's cpu_last / cpu_max) compiles and shows plausible numbers.
 * They measure this computer, not the chip: never read CPU headroom from a host run. The screens that show them
 * therefore differ from run to run. */
#include <time.h>
#define FM1_TICKS_PER_US 24u
static uint32_t fm1_ticks(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 24000000u + (uint64_t)ts.tv_nsec * 24u / 1000u);
}

static volatile uint32_t fm1_ms;
static uint32_t wt_ui_poll(void) { return 0; }   /* the upload screen (wtlink.h) exists only on the device */
static void wt_user_poll(uint32_t t1, uint32_t t2) { (void)t1; (void)t2; }   /* ... and so does the user tables' CRC check */
static struct { uint32_t notes, buttons, frames; uint8_t raw[11]; } fm1_in;   /* frames / raw: what the boot scan saw (fake here) */
static uint32_t host_edges, host_note_edges;
static int32_t host_enc[7];
static uint64_t host_led;
static uint32_t fm1_input_edges(uint32_t *released) { uint32_t b = host_edges; host_edges = 0; *released = 0; return b; }
static uint32_t fm1_input_note_edges(void) { uint32_t b = host_note_edges; host_note_edges = 0; return b; }
static int32_t fm1_enc_take(uint32_t e)         /* detents since the last call (+ = clockwise), then 0 */
{
    int32_t d;
    if (e >= 7u) return 0;
    d = host_enc[e]; host_enc[e] = 0;
    return d;
}
static void fm1_led_key(uint32_t id, int on)
{
    uint64_t m = 1ull << (id & 63u);
    on = on != 0;
    if (id < 64u && (host_led & m ? 1 : 0) != on) {
        host_led ^= m;
        printf("%6u ms  LED %u %s\n", fm1_ms, id, on ? "on" : "off");
    }
}

/* the screen: 240 x 240 RGB565, as the ST7789 gets it (gfx.c sends the bytes swapped) */
static uint16_t fb[240 * 240];
static void lcd_sync(void) {}
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, j;
    for (j = y; j < y + h && j < 240u; j++)
        for (i = x; i < x + w && i < 240u; i++)
            fb[j * 240u + i] = c;
}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px)
{
    uint32_t i, j;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            if (x + i < 240u && y + j < 240u) {
                uint16_t c = px[j * w + i];
                fb[(y + j) * 240u + x + i] = (uint16_t)(c >> 8 | c << 8);
            }
}
#define CV_MAX (240u * 64u)
#include "gfx.c"

static uint8_t host_store[256 + 4];
static int settings_load(void *p, uint32_t n)
{
    if (host_store[0] != 1 || n > 256u) return -1;
    memcpy(p, host_store + 4, n);
    return 0;
}
static int settings_save(const void *p, uint32_t n)
{
    host_store[0] = 1;
    memcpy(host_store + 4, p, n);
    printf("%6u ms  settings saved (%u B)\n", fm1_ms, n);
    return 0;
}

/* the user patch log (patchlog.h) on a NOR mock: erase sets 0xFF, program only clears bits. DINGHY_PATCHES=file keeps the
 * 16 KiB window between runs (read at start, written at the end), so a script can start with saved patches. */
static uint8_t host_flash[0x100000], flash_ok = 1;
#define PL_W_LO 0x000E5000u
#define PL_W_HI 0x000E9000u
static void fm1_wdt_feed(void) {}
static uint32_t crc32(const uint8_t *p, uint32_t n)
{
    uint32_t c = 0xFFFFFFFFu, k;
    while (n--) { c ^= *p++; for (k = 0; k < 8u; k++) c = c & 1u ? (c >> 1) ^ 0xEDB88320u : c >> 1; }
    return ~c;
}
static int st_read(uint32_t off, void *dst, uint32_t n) { if (off + n > sizeof host_flash) return -2; memcpy(dst, host_flash + off, n); return 0; }
static int fl_erase4k(uint32_t off, uint32_t *took)
{
    if (off < PL_W_LO || off + 0x1000u > PL_W_HI || (off & 0xFFFu)) { fprintf(stderr, "host: erase outside the patch log window\n"); exit(1); }
    memset(host_flash + off, 0xFF, 0x1000);
    *took = 17000u;
    return 0;
}
static int fl_write(uint32_t off, const uint8_t *s, uint32_t n)
{
    uint32_t i;
    if (off < PL_W_LO || off + n > PL_W_HI) { fprintf(stderr, "host: write outside the patch log window\n"); exit(1); }
    for (i = 0; i < n; i++) {
        if ((host_flash[off + i] & s[i]) != s[i]) { fprintf(stderr, "host: program over a 0 bit at %#x\n", off + i); exit(1); }
        host_flash[off + i] &= s[i];
    }
    return 0;
}
#include "patchlog.h"
static int quiet(void);                           /* engine.h */
#include "patchlink.h"
static const char *host_patches_file;
static void host_patches_save(void)
{
    FILE *f = host_patches_file ? fopen(host_patches_file, "wb") : 0;
    if (f) { fwrite(host_flash + PL_W_LO, 1, PL_W_HI - PL_W_LO, f); fclose(f); }
}

static uint8_t mq[64][3];
static uint32_t mq_w, mq_r;
static int midi_get(uint8_t *st, uint8_t *d1, uint8_t *d2)
{
    if (mq_r == mq_w) return 0;
    *st = mq[mq_r % 64u][0]; *d1 = mq[mq_r % 64u][1]; *d2 = mq[mq_r % 64u][2];
    mq_r++;
    return 1;
}
static void host_arp_trace(int on, uint32_t n) { printf("%6u ms  ARP %s %u\n", fm1_ms, on ? "on" : "off", n); }
static void midi_send(uint32_t st, uint32_t d1, uint32_t d2) { printf("%6u ms  MIDI out %02X %u %u\n", fm1_ms, st, d1, d2); }

static void app_audio(int32_t *out, uint32_t n);
static void app_main(void);
static void patch_set(uint32_t id, int32_t v);                   /* patch.h */

/* DINGHY_USERTAB="K:file.vwt[:bad|:unchecked];..." fills user slot K from a table file (tools/fm1_wavetable.py build):
 * as the device after its CRC check (usable), or damaged (listed as BAD, not given to the engine), or not checked yet */
static void host_usertab(const char *spec)
{
    char *dup = strdup(spec), *tok, *save = 0;
    for (tok = strtok_r(dup, ";", &save); tok; tok = strtok_r(0, ";", &save)) {
        unsigned k = (unsigned)atoi(tok);
        char *path = strchr(tok, ':'), *mode;
        FILE *f;
        static uint8_t img[64 + 35648];
        int16_t *data;
        if (!path || k >= UT_SLOTS) { fprintf(stderr, "DINGHY_USERTAB: bad entry '%s'\n", tok); exit(1); }
        path++;
        mode = strchr(path, ':');
        if (mode) *mode++ = 0;
        f = fopen(path, "rb");
        if (!f || fread(img, 1, sizeof img, f) != sizeof img) { fprintf(stderr, "DINGHY_USERTAB: cannot read %s\n", path); exit(1); }
        fclose(f);
        data = malloc(35648);
        memcpy(data, img + 64, 35648);
        wt_dir[k].valid = 1; wt_dir[k].frames = img[7]; wt_dir[k].levels = img[8];
        memcpy(wt_dir[k].name, img + 10, 8); wt_dir[k].name[8] = 0;
        wt_dir[k].bytes = 35648; wt_dir[k].crc = 0;
        wt_dir[k].state = mode && !strcmp(mode, "bad") ? UT_BAD : mode && !strcmp(mode, "unchecked") ? UT_UNCHECKED : UT_OK;
        eng_user_base[k] = wt_dir[k].state == UT_OK ? data : 0;
    }
    free(dup);
}

/* the script: at t ms, keys down (bit k = key k, 0 = F3), a button edge, a MIDI message. A script file (the second
 * argument) replaces it, a line each: "T keys MASK" (hex or decimal), "T btn ID", "T enc ID DELTA", "T midi ST D1 D2",
 * "T patch ID VALUE" (engine parameter, patch.h), "T bdown ID" / "T bup ID" (a button pressed and held / released: fm1_in.buttons), "T shot NAME" (screen -> OUT-NAME.ppm at that time; any shot line replaces the default 1 s / 3 s screens), "T end" */
typedef struct { uint32_t t, keys; int btn; uint8_t st, d1, d2; int enc; int32_t delta; char shot[24]; uint8_t hold; } host_ev_t;
static const host_ev_t SCRIPT0[] = {
    {200, 1u << 7, -1, 0, 0, 0},                                /* C4 */
    {700, 1u << 7 | 1u << 11 | 1u << 14, -1, 0, 0, 0},          /* + E4 G4: a chord */
    {1500, 0, -1, 0, 0, 0},
    {1700, 0, -1, 0x90, 81, 120},                               /* MIDI in: A5 */
    {2300, 0, -1, 0x80, 81, 0},
    {2500, 0, BTN_OCT_UP, 0, 0, 0},
    {2700, 1u << 2 | 1u << 9, -1, 0, 0, 0},                     /* G4 + D5, an octave up */
    {3400, 0, -1, 0, 0, 0},
};
static const host_ev_t *script = SCRIPT0;
static uint32_t script_n = sizeof SCRIPT0 / sizeof SCRIPT0[0], end_ms = 6500, frame_fps, shots_named;
static const uint32_t SHOT[] = {1000, 3000};

static void read_script(const char *path)
{
    static host_ev_t ev[4096];
    char line[256], what[16];
    FILE *f = fopen(path, "r");
    uint32_t n = 0;
    if (!f) { perror(path); exit(1); }
    while (fgets(line, sizeof line, f) && n < 4096u) {
        host_ev_t e = {0, 0, -1, 0, 0, 0, 0, 0, {0}, 0};
        unsigned a = 0, b = 0, c = 0;
        if (line[0] == '#' || sscanf(line, "%u %15s", &e.t, what) != 2) continue;
        if (!strcmp(what, "end")) { end_ms = e.t; continue; }
        if (!strcmp(what, "shot")) {
            if (sscanf(line, "%*u %*s %23s", e.shot) != 1) continue;
            shots_named = 1;
            ev[n++] = e;
            continue;
        }
        if (!strcmp(what, "keys")) { sscanf(line, "%*u %*s %i", (int *)&e.keys); }
        else if (!strcmp(what, "btn")) { sscanf(line, "%*u %*s %d", &e.btn); }
        else if (!strcmp(what, "bdown")) { sscanf(line, "%*u %*s %d", &e.btn); e.hold = 1; }   /* pressed and held */
        else if (!strcmp(what, "bup")) { sscanf(line, "%*u %*s %d", &e.btn); e.hold = 2; }     /* released */
        else if (!strcmp(what, "midi")) { sscanf(line, "%*u %*s %i %u %u", (int *)&a, &b, &c); e.st = (uint8_t)a; e.d1 = (uint8_t)b; e.d2 = (uint8_t)c; }
        else if (!strcmp(what, "patch")) {
            int id = -1, v = 0;
            if (sscanf(line, "%*u %*s %d %d", &id, &v) != 2 || id < 0) continue;
            e.enc = id; e.delta = v; e.hold = 3;
        }
        else if (!strcmp(what, "enc")) {
            int id = -1, d = 0;
            if (sscanf(line, "%*u %*s %d %d", &id, &d) != 2 || id < 0 || id > 6) continue;
            e.enc = id; e.delta = d;
            if (!d) continue;
        }
        else continue;
        ev[n++] = e;
    }
    fclose(f);
    script = ev;
    script_n = n;
}

static const char *host_out = "build/host/tastywaves";
static int16_t *wav;
static uint32_t wav_n;

static void write_ppm_as(const char *name)
{
    FILE *f;
    uint32_t i;
    if (!(f = fopen(name, "wb"))) { perror(name); exit(1); }
    fprintf(f, "P6 240 240 255\n");
    for (i = 0; i < 240u * 240u; i++) {
        uint16_t c = fb[i];
        fputc((c >> 11) * 255 / 31, f);
        fputc((c >> 5 & 63) * 255 / 63, f);
        fputc((c & 31) * 255 / 31, f);
    }
    fclose(f);
}
static void write_ppm(uint32_t t)
{
    char name[512];
    snprintf(name, sizeof name, "%s-%u.ppm", host_out, t);
    write_ppm_as(name);
    printf("%6u ms  screen -> %s\n", t, name);
}

static void write_wav(void)
{
    char name[512];
    FILE *f;
    uint32_t bytes = wav_n * 4u, v[] = {16, 0x00020001u, FS, FS * 4u, 0x00100004u};
    snprintf(name, sizeof name, "%s.wav", host_out);
    if (!(f = fopen(name, "wb"))) { perror(name); exit(1); }
    fwrite("RIFF", 1, 4, f); bytes += 36; fwrite(&bytes, 4, 1, f); bytes -= 36;
    fwrite("WAVEfmt ", 1, 8, f); fwrite(v, 4, 5, f);
    fwrite("data", 1, 4, f); fwrite(&bytes, 4, 1, f);
    fwrite(wav, 4, wav_n, f);
    fclose(f);
    printf("sound  -> %s (%u frames)\n", name, wav_n);
}

static void platform_poll(void)                 /* one call = one millisecond */
{
    static uint32_t step, shot, owed, frame;
    static int32_t blk[2u * HALF_FRAMES];
    fm1_ms++;
    while (step < script_n && script[step].t <= fm1_ms) {
        if (script[step].shot[0]) {
            char name[512];
            snprintf(name, sizeof name, "%s-%s.ppm", host_out, script[step].shot);
            write_ppm_as(name);
            printf("%6u ms  screen -> %s\n", fm1_ms, name);
        } else if (script[step].st) {
            mq[mq_w % 64u][0] = script[step].st; mq[mq_w % 64u][1] = script[step].d1; mq[mq_w % 64u][2] = script[step].d2;
            mq_w++;
        } else if (script[step].hold == 3) {
            patch_set((uint32_t)script[step].enc, script[step].delta);   /* "T patch ID VALUE": the engine parameter directly */
        } else if (script[step].btn >= 0) {
            uint32_t bit = 1u << script[step].btn;
            if (script[step].hold == 2) fm1_in.buttons &= ~bit;     /* bup: no press edge */
            else {
                host_edges |= bit;                                   /* btn: a press edge only; bdown: and held */
                if (script[step].hold == 1) fm1_in.buttons |= bit;
            }
        }
        else if (script[step].delta)
            host_enc[script[step].enc] += script[step].delta;
        else {
            host_note_edges |= script[step].keys & ~fm1_in.notes;
            fm1_in.notes = script[step].keys;
        }
        step++;
    }
    owed += FS;                                 /* the frames this millisecond is worth, in blocks as the ISR renders */
    while (owed >= 1000u * HALF_FRAMES) {
        uint32_t i;
        owed -= 1000u * HALF_FRAMES;
        memset(blk, 0, sizeof blk);
        app_audio(blk, HALF_FRAMES);
        wav = realloc(wav, (wav_n + HALF_FRAMES) * 4u);
        for (i = 0; i < 2u * HALF_FRAMES; i++) {
            int32_t v = blk[i];                 /* (USB audio at the MASTER knob all the way up) */
            wav[2u * wav_n + i] = (int16_t)(v > 32767 ? 32767 : v < -32767 ? -32767 : v);
        }
        wav_n += HALF_FRAMES;
    }
    if (frame_fps) {                            /* a film: a frame every 1/fps s, as the screen is then */
        if ((uint64_t)frame * 1000u <= (uint64_t)fm1_ms * frame_fps) {
            char name[512];
            snprintf(name, sizeof name, "%s-%05u.ppm", host_out, frame++);
            write_ppm_as(name);
        }
    } else if (!shots_named && shot < sizeof SHOT / sizeof SHOT[0] && fm1_ms == SHOT[shot])
        write_ppm(SHOT[shot++]);
    if (fm1_ms >= end_ms) {
        write_wav();
        exit(0);
    }
}

int main(int argc, char **argv)
{
    if (argc > 1) host_out = argv[1];
    if (argc > 2) read_script(argv[2]);
    if (argc > 3) frame_fps = (uint32_t)atoi(argv[3]);
    memset(host_flash, 0xFF, sizeof host_flash);
    if ((host_patches_file = getenv("DINGHY_PATCHES")) != 0) {
        FILE *f = fopen(host_patches_file, "rb");
        if (f) { if (fread(host_flash + PL_W_LO, 1, PL_W_HI - PL_W_LO, f)) {} fclose(f); }
        atexit(host_patches_save);
    }
    if (getenv("DINGHY_USERTAB")) host_usertab(getenv("DINGHY_USERTAB"));
    {   /* host has no boot scan: fake values for the BOOT KEYS page (GLO, third set): OCT- held, 80 frames */
        uint32_t i;
        fm1_in.buttons = 0x0001u;
        fm1_in.frames = 80;
        for (i = 0; i < 11u; i++) fm1_in.raw[i] = (uint8_t)(0xA0u + i);
    }
    app_main();
    return 0;
}
