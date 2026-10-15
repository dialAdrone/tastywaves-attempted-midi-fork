/* SPDX-License-Identifier: GPL-3.0-only */
/* ui_screens.h: VAVEWave screens (T18). Draws from a plain struct of display values (ui_view_t): no engine, no
 * patch, no globals but the shadow struct. Included from tests/ui_demo.c (host mock-up, fake values) and, through
 * panel.h, from vavewave.c (the real app: panel.h fills the view from the patch).
 *
 * Needs: gfx.c (cv_begin / cv_rect / cv_line / cv_text / cv_blit, cv_oy, FONT_S / FONT_L, RGB) and BTN_* (platform.c).
 * Integer only. Main loop only (never from the ISR).
 *
 * Layout (all pages): y 0..51 four knob columns (60 px each), y 54..179 the middle (126 rows, page dependent),
 * y 182 a status line, y 202 the patch name. Every piece has a "shadow" copy of what it last drew
 * (ui_shadow_t) and is redrawn only when its inputs change. Canvas rule: w * h <= CV_MAX (240 x 64). */
#pragma once

#define UI_ACC  RGB(180, 140, 255)              /* lilac, as the logo */
#define UI_ACC2 RGB(120, 90, 180)
#define UI_ACC3 RGB(44, 32, 76)
#define UI_TXT  RGB(225, 225, 230)
#define UI_GRAY RGB(130, 130, 142)
#define UI_DIM  RGB(58, 58, 72)

#define UI_Y_MID   54u                          /* the middle area: 126 rows */
#define UI_MID_H   126u
#define UI_Y_LINEA 182u
#define UI_Y_PATCH 202u

enum { UI_P_HOME, UI_P_EDIT, UI_P_FX, UI_P_ENV, UI_P_LFO, UI_P_SEL, UI_P_GLO, UI_P_HELP, UI_P_N };
enum { UI_H_WATERFALL, UI_H_SPECTRUM, UI_H_MINIMAL, UI_H_N };

typedef struct {
    uint8_t page, variant;                      /* UI_P_*; HOME look UI_H_* */
    uint8_t sub;                                /* ENV: 0 amp 1 filter; EDIT: 0 osc1 1 osc2 2 sub 3 noise */
    uint8_t touched;                            /* knob last turned 0..3, 4 none: its value is lilac */
    uint8_t voices, voices_max, cpu, modified;  /* voices sounding / available, CPU % of the block, patch edited */
    uint8_t table, frame, nframes;              /* table index, nearest frame 0..nframes-1, frames in the table */
    uint16_t pos;                               /* 12-bit wavetable position 0..4095 */
    char patch[16], tname[8];
    const char *klabel[4];                      /* <= 7 characters */
    char kval[4][8];                            /* what to print: <= 3 characters big, 4..7 small */
    uint8_t kfrac[4];                           /* bar fill 0..255 */
    uint8_t kbip;                               /* bit i: bar of knob i grows from the middle */
    uint8_t ksmall;                             /* bit i: value of knob i in the small font (ENV: times) */
    uint8_t env[2][4];                          /* ENV page: amp and filter A D S R, 0..127 */
    const int16_t *frames;                      /* frame 0, level 0 of the current table (256 samples a frame) */
    uint16_t stride;                            /* int16 between frames (WT_FRAME_LEN) */
    const int16_t *wave2;                       /* current frame, mip level 2 (64 samples): the spectrum */
    /* the real app (panel.h); all zero in the mock-up */
    const int16_t *wave;                        /* EDIT: 256 samples to draw instead of the frame (morph + BEND) or NULL */
    uint32_t wave_key;                          /* changes when `wave` does (the shadow compares it) */
    uint32_t ms;                                /* the clock (ms): the waterfall redraws once the position rests; 0 in the mock-up */
    uint8_t cpu_max, flash;                     /* worst CPU % since reset; flash: 1 = "SAVED" instead of the patch name */
    uint8_t nsets, set;                         /* FX / LFO / GLO: knob sets on the page, the one shown */
    uint8_t flt_cut, flt_res, flt_mode;         /* FX picture: cutoff, resonance 0..127, mode 0 LP 1 BP 2 HP */
    uint8_t lfo_shape, drive;                   /* LFO picture (0 sine 1 tri 2 saw 3 square 4 S&H), GLO picture 0..127 */
    char ptxt[8];                               /* SEL picture: the voice mode, big */
    char list[8][16];                           /* FX / LFO / SEL / GLO: 4 rows x 2 columns of "LABEL VALUE" (14 chars) */
    char boot[2][32];                           /* GLO set 3 (BOOT KEYS): "BTN 0x0001 FR 80", "RAW" + 11 bytes hex */
} ui_view_t;

typedef struct {
    uint8_t valid, page, variant, sub, touched, voices, cpu, modified, table, frame, kbip, ksmall;
    uint16_t pos;
    char patch[16], tname[8], linea[48], kval[4][8];
    const char *klabel[4];
    uint8_t kfrac[4], env[2][4], harm[16];
    uint8_t flash, nsets, set;
    uint32_t wave_key, pic_key;
    uint32_t t_chg;                             /* waterfall: ms of the last frame change */
    uint8_t wf_arrow, wf_frame;                 /* waterfall: frame of the arrow on screen, of the white trace */
    char list[8][16], ptxt[8];
} ui_shadow_t;

