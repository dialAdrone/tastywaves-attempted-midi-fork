/* SPDX-License-Identifier: GPL-3.0-only */
/* ui_screens.h: VAVEWave screens (T18). Draws from a plain struct of display values (ui_view_t): no engine, no
 * patch, no globals but the shadow struct. Included from tests/ui_demo.c (host mock-up, fake values) and, through
 * panel.h, from vavewave.c (the real app: panel.h fills the view from the patch).
 *
 * Needs: gfx.c (cv_begin / cv_rect / cv_line / cv_text / cv_blit, cv_oy, FONT_S / FONT_M / FONT_L, RGB) and BTN_* (platform.c).
 * Integer only. Main loop only (never from the ISR).
 *
 * Layout: y 0..35 four knob columns (60 px each: label, value, bar), then on every page but HOME the tab strip
 * (y 38, with the set tag under the tab shown when a tab has two knob sets) and the page's picture (y 66..199, the
 * biggest thing on the screen; the knob values are not repeated in it); HOME: the middle y 56..181. y 202 the status
 * line, y 218 the patch name. Every piece has a "shadow" copy of what it last drew (ui_shadow_t) and is redrawn only
 * when its inputs change. Canvas rule: w * h <= CV_MAX (240 x 64): a taller picture is drawn in strips (ui_pic). */
#pragma once

#define UI_ACC  RGB(180, 140, 255)              /* lilac, as the logo */
#define UI_ACC2 RGB(120, 90, 180)
#define UI_ACC3 RGB(44, 32, 76)
#define UI_TXT  RGB(225, 225, 230)
#define UI_GRAY RGB(130, 130, 142)
#define UI_DIM  RGB(58, 58, 72)

#define UI_KNOB_H  36u                          /* the knob row: label (FONT_S), value (FONT_M), bar */
#define UI_Y_MID   56u                          /* HOME: the middle area, 126 rows */
#define UI_Y_TABS  38u                          /* every page but HOME: the tab strip (15 rows), the set tag under it (13 rows) */
#define UI_TABS_H  28u
#define UI_Y_BODY  66u                          /* the page's picture: 134 rows to the status line */
#define UI_BODY_H  134u
#define UI_Y_LINEA 202u                         /* the status line, 14 rows */
#define UI_Y_PATCH 218u                         /* the patch name (FONT_M), 22 rows */
#define UI_PIC_MS  60u                          /* a changing picture is redrawn at most this often (a full one is ~40 ms of SPI) */

enum { UI_P_HOME, UI_P_EDIT, UI_P_FX, UI_P_ENV, UI_P_LFO, UI_P_SEL, UI_P_GLO, UI_P_ARP, UI_P_SEQ, UI_P_HELP, UI_P_N };
enum { UI_H_WATERFALL, UI_H_SPECTRUM, UI_H_MINIMAL, UI_H_N };

typedef struct {
    uint8_t page, variant;                      /* UI_P_*; HOME look UI_H_* */
    uint8_t sub;                                /* ENV: 0 amp 1 filter; EDIT: 0 osc1 1 osc2 2 sub 3 noise */
    uint8_t touched;                            /* knob last turned 0..3, 4 none: its value is lilac */
    uint8_t voices, voices_max, cpu, modified;  /* voices sounding / available, CPU % of the block, patch edited */
    uint8_t table, frame, nframes;              /* table index, nearest frame 0..nframes-1, frames in the table */
    uint16_t pos;                               /* 12-bit wavetable position 0..4095 */
    char patch[16], tname[9], fmsg[12];         /* fmsg: what the confirmation says while `flash` is set (empty: SAVED) */
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
    uint8_t latch;                              /* LATCH is on: the voice counter on the status line reads L instead of V, in the accent colour */
    uint8_t flt_cut, flt_res, flt_mode;         /* FX picture: cutoff, resonance 0..127, mode 0 LP 1 BP 2 HP */
    int8_t flt_env;                             /* FX picture: the envelope's reach on the cutoff, -64..63 semitones */
    uint8_t lfo_shape, drive;                   /* LFO picture (0 sine 1 tri 2 saw 3 square 4 S&H), GLO picture 0..127 */
    uint8_t lfo_rate;                           /* LFO picture: 0..127, the cycles drawn (1 .. 8, log) */
    uint8_t mod[4];                             /* GLO CONTROL picture: wheel > pos, wheel > vibrato, aftertouch > cutoff, velocity > level, 0..127 */
    uint8_t fxt[4];                             /* FX slot picture: the four slots' effect types (0 off) */
    uint8_t vmode, wdest, qual, ecurve;         /* SEL picture: voice mode (0 poly 1 mono 2 legato), BEND on (0 both 1 osc1 2 osc2), quality (0 hi 1 eco), env curve (0 lin 1 exp) */
    uint8_t arp_mode, arp_oct, arp_gate, arp_swing;     /* ARP picture: mode (0 off), octaves 1..4, gate 1..127, swing 0..50 */
    char ptxt[8];                               /* SEQ picture: the state (OFF PLAY STOP RECORD) or the lane's target */
    char info2[40];                             /* FX slot pictures: the once-only effects */
    char info[40];                              /* ARP and SEQ pictures: tempo and rate; steps */
    uint8_t seq_len, seq_pos, seq_rec, seq_on;  /* SEQ picture: steps, the step playing / being recorded, step input on, running */
    uint32_t seq_mask;                          /* SEQ picture: bit i = step i has a note (else a rest) */
    uint32_t seq_tie;                           /* SEQ picture: bit i = step i is tied to the step before */
    uint8_t seq_note[32];                       /* SEQ picture: the note each step plays now (transposition included) + 1; 0 = rest / out of range */
    uint8_t seq_mod[32];                        /* SEQ picture, MOD set: the lane, 0..127 (64 = neutral) */
    uint8_t seq_set, seq_cur;                   /* SEQ picture: the MOD set is shown; the lane step being edited */
    uint8_t seq_tr;                             /* SEQ picture: a key is held, the notes shown are transposed */
    char boot[2][32];                           /* GLO set 3 (BOOT KEYS): "BTN 0x0001 FR 80", "RAW" + 11 bytes hex */
    const char *const *tabn;                    /* the page's tab names (NULL: no strip, as in the mock-up) */
    uint8_t ntabs, tab;                         /* tabs on the page, the one shown */
    uint8_t tsets, tset;                        /* knob sets of the tab shown (2: the set tag "1/2" under it), the one shown */
    char help[14][32];                          /* HELP: the lines of the page shown ("LABEL  TEXT": the label is lilac) */
} ui_view_t;

