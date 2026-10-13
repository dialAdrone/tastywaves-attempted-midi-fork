/* SPDX-License-Identifier: GPL-3.0-only */
/* synth.h: the VAVEWave v1 engine (T16). Included once from engine.h when BENCH is 0; no registers, ISR code
 * except where marked. The UI talks to it only through patch.h.
 *
 * Signal path, per voice (12):
 *   osc 1, osc 2: wavetable, 32 frames, position morphs between neighbour frames; read with an 8-tap Kaiser
 *                 sinc (P_QUALITY 0) or Catmull-Rom (1, eco; 4 taps from a 256-phase table); mip level 0..6 per osc per control step from the
 *                 (warped) phase step, with 1/4 octave hysteresis on the dark side; level 6 = fundamental only
 *                 (notes above fs/4); BEND = a smooth phase warp of the read, ph' = ph + A sin(2 pi ph) (self phase
 *                 modulation at the fundamental: smooth at the wrap, so it adds no corner to alias); its steepest
 *                 slope (1 + |b|, b <= 0.95) raises the mip level so the squeezed half does not alias either. (A
 *                 CZ-style two-segment map was measured first: its corners alias at 23..49 dBc, so it was dropped.)
 *   + sub (sine, osc 1 - 1 octave) + noise  ->  2-pole SVF (trapezoidal, LP/BP/HP, Q28 coefficients, 64-bit
 *   products)  ->  amp (envelope x velocity x volume x tremolo, ramped per frame)  ->  mono bus
 * Then: fx chain (stereo hooks, empty in v1)  ->  master drive + soft clip  ->  out.
 *
 * Control rate: everything slow runs once per ENG_CR = 32 frames (8 steps per 256-frame block): both ADSRs, the
 * LFO, glide, pitch -> phase step, mip level, position, filter coefficients. Zipper: the amp gain is ramped per
 * frame across the step; patch values are smoothed per block (one-pole, ~20 ms) and interpolated per step.
 *
 * Fixed point: signals Q15 (|osc| <= 0.9 FS); filter states carry the signal x 16 (finer integrator steps at low
 * cutoff); envelopes Q30; pitch in 1/256 semitone (MIDI note numbers); phases uint32 (2^32 = a cycle).
 * No float, no 64-bit division: the per-step divides are 32-bit (h of the SVF, the BEND slopes).
 *
 * Overload guard (as the bench): before each voice, if the block has used more than 3/4 of its budget the rest are
 * skipped (eng_tap.shed): the main loop always keeps time for platform_poll() and the watchdog.
 *
 * FX hook: fx_chain[FX_SLOTS] = {fn, state, on}; fn(lr, n, state) runs on the stereo int32 bus (Q15, L R L R ...)
 * after the voices and before the soft clip. The main loop installs fn/state first, then sets on (one writer).
 * Pool budget for the effects to come (pool 336 KB, 30 KB used by the canvas): stereo delay 0.5 s 2 x 22050 x int16
 * = 88 KB; chorus/flanger 2 x 2048 x int16 = 8 KB; reverb (Felucca fx.c style) 40..64 KB; phaser state < 1 KB.
 * CPU for them is not in the budget measured for v1. */
#pragma once
#include "wt_bench.h"                           /* WTG_*: 12 tables x 32 frames, 7 guarded mip levels */
#include "sinc8_taps.h"
#include "engine_tables.h"

#define ENG_CR 32u                              /* frames per control step */
#define ENG_CR_SHIFT 5u
#define ENG_STEPS (HALF_FRAMES / ENG_CR)        /* 8 */
#if ENG_CR != ENG_TABLES_CR
#error "ENG_CR must match tools/gen_engine_tables.py CR"
#endif
#define ENG_SHED_TICKS (AUDIO_BUDGET_TICKS * 3u / 4u)
#define ENG_INLINE static inline __attribute__((always_inline))
#define Q30 (1 << 30)
#define POS_MAX (31 * 256)                      /* positions: 256 per frame, 0 .. 31 frames */
#define INC_LIM 0x7C000000u                     /* 21.4 kHz: no phase step above this */
#define VOICE_GAIN 4915                         /* 0.15 Q15: 4 voices with both oscillators at full level ~ 1 FS */
#define SVF_X 16                                /* filter states carry the signal x 16 */
#define CUT_MAX (134 * 256 + 255)               /* SVF_G has notes 0..135 */

enum { ENV_IDLE, ENV_A, ENV_D, ENV_S, ENV_R };
enum { RD_SINC, RD_CUBIC };

typedef struct {
    uint32_t ph[2], phs;                        /* oscillator phases, sub phase */
    int32_t pitch, target;                      /* 1/256 semitone: glide state */
    int32_t env_a, env_m;                       /* Q30 */
    int32_t s1, s2;                             /* SVF states ic1, ic2 (x SVF_X) */
    int32_t amp;                                /* Q30 gain at the end of the last control step */
    uint32_t age;
    uint8_t st_a, st_m, note, vel, gate, L[2];
} voice_t;
static voice_t vx[ENG_NV];