/* ---- small helpers (own copies: ui.h's s_cat / s_num belong to the sine build) */
static char *ui_cat(char *d, const char *s) { while (*s) *d++ = *s++; *d = 0; return d; }
static char *ui_num(char *d, int32_t v)
{
    char t[12];
    int n = 0;
    uint32_t u = (uint32_t)(v < 0 ? -v : v);
    do { t[n++] = (char)('0' + u % 10u); u /= 10u; } while (u);
    if (v < 0) *d++ = '-';
    while (n) *d++ = t[--n];
    *d = 0;
    return d;
}
static int ui_eq(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}
static void ui_copy(char *d, const char *s, uint32_t n)
{
    uint32_t i = 0;
    while (i + 1u < n && s[i]) { d[i] = s[i]; i++; }
    d[i] = 0;
}

/* a time for a 0..127 knob, as the engine's curve: 0.5 ms * 20000^(v/127) (0.5 ms .. 10 s). 2^(n/16) from a 16-entry
 * table, n = 1.8 v (octaves x 16). Text: "4.5MS" "70MS" "1.2S" "10S" */
static void ui_fmt_time(char *d, uint32_t v)
{
    static const uint16_t T16[16] = {256, 267, 279, 291, 304, 317, 331, 345, 362, 378, 395, 412, 431, 449, 470, 490};
    uint32_t n = (v > 127u ? 127u : v) * 9u / 5u;
    uint32_t ms10 = (((uint32_t)T16[n & 15u] << (n >> 4)) * 5u) >> 8;   /* ms x 10: 5 .. 97300 */
    if (ms10 < 100u) { d = ui_num(d, (int32_t)(ms10 / 10u)); *d++ = '.'; *d++ = (char)('0' + ms10 % 10u); ui_cat(d, "MS"); }
    else if (ms10 < 10000u) { d = ui_num(d, (int32_t)((ms10 + 5u) / 10u)); ui_cat(d, "MS"); }
    else if (ms10 < 100000u) { d = ui_num(d, (int32_t)(ms10 / 10000u)); *d++ = '.'; *d++ = (char)('0' + ms10 / 1000u % 10u); *d++ = 'S'; *d = 0; }
    else { d = ui_num(d, (int32_t)((ms10 + 5000u) / 10000u)); ui_cat(d, "S"); }
}

/* ---- the four knob columns: label (FONT_S), value (FONT_L, or FONT_S when long), bar. 60 x 52 canvas, 6 KB each */
static void ui_knob(uint32_t i, const ui_view_t *v)
{
    const char *val = v->kval[i];
    int32_t n = 0, w, fill;
    uint16_t col = v->touched == i ? UI_ACC : UI_TXT;
    while (val[n]) n++;
    cv_begin(60, 52, C_BLACK);
    cv_text((60 - text_w(&FONT_S, v->klabel[i])) / 2, 0, &FONT_S, v->klabel[i], UI_GRAY);
    if (n <= 3 && !(v->ksmall >> i & 1u)) cv_text((60 - text_w(&FONT_L, val)) / 2, 14, &FONT_L, val, col);
    else cv_text((60 - text_w(&FONT_S, val)) / 2, 22, &FONT_S, val, col);
    cv_rect(4, 48, 52, 3, UI_DIM);
    w = (int32_t)v->kfrac[i] * 52 >> 8;
    if (v->kbip >> i & 1u) {                    /* from the middle */
        fill = w - 26;
        if (fill >= 0) cv_rect(30, 48, fill + 1, 3, v->touched == i ? UI_ACC : UI_ACC2);
        else cv_rect(30 + fill, 48, -fill, 3, v->touched == i ? UI_ACC : UI_ACC2);
        cv_rect(30, 46, 1, 7, UI_GRAY);
    } else
        cv_rect(4, 48, w, 3, v->touched == i ? UI_ACC : UI_ACC2);
    cv_blit(i * 60u, 0);
}

/* ---- the position bar: 32 frame ticks, marker at pos. 240 x h canvas */
static void ui_posbar(uint32_t y, uint32_t h, uint32_t pos)
{
    int32_t f, mx = 4 + (int32_t)(pos * 228u >> 12), th = (int32_t)h - 2;
    cv_begin(240, h, C_BLACK);
    cv_rect(4, 1, 232, th, UI_DIM);
    cv_rect(4, 1, mx - 4, th, UI_ACC3);
    for (f = 0; f < 32; f++)
        cv_rect(4 + ((f * 29) >> 2), th - 3, 1, 4, UI_GRAY);
    cv_rect(mx - 1, 0, 4, (int32_t)h, C_WHITE);
    cv_blit(0, y);
}

