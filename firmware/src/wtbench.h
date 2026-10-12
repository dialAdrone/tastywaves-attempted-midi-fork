/* SPDX-License-Identifier: GPL-3.0-only */
/* wtbench.h: the wavetable oscillator CPU bench (BENCH build; included from engine.h, no registers here).
 * Throwaway but honest: it measures what the v1 engine's oscillators will cost, nothing else.
 *
 * Measured per active voice and block (app_audio, timed in engine.h):
 *   2 oscillators (osc 2 = osc 1 + 3.4 cents), each: mip level from inc (per block), table read with the selected
 *   interpolator in phase, at two adjacent frames, then the frame lerp (Q14, position morphing slowly per block);
 *   both accumulate into a per-voice buffer; then a per-frame amplitude ramp to the gate and the stereo mix.
 * NOT measured: envelopes, LFO, filter, drive, delay, voice allocation in the ISR (estimate those separately).
 *
 * Interpolators (bench_ip): 0 linear (2 taps, 15-bit fraction); 1 Catmull-Rom (4 taps, 13-bit fraction: provably
 * no int32 overflow for |sample| <= 29490 and adjacent steps <= 28940, true of the factory tables); 2 8-tap Kaiser
 * windowed sinc, 256 phases, Q15 (firmware/gen/sinc8_taps.h). Tables: firmware/gen/wt_bench.h, the factory data
 * re-laid with 3 + 4 guard samples per level (tools/gen_bench_tables.py), const in the XIP image.
 *
 * Overload guard: before each voice, if the block has used more than BENCH_SHED_TICKS the remaining voices are
 * skipped (counted in bench_shed): the main loop always keeps time for platform_poll() and the watchdog. */
#pragma once
#include "wt_bench.h"
#include "sinc8_taps.h"

enum { IP_LIN, IP_CUBIC, IP_SINC8, IP_N };
static volatile uint32_t bench_ip = IP_LIN;    /* main writes: the interpolator */
static volatile uint32_t bench_nv = NV;         /* main writes: voices rendered (0 = only the ISR overhead) */
static volatile uint32_t bench_same;            /* main writes: 1 = every voice reads table 0, else voice v reads table v % 12 */
static volatile uint32_t bench_shed;            /* ISR writes: voices skipped by the overload guard, last block */
static volatile uint32_t bench_shed_blocks;     /* ISR writes: blocks with any voice skipped, since boot */
#define BENCH_SHED_TICKS (AUDIO_BUDGET_TICKS * 3u / 4u)

static int32_t vbuf[HALF_FRAMES];               /* one voice's two oscillators, summed (kept zero between voices) */
static uint32_t wt_morph;                       /* ISR: the position LFO, 16-bit phase, one step per block */
#define WT_MORPH_STEP 48u                       /* 65536 / 48 blocks = 7.9 s per triangle cycle */
#define WT_POS_MAX (31u * 128u - 1u)            /* 12-bit position (128 per frame); < 31 frames: frame + 1 exists */

static uint32_t wt_level(uint32_t inc)          /* the smallest L with inc < 2^(25 + L): (64 >> L) harmonics < fs/2 */
{
    uint32_t L = 0;
    while (L < WTG_LEVELS - 1u && inc >= (1u << (25u + L)))
        L++;
    return L;
}

/* p = sample 0 of the level at frame f; frame f + 1 is p + WTG_FRAME_LEN. x = frame lerp, Q14. Adds to buf. */
static uint32_t osc_lin(int32_t *buf, uint32_t n, const int16_t *p, uint32_t ph, uint32_t inc, uint32_t L, int32_t x)
{
    const int16_t *q = p + WTG_FRAME_LEN;
    uint32_t sh = 24u + L, sf = 9u + L, i;
    for (i = 0; i < n; i++) {
        uint32_t k = ph >> sh;
        int32_t fr = (int32_t)((ph >> sf) & 0x7FFFu), s0, s1;
        s0 = p[k] + (((p[k + 1u] - p[k]) * fr) >> 15);
        s1 = q[k] + (((q[k + 1u] - q[k]) * fr) >> 15);
        buf[i] += s0 + (((s1 - s0) * x) >> 14);
        ph += inc;
    }
    return ph;
}

static inline int32_t cr4(const int16_t *p, int32_t u)  /* Catmull-Rom at p[0] + u (Q13), reads p[-1..2] */
{
    int32_t ym = p[-1], y0 = p[0], y1 = p[1], y2 = p[2];
    int32_t c1 = (y1 - ym) >> 1;
    int32_t c3 = (((y0 - y1) * 3) >> 1) + ((y2 - ym) >> 1);
    int32_t c2 = ym - y0 + c1 - c3;
    return ((((((c3 * u) >> 13) + c2) * u >> 13) + c1) * u >> 13) + y0;
}