typedef struct { int32_t A; } warp_t;            /* the BEND map: ph + A sin(ph), A = b / 2 pi x 2^32 (0: off) */

/* per control step, the same for every voice (computed once per block) */
typedef struct {
    int32_t lfo;                                /* Q15, bipolar */
    int32_t pos[2];                             /* 0..POS_MAX before the mod envelope */
    int32_t lvl[2], sub, noise;                 /* Q15 */
    int32_t warp;                               /* -64..63 << 8 */
    int32_t cut;                                /* 1/256 semitone, before the per-voice terms */
    int32_t fenv;                               /* env amount << 8 */
    int32_t r2;                                 /* 2R, Q28 */
    int32_t pitch;                              /* bend + vibrato, 1/256 semitone */
    int32_t gain;                               /* Q15: volume x VOICE_GAIN x tremolo */
} gstep_t;

/* the smoothed patch values: index into SM_ID, value << 8 */
static const uint8_t SM_ID[] = {P_POS1, P_POS2, P_LEVEL1, P_LEVEL2, P_SUB, P_NOISE, P_WARP, P_CUTOFF, P_RESO,
                                P_FENV, P_DRIVE, P_VOLUME};
enum { SM_POS1, SM_POS2, SM_LVL1, SM_LVL2, SM_SUB, SM_NOISE, SM_WARP, SM_CUT, SM_RESO, SM_FENV, SM_DRIVE, SM_VOL,
       SM_BEND, SM_MOD, SM_AT, SM_N };

static struct {
    int32_t p[P_COUNT];                         /* this block's patch snapshot, clamped */
    int32_t sm[SM_N], sm_prev[SM_N];
    int sm_init;
    uint32_t lfo_ph, lfo_rnd, noise;
    int32_t lfo_raw, lfo;
    uint32_t ages, last_v;
    int32_t last_pitch;
    uint8_t stack[16], nstack;                  /* held notes (mono / legato), newest last */
    int32_t vmode;
} es = {.noise = 0x1234567u, .lfo_rnd = 0x89ABCDEu, .vmode = -1};

static gstep_t gs[ENG_STEPS];
static int32_t vbuf[ENG_CR];                    /* one voice's oscillators, summed (kept zero between uses) */
static int32_t mixbus[HALF_FRAMES];             /* all voices, mono (kept zero between blocks) */

typedef void (*fx_fn)(int32_t *lr, uint32_t n, void *state);
#define FX_SLOTS 4u
static struct { fx_fn fn; void *state; volatile uint32_t on; } fx_chain[FX_SLOTS];   /* main writes; v1: empty */
static int32_t fx_bus[2u * HALF_FRAMES];

/* ---------------------------------------------------------------- small helpers */
static int32_t sine(uint32_t ph)                /* a full cycle over 2^32 -> -32767..32767 (refined parabola, ~0.1 %) */
{
    int32_t x = (int32_t)ph >> 16, ax = x < 0 ? -x : x, y, ay;
    y = (x * (32768 - ax)) >> 13;
    ay = y < 0 ? -y : y;
    y += ((((y * ay) >> 15) - y) * 7373) >> 15;
    return y > 32767 ? 32767 : y < -32767 ? -32767 : y;
}

static uint32_t pitch_inc(int32_t p)            /* 1/256 semitone (MIDI note numbers) -> phase step at 44.1 kHz */
{
    uint32_t n, base;
    if (p < 0) p = 0;
    if (p > 131 * 256 + 255) p = 131 * 256 + 255;
    n = (uint32_t)p >> 8;
    base = INC_C9[n % 12u] >> (10u - n / 12u);
    return base + (uint32_t)(((uint64_t)base * FINE16[(uint32_t)p & 255u]) >> 16);
}

static uint32_t level_pick(uint32_t L, uint32_t inc)   /* mip level: up at the ceiling, down 1/4 octave below it */
{
    while (L < WTG_LEVELS - 1u && inc >= (1u << (25u + L))) L++;
    while (L > 0u && inc < HYST_DN[L]) L--;
    return L;
}

/* BEND: w = -64..63 << 8 -> b = 0.95 w / 16384, A = b / 2 pi x 2^32 = w x 39638 (|A| <= 6.5e8). The steepest
 * slope 1 + |b| may not push the phase step past INC_LIM, so |b| is limited per note. Returns the effective (steepest)
 * phase step, which picks the mip level. */
static uint32_t warp_setup(warp_t *wp, int32_t w, uint32_t inc)
{
    uint32_t aw, sm;
    if (w > 64 * 256) w = 64 * 256;
    if (w < -64 * 256) w = -64 * 256;
    aw = (uint32_t)(w < 0 ? -w : w);
    if (!aw) { wp->A = 0; return inc; }
    sm = 65536u + ((aw * 243u) >> 6);           /* 1 + |b|, Q16 (1.95 at |w| = 16384) */
    if ((uint64_t)inc * sm > ((uint64_t)INC_LIM << 16)) {   /* too steep for this note: less bend */
        uint32_t allow = INC_LIM / ((inc >> 16) | 1u);       /* steepest slope allowed, Q16 (> 1.27: inc < 2^31 / 1.27) */
        if (allow <= 65536u) { wp->A = 0; return inc; }
        aw = ((allow - 65536u) << 6) / 243u;
        sm = 65536u + ((aw * 243u) >> 6);
        w = w < 0 ? -(int32_t)aw : (int32_t)aw;
    }
    wp->A = w * 39638;
    return (uint32_t)(((uint64_t)inc * sm) >> 16);
}