/* ---- one cycle of the current frame, 256 samples over 239 px */
static void ui_wave(uint32_t y, uint32_t h, const int16_t *s, uint16_t col)
{
    int32_t i, mid = (int32_t)h / 2, amp = mid - 3, px = 0, py = mid;
    cv_begin(240, h, C_BLACK);
    cv_rect(0, mid, 240, 1, UI_DIM);
    for (i = 0; i < 256; i++) {
        int32_t x = i * 239 >> 8, yy = mid - ((int32_t)s[i] * amp >> 15);
        if (i) cv_line(px, py, x, yy, col);
        px = x;
        py = yy;
    }
    cv_blit(0, y);
}

/* ---- the harmonic spectrum: 16 bars of a 64-sample DFT (mip level 2 keeps exactly harmonics 1..16).
 * 2 x 16 x 64 multiplies, only when the frame changes. Height = log2 magnitude, 4 px per 6 dB, 44 px = 66 dB. */
static const int16_t UI_SIN64[64] = {
    0, 1606, 3196, 4756, 6270, 7723, 9102, 10394, 11585, 12665, 13623, 14449, 15137, 15679, 16069, 16305,
    16384, 16305, 16069, 15679, 15137, 14449, 13623, 12665, 11585, 10394, 9102, 7723, 6270, 4756, 3196, 1606,
    0, -1606, -3196, -4756, -6270, -7723, -9102, -10394, -11585, -12665, -13623, -14449, -15137, -15679, -16069,
    -16305, -16384, -16305, -16069, -15679, -15137, -14449, -13623, -12665, -11585, -10394, -9102, -7723, -6270,
    -4756, -3196, -1606};

static void ui_spectrum_calc(const int16_t *w, uint8_t *out)
{
    uint32_t h, n;
    for (h = 1; h <= 16u; h++) {
        int32_t re = 0, im = 0, a, b, m, mag, e;
        for (n = 0; n < 64u; n++) {
            int32_t x = w[n] >> 6;
            re += x * UI_SIN64[(h * n + 16u) & 63u];        /* cos */
            im += x * UI_SIN64[(h * n) & 63u];              /* sin */
        }
        a = (re < 0 ? -re : re) >> 14;
        b = (im < 0 ? -im : im) >> 14;
        mag = a > b ? a + (b >> 1) - (b >> 3) : b + (a >> 1) - (a >> 3);   /* |z| ~ max + 3/8 min */
        if (mag < 8) { out[h - 1u] = 0; continue; }
        for (e = 3; e < 20 && (mag >> (e + 1)) != 0; e++) {}
        m = e * 4 + ((mag >> (e - 2)) & 3);                 /* log2 in quarter steps */
        m -= 12;
        out[h - 1u] = (uint8_t)(m > 44 ? 44 : m < 0 ? 0 : m);
    }
}

static void ui_spectrum(uint32_t y, const uint8_t *hm)
{
    uint32_t i;
    cv_begin(240, 48, C_BLACK);
    for (i = 0; i < 16u; i++) {
        int32_t hh = hm[i];
        cv_rect(8 + (int32_t)i * 14, 47 - hh, 13, hh + 1, i == 0 ? C_WHITE : UI_ACC);
    }
    cv_rect(0, 47, 240, 1, UI_GRAY);
    cv_text(206, 0, &FONT_S, "HARM", UI_GRAY);
    cv_blit(0, y);
}

/* ---- the waterfall: all frames of the table, frame 0 in front, each one 3 px further back and 2 px to the right.
 * 128 points per trace (every 2nd sample of level 0), y = base - s/2048 (+-15). Drawn back to front with a black
 * fill under every line (hidden lines), then the current frame in white on top, no fill. Integer only.
 * Staging: the region is 240 x 126 = two canvases of 240 x 63 (CV_MAX is 240 x 64); both are drawn from the
 * same traces with cv_oy = -strip_top, skipping traces that do not touch the strip. Pixel writes: about
 * 32 x 128 x 15 (fill) per full pass = 60 k, lines 4 k; blit 2 x 30 KB over SPI. */
#define UI_WF_BASE 108
#define UI_WF_STRIP 63

static void ui_trace(const int16_t *s, int32_t f, uint16_t col, int fill)
{
    int32_t base = UI_WF_BASE - f * 3, x0 = 8 + f * 2, j, px = x0, py = base - (s[0] >> 11);
    for (j = 1; j < 128; j++) {
        int32_t x = x0 + (j * 5 >> 2), y = base - (s[2 * j] >> 11);
        if (fill) cv_rect(px, py + 1, x - px + 1, base + 16 - py, C_BLACK);
        cv_line(px, py, x, y, col);
        px = x;
        py = y;
    }
}