static uint32_t osc_cubic(int32_t *buf, uint32_t n, const int16_t *p, uint32_t ph, uint32_t inc, uint32_t L, int32_t x)
{
    uint32_t sh = 24u + L, sf = 11u + L, i;
    for (i = 0; i < n; i++) {
        uint32_t k = ph >> sh;
        int32_t u = (int32_t)((ph >> sf) & 0x1FFFu), s0, s1;
        s0 = cr4(p + k, u);
        s1 = cr4(p + k + WTG_FRAME_LEN, u);
        buf[i] += s0 + (((s1 - s0) * x) >> 14);
        ph += inc;
    }
    return ph;
}

static uint32_t osc_sinc8(int32_t *buf, uint32_t n, const int16_t *p, uint32_t ph, uint32_t inc, uint32_t L, int32_t x)
{
    uint32_t sh = 24u + L, sf = 16u + L, i;
    for (i = 0; i < n; i++) {
        const int16_t *a = p + (ph >> sh) - 3, *b = a + WTG_FRAME_LEN;
        const int16_t *c = SINC8_TAPS + (((ph >> sf) & 255u) << 3);
        int32_t c0 = c[0], c1 = c[1], c2 = c[2], c3 = c[3], c4 = c[4], c5 = c[5], c6 = c[6], c7 = c[7], s0, s1;
        s0 = (c0 * a[0] + c1 * a[1] + c2 * a[2] + c3 * a[3] + c4 * a[4] + c5 * a[5] + c6 * a[6] + c7 * a[7]) >> 15;
        s1 = (c0 * b[0] + c1 * b[1] + c2 * b[2] + c3 * b[3] + c4 * b[4] + c5 * b[5] + c6 * b[6] + c7 * b[7]) >> 15;
        buf[i] += s0 + (((s1 - s0) * x) >> 14);    /* |sum| <= 29490 * 52244 (sum of |taps|) < 2^31 */
        ph += inc;
    }
    return ph;
}

static void engine_render(int32_t *out, uint32_t n, uint32_t t0)
{
    uint32_t v, i, ip = bench_ip, nv = bench_nv, same = bench_same, shed = 0;
    if (nv > NV) nv = NV;
    if (n > HALF_FRAMES) n = HALF_FRAMES;
    if (!n) return;
    wt_morph += WT_MORPH_STEP;
    for (v = 0; v < nv; v++) {
        uint32_t inc = voice[v].inc, inc2 = inc + (inc >> 9), m, tri, pos, L1, L2;
        int32_t amp = voice[v].amp, g = voice[v].gate, a, st, x;
        const int16_t *base;
        if (!amp && !g)
            continue;                           /* silent: costs nothing */
        if (fm1_ticks() - t0 > BENCH_SHED_TICKS) { shed = nv - v; break; }
        m = (wt_morph + v * 5461u) & 0xFFFFu;   /* each voice at its own place in the morph */
        tri = m < 32768u ? m : 65535u - m;
        pos = (tri * WT_POS_MAX) >> 15;
        x = (int32_t)((pos & 127u) << 7);
        base = WTG_DATA + ((same ? 0u : v % WTG_TABLES) * WTG_FRAMES + (pos >> 7)) * WTG_FRAME_LEN + WTG_PRE;
        L1 = wt_level(inc);
        L2 = wt_level(inc2);
        if (ip == IP_SINC8) {
            voice[v].ph = osc_sinc8(vbuf, n, base + WTG_LEVEL_OFF[L1], voice[v].ph, inc, L1, x);
            voice[v].ph2 = osc_sinc8(vbuf, n, base + WTG_LEVEL_OFF[L2], voice[v].ph2, inc2, L2, x);
        } else if (ip == IP_CUBIC) {
            voice[v].ph = osc_cubic(vbuf, n, base + WTG_LEVEL_OFF[L1], voice[v].ph, inc, L1, x);
            voice[v].ph2 = osc_cubic(vbuf, n, base + WTG_LEVEL_OFF[L2], voice[v].ph2, inc2, L2, x);
        } else {
            voice[v].ph = osc_lin(vbuf, n, base + WTG_LEVEL_OFF[L1], voice[v].ph, inc, L1, x);
            voice[v].ph2 = osc_lin(vbuf, n, base + WTG_LEVEL_OFF[L2], voice[v].ph2, inc2, L2, x);
        }
        a = amp * 256;                          /* the level in Q23, a linear ramp to the gate over the block */
        st = (g - amp) * 256 / (int32_t)n;      /* n > 0 (checked above) */
        for (i = 0; i < n; i++) {
            int32_t s = (vbuf[i] * (a >> 8)) >> 15;
            vbuf[i] = 0;
            a += st;
            out[2u * i] += s;
            out[2u * i + 1u] += s;
        }
        voice[v].amp = g;
    }
    bench_shed = shed;
    if (shed) bench_shed_blocks++;
}