ENG_INLINE uint32_t warp_ph(uint32_t ph, int32_t A)
{
    uint32_t i = ph >> 24;
    int32_t s = SIN_T[i] + (((SIN_T[i + 1u] - SIN_T[i]) * (int32_t)((ph >> 9) & 0x7FFFu)) >> 15);
    return ph + (uint32_t)(int32_t)(((int64_t)s * A) >> 15);
}

static uint32_t eng_warp_preview(uint32_t ph, int32_t warp)   /* main loop: the map at a low note, for drawing */
{
    warp_t w;
    warp_setup(&w, warp * 256, 1u << 20);
    return w.A ? warp_ph(ph, w.A) : ph;
}

static const int16_t *eng_frames(uint32_t table, uint32_t *stride)
{
    if (stride) *stride = WTG_FRAME_LEN;
    return WTG_DATA + (table % WTG_TABLES) * WTG_FRAMES * WTG_FRAME_LEN + WTG_PRE;
}

/* ---------------------------------------------------------------- oscillators (per frame: the hot loops)
 * p = sample 0 of level L of frame f; frame f + 1 is p + WTG_FRAME_LEN. wa / wb = level x (1 - x) / level x x, Q15
 * (the frame morph and the level in one pair of multiplies). Each adds n frames to buf and returns the phase. */
ENG_INLINE int32_t rd_sinc(const int16_t *p, uint32_t w, uint32_t sh, uint32_t sf, int32_t wa, int32_t wb)
{
    const int16_t *a = p + (w >> sh) - 3, *b = a + WTG_FRAME_LEN;
    const int16_t *c = SINC8_TAPS + (((w >> sf) & 255u) << 3);
    int64_t s0, s1;                             /* 64-bit multiply-accumulate: one instruction a tap and frame */
    int32_t t;
    t = c[0]; s0 = (int64_t)t * a[0]; s1 = (int64_t)t * b[0];
    t = c[1]; s0 += (int64_t)t * a[1]; s1 += (int64_t)t * b[1];
    t = c[2]; s0 += (int64_t)t * a[2]; s1 += (int64_t)t * b[2];
    t = c[3]; s0 += (int64_t)t * a[3]; s1 += (int64_t)t * b[3];
    t = c[4]; s0 += (int64_t)t * a[4]; s1 += (int64_t)t * b[4];
    t = c[5]; s0 += (int64_t)t * a[5]; s1 += (int64_t)t * b[5];
    t = c[6]; s0 += (int64_t)t * a[6]; s1 += (int64_t)t * b[6];
    t = c[7]; s0 += (int64_t)t * a[7]; s1 += (int64_t)t * b[7];
    /* |s0 >> 15| <= 29490 * 52244 / 32768 = 47019; x (wa + wb < 2^15): < 2^31 */
    return (int32_t)(((int64_t)(int32_t)(s0 >> 15) * wa + (int64_t)(int32_t)(s1 >> 15) * wb) >> 15);
}

ENG_INLINE int32_t rd_cubic(const int16_t *p, uint32_t w, uint32_t sh, uint32_t sf, int32_t wa, int32_t wb)
{                                               /* Catmull-Rom as 4 taps from CR4_TAPS (256 phases), reads i-1 .. i+2 */
    const int16_t *a = p + (w >> sh) - 1, *b = a + WTG_FRAME_LEN;
    const int16_t *c = CR4_TAPS + (((w >> sf) & 255u) << 2);
    int64_t s0, s1;
    int32_t t;
    t = c[0]; s0 = (int64_t)t * a[0]; s1 = (int64_t)t * b[0];
    t = c[1]; s0 += (int64_t)t * a[1]; s1 += (int64_t)t * b[1];
    t = c[2]; s0 += (int64_t)t * a[2]; s1 += (int64_t)t * b[2];
    t = c[3]; s0 += (int64_t)t * a[3]; s1 += (int64_t)t * b[3];
    return (int32_t)(((int64_t)(int32_t)(s0 >> 15) * wa + (int64_t)(int32_t)(s1 >> 15) * wb) >> 15);
}