static void ui_waterfall(const ui_view_t *v)
{
    uint32_t strip;
    int32_t f, nf = v->nframes > 32u ? 32 : (int32_t)v->nframes;
    for (strip = 0; strip < 2u; strip++) {
        int32_t top = (int32_t)strip * UI_WF_STRIP, hf = v->frame;
        cv_begin(240, UI_WF_STRIP, C_BLACK);
        cv_oy = -top;
        for (f = nf - 1; f >= 0; f--) {         /* back to front */
            int32_t base = UI_WF_BASE - f * 3;
            uint16_t col = f < 8 ? UI_ACC : f < 16 ? RGB(150, 115, 215) : f < 24 ? RGB(115, 90, 170) : RGB(86, 66, 126);
            if (base + 17 < top || base - 16 > top + UI_WF_STRIP) continue;
            ui_trace(v->frames + (uint32_t)f * v->stride, f, col, 1);
        }
        if (hf < nf) {
            int32_t b = UI_WF_BASE - hf * 3, x = 8 + hf * 2;
            ui_trace(v->frames + (uint32_t)hf * v->stride, hf, C_WHITE, 0);
            cv_line(x - 7, b - 3, x - 7, b + 3, C_WHITE);      /* an arrow on the left */
            cv_line(x - 7, b - 3, x - 3, b, C_WHITE);
            cv_line(x - 7, b + 3, x - 3, b, C_WHITE);
        }
        cv_oy = 0;
        cv_blit(0, UI_Y_MID + (uint32_t)top);
        platform_poll();                        /* keep USB and the watchdog fed between the strips */
    }
}

/* the arrow alone (8 x 9 px, left of the frame's first point): the cheap indicator while the position moves */
static void ui_wf_arrow(uint32_t f, int draw)
{
    int32_t b = UI_WF_BASE - (int32_t)f * 3;
    cv_begin(8, 9, C_BLACK);
    if (draw) {
        cv_line(1, 1, 1, 7, C_WHITE);
        cv_line(1, 1, 5, 4, C_WHITE);
        cv_line(1, 7, 5, 4, C_WHITE);
    }
    cv_blit(f * 2u, UI_Y_MID + (uint32_t)(b - 4));
}

/* ---- ENV: one ADSR curve in a 240 x 63 canvas. Segment widths follow the values (6..50 px), sustain is a fixed
 * hold. The active envelope is lilac with its area filled, the other is grey. */
static void ui_env(uint32_t y, const char *name, const uint8_t *e, int active)
{
    int32_t x0 = 20, base = 44, top = 12, sl = base - ((int32_t)e[2] * (base - top) >> 7);
    int32_t x1 = x0 + 6 + ((int32_t)e[0] * 44 >> 7), x2 = x1 + 6 + ((int32_t)e[1] * 44 >> 7), x3 = x2 + 36;
    int32_t x4 = x3 + 6 + ((int32_t)e[3] * 44 >> 7), x, yy;
    uint16_t line = active ? UI_ACC : UI_GRAY;
    cv_begin(240, 63, C_BLACK);
    if (active) {                               /* the area under the curve, a column at a time */
        for (x = x0; x <= x4; x++) {
            if (x <= x1) yy = base + (top - base) * (x - x0) / (x1 - x0);
            else if (x <= x2) yy = top + (sl - top) * (x - x1) / (x2 - x1);
            else if (x <= x3) yy = sl;
            else yy = sl + (base - sl) * (x - x3) / (x4 - x3);
            cv_rect(x, yy, 1, base - yy + 1, UI_ACC3);
        }
    }
    cv_rect(8, base, 224, 1, UI_DIM);
    cv_line(x0, base, x1, top, line);
    cv_line(x1, top, x2, sl, line);
    cv_line(x2, sl, x3, sl, line);
    cv_line(x3, sl, x4, base, line);
    cv_rect(x1 - 1, top - 1, 3, 3, active ? C_WHITE : UI_GRAY);
    cv_rect(x2 - 1, sl - 1, 3, 3, active ? C_WHITE : UI_GRAY);
    cv_text(8, 0, &FONT_S, name, active ? UI_ACC : UI_GRAY);
    cv_text((x0 + x1) / 2 - 4, 47, &FONT_S, "A", UI_GRAY);
    cv_text((x1 + x2) / 2 - 4, 47, &FONT_S, "D", UI_GRAY);
    cv_text((x2 + x3) / 2 - 4, 47, &FONT_S, "S", UI_GRAY);
    cv_text((x3 + x4) / 2 - 4, 47, &FONT_S, "R", UI_GRAY);
    cv_blit(0, y);
}

/* ---- EDIT: oscillator selector chips */
static void ui_chips(uint32_t y, uint32_t sel)
{
    static const char *const N[4] = {"OSC1", "OSC2", "SUB", "NOISE"};
    uint32_t i;
    cv_begin(240, 18, C_BLACK);
    for (i = 0; i < 4u; i++) {
        int32_t x = 4 + (int32_t)i * 58;
        if (i == sel) {
            cv_rect(x, 0, 54, 17, UI_ACC);
            cv_text(x + (54 - text_w(&FONT_S, N[i])) / 2, 1, &FONT_S, N[i], C_BLACK);
        } else {
            cv_rect(x, 0, 54, 17, UI_DIM);
            cv_rect(x + 1, 1, 52, 15, C_BLACK);
            cv_text(x + (54 - text_w(&FONT_S, N[i])) / 2, 1, &FONT_S, N[i], UI_GRAY);
        }
    }
    cv_blit(0, y);
}

static void ui_text_line(uint32_t y, const char *s, uint16_t col)
{
    draw_text_box(0, y, 240, &FONT_S, s, col, 1);
}