typedef struct {
    uint8_t valid, page, variant, sub, touched, voices, cpu, modified, table, frame, kbip, ksmall;
    uint16_t pos;
    char patch[16], tname[9], fmsg[12], linea[48], kval[4][8];
    const char *klabel[4];
    uint8_t kfrac[4], env[2][4], harm[16];
    uint8_t flash, latch;
    uint32_t wave_key, pic_key;
    uint32_t t_chg;                             /* waterfall: ms of the last frame change */
    uint32_t t_pic;                             /* ms the page's picture was last drawn */
    uint8_t wf_arrow, wf_frame;                 /* waterfall: frame of the arrow on screen, of the white trace */
    char ptxt[8], info[40], info2[40];
} ui_shadow_t;

/* ---- small helpers (own copies: ui.h's s_cat / s_num belong to the sine build) */
static char *ui_cat(char *d, const char *s) { while (*s) *d++ = *s++; *d = 0; return d; }
static char *ui_cat5(char *d, const char *s) { uint32_t n = 0; while (*s && n++ < 5u) *d++ = *s++; *d = 0; return d; }   /* a table name on the status line: 5 characters */
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

/* ---- the four knob columns: label (FONT_S), value (FONT_M, or FONT_S when long), bar. 60 x 36 canvas, 4.3 KB each */
static void ui_knob(uint32_t i, const ui_view_t *v)
{
    const char *val = v->kval[i];
    int32_t n = 0, w, fill;
    uint16_t col = v->touched == i ? UI_ACC : UI_TXT;
    while (val[n]) n++;
    cv_begin(60, UI_KNOB_H, C_BLACK);           /* (the fonts' capitals: FONT_S rows 2..11, FONT_M rows 4..18 of the cell) */
    cv_text((60 - text_w(&FONT_S, v->klabel[i])) / 2, -1, &FONT_S, v->klabel[i], UI_GRAY);
    if (n <= 4 && !(v->ksmall >> i & 1u)) cv_text((60 - text_w(&FONT_M, val)) / 2, 9, &FONT_M, val, col);
    else cv_text((60 - text_w(&FONT_S, val)) / 2, 12, &FONT_S, val, col);
    cv_rect(4, 31, 52, 3, UI_DIM);
    w = (int32_t)v->kfrac[i] * 52 >> 8;
    if (v->kbip >> i & 1u) {                    /* from the middle */
        fill = w - 26;
        if (fill >= 0) cv_rect(30, 31, fill + 1, 3, v->touched == i ? UI_ACC : UI_ACC2);
        else cv_rect(30 + fill, 31, -fill, 3, v->touched == i ? UI_ACC : UI_ACC2);
        cv_rect(30, 29, 1, 7, UI_GRAY);
    } else
        cv_rect(4, 31, w, 3, v->touched == i ? UI_ACC : UI_ACC2);
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

/* ---- one cycle of the current frame, 256 samples over 239 px, h rows (into the canvas begun; thick: 2 px lines) */
static void ui_wave_in(uint32_t h, const int16_t *s, uint16_t col, int thick)
{
    int32_t i, mid = (int32_t)h / 2, amp = mid - 3, px = 0, py = mid;
    cv_rect(0, mid, 240, 1, UI_DIM);
    for (i = 0; i < 256; i++) {
        int32_t x = i * 239 >> 8, yy = mid - ((int32_t)s[i] * amp >> 15);
        if (i) { cv_line(px, py, x, yy, col); if (thick) cv_line(px, py + 1, x, yy + 1, col); }
        px = x;
        py = yy;
    }
}

static void ui_wave(uint32_t y, uint32_t h, const int16_t *s, uint16_t col)
{
    cv_begin(240, h, C_BLACK);
    ui_wave_in(h, s, col, 0);
    cv_blit(0, y);
}

/* ---- a picture taller than a canvas: drawn in strips of 64 rows, each from the same function with cv_oy = -strip
 * top (the drawing clips to the strip). Each strip costs the whole drawing again; the pictures are cheap. */
typedef void (*ui_pic_fn)(const ui_view_t *v);
static void ui_pic(uint32_t y, uint32_t h, ui_pic_fn f, const ui_view_t *v)
{
    uint32_t top;
    for (top = 0; top < h; top += 64u) {
        cv_begin(240, h - top < 64u ? h - top : 64u, C_BLACK);
        cv_oy = -(int32_t)top;
        f(v);
        cv_oy = 0;
        cv_blit(0, y + top);
        platform_poll();                        /* keep USB and the watchdog fed between the strips */
    }
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

/* ---- ENV: the envelope of the tab shown, the whole picture (ui_pic). Segment widths follow the values (6..60 px),
 * sustain is a fixed hold; the area under the curve filled. */
static void ui_env_fn(const ui_view_t *v)
{
    const uint8_t *e = v->env[v->sub & 1u];
    int32_t x0 = 12, base = 110, top = 12, sl = base - ((int32_t)e[2] * (base - top) >> 7);
    int32_t x1 = x0 + 6 + ((int32_t)e[0] * 54 >> 7), x2 = x1 + 6 + ((int32_t)e[1] * 54 >> 7), x3 = x2 + 40;
    int32_t x4 = x3 + 6 + ((int32_t)e[3] * 54 >> 7), x, yy;
    for (x = x0; x <= x4; x++) {                /* the area under the curve, a column at a time */
        if (x <= x1) yy = base + (top - base) * (x - x0) / (x1 - x0);
        else if (x <= x2) yy = top + (sl - top) * (x - x1) / (x2 - x1);
        else if (x <= x3) yy = sl;
        else yy = sl + (base - sl) * (x - x3) / (x4 - x3);
        cv_rect(x, yy, 1, base - yy + 1, UI_ACC3);
    }
    cv_rect(4, base, 232, 1, UI_DIM);
    for (x = 0; x < 2; x++) {                   /* 2 px lines */
        cv_line(x0, base - x, x1, top - x, UI_ACC);
        cv_line(x1, top - x, x2, sl - x, UI_ACC);
        cv_line(x2, sl - x, x3, sl - x, UI_ACC);
        cv_line(x3, sl - x, x4, base - x, UI_ACC);
    }
    cv_rect(x1 - 2, top - 2, 5, 5, C_WHITE);
    cv_rect(x2 - 2, sl - 2, 5, 5, C_WHITE);
    cv_rect(x3 - 2, sl - 2, 5, 5, C_WHITE);
    cv_text((x0 + x1) / 2 - 4, 113, &FONT_S, "A", UI_GRAY);
    cv_text((x1 + x2) / 2 - 4, 113, &FONT_S, "D", UI_GRAY);
    cv_text((x2 + x3) / 2 - 4, 113, &FONT_S, "S", UI_GRAY);
    cv_text((x3 + x4) / 2 - 4, 113, &FONT_S, "R", UI_GRAY);
}

/* ---- the tab strip (every page but HOME): all the page's tabs, the one shown filled; when that tab has two knob
 * sets, a tag under it says which one ("1/2", "SET 1/2" where it fits). One 240 x 28 canvas */
static void ui_tabs(uint32_t y, const ui_view_t *v)
{
    uint32_t i, n = v->ntabs ? v->ntabs : 1u;
    int32_t w = (232 - 4 * ((int32_t)n - 1)) / (int32_t)n;
    cv_begin(240, UI_TABS_H, C_BLACK);
    for (i = 0; i < n; i++) {
        int32_t x = 4 + (int32_t)i * (w + 4);
        const char *t = v->tabn[i];
        if (i == v->tab) {
            cv_rect(x, 0, w, 15, UI_ACC);
            cv_text(x + (w - text_w(&FONT_S, t)) / 2, 0, &FONT_S, t, C_BLACK);
            if (v->tsets > 1u) {
                char g[12], *d = g;
                if (w >= 64) d = ui_cat(d, "SET ");
                d = ui_num(d, v->tset + 1);
                d = ui_cat(d, "/");
                ui_num(d, v->tsets);
                cv_rect(x, 15, w, 13, UI_ACC3);
                cv_rect(x, 15, 1, 13, UI_ACC);
                cv_rect(x + w - 1, 15, 1, 13, UI_ACC);
                cv_rect(x, 27, w, 1, UI_ACC);
                cv_text(x + (w - text_w(&FONT_S, g)) / 2, 14, &FONT_S, g, C_WHITE);
            }
        } else {
            cv_rect(x, 0, w, 15, UI_DIM);
            cv_rect(x + 1, 1, w - 2, 13, C_BLACK);
            cv_text(x + (w - text_w(&FONT_S, t)) / 2, 0, &FONT_S, t, UI_GRAY);
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

/* the status line: left what this page is, right voices and CPU. One 240 x 14 canvas, 6.7 KB */
static void ui_linea(uint32_t y, const char *left, const ui_view_t *v)
{
    char r[24], *d = r;
    d = ui_cat(d, v->latch ? "L" : "V");      /* fixed field widths: the text must not move as digits change (L: latch on) */
    if (v->voices < 10u) d = ui_cat(d, " ");
    d = ui_num(d, v->voices);
    d = ui_cat(d, "/");
    d = ui_num(d, v->voices_max);
    d = ui_cat(d, " CPU ");
    if (v->cpu < 10u) d = ui_cat(d, " ");
    d = ui_num(d, v->cpu);
    ui_cat(d, "%");
    cv_begin(240, 14, C_BLACK);
    cv_text(4, 0, &FONT_S, left, UI_ACC);
    cv_text(236 - text_w(&FONT_S, r), 0, &FONT_S, r, v->cpu > 80u ? RGB(255, 90, 90) : UI_GRAY);
    if (v->latch) cv_text(236 - text_w(&FONT_S, r), 0, &FONT_S, "L", UI_ACC);
    cv_blit(0, y);
}

/* the patch name: FONT_M in a 240 x 22 canvas, or FONT_L (32 rows) on the minimal HOME look */
static void ui_patch(uint32_t y, const ui_view_t *v, uint16_t col, int big)
{
    const felucca_font_t *f = big ? &FONT_L : &FONT_M;
    const char *m = v->flash ? (v->fmsg[0] ? v->fmsg : "SAVED") : v->patch;
    int32_t ty = big ? 0 : -2;
    cv_begin(240, big ? 32u : 22u, C_BLACK);
    cv_text((240 - text_w(f, m)) / 2, ty, f, m, v->flash ? UI_ACC : col);
    if (!v->flash && v->modified) cv_rect(0, big ? 13 : 6, 5, 6, UI_ACC);      /* edited since loaded / saved */
    cv_blit(0, y);
}

static void ui_left_text(char *out, const ui_view_t *v)       /* the left part of the status line: the page, the tab n/N */
{
    static const char *const PN[UI_P_N] = {"", "EDIT", "FX", "ENV", "LFO", "VOICE", "GLOBAL", "ARP", "SEQ", ""};
    char *d = out;
    if (v->page == UI_P_HOME || v->page >= UI_P_HELP) {
        if (v->variant != UI_H_MINIMAL) { d = ui_cat5(d, v->tname); d = ui_cat(d, " "); }     /* (with the table name: no FRM, the line is full) */
        else d = ui_cat(d, "FRM ");
        d = ui_num(d, v->frame + 1);
        d = ui_cat(d, "/");
        ui_num(d, v->nframes);
        return;
    }
    d = ui_cat(d, PN[v->page]);
    if (v->page == UI_P_SEQ && v->seq_rec) d = ui_cat(d, " REC");
    if (v->ntabs > 1u) { d = ui_cat(d, " "); d = ui_num(d, v->tab + 1); d = ui_cat(d, "/"); d = ui_num(d, v->ntabs); }
    if (v->page == UI_P_EDIT) { d = ui_cat(d, " "); ui_cat5(d, v->tname); }
}

/* ---- the pages' pictures: each fills the body (240 x UI_BODY_H, drawn by ui_pic in strips), y 0 = the body's top */
static void ui_filter_fn(const ui_view_t *v)    /* FX FILTER: the response, the cutoff line, the envelope's reach as an arrow */
{
    int32_t x, xc = 8 + (int32_t)v->flt_cut * 224 / 127, base = 108, top = 16, bump = (int32_t)v->flt_res * 44 / 127, py = 0;
    int32_t xe = xc + (int32_t)v->flt_env * 15 / 8;         /* semitones: the 224 px axis is ~120 semitones */
    static const char *const TK[3] = {"100", "1K", "10K"};
    static const int16_t TX[3] = {60, 134, 209};      /* 100 Hz, 1 kHz, 10 kHz on the 20 Hz .. 19 kHz log axis */
    cv_rect(8, base, 224, 1, UI_DIM);
    for (x = 0; x < 3; x++) {
        cv_rect(TX[x], base, 1, 4, UI_GRAY);
        cv_text(TX[x] - text_w(&FONT_S, TK[x]) / 2, 112, &FONT_S, TK[x], UI_GRAY);
    }
    for (x = 8; x <= 232; x++) {
        int32_t dd = x - xc, ad = dd < 0 ? -dd : dd, yy;
        if (v->flt_mode == 0) yy = top + (dd > 0 ? dd * 5 / 3 : 0);               /* 12 dB / octave */
        else if (v->flt_mode == 2) yy = top + (dd < 0 ? -dd * 5 / 3 : 0);
        else yy = top + 24 + ad * 5 / 6;                                          /* band pass: 6 dB / octave */
        if (ad < 24) yy -= bump * (24 - ad) / 24;                                 /* the resonance peak */
        if (yy < 0) yy = 0;
        if (yy > base) yy = base;
        cv_rect(x, yy, 1, base - yy + 1, UI_ACC3);
        if (x > 8) { cv_line(x - 1, py, x, yy, UI_ACC); cv_line(x - 1, py + 1, x, yy + 1, UI_ACC); }
        py = yy;
    }
    {   /* the cutoff: a white line and its frequency. 0..127 = 20 Hz x 2^(0.94 c / 12): n = 1/16 octaves */
        static const uint16_t T16[16] = {256, 267, 279, 291, 304, 317, 331, 345, 362, 378, 395, 412, 431, 449, 470, 490};
        uint32_t n = (uint32_t)v->flt_cut * 94u / 75u, hz = (20u * T16[n & 15u] << (n >> 4)) >> 8;
        char t[8], *d = t;
        int32_t tw;
        if (hz < 1000u) ui_num(d, (int32_t)hz);
        else if (hz < 10000u) { d = ui_num(d, (int32_t)(hz / 1000u)); *d++ = '.'; *d++ = (char)('0' + hz / 100u % 10u); *d++ = 'K'; *d = 0; }
        else { d = ui_num(d, (int32_t)((hz + 500u) / 1000u)); ui_cat(d, "K"); }
        tw = text_w(&FONT_S, t);
        cv_rect(xc, 10, 2, base - 10, C_WHITE);
        x = v->flt_mode == 0 ? xc - 5 - tw : xc + 6;               /* on the pass side, under the curve's top */
        if (x < 8) x = xc + 6;
        if (x + tw > 236) x = xc - 5 - tw;
        cv_text(x, 26, &FONT_S, t, C_WHITE);
    }
    if (xe < 8) xe = 8;
    if (xe > 232) xe = 232;
    if (xe != xc) {                             /* the envelope sweeps the cutoff from xc to xe */
        int32_t dir = xe > xc ? -1 : 1;
        cv_rect(xe < xc ? xe : xc, 4, (xe > xc ? xe - xc : xc - xe) + 1, 1, UI_ACC2);
        cv_line(xe, 4, xe + 4 * dir, 0, UI_ACC2);
        cv_line(xe, 4, xe + 4 * dir, 8, UI_ACC2);
        for (x = 6; x < base; x += 4) cv_rect(xe, x, 1, 2, UI_ACC2);
    }
}

static void ui_lfo_fn(const ui_view_t *v)       /* LFO: the shape, 1 cycle at RATE 0 to 8 at RATE 127 (2^(3 r / 127), as the rate's log scale) */
{
    static const uint16_t T16[16] = {256, 267, 279, 291, 304, 317, 331, 345, 362, 378, 395, 412, 431, 449, 470, 490};
    uint32_t n = (uint32_t)v->lfo_rate * 48u / 127u, cyc = (uint32_t)T16[n & 15u] << (n >> 4);   /* cycles x 256 */
    int32_t x, px = 8, py = 62;
    cv_rect(8, 62, 224, 1, UI_DIM);
    for (x = 0; x < 224; x++) {
        int32_t ph = (int32_t)((uint32_t)x * cyc / 896u), q = ph & 63, st = ph >> 4, val, yy;   /* 64 a cycle: x cyc 64 / (224 256) */
        switch (v->lfo_shape) {
        case 0: val = UI_SIN64[q]; break;
        case 1: val = q < 16 ? q * 1024 : q < 48 ? (32 - q) * 1024 : (q - 64) * 1024; break;
        case 2: val = -16384 + q * 512; break;
        case 3: val = q < 32 ? 16384 : -16384; break;
        default: val = (((st * 2654435761u) >> 12) & 31u) * 1024 - 16000; break;   /* S&H: 4 steps a cycle */
        }
        yy = 62 - (val * 54 >> 14);
        if (x) { cv_line(px, py, 8 + x, yy, UI_ACC); cv_line(px, py + 1, 8 + x, yy + 1, UI_ACC); }
        px = 8 + x;
        py = yy;
    }
}

static void ui_drive_fn(const ui_view_t *v)     /* GLO OUTPUT: the drive's curve; the worst CPU load since reset */
{
    int32_t i, g = 16 + (int32_t)v->drive * 240 / 127, py = 0, cx = 70, cy = 66;   /* gain 1 .. 16, Q4 */
    char t[8], *d = t;
    cv_rect(cx - 60, cy, 121, 1, UI_DIM);
    cv_rect(cx, cy - 60, 1, 121, UI_DIM);
    for (i = 0; i <= 58; i++) {                 /* the right half, mirrored for the left (odd symmetry) */
        int32_t u = i * g * 4096 / (58 * 16);   /* x * gain, Q12 */
        int32_t sat = u * 8192 / (4096 + u);    /* a soft knee, x / (1 + x) x 2 */
        int32_t h = (sat > 4096 ? 4096 : sat) * 56 >> 12;
        if (i) {
            cv_line(cx + i - 1, cy - py, cx + i, cy - h, UI_ACC);
            cv_line(cx + i - 1, cy - py - 1, cx + i, cy - h - 1, UI_ACC);
            cv_line(cx - i + 1, cy + py, cx - i, cy + h, UI_ACC);
            cv_line(cx - i + 1, cy + py + 1, cx - i, cy + h + 1, UI_ACC);
        }
        py = h;
    }
    d = ui_num(d, v->cpu_max);
    ui_cat(d, "%");
    cv_text(152, 40, &FONT_S, "CPU MAX", UI_GRAY);
    cv_text(152, 54, &FONT_M, t, v->cpu_max > 80u ? RGB(255, 90, 90) : UI_TXT);
}

static void ui_ctrl_fn(const ui_view_t *v)      /* GLO CONTROL: what the wheel, aftertouch and velocity reach; a line per route, thicker = more */
{
    static const char *const SRC[3] = {"WHEEL", "AFTERT", "VELO"};
    static const char *const DST[4] = {"POS", "VIB", "CUTOFF", "LEVEL"};
    static const uint8_t SY0[3] = {4, 68, 100}, SH[3] = {56, 24, 24}, FROM[4] = {0, 0, 1, 2};
    int32_t i, k;
    for (i = 0; i < 3; i++) {
        int32_t y = SY0[i], h = SH[i];
        cv_rect(4, y, 80, h, UI_DIM);
        cv_rect(5, y + 1, 78, h - 2, C_BLACK);
        cv_text(44 - text_w(&FONT_S, SRC[i]) / 2, y + h / 2 - 7, &FONT_S, SRC[i], UI_TXT);
    }
    for (i = 0; i < 4; i++) {
        int32_t y = 4 + i * 32, a = v->mod[i], s = FROM[i], sy = SY0[s] + SH[s] / 2 + (s == 0 ? (i ? 10 : -10) : 0);
        int32_t th = a ? 1 + a / 32 : 1;
        uint16_t c = a ? (a >= 64 ? UI_ACC : UI_ACC2) : UI_DIM;
        cv_rect(156, y, 80, 24, a ? UI_ACC2 : UI_DIM);
        cv_rect(157, y + 1, 78, 22, C_BLACK);
        cv_text(196 - text_w(&FONT_S, DST[i]) / 2, y + 5, &FONT_S, DST[i], a ? UI_TXT : UI_GRAY);
        for (k = 0; k < th; k++) cv_line(85, sy + k - th / 2, 155, y + 12 + k - th / 2, c);
    }
}

static void ui_boot_fn(const ui_view_t *v)      /* GLO BOOT: what the boot key scan saw */
{
    cv_text(4, 4, &FONT_S, "BOOT KEYS (SCAN AT POWER-ON)", UI_GRAY);
    cv_text(4, 24, &FONT_S, v->boot[0], UI_TXT);
    cv_text(4, 44, &FONT_S, v->boot[1], UI_TXT);
}

static void ui_fx_fn(const ui_view_t *v)        /* FX slot: the chain of four slots, the one shown filled; where delay and reverb are */
{
    static const char *const NM[6] = {"-", "CHO", "FLG", "PHA", "DLY", "REV"};
    int32_t i, cur = (int32_t)v->sub - 2;
    for (i = 0; i < 4; i++) {
        int32_t x = 4 + i * 60, t = v->fxt[i] < 6u ? v->fxt[i] : 0;
        char n[2] = {(char)('1' + i), 0};
        cv_text(x + 22, 2, &FONT_S, n, i == cur ? UI_ACC : UI_GRAY);
        if (i == cur) cv_rect(x, 20, 52, 66, UI_ACC);
        else { cv_rect(x, 20, 52, 66, t ? UI_ACC2 : UI_DIM); cv_rect(x + 1, 21, 50, 64, t ? UI_ACC3 : C_BLACK); }
        cv_text(x + (52 - text_w(&FONT_M, NM[t])) / 2, 40, &FONT_M, NM[t], i == cur ? C_BLACK : t ? UI_TXT : UI_DIM);
        if (i < 3) { cv_line(x + 53, 47, x + 57, 53, UI_GRAY); cv_line(x + 53, 59, x + 57, 53, UI_GRAY); }
    }
    cv_text(4, 100, &FONT_S, v->info2, UI_GRAY);
}

static void ui_sel_fn(const ui_view_t *v)       /* SEL: a panel per knob, in knob order (MODE BEND> top, QUAL CURVE below) */
{
    int32_t i, px, py, x, y;
    cv_text(4, 0, &FONT_S, "MODE", UI_GRAY);
    cv_text(124, 0, &FONT_S, "BEND>", UI_GRAY);
    cv_text(4, 68, &FONT_S, "QUAL", UI_GRAY);
    cv_text(124, 68, &FONT_S, "CURVE", UI_GRAY);
    cv_rect(119, 2, 1, 128, UI_DIM);
    cv_rect(4, 65, 232, 1, UI_DIM);
    if (v->vmode == 0) {                        /* POLY: the 12 voices, lit while they sound */
        uint32_t n = v->voices_max < 12u ? v->voices_max : 12u, k;
        for (k = 0; k < n; k++) {
            x = 4 + (int32_t)(k % 6u) * 19; y = 16 + (int32_t)(k / 6u) * 24;
            if (k < v->voices) cv_rect(x, y, 16, 21, UI_ACC);
            else { cv_rect(x, y, 16, 21, UI_DIM); cv_rect(x + 1, y + 1, 14, 19, C_BLACK); }
        }
    } else {                                    /* MONO / LEGATO: one voice; LEGATO ties the notes (a tie under it) */
        if (v->voices) cv_rect(28, 16, 64, 30, UI_ACC);
        else { cv_rect(28, 16, 64, 30, UI_DIM); cv_rect(29, 17, 62, 28, C_BLACK); }
        cv_text(60 - text_w(&FONT_S, v->vmode == 1 ? "MONO" : "LEGATO") / 2, 22, &FONT_S, v->vmode == 1 ? "MONO" : "LEGATO", v->voices ? C_BLACK : UI_GRAY);
        if (v->vmode == 2) { cv_line(30, 52, 44, 58, UI_ACC); cv_line(44, 58, 76, 58, UI_ACC); cv_line(76, 58, 90, 52, UI_ACC); }
    }
    for (i = 0; i < 2; i++) {                   /* BEND>: the oscillators it bends, lit */
        int on = v->wdest == 0 || v->wdest == (uint8_t)(i + 1);
        x = 126 + i * 56;
        cv_rect(x, 22, 50, 32, on ? UI_ACC : UI_DIM);
        if (!on) cv_rect(x + 1, 23, 48, 30, C_BLACK);
        cv_text(x + 25 - text_w(&FONT_S, i ? "OSC2" : "OSC1") / 2, 30, &FONT_S, i ? "OSC2" : "OSC1", on ? C_BLACK : UI_GRAY);
    }
    /* QUAL: a cycle drawn smooth (HI) or as a coarse polyline with its points (ECO) */
    px = 8; py = 104;
    for (i = 0; i <= 104; i += v->qual ? 13 : 1) {
        x = 8 + i; y = 104 - (UI_SIN64[(i * 64 / 104) & 63] * 24 >> 14);
        if (i) { cv_line(px, py, x, y, UI_ACC); cv_line(px, py + 1, x, y + 1, UI_ACC); }
        if (v->qual) cv_rect(x - 1, y - 1, 3, 3, C_WHITE);
        px = x; py = y;
    }
    cv_text(116 - text_w(&FONT_S, v->qual ? "ECO" : "HI"), 68, &FONT_S, v->qual ? "ECO" : "HI", UI_GRAY);
    /* CURVE: the decay / release, straight (LIN) or exponential (EXP, ~ (1 - t)^3) */
    px = 128; py = 82;
    for (i = 1; i <= 100; i++) {
        int32_t r = 100 - i, l = v->ecurve ? r * r * r / 10000 : r;     /* level 0..100 */
        x = 128 + i; y = 128 - l * 46 / 100;
        cv_line(px, py, x, y, UI_ACC); cv_line(px, py + 1, x, y + 1, UI_ACC);
        px = x; py = y;
    }
    cv_rect(128, 128, 104, 1, UI_DIM);
    cv_text(236 - text_w(&FONT_S, v->ecurve ? "EXP" : "LIN"), 68, &FONT_S, v->ecurve ? "EXP" : "LIN", UI_GRAY);
}

static void ui_arp_fn(const ui_view_t *v)       /* ARP: 16 steps of the pattern a three-note chord plays (gate = note length, swing = late odd steps) */
{
    static const uint8_t RND[16] = {2, 0, 5, 1, 4, 3, 0, 5, 2, 4, 1, 3, 5, 0, 2, 4};
    int32_t oct = v->arp_oct < 1u ? 1 : v->arp_oct > 4u ? 4 : v->arp_oct, n = 3 * oct, s, lh = 96 / n;
    int32_t gw = (int32_t)v->arp_gate * 13 / 127, sw = (int32_t)v->arp_swing * 14 / 100;
    cv_text(4, 0, &FONT_S, v->info, UI_GRAY);
    if (!v->arp_mode) { cv_text((240 - text_w(&FONT_M, "ARP OFF")) / 2, 60, &FONT_M, "ARP OFF", UI_DIM); return; }
    cv_rect(8, 117, 224, 1, UI_DIM);
    for (s = 0; s < 16; s++) {
        int32_t x = 8 + s * 14 + (s & 1 ? sw : 0), k, lo, hi, m = 2 * n - 2 > 0 ? 2 * n - 2 : 1;
        switch (v->arp_mode) {
        case 1: lo = s % n; break;                                       /* UP */
        case 2: lo = n - 1 - s % n; break;                               /* DOWN */
        case 3: k = s % m; lo = k < n ? k : m - k; break;                /* UP-DN */
        case 4: k = s % (2 * n); lo = k < n ? k : 2 * n - 1 - k; break;  /* UP-DN2: the ends twice */
        case 5: lo = RND[s] % n; break;                                  /* RANDOM */
        case 6: { static const uint8_t PL[3] = {0, 2, 1}; lo = PL[s % 3] + 3 * (s / 3 % oct); break; }   /* PLAYED: in the order played (C G E) */
        default: lo = -1; break;                                         /* CHORD: all notes each step */
        }
        hi = lo < 0 ? n - 1 : lo;
        if (lo < 0) lo = 0;
        for (k = lo; k <= hi; k++)
            cv_rect(x, 116 - (k + 1) * lh + 1, gw < 2 ? 2 : gw, lh - 1 < 2 ? 2 : lh - 1, s ? UI_ACC : C_WHITE);
    }
}

static void ui_edit_fn(const ui_view_t *v)      /* EDIT: one cycle of the oscillator's wave (the morph and BEND included), tall */
{
    ui_wave_in(112, v->wave ? v->wave : v->frames + (uint32_t)v->frame * v->stride, UI_ACC, 1);
}

static uint32_t ui_seq_h(const ui_view_t *v)               /* a hash of the notes shown, for the redraw check */
{
    uint32_t i, h = 2166136261u;
    for (i = 0; i < 32u; i++) h = (h ^ (v->seq_set ? v->seq_mod[i] : v->seq_note[i])) * 16777619u;
    return h ^ (uint32_t)v->seq_set << 5 ^ (uint32_t)v->seq_cur << 12;
}

static void ui_seq_grid(uint32_t y, const ui_view_t *v)       /* the pattern: 4 rows of 8 tiles with the note each plays; white = playing / being recorded, yellow = transposed */
{
    static const char NN[12][3] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    uint32_t i, h;
    for (h = 0; h < 2u; h++) {                  /* two canvases of two tile rows (a canvas holds 64 rows at most) */
    cv_begin(240, 50, C_BLACK);
    for (i = h * 16u; i < h * 16u + 16u; i++) {
        int32_t x = (int32_t)(i & 7u) * 30, ty = (int32_t)((i >> 3) & 1u) * 25;
        uint32_t cur = i == v->seq_pos && (v->seq_on || v->seq_rec);
        if (v->seq_set) {                       /* the lane: a bar a step from the middle line, the step being edited framed */
            int32_t mv = (int32_t)v->seq_mod[i] - 64, bh = (mv < 0 ? -mv : mv) * 11 / 64;
            uint32_t on = i < v->seq_len || !v->seq_len;
            uint16_t c = i == v->seq_pos && v->seq_on ? C_WHITE : UI_ACC;
            cv_rect(x, ty, 29, 24, on ? UI_ACC3 : C_BLACK);
            cv_rect(x + 3, ty + 12, 23, 1, UI_DIM);
            if (bh) cv_rect(x + 3, mv > 0 ? ty + 12 - bh : ty + 13, 23, bh, c);
            if (v->seq_set == 1u && i == v->seq_cur) { cv_rect(x, ty, 29, 1, RGB(255, 200, 90)); cv_rect(x, ty + 23, 29, 1, RGB(255, 200, 90)); cv_rect(x, ty, 1, 24, RGB(255, 200, 90)); cv_rect(x + 28, ty, 1, 24, RGB(255, 200, 90)); }
        } else if (i < v->seq_len) {
            uint32_t nt = v->seq_note[i];
            uint16_t bg = cur ? C_WHITE : (v->seq_mask >> i & 1u) ? UI_ACC3 : C_BLACK;
            uint16_t fg = cur ? C_BLACK : v->seq_tr ? RGB(255, 200, 90) : UI_ACC;
            cv_rect(x, ty, 29, 24, bg);
            if (!(v->seq_mask >> i & 1u)) cv_rect(x + 11, ty + 11, 7, 2, cur ? C_BLACK : UI_DIM);      /* a rest: a dash */
            else if (nt) {
                char t[6], *d = t;
                int32_t m = (int32_t)nt - 1, o = m / 12 - 1;
                d = ui_cat(d, NN[m % 12]);
                if (o < 0) *d++ = '-'; else *d++ = (char)('0' + o);
                *d = 0;
                cv_text(x + (29 - text_w(&FONT_S, t)) / 2, ty + 5, &FONT_S, t, fg);
                if (v->seq_tie >> i & 1u) cv_rect(x, ty + 11, 2, 2, fg);                                 /* tied to the step before */
            }
        } else if (v->seq_rec && i == v->seq_pos) cv_rect(x, ty, 29, 24, C_WHITE);
        else cv_rect(x + 12, ty + 11, 5, 2, UI_ACC3);
    }
    cv_blit(0, y + h * 50u);
    }
}

/* ---- HELP (hold HOME): two text pages, one 240 x 16 canvas a line. A line is "LABEL  what it does": the text up
 * to the first double space is drawn lilac, the rest white; a line without one is gray (the footer). */
/* ---- HELP: 14 text lines the panel fills (panel.h pn_help): "LABEL  TEXT", the label lilac; a line with no double space is
 * grey (the first white); a line starting with a space continues the one above, plain */
static void ui_help(const ui_view_t *v)
{
    uint32_t i, n, m;
    char lab[32];
    for (i = 0; i < 14u; i++) {
        const char *t = v->help[i];
        for (n = 0; t[n] && !(t[n] == ' ' && t[n + 1u] == ' '); n++) {}
        cv_begin(240, 16, C_BLACK);
        if (t[0] == ' ') {                      /* a continued line: plain text */
            cv_text(4, 0, &FONT_S, t + 1, UI_TXT);
        } else if (!t[n]) {
            cv_text(4, 0, &FONT_S, t, i ? UI_GRAY : UI_TXT);
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
        if (full || s->sub != v->sub) ui_help(v);
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

    if (v->page != UI_P_HOME && v->tabn && (full || s->sub != v->sub)) ui_tabs(UI_Y_TABS, v);

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
            ui_spectrum(UI_Y_MID + 74u, s->harm);
        }
        if (full || s->pos != v->pos) ui_posbar(UI_Y_MID + 58u, 12, v->pos);
    } else if (home3) {
        if (full || !ui_eq(s->patch, v->patch) || s->flash != v->flash || !ui_eq(s->fmsg, v->fmsg) || s->modified != v->modified) ui_patch(58, v, C_WHITE, 1);
        if (frame_new) {
            draw_text_box(0, 92, 240, &FONT_L, v->tname, UI_ACC, 1);
            ui_wave(156, 40, v->frames + (uint32_t)v->frame * v->stride, UI_ACC2);
        }
        if (full || s->pos != v->pos) ui_posbar(130, 20, v->pos);
        if (full || s->voices != v->voices) ui_dots(222, v->voices, v->voices_max);
        left_y = 200;
    } else if (v->page == UI_P_EDIT) {
        if (frame_new || s->wave_key != v->wave_key || s->sub != v->sub) ui_pic(UI_Y_BODY, 114, ui_edit_fn, v);
        if (full || s->pos != v->pos) ui_posbar(UI_Y_BODY + 116u, 18, v->pos);
    } else if (v->page != UI_P_HOME) {          /* one picture per page and tab, redrawn when what it shows changes */
        uint32_t pk, tc;
        int textch = 0;
        ui_pic_fn f = 0;
        switch (v->page) {
        case UI_P_FX:
            if (v->sub >= 2u) { f = ui_fx_fn; pk = 0x50000000u | v->sub | (uint32_t)v->fxt[0] << 4 | (uint32_t)v->fxt[1] << 8 | (uint32_t)v->fxt[2] << 12 | (uint32_t)v->fxt[3] << 16; textch = !ui_eq(s->info2, v->info2); }
            else { f = ui_filter_fn; pk = (uint32_t)v->flt_cut | (uint32_t)v->flt_res << 8 | (uint32_t)v->flt_mode << 16 | (uint32_t)(uint8_t)v->flt_env << 20; }
            break;
        case UI_P_ENV: f = ui_env_fn; pk = (uint32_t)v->sub << 28 ^ (uint32_t)v->env[v->sub & 1u][0] ^ (uint32_t)v->env[v->sub & 1u][1] << 7 ^ (uint32_t)v->env[v->sub & 1u][2] << 14 ^ (uint32_t)v->env[v->sub & 1u][3] << 21; break;
        case UI_P_LFO: f = ui_lfo_fn; pk = 0x10000000u | v->lfo_shape | (uint32_t)v->lfo_rate << 8; break;
        case UI_P_SEL: f = ui_sel_fn; pk = 0x30000000u | v->voices | (uint32_t)v->voices_max << 8 | (uint32_t)v->vmode << 16 | (uint32_t)v->wdest << 18 | (uint32_t)v->qual << 20 | (uint32_t)v->ecurve << 21; break;
        case UI_P_ARP: f = ui_arp_fn; pk = 0x20000000u | v->arp_mode | (uint32_t)v->arp_oct << 4 | (uint32_t)v->arp_gate << 8 | (uint32_t)v->arp_swing << 16; textch = !ui_eq(s->info, v->info); break;
        case UI_P_SEQ:
            pk = v->seq_mask ^ (uint32_t)v->seq_len << 24 ^ (uint32_t)v->seq_pos << 16 ^ (uint32_t)v->seq_rec << 9 ^ (uint32_t)v->seq_on << 10 ^ v->seq_tie * 2654435761u ^ (uint32_t)v->seq_tr << 8 ^ ui_seq_h(v) ^ 0x40000000u;
            textch = !ui_eq(s->ptxt, v->ptxt) || !ui_eq(s->info, v->info);
            break;
        default:                                /* GLO */
            if (v->sub == 2u) { f = ui_boot_fn; pk = 0x60000000u; }
            else if (v->sub == 1u) { f = ui_ctrl_fn; pk = 0x70000000u ^ (uint32_t)v->mod[0] ^ (uint32_t)v->mod[1] << 7 ^ (uint32_t)v->mod[2] << 14 ^ (uint32_t)v->mod[3] << 21; }
            else { f = ui_drive_fn; pk = (uint32_t)v->drive | (uint32_t)v->cpu_max << 8; }
            break;
        }
        tc = full || s->sub != v->sub;          /* a new tab: at once; else at most every UI_PIC_MS while it changes */
        if (tc || ((s->pic_key != pk || textch) && (v->ms == 0 || v->ms - s->t_pic >= UI_PIC_MS))) {
            if (f) ui_pic(UI_Y_BODY, UI_BODY_H, f, v);
            else {                              /* SEQ: the state and the steps, then the grid */
                ui_text_line(UI_Y_BODY, v->ptxt, UI_ACC);
                ui_text_line(UI_Y_BODY + 16u, v->info, UI_GRAY);
                ui_seq_grid(UI_Y_BODY + 34u, v);
            }
            s->pic_key = pk;
            s->t_pic = v->ms;
            ui_copy(s->ptxt, v->ptxt, 8);
            ui_copy(s->info, v->info, 40);
            ui_copy(s->info2, v->info2, 40);
        }
    }

    ui_left_text(left, v);                      /* the status line: when its text, voices or CPU changed */
    if (full || !ui_eq(s->linea, left) || s->voices != v->voices || s->cpu != v->cpu || s->latch != v->latch) {
        ui_linea(left_y, left, v);
        ui_copy(s->linea, left, 48);
    }
    if (!home3 && (full || !ui_eq(s->patch, v->patch) || s->modified != v->modified || s->flash != v->flash || !ui_eq(s->fmsg, v->fmsg)))
        ui_patch(UI_Y_PATCH, v, C_WHITE, 0);

    s->page = v->page; s->variant = v->variant; s->sub = v->sub;
    s->voices = v->voices; s->cpu = v->cpu; s->modified = v->modified;
    s->table = v->table; s->frame = v->frame; s->pos = v->pos;
    s->flash = v->flash; s->latch = v->latch; s->wave_key = v->wave_key;
    ui_copy(s->patch, v->patch, 16);
    ui_copy(s->fmsg, v->fmsg, 12);
    ui_copy(s->tname, v->tname, 9);
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