static uint32_t osc_sinc(int32_t *buf, const int16_t *p, uint32_t ph, uint32_t inc, uint32_t L, int32_t wa, int32_t wb)
{
    uint32_t sh = 24u + L, sf = 16u + L, i;
    for (i = 0; i < ENG_CR; i++) {
        buf[i] += rd_sinc(p, ph, sh, sf, wa, wb);
        ph += inc;
    }
    return ph;
}
static uint32_t osc_sinc_w(int32_t *buf, const int16_t *p, uint32_t ph, uint32_t inc, uint32_t L, int32_t wa, int32_t wb,
                           const warp_t *wp)
{
    uint32_t sh = 24u + L, sf = 16u + L, i;
    int32_t A = wp->A;
    for (i = 0; i < ENG_CR; i++) {
        buf[i] += rd_sinc(p, warp_ph(ph, A), sh, sf, wa, wb);
        ph += inc;
    }
    return ph;
}
static uint32_t osc_cubic(int32_t *buf, const int16_t *p, uint32_t ph, uint32_t inc, uint32_t L, int32_t wa, int32_t wb)
{
    uint32_t sh = 24u + L, sf = 16u + L, i;
    for (i = 0; i < ENG_CR; i++) {
        buf[i] += rd_cubic(p, ph, sh, sf, wa, wb);
        ph += inc;
    }
    return ph;
}
static uint32_t osc_cubic_w(int32_t *buf, const int16_t *p, uint32_t ph, uint32_t inc, uint32_t L, int32_t wa, int32_t wb,
                            const warp_t *wp)
{
    uint32_t sh = 24u + L, sf = 16u + L, i;
    int32_t A = wp->A;
    for (i = 0; i < ENG_CR; i++) {
        buf[i] += rd_cubic(p, warp_ph(ph, A), sh, sf, wa, wb);
        ph += inc;
    }
    return ph;
}
static uint32_t osc_sub(int32_t *buf, uint32_t ph, uint32_t inc, int32_t lvl)   /* sine from SIN_T, interpolated */
{
    uint32_t i;
    for (i = 0; i < ENG_CR; i++) {
        uint32_t j = ph >> 24;
        int32_t sn = SIN_T[j] + (((SIN_T[j + 1u] - SIN_T[j]) * (int32_t)((ph >> 9) & 0x7FFFu)) >> 15);
        buf[i] += (sn * lvl) >> 15;
        ph += inc;
    }
    return ph;
}
static uint32_t osc_noise(int32_t *buf, uint32_t seed, int32_t lvl)
{
    uint32_t i;
    for (i = 0; i < ENG_CR; i++) {
        seed = seed * 1664525u + 1013904223u;
        buf[i] += (((int32_t)seed >> 17) * lvl) >> 15;      /* +-0.5 FS at full level */
    }
    return seed;
}

/* ---------------------------------------------------------------- filter + amp + mix (per frame)
 * Trapezoidal SVF (Simper / Cytomic): a1 = 1 / (1 + g (g + k)), a2 = g a1, a3 = g a2, k = 2R (Q28);
 *   v3 = x - ic2; v1 = a1 ic1 + a2 v3; v2 = ic2 + a2 ic1 + a3 v3; ic1 = 2 v1 - ic1; ic2 = 2 v2 - ic2;
 *   LP = v2, BP = v1, HP = x - k v1 - v2.
 * Two 64-bit multiply-accumulates per output (one instruction each on pi32v2). States carry the signal x SVF_X.
 * Bound (no clamp needed): |x| <= 16 x (2 x 47019 + 32767 + 16384) = 2.3e6; the largest gain of any state is about
 * 1 / (2R) = 33 at R = 0.015, so |ic1|, |ic2| < 2 x 33 x 2.3e6 = 1.5e8 < 2^31, and every product < 2^57. */
#define SVF_LOOP(OUT)                                                                                         \
    for (i = 0; i < ENG_CR; i++) {                                                                            \
        int32_t x = vbuf[i] * SVF_X, v3 = x - ic2, v1, v2;                                                    \
        vbuf[i] = 0;                                                                                          \
        v1 = (int32_t)(((int64_t)a1 * ic1 + (int64_t)a2 * v3) >> 28);                                        \
        v2 = ic2 + (int32_t)(((int64_t)a2 * ic1 + (int64_t)a3 * v3) >> 28);                                  \
        ic1 = 2 * v1 - ic1;                                                                                   \
        ic2 = 2 * v2 - ic2;                                                                                   \
        a += da;                                                                                              \
        mix[i] += (int32_t)(((int64_t)(OUT) * a) >> 34);    /* Q30 gain, x 16 states: >> 30 + 4 */          \
    }

static void voice_out(voice_t *vp, int32_t *mix, uint32_t mode, int32_t g, int32_t k, int32_t amp1)
{
    int32_t ic1 = vp->s1, ic2 = vp->s2, a = vp->amp, da = (amp1 - vp->amp) >> ENG_CR_SHIFT, a1, a2, a3;
    uint32_t i, den;
    /* den = 1 + g (g + k) in Q16 (g <= 3.3, k <= 1.42: den < 17); a1 = 1 / den: Q16 by a 32-bit divide, then Q28 */
    den = 65536u + (uint32_t)(((int64_t)g * (g + k)) >> 40);
    a1 = (int32_t)((0xFFFFFFFFu / den) << 12);  /* den >= 65536: no divide by zero; a1 <= 2^28 */
    a2 = (int32_t)(((int64_t)g * a1) >> 28);
    a3 = (int32_t)(((int64_t)g * a2) >> 28);
    if (mode == 1u) { SVF_LOOP(v1) }
    else if (mode == 2u) { SVF_LOOP(x - (int32_t)(((int64_t)k * v1) >> 28) - v2) }
    else { SVF_LOOP(v2) }
    vp->s1 = ic1;
    vp->s2 = ic2;
    vp->amp = amp1;
}