static void ui_dots(uint32_t y, uint32_t used, uint32_t max)
{
    uint32_t i;
    cv_begin(240, 12, C_BLACK);
    for (i = 0; i < max && i < 12u; i++)
        cv_rect(24 + (int32_t)i * 16, 2, 10, 8, i < used ? UI_ACC : UI_DIM);
    cv_blit(0, y);
}

/* the status line: left what this page is, right voices and CPU. One 240 x 16 canvas, 7.6 KB */
static void ui_linea(uint32_t y, const char *left, const ui_view_t *v)
{
    char r[24], *d = r;
    d = ui_cat(d, "V");                       /* fixed field widths: the text must not move as digits change */
    if (v->voices < 10u) d = ui_cat(d, " ");
    d = ui_num(d, v->voices);
    d = ui_cat(d, "/");
    d = ui_num(d, v->voices_max);
    d = ui_cat(d, " CPU");
    if (v->cpu < 10u) d = ui_cat(d, " ");
    d = ui_num(d, v->cpu);
    ui_cat(d, "%");
    cv_begin(240, 16, C_BLACK);
    cv_text(4, 0, &FONT_S, left, UI_ACC);
    cv_text(236 - text_w(&FONT_S, r), 0, &FONT_S, r, v->cpu > 80u ? RGB(255, 90, 90) : UI_GRAY);
    cv_blit(0, y);
}

static void ui_patch(uint32_t y, const ui_view_t *v, uint16_t col)
{
    cv_begin(240, 32, C_BLACK);
    if (v->flash) {
        cv_text((240 - text_w(&FONT_L, "SAVED")) / 2, 0, &FONT_L, "SAVED", UI_ACC);
        cv_blit(0, y);
        return;
    }
    cv_text((240 - text_w(&FONT_L, v->patch)) / 2, 0, &FONT_L, v->patch, col);
    if (v->modified) cv_rect(0, 13, 5, 6, UI_ACC);       /* edited since loaded / saved */
    cv_blit(0, y);
}

static void ui_left_text(char *out, const ui_view_t *v)       /* the left part of the status line */
{
    static const char *const OSC[4] = {"OSC1", "OSC2", "SUB", "NOISE"};
    char *d = out;
    switch (v->page) {
    case UI_P_EDIT: d = ui_cat(d, OSC[v->sub & 3u]); d = ui_cat(d, " "); ui_cat(d, v->tname); break;
    case UI_P_ENV: d = ui_cat(d, "ENV "); ui_cat(d, v->sub ? "FILTER" : "AMP"); break;
    case UI_P_FX: ui_cat(d, v->sub ? "FILTER 2/2" : "FILTER 1/2"); break;
    case UI_P_LFO: ui_cat(d, v->sub ? "LFO 2/2" : "LFO 1/2"); break;
    case UI_P_SEL: ui_cat(d, "VOICE"); break;
    case UI_P_GLO: ui_cat(d, v->sub == 2u ? "BOOT KEYS 3/3" : v->sub ? "GLOBAL 2/3" : "GLOBAL 1/3"); break;
    default:
        if (v->variant != UI_H_MINIMAL) { d = ui_cat(d, v->tname); d = ui_cat(d, " "); }
        d = ui_cat(d, "FRM ");
        d = ui_num(d, v->frame + 1);
        d = ui_cat(d, "/");
        ui_num(d, v->nframes);
    }
}

/* ---- FX / LFO / SEL / GLO: one picture (240 x 56) and a two column list of the page's eight values (240 x 64) */
static void ui_filter_pic(uint32_t y, const ui_view_t *v)
{
    int32_t x, xc = 8 + (int32_t)v->flt_cut * 224 / 127, base = 38, top = 14, bump = (int32_t)v->flt_res * 22 / 127, py = 0;
    static const char *const TK[3] = {"100", "1K", "10K"};
    static const int16_t TX[3] = {60, 134, 209};      /* 100 Hz, 1 kHz, 10 kHz on the 20 Hz .. 19 kHz log axis */
    cv_begin(240, 56, C_BLACK);
    cv_rect(8, base, 224, 1, UI_DIM);
    for (x = 0; x < 3; x++) {
        cv_rect(TX[x], base, 1, 3, UI_GRAY);
        cv_text(TX[x] - text_w(&FONT_S, TK[x]) / 2, 40, &FONT_S, TK[x], UI_DIM);
    }
    for (x = 8; x <= 232; x++) {
        int32_t dd = x - xc, ad = dd < 0 ? -dd : dd, yy;
        if (v->flt_mode == 0) yy = top + (dd > 0 ? dd * 5 / 9 : 0);               /* 12 dB / octave ~ 0.55 px / px */
        else if (v->flt_mode == 2) yy = top + (dd < 0 ? -dd * 5 / 9 : 0);
        else yy = top + 8 + ad * 5 / 18;                                          /* band pass: 6 dB / octave */
        if (ad < 16) yy -= bump * (16 - ad) / 16;                                 /* the resonance peak */
        if (yy < 0) yy = 0;
        if (yy > base) yy = base;
        cv_rect(x, yy, 1, base - yy + 1, UI_ACC3);
        if (x > 8) cv_line(x - 1, py, x, yy, UI_ACC);
        py = yy;
    }
    cv_rect(xc, top - 6, 1, base - top + 6, UI_GRAY);
    cv_blit(0, y);
}

