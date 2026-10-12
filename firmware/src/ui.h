/* SPDX-License-Identifier: GPL-3.0-only */
/* ui.h: the screen. Included once from vavewave.c after engine.h (main loop only: never call from the ISR).
 * Sine build: the name, the last note big, the ISR cost, the keyboard with what sounds, the octave.
 * BENCH build: what is measured, the settings, the ISR cost, the keyboard, the button legend. */
#pragma once

#define C_ACC RGB(180, 140, 255)                /* lilac, as the logo */
#define C_KEY RGB(210, 210, 210)
#define C_OFF RGB(90, 90, 90)
#define KEY0 53u                                /* key 0 = F3 */
static int black(uint32_t note) { return (0x54Au >> (note % 12u)) & 1u; }

static char *s_cat(char *d, const char *s) { while (*s) *d++ = *s++; *d = 0; return d; }
static char *s_num(char *d, int32_t v, int dec)  /* decimal, dec digits after the point: (-63, 1) -> "-6.3" */
{                                               /* (libc.c's fmt_fix is not in the host build) */
    char t[12];
    int n = 0;
    uint32_t u = (uint32_t)(v < 0 ? -v : v);
    do {
        t[n++] = (char)('0' + u % 10u);
        u /= 10u;
        if (n == dec) t[n++] = '.';
    } while (u || n <= dec || (dec && n == dec + 1));   /* "0.5", not ".5" */
    if (v < 0) *d++ = '-';
    while (n) *d++ = t[--n];
    *d = 0;
    return d;
}

static void draw_note(void)
{
    static const char *const NAME[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    char s[8] = "-";
    if (last_note < 128u) {
        const char *p = NAME[last_note % 12u];
        uint32_t i = 0;
        while (*p) s[i++] = *p++;
        if (last_note >= 12u) s[i++] = (char)('0' + last_note / 12u - 1u);
        s[i] = 0;
    }
    draw_text_box(0, 82, 240, &FONT_L, s, C_WHITE, 1);
}

static void draw_keys(uint32_t lit)             /* bit k: key k sounds */
{
    uint32_t k, w = 0;
    cv_begin(240, 56, C_BLACK);
    for (k = 0; k < 27u; k++)                   /* 16 white keys of 14 px from x = 8 */
        if (!black(KEY0 + k)) {
            cv_rect(8 + (int32_t)w * 14, 0, 13, 56, lit >> k & 1u ? C_ACC : C_KEY);
            w++;
        }
    for (k = 0, w = 0; k < 27u; k++)            /* the black ones on the line between two white */
        if (black(KEY0 + k))
            cv_rect(8 + (int32_t)w * 14 - 5, 0, 9, 34, lit >> k & 1u ? C_ACC : C_BLACK);
        else
            w++;
    cv_blit(0, 140);
}

static void draw_status(int32_t oct)
{
    char s[12] = "OCTAVE +0";
    s[7] = oct < 0 ? '-' : '+';
    s[8] = (char)('0' + (oct < 0 ? -oct : oct));
    draw_text_box(0, 212, 240, &FONT_S, s, C_OFF, 1);
}

/* ---- the ISR cost (T10): ticks -> "1234us 21.3%" of the block budget */
static char *s_cost(char *d, uint32_t ticks)
{
    uint32_t t = ticks > 4000000u ? 4000000u : ticks;             /* 166 ms: t * 1000 still fits 32 bits */
    uint32_t pm = t * 1000u / AUDIO_BUDGET_TICKS;                /* per mille of the budget */
    d = s_num(d, (int32_t)(t / FM1_TICKS_PER_US), 0);
    d = s_cat(d, "us ");
    d = s_num(d, (int32_t)pm, 1);
    return s_cat(d, "%");
}

#if !BENCH
static void draw_cpu(uint32_t last, uint32_t max) /* one line between the note and the keyboard */
{
    char s[40], *d = s;
    d = s_cat(d, "ISR ");
    d = s_cost(d, last);
    d = s_cat(d, " MAX ");
    s_cost(d, max);
    draw_text_box(0, 118, 240, &FONT_S, s, C_OFF, 1);
}
#else
static void draw_bench_static(void)
{
    lcd_fill(0, 0, 240, 240, C_BLACK);
    draw_text_box(0, 0, 240, &FONT_S, "BENCH: OSC+FRAME LERP+AMP", C_ACC, 1);
    draw_text_box(0, 16, 240, &FONT_S, "2 osc/voice; no env/filt/FX", C_OFF, 1);
    draw_text_box(0, 200, 240, &FONT_S, "EDIT interp  SEL voices", C_OFF, 1);
    draw_text_box(0, 218, 240, &FONT_S, "PLAY hold FX tbl HOME max", C_OFF, 1);
}

static void draw_bench_cfg(uint32_t ip, uint32_t nv, uint32_t same, uint32_t hold, int32_t oct)
{
    static const char *const IPN[IP_N] = {"LINEAR", "CUBIC 4-TAP", "SINC 8-TAP"};
    char s[48], *d = s;
    d = s_cat(d, "INTERP ");
    s_cat(d, IPN[ip < IP_N ? ip : 0]);
    draw_text_box(0, 36, 240, &FONT_S, s, C_WHITE, 0);
    d = s_cat(s, "VOICES ");
    d = s_num(d, (int32_t)nv, 0);
    d = s_cat(d, "x2  ");
    d = s_cat(d, same ? "TBL SAME" : "TBL 12 DIFF");
    draw_text_box(0, 52, 240, &FONT_S, s, C_WHITE, 0);
    d = s_cat(s, hold ? "HOLD 12-NOTE CHORD  OCT " : "HOLD OFF  OCT ");
    *d++ = oct < 0 ? '-' : '+';
    *d++ = (char)('0' + (oct < 0 ? -oct : oct));
    *d = 0;
    draw_text_box(0, 68, 240, &FONT_S, s, C_KEY, 0);
}

static void draw_bench_cost(uint32_t last, uint32_t max, uint32_t shed, uint32_t shed_blocks)
{
    char s[48], *d;
    d = s_cat(s, "LAST ");
    s_cost(d, last);
    draw_text_box(0, 86, 240, &FONT_S, s, C_WHITE, 0);
    d = s_cat(s, "MAX  ");
    s_cost(d, max);
    draw_text_box(0, 102, 240, &FONT_S, s, C_ACC, 0);
    d = s_cat(s, "SHED ");
    d = s_num(d, (int32_t)shed, 0);
    d = s_cat(d, " vc, ");
    d = s_num(d, (int32_t)shed_blocks, 0);
    s_cat(d, " blk");
    draw_text_box(0, 118, 240, &FONT_S, s, shed_blocks ? RGB(255, 80, 80) : C_OFF, 0);
}
#endif