/* ---------------------------------------------------------------- envelopes (per control step) */
static int32_t env_step(uint8_t *st, int32_t lvl, int32_t a, int32_t d, int32_t s, int32_t r, int32_t lin)
{
    switch (*st) {
    case ENV_A:
        if (lvl > Q30 - ENV_STEP[a]) lvl = Q30;     /* lvl + step must not reach 2^31 */
        else lvl += ENV_STEP[a];
        if (lvl >= Q30) { lvl = Q30; *st = ENV_D; }
        break;
    case ENV_D:
        if (lin) lvl -= ENV_STEP[d];
        else lvl = s + (int32_t)(((int64_t)(lvl - s) * ENV_COEF[d]) >> 30);
        if (lvl - s < (1 << 14)) { lvl = s; *st = ENV_S; }
        break;
    case ENV_S:
        lvl += (s - lvl) >> 3;                  /* follows a sustain change without a step */
        break;
    case ENV_R:
        if (lin) lvl -= ENV_STEP[r];
        else lvl = (int32_t)(((int64_t)lvl * ENV_COEF[r]) >> 30);
        if (lvl < (1 << 16)) { lvl = 0; *st = ENV_IDLE; }
        break;
    default:
        lvl = 0;
        break;
    }
    return lvl;
}

/* ---------------------------------------------------------------- voices: allocation (ISR, from the event ring) */
static void voice_start(uint32_t v, uint32_t note, uint32_t vel, int retrig)
{
    voice_t *vp = &vx[v];
    int32_t t = (int32_t)note << 8;
    int idle = vp->st_a == ENV_IDLE && !vp->amp;
    if (idle) {                                 /* from silence: clean filter, glide from the last note played */
        vp->s1 = vp->s2 = 0;
        vp->pitch = es.p[P_GLIDE] && es.last_pitch ? es.last_pitch : t;
    } else if (!es.p[P_GLIDE])
        vp->pitch = t;
    vp->target = t;
    vp->note = (uint8_t)note;
    vp->vel = (uint8_t)vel;
    vp->gate = 1;
    vp->age = ++es.ages;
    if (retrig || idle) {                       /* both envelopes restart from where they are: no click */
        vp->st_a = ENV_A;
        vp->st_m = ENV_A;
    }
    es.last_pitch = t;
    es.last_v = v;
}

static void voice_release(voice_t *vp)
{
    vp->gate = 0;
    if (vp->st_a != ENV_IDLE) vp->st_a = ENV_R;
    if (vp->st_m != ENV_IDLE) vp->st_m = ENV_R;
}

static void note_on(uint32_t note, uint32_t vel)
{
    uint32_t v, pick = 0, best = 0xFFFFFFFFu;
    if (es.vmode != 0) {                        /* mono / legato: voice 0, a stack of held notes */
        uint32_t k, j = 0;
        int legato = es.vmode == 2 && vx[0].gate;
        for (k = 0; k < es.nstack; k++)         /* (a note held twice is moved to the top) */
            if (es.stack[k] != note) es.stack[j++] = es.stack[k];
        es.nstack = (uint8_t)j;
        if (es.nstack >= sizeof es.stack) {     /* full: forget the oldest */
            for (k = 1; k < es.nstack; k++) es.stack[k - 1] = es.stack[k];
            es.nstack--;
        }
        es.stack[es.nstack++] = (uint8_t)note;
        voice_start(0, note, legato ? vx[0].vel : vel, !legato);   /* legato: no retrigger, glide if set */
        return;
    }
    for (v = 0; v < ENG_NV; v++) {              /* the same note, else silent, else released (quietest), else oldest */
        voice_t *vp = &vx[v];
        uint32_t score;
        if (vp->note == note && (vp->gate || vp->st_a != ENV_IDLE)) score = 0;
        else if (vp->st_a == ENV_IDLE && !vp->amp) score = 1u + (vp->age & 0xFFFFFFu);
        else if (!vp->gate) score = 0x40000000u + ((uint32_t)vp->env_a >> 8);
        else score = 0x80000000u + (vp->age & 0x3FFFFFFFu);
        if (score < best) { best = score; pick = v; }
    }
    voice_start(pick, note, vel, 1);
}

static void note_off(uint32_t note)
{
    uint32_t v;
    if (es.vmode != 0) {
        uint32_t k, j = 0;
        for (k = 0; k < es.nstack; k++)
            if (es.stack[k] != note) es.stack[j++] = es.stack[k];
        es.nstack = (uint8_t)j;
        if (vx[0].note != note || !vx[0].gate) return;
        if (es.nstack) {                        /* back to the newest note still held, legato */
            voice_start(0, es.stack[es.nstack - 1u], vx[0].vel, 0);
        } else
            voice_release(&vx[0]);
        return;
    }
    for (v = 0; v < ENG_NV; v++)
        if (vx[v].note == note && vx[v].gate) voice_release(&vx[v]);
}