static void ui_lfo_pic(uint32_t y, uint32_t shape)
{
    int32_t x, px = 8, py = 28;
    cv_begin(240, 56, C_BLACK);
    cv_rect(8, 28, 224, 1, UI_DIM);
    for (x = 0; x < 224; x++) {
        int32_t ph = x * 4 / 7, q = ph & 63, st = ph >> 4, val, yy;
        switch (shape) {
        case 0: val = UI_SIN64[q]; break;
        case 1: val = q < 16 ? q * 1024 : q < 48 ? (32 - q) * 1024 : (q - 64) * 1024; break;
        case 2: val = -16384 + q * 512; break;
        case 3: val = q < 32 ? 16384 : -16384; break;
        default: val = (((st * 2654435761u) >> 12) & 31u) * 1024 - 16000; break;   /* S&H: 4 steps a cycle */
        }
        yy = 28 - (val * 22 >> 14);
        if (x) cv_line(px, py, 8 + x, yy, UI_ACC);
        px = 8 + x;
        py = yy;
    }
    cv_blit(0, y);
}

static void ui_drive_pic(uint32_t y, const ui_view_t *v)
{
    int32_t i, g = 16 + (int32_t)v->drive * 240 / 127, py = 0;             /* gain 1 .. 16, Q4 */
    char t[24], *d = t;
    cv_begin(240, 56, C_BLACK);
    cv_rect(8, 28, 100, 1, UI_DIM);
    cv_rect(58, 3, 1, 50, UI_DIM);
    for (i = 0; i <= 50; i++) {                 /* the right half, mirrored for the left (odd symmetry) */
        int32_t u = i * g * 4096 / 800;         /* x * gain, Q12 */
        int32_t sat = u * 8192 / (4096 + u);    /* a soft knee, x / (1 + x) x 2 */
        int32_t h = (sat > 4096 ? 4096 : sat) * 24 >> 12;
        if (i) {
            cv_line(58 + i - 1, 28 - py, 58 + i, 28 - h, UI_ACC);
            cv_line(58 - i + 1, 28 + py, 58 - i, 28 + h, UI_ACC);
        }
        py = h;
    }
    d = ui_cat(d, "CPU MAX ");
    d = ui_num(d, v->cpu_max);
    ui_cat(d, "%");
    cv_text(124, 8, &FONT_S, t, v->cpu_max > 80u ? RGB(255, 90, 90) : UI_GRAY);
    cv_text(124, 28, &FONT_S, "EDIT+HOME", UI_DIM);
    cv_text(124, 40, &FONT_S, "RESETS MAX", UI_DIM);
    cv_blit(0, y);
}

static void ui_boot_pic(uint32_t y, const ui_view_t *v)       /* GLO set 3: what the boot key scan saw */
{
    cv_begin(240, 56, C_BLACK);
    cv_text(4, 0, &FONT_S, "BOOT KEYS (SCAN AT POWER-ON)", UI_GRAY);
    cv_text(4, 14, &FONT_S, v->boot[0], UI_TXT);
    cv_text(4, 28, &FONT_S, v->boot[1], UI_TXT);
    cv_text(4, 42, &FONT_S, "RAW = COLUMN 0..10, BIT = ROW", UI_DIM);
    cv_blit(0, y);
}

static void ui_list(uint32_t y, const ui_view_t *v)
{
    uint32_t i;
    cv_begin(240, 64, C_BLACK);
    for (i = 0; i < 8u; i++) {
        uint32_t col = i >> 2, row = i & 3u;
        uint16_t c = col == v->set ? UI_TXT : RGB(95, 95, 110);
        if (!v->list[i][0]) continue;
                cv_text(4 + (int32_t)col * 120, (int32_t)row * 16, &FONT_S, v->list[i], c);
    }
    cv_blit(0, y);
}


/* ---- HELP (hold HOME): two text pages, one 240 x 16 canvas a line. A line is "LABEL  what it does": the text up
 * to the first double space is drawn lilac, the rest white; a line without one is gray (the footer). */
static const char *const UI_HELP[2][14] = {
    {"HELP 1/2  BUTTONS",
     "HOME  3 looks (press again)",
     "EDIT  OSC1 OSC2 SUB NOISE",
     "FX    FILTER 1, 2",
     "ENV   AMP, FILTER",
     "LFO   LFO 1, 2",
     "SEL   VOICE MODE, QUALITY",
     "GLO   VOLUME, GLIDE, MIDI",
     "SAVE  STORE THE PATCH",
     "OCT- OCT+  OCTAVE DOWN, UP",
     "ARP SEQ PLAY REC  NOT YET",
     "EDIT+HOME  RESET CPU MAX",
     "EDIT+F# G# A# C#  PICK OSC",
     "HOME: NEXT PAGE, THEN EXIT"},
    {"HELP 2/2  KNOBS",
     "ALGORITHM  WAVETABLE",
     "SELECT     POSITION IN TABLE",
     "PRESETS    8 FACTORY PATCHES",
     "HOME  CUT RESO ATK REL",
     "EDIT  POS DETUNE LEVEL BEND",
     "FX    CUT RESO FENV KEYTRK",
     "FX 2  MODE FVEL E>POS E>BEND",
     "ENV   ATK DEC SUS REL",
     "LFO   SHAPE RATE >POS >PITCH",
     "LFO 2  SHAPE RATE >CUT >AMP",
     "SEL   MODE BEND> QUAL CURVE",
     "GLO   VOLUME DRIVE GLIDE PB",
     "GLO 2  MW>POS MW>VIB AT VEL"}};

static void ui_help(uint32_t page)
{
    uint32_t i, n, m;
    char lab[32];
    for (i = 0; i < 14u; i++) {
        const char *t = UI_HELP[page & 1u][i];
        for (n = 0; t[n] && !(t[n] == ' ' && t[n + 1u] == ' '); n++) {}
        cv_begin(240, 16, C_BLACK);
        if (!t[n]) {
            cv_text(4, 0, &FONT_S, t, UI_GRAY);
        } else {
            for (m = 0; m < n && m < 31u; m++) lab[m] = t[m];
            lab[m] = 0;
            cv_text(4, 0, &FONT_S, lab, UI_ACC);
            cv_text(4 + (int32_t)n * 8, 0, &FONT_S, t + n, UI_TXT);
        }
        cv_blit(0, 2 + i * 16u);
    }
}

/* ---- the whole screen: redraws only what differs from the shadow; the first call (or a page / look change)
 * clears the screen once (115 KB) and draws everything. Typical later cost: a knob turn = 1 column (6 KB); a
 * position move = the frame view (waterfall 61 KB, spectrum look 17 + 6 + 8 KB) only when the nearest frame
 * changes; CPU / voices = the 7.6 KB status line, at most 4 times a second (the caller rate-limits cpu). */
static void ui_draw(const ui_view_t *v, ui_shadow_t *s)
{
    uint32_t i, full = !s->valid || s->page != v->page || s->variant != v->variant;
    uint32_t frame_new, left_y = UI_Y_LINEA;
    char left[24];
    int home3 = v->page == UI_P_HOME && v->variant == UI_H_MINIMAL;

    if (full) {
        lcd_fill(0, 0, 240, 240, C_BLACK);
        platform_poll();                        /* 115 KB over SPI took tens of ms: USB and the watchdog */
        s->valid = 1;
        s->tname[0] = 0xFF;                     /* never equal */
    }
    if (v->page == UI_P_HELP) {                 /* text only: no knob columns, status line or patch name */
        if (full || s->sub != v->sub) ui_help(v->sub);
        s->page = v->page; s->variant = v->variant; s->sub = v->sub;
        return;
    }
    for (i = 0; i < 4u; i++) {
        uint32_t ch = full || s->klabel[i] != v->klabel[i] || s->kfrac[i] != v->kfrac[i] || !ui_eq(s->kval[i], v->kval[i])
                      || ((v->kbip ^ s->kbip) >> i & 1u) || ((v->ksmall ^ s->ksmall) >> i & 1u) || ((v->touched == i) != (s->touched == i));
        if (ch) {
            ui_knob(i, v);
            s->klabel[i] = v->klabel[i];
            s->kfrac[i] = v->kfrac[i];
            ui_copy(s->kval[i], v->kval[i], 8);
        }
    }
    s->kbip = v->kbip;
    s->ksmall = v->ksmall;
    s->touched = v->touched;

    frame_new = full || s->table != v->table || s->frame != v->frame || s->tname[0] != v->tname[0];
    if (v->page == UI_P_HOME && v->variant == UI_H_WATERFALL) {
        /* a full pass is tens of ms: while the frame keeps changing only the arrow moves (0.5 KB); the traces and
         * the white frame follow 120 ms after the last change. A new table redraws at once. */
        if (frame_new && (full || s->table != v->table || s->tname[0] != v->tname[0])) {
            ui_waterfall(v);
            s->wf_arrow = s->wf_frame = v->frame;
            s->t_chg = v->ms;
        } else if (s->wf_arrow != v->frame) {
            ui_wf_arrow(s->wf_arrow, 0);
            ui_wf_arrow(v->frame, 1);
            s->wf_arrow = v->frame;
            s->t_chg = v->ms;
        } else if (s->wf_frame != v->frame && (v->ms == 0 || v->ms - s->t_chg >= 120u)) {
            ui_waterfall(v);
            s->wf_frame = v->frame;
        }
    } else if (v->page == UI_P_HOME && v->variant == UI_H_SPECTRUM) {
        if (frame_new) {
            ui_wave(UI_Y_MID, 56, v->frames + (uint32_t)v->frame * v->stride, UI_ACC);
            ui_spectrum_calc(v->wave2, s->harm);
            ui_spectrum(128, s->harm);
        }
        if (full || s->pos != v->pos) ui_posbar(112, 12, v->pos);
    } else if (home3) {
        if (full || !ui_eq(s->patch, v->patch) || s->flash != v->flash || s->modified != v->modified) ui_patch(58, v, C_WHITE);
        if (frame_new) {
            draw_text_box(0, 92, 240, &FONT_L, v->tname, UI_ACC, 1);
            ui_wave(156, 40, v->frames + (uint32_t)v->frame * v->stride, UI_ACC2);
        }
        if (full || s->pos != v->pos) ui_posbar(130, 20, v->pos);
        if (full || s->voices != v->voices) ui_dots(222, v->voices, v->voices_max);
        left_y = 200;
    } else if (v->page == UI_P_EDIT) {
        if (frame_new || s->wave_key != v->wave_key || s->sub != v->sub)
            ui_wave(UI_Y_MID, 56, v->wave ? v->wave : v->frames + (uint32_t)v->frame * v->stride, UI_ACC);
        if (full || s->pos != v->pos) ui_posbar(112, 16, v->pos);
        if (full || s->sub != v->sub) ui_chips(131, v->sub);
        if (full) {
            ui_text_line(153, "ALG TABLE   SEL POSITION", UI_GRAY);
            ui_text_line(166, "EDIT + OP KEYS: OSC", UI_DIM);
        }
    } else if (v->page == UI_P_ENV) {
        for (i = 0; i < 2u; i++)
            if (full || s->sub != v->sub || s->env[i][0] != v->env[i][0] || s->env[i][1] != v->env[i][1] ||
                s->env[i][2] != v->env[i][2] || s->env[i][3] != v->env[i][3])
                ui_env(UI_Y_MID + i * 64u, i ? "FILTER ENV" : "AMP ENV", v->env[i], v->sub == i);
    } else if (v->page != UI_P_HOME) {          /* FX, LFO, SEL, GLO */
        uint32_t pk = v->page == UI_P_FX ? (uint32_t)v->flt_cut | (uint32_t)v->flt_res << 8 | (uint32_t)v->flt_mode << 16
                    : v->page == UI_P_LFO ? v->lfo_shape : v->page == UI_P_SEL ? v->voices | (uint32_t)v->voices_max << 8
                    : v->sub == 2u ? 0x40000000u : (uint32_t)v->drive | (uint32_t)v->cpu_max << 8;
        uint32_t lch = full || s->set != v->set;
        for (i = 0; i < 8u && !lch; i++) lch = !ui_eq(s->list[i], v->list[i]);
        if (full || s->pic_key != pk || (v->page == UI_P_SEL && !ui_eq(s->ptxt, v->ptxt))) {
            switch (v->page) {
            case UI_P_FX: ui_filter_pic(UI_Y_MID, v); break;
            case UI_P_LFO: ui_lfo_pic(UI_Y_MID, v->lfo_shape); break;
            case UI_P_SEL:
                draw_text_box(0, 58, 240, &FONT_L, v->ptxt, UI_ACC, 1);
                ui_dots(100, v->voices, v->voices_max);
                break;
            default:
                if (v->sub == 2u) ui_boot_pic(UI_Y_MID, v);
                else ui_drive_pic(UI_Y_MID, v);
                break;
            }
            s->pic_key = pk;
            ui_copy(s->ptxt, v->ptxt, 8);
        }
        if (lch) {
            ui_list(116, v);
            for (i = 0; i < 8u; i++) ui_copy(s->list[i], v->list[i], 16);
            s->set = v->set;
        }
    }

    ui_left_text(left, v);                      /* the status line: when its text, voices or CPU changed */
    if (full || !ui_eq(s->linea, left) || s->voices != v->voices || s->cpu != v->cpu) {
        ui_linea(left_y, left, v);
        ui_copy(s->linea, left, 48);
    }
    if (!home3 && (full || !ui_eq(s->patch, v->patch) || s->modified != v->modified || s->flash != v->flash))
        ui_patch(UI_Y_PATCH, v, C_WHITE);

    s->page = v->page; s->variant = v->variant; s->sub = v->sub;
    s->voices = v->voices; s->cpu = v->cpu; s->modified = v->modified;
    s->table = v->table; s->frame = v->frame; s->pos = v->pos;
    s->flash = v->flash; s->wave_key = v->wave_key;
    ui_copy(s->patch, v->patch, 16);
    ui_copy(s->tname, v->tname, 8);
    for (i = 0; i < 8u; i++) s->env[i >> 2][i & 3u] = v->env[i >> 2][i & 3u];
}

/* ---- button LEDs: the page button of the shown page, SAVE blinking while the patch is edited, PLAY unused (v1).
 * Returns a mask over BTN_* ids; `blink` toggles about 2 Hz. Key LEDs: a key is lit while it sounds (as today). */
static uint32_t ui_button_leds(const ui_view_t *v, uint32_t blink)
{
    uint32_t m = 0;
    switch (v->page) {
    case UI_P_HOME: m |= 1u << BTN_HOME; break;
    case UI_P_EDIT: m |= 1u << BTN_EDIT; break;
    case UI_P_FX: m |= 1u << BTN_FX; break;
    case UI_P_ENV: m |= 1u << BTN_ENV; break;
    case UI_P_LFO: m |= 1u << BTN_LFO; break;
    case UI_P_SEL: m |= 1u << BTN_SEL; break;
    case UI_P_GLO: m |= 1u << BTN_GLO; break;
    default: break;
    }
    if (v->modified && blink) m |= 1u << BTN_SAVE;
    return m;
}