static void all_off(int hard)
{
    uint32_t v;
    es.nstack = 0;
    for (v = 0; v < ENG_NV; v++) {
        voice_release(&vx[v]);
        if (hard) { vx[v].st_a = vx[v].st_m = ENV_IDLE; vx[v].env_a = vx[v].env_m = 0; }   /* amp ramps out in one step */
    }
}

static void events_drain(void)
{
    uint32_t t = ev_tail, h = ev_head;
    while (t != h) {
        uint32_t e = ev_ring[t % EV_N], ty = e >> 16, note = (e >> 8) & 127u, vel = e & 127u;
        if (ty == EV_ON && vel) note_on(note, vel);
        else if (ty == EV_ON || ty == EV_OFF) note_off(note);
        else if (ty == EV_ALLOFF) all_off(0);
        else if (ty == EV_PANIC) all_off(1);
        t++;
    }
    ev_tail = t;
}

/* ---------------------------------------------------------------- per block: patch snapshot, smoothing, LFO */
static void block_control(void)
{
    uint32_t i, k;
    int32_t *p = es.p;
    for (i = 0; i < P_COUNT; i++) p[i] = pclamp(i, patch.p[i]);
    if (p[P_VMODE] != es.vmode) {               /* a new voice mode: release everything */
        if (es.vmode >= 0) all_off(0);
        es.vmode = p[P_VMODE];
    }
    {   /* smoothing: one-pole per block (~20 ms), interpolated per control step below */
        int32_t tgt[SM_N];
        for (k = 0; k < SM_N - 3u; k++) tgt[k] = p[SM_ID[k]] * 256;
        tgt[SM_BEND] = eng_ctl.bend;            /* -8192..8191 */
        tgt[SM_MOD] = eng_ctl.mod * 256;
        tgt[SM_AT] = eng_ctl.at * 256;
        if (tgt[SM_BEND] < -8192) tgt[SM_BEND] = -8192;
        if (tgt[SM_BEND] > 8191) tgt[SM_BEND] = 8191;
        for (k = 0; k < SM_N; k++) {
            int32_t d;
            if (!es.sm_init) es.sm[k] = tgt[k];
            es.sm_prev[k] = es.sm[k];
            d = tgt[k] - es.sm[k];
            es.sm[k] += (d > -8 && d < 8) ? d : d / 4;
        }
        es.sm_init = 1;
    }
    for (i = 0; i < ENG_STEPS; i++) {
        gstep_t *g = &gs[i];
        int32_t s[SM_N], raw, vib, lfo, dq, r, rf;
        uint32_t ph0 = es.lfo_ph;
        for (k = 0; k < SM_N; k++)
            s[k] = es.sm_prev[k] + (((es.sm[k] - es.sm_prev[k]) * (int32_t)(i + 1u)) >> 3);
        /* LFO */
        es.lfo_ph += LFO_INC[p[P_LFO_RATE]];
        if (es.lfo_ph < ph0) es.lfo_rnd = es.lfo_rnd * 1664525u + 1013904223u;   /* a new cycle: new S&H value */
        switch (p[P_LFO_SHAPE]) {
        case 0: raw = sine(es.lfo_ph); break;
        case 1: raw = (int32_t)(es.lfo_ph >> 15) - 65536; raw = (raw < 0 ? -raw : raw) - 32768;          /* tri */
                raw = raw > 32767 ? 32767 : raw; break;
        case 2: raw = (int32_t)(es.lfo_ph >> 16) - 32768; break;                                       /* saw up */
        case 3: raw = es.lfo_ph < 0x80000000u ? 32767 : -32767; break;
        default: raw = (int32_t)(es.lfo_rnd >> 16) - 32768; break;
        }
        es.lfo += (raw - es.lfo) >> 1;          /* a slew of ~1.5 ms: square / saw / S&H edges do not click */
        g->lfo = lfo = es.lfo;
        /* positions: patch + LFO (+-16 frames) + mod wheel (0..+16 frames); the mod envelope is per voice */
        for (k = 0; k < 2u; k++) {
            int32_t pp = (s[k ? SM_POS2 : SM_POS1] * 31) >> 12;
            pp += (p[P_LFO_POS] * lfo) >> 10;
            pp += ((s[SM_MOD] >> 8) * p[P_MW_POS]) >> 2;
            g->pos[k] = pp;
        }
        g->lvl[0] = (s[SM_LVL1] * 258) >> 8;    /* 0..127 << 8 -> Q15 */
        g->lvl[1] = (s[SM_LVL2] * 258) >> 8;
        g->sub = (s[SM_SUB] * 258) >> 8;
        g->noise = (s[SM_NOISE] * 258) >> 8;
        g->warp = s[SM_WARP];
        g->fenv = s[SM_FENV];
        /* cutoff: 20.6 Hz + 0.94 semitone per step, + LFO (+-63 st) + aftertouch (0..+48 st) */
        g->cut = 4096 + ((s[SM_CUT] * 240) >> 8) + ((p[P_LFO_CUT] * lfo) >> 8) + (((s[SM_AT] >> 8) * p[P_AT_CUT] * 3) >> 2);
        r = s[SM_RESO] >> 8;
        rf = s[SM_RESO] & 255;
        if (r >= 127) { r = 126; rf = 256; }
        g->r2 = SVF_2R[r] + (int32_t)(((int64_t)(SVF_2R[r + 1] - SVF_2R[r]) * rf) >> 8);
        /* pitch: bend (range in semitones) + vibrato (LFO depth and mod wheel, quadratic) */
        vib = p[P_LFO_PITCH] * p[P_LFO_PITCH] * 3 + (((s[SM_MOD] >> 8) * p[P_MW_VIB] * p[P_MW_VIB] * 3) >> 7);
        g->pitch = ((s[SM_BEND] * p[P_BEND_RANGE]) >> 5) + (int32_t)(((int64_t)vib * lfo) >> 19);
        /* gain: volume x VOICE_GAIN x tremolo */
        dq = p[P_LFO_AMP] * 258;
        g->gain = (((s[SM_VOL] * 258) >> 8) * VOICE_GAIN) >> 15;
        g->gain = (g->gain * (32767 - (((dq >> 1) * (32767 - lfo)) >> 15))) >> 15;
    }
}

/* ---------------------------------------------------------------- one voice, one block */
static void voice_render(uint32_t v, uint32_t nsteps)
{
    voice_t *vp = &vx[v];
    const int32_t *p = es.p;
    uint32_t i, k, q = (uint32_t)p[P_QUALITY], wdest = (uint32_t)p[P_WARP_DEST];
    int32_t sus_a = p[P_S1] * 8454660, sus_m = p[P_S2] * 8454660, lin = !p[P_ECURVE];   /* 127 -> 2^30 */
    int32_t gk = GLIDE_K[p[P_GLIDE]], kt = p[P_KEYTRK], vg;
    const int16_t *tb[2];
    tb[0] = WTG_DATA + (uint32_t)p[P_TABLE1] * WTG_FRAMES * WTG_FRAME_LEN + WTG_PRE;
    tb[1] = WTG_DATA + (uint32_t)p[P_TABLE2] * WTG_FRAMES * WTG_FRAME_LEN + WTG_PRE;
    /* velocity -> level: 1 - sens * (1 - vel / 127) */
    vg = 32767 - (((p[P_VEL_AMP] * 258) * ((127 - vp->vel) * 258)) >> 15);
    for (i = 0; i < nsteps; i++) {
        const gstep_t *g = &gs[i];
        int32_t em, pitch, cut, a1, fr, gg;
        uint32_t inc0 = 0;
        vp->env_a = env_step(&vp->st_a, vp->env_a, p[P_A1], p[P_D1], sus_a, p[P_R1], lin);
        vp->env_m = env_step(&vp->st_m, vp->env_m, p[P_A2], p[P_D2], sus_m, p[P_R2], lin);
        em = vp->env_m >> 15;                   /* Q15 */
        if (vp->pitch != vp->target) {          /* glide: one-pole in pitch, snaps when within 1/256 semitone */
            int32_t d = vp->target - vp->pitch, st = (int32_t)(((int64_t)d * gk) >> 16);
            vp->pitch = st ? vp->pitch + st : vp->target;
        }
        pitch = vp->pitch + g->pitch;
        for (k = 0; k < 2u; k++) {              /* the two wavetable oscillators */
            int32_t lvl = g->lvl[k], pos, x, wa, wb, w;
            uint32_t inc, eff, f, L;
            const int16_t *fp;
            warp_t wp;
            inc = pitch_inc(pitch + p[k ? P_SEMI2 : P_SEMI1] * 256 + p[k ? P_FINE2 : P_FINE1] * 2);
            if (!k) inc0 = inc;
            if (!lvl) { vp->ph[k] += inc << ENG_CR_SHIFT; continue; }   /* silent: keep the phase only */
            w = (wdest == 0u || wdest == k + 1u) ? g->warp + ((em * p[P_MENV_WARP]) >> 7) : 0;
            eff = warp_setup(&wp, w, inc);
            L = vp->L[k] = (uint8_t)level_pick(vp->L[k], eff);
            pos = g->pos[k] + ((em * p[P_MENV_POS]) >> 8);
            pos = pos < 0 ? 0 : pos > POS_MAX ? POS_MAX : pos;
            f = (uint32_t)pos >> 8;
            x = (pos & 255) << 7;               /* Q15 */
            if (f >= WTG_FRAMES - 1u) { f = WTG_FRAMES - 2u; x = 32768; }
            wa = (lvl * (32768 - x)) >> 15;
            wb = (lvl * x) >> 15;
            fp = tb[k] + f * WTG_FRAME_LEN + WTG_LEVEL_OFF[L];
            if (q == RD_CUBIC)
                vp->ph[k] = wp.A ? osc_cubic_w(vbuf, fp, vp->ph[k], inc, L, wa, wb, &wp) : osc_cubic(vbuf, fp, vp->ph[k], inc, L, wa, wb);
            else
                vp->ph[k] = wp.A ? osc_sinc_w(vbuf, fp, vp->ph[k], inc, L, wa, wb, &wp) : osc_sinc(vbuf, fp, vp->ph[k], inc, L, wa, wb);
            if (!k && v == es.last_v) eng_tap.pos = ((uint32_t)pos * 33u) >> 6;   /* 0..4092 for the UI */
        }
        if (g->sub) vp->phs = osc_sub(vbuf, vp->phs, inc0 >> 1, g->sub);
        else vp->phs += (inc0 >> 1) << ENG_CR_SHIFT;
        if (g->noise) es.noise = osc_noise(vbuf, es.noise, g->noise);
        /* filter: cutoff + key tracking + mod envelope + velocity */
        cut = g->cut + (((int32_t)vp->note - 60) * kt * 2) + (int32_t)(((int64_t)em * g->fenv) >> 15)
            + ((vp->vel * p[P_FVEL] * 3) >> 2);
        cut = cut < 0 ? 0 : cut > CUT_MAX ? CUT_MAX : cut;
        fr = cut & 255;
        gg = SVF_G[cut >> 8] + (int32_t)(((int64_t)(SVF_G[(cut >> 8) + 1] - SVF_G[cut >> 8]) * fr) >> 8);
        /* amp: envelope x velocity x (volume x tremolo), Q30 */
        a1 = (int32_t)(((int64_t)vp->env_a * ((vg * g->gain) >> 15)) >> 15);
        voice_out(vp, mixbus + i * ENG_CR, (uint32_t)p[P_FMODE], gg, g->r2, a1);
    }
}

/* ---------------------------------------------------------------- output: soft clip */
ENG_INLINE int32_t softclip(int32_t x)          /* linear to 0.5 FS, then 0.5 FS x tanh: never above 32767 */
{
    int32_t a = x < 0 ? -x : x, u, i, y;
    if (a <= 16384) return x;
    u = a - 16384;
    if (u > 131071) u = 131071;
    i = u >> 9;
    y = 16384 + SOFTCLIP[i] + (((SOFTCLIP[i + 1] - SOFTCLIP[i]) * (u & 511)) >> 9);
    return x < 0 ? -y : y;
}

static void engine_render(int32_t *out, uint32_t n, uint32_t t0)
{
    uint32_t v, i, nsteps, shed = 0, busy = 0, peak = 0, nfx = 0;
    int32_t gd;
    if (n > HALF_FRAMES) n = HALF_FRAMES;
    nsteps = n >> ENG_CR_SHIFT;                 /* the platform sends 256; a partial step stays silent */
    if (!nsteps) return;
    n = nsteps << ENG_CR_SHIFT;
    block_control();
    events_drain();
    for (v = 0; v < ENG_NV; v++) {
        voice_t *vp = &vx[v];
        if (vp->st_a == ENV_IDLE && !vp->amp) continue;     /* silent: costs nothing */
        if (fm1_ticks() - t0 > ENG_SHED_TICKS) { shed++; busy |= 1u << v; continue; }   /* frozen, not lost */
        voice_render(v, nsteps);
        if (vp->st_a != ENV_IDLE || vp->amp) busy |= 1u << v;
    }
    gd = DRIVE_G[es.sm[SM_DRIVE] >> 8] >> 2;    /* Q6, <= 1024: (|bus| <= 2^20) x gd < 2^30 */
    for (i = 0; i < FX_SLOTS; i++) nfx += fx_chain[i].on && fx_chain[i].fn;
    if (!nfx) {                                 /* mono to the end */
        for (i = 0; i < n; i++) {
            int32_t m = mixbus[i], y;
            uint32_t ay;
            m = m > (1 << 20) ? (1 << 20) : m < -(1 << 20) ? -(1 << 20) : m;   /* 32 FS */
            y = softclip((m * gd) >> 6);
            ay = (uint32_t)(y < 0 ? -y : y);
            mixbus[i] = 0;
            if (ay > peak) peak = ay;
            out[2u * i] += y;
            out[2u * i + 1u] += y;
        }
    } else {                                    /* stereo bus through the fx hooks, then the clip */
        for (i = 0; i < n; i++) {
            int32_t m = mixbus[i];
            mixbus[i] = 0;
            fx_bus[2u * i] = fx_bus[2u * i + 1u] = m;
        }
        for (i = 0; i < FX_SLOTS; i++)
            if (fx_chain[i].on && fx_chain[i].fn) fx_chain[i].fn(fx_bus, n, fx_chain[i].state);
        for (i = 0; i < 2u * n; i++) {
            int32_t m = fx_bus[i], y;
            uint32_t ay;
            m = m > (1 << 20) ? (1 << 20) : m < -(1 << 20) ? -(1 << 20) : m;
            y = softclip((m * gd) >> 6);
            ay = (uint32_t)(y < 0 ? -y : y);
            if (ay > peak) peak = ay;
            out[i] += y;
        }
    }
    for (v = 0; v < ENG_NV; v++)
        eng_tap.vnote[v] = vx[v].note | (vx[v].gate ? 0x100u : 0u) | (busy >> v & 1u ? 0x200u : 0u);
    eng_tap.busy = busy;
    eng_tap.peak = peak;
    eng_tap.shed = shed;
    if (shed) eng_tap.shed_blocks++;
}
