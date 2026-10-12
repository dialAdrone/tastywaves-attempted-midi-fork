/* SPDX-License-Identifier: GPL-3.0-only */
/* engine.h: the sound. Included once from vavewave.c, after platform.c (one translation unit; no registers here).
 *   voices      voice[NV]: inc / gate written by the main loop, amp / ph by the ISR
 *   app_audio   the audio ISR entry: renders, and times itself (cpu_last / cpu_max, T10)
 *   play/stop   voice allocation (main loop)
 * BENCH (vavewave.c) swaps the 4-voice sine for the wavetable oscillator bench (wtbench.h). */
#pragma once

#ifndef BENCH
#define BENCH 0
#endif

/* ---- ISR timing (T10): ticks of fm1_ticks() (24 per us) spent in app_audio per block; budget = one block */
#define AUDIO_BUDGET_TICKS (HALF_FRAMES * (FM1_TICKS_PER_US * 10000u) / (FS / 100u))   /* 256 fr: 139319 = 5805 us */
static volatile uint32_t cpu_last, cpu_max;     /* ISR writes */
static volatile uint32_t cpu_reset_req;         /* main writes: +1 = start cpu_max over */

/* ---- the voices */
#if BENCH
#define NV 12u                                  /* voices (2 oscillators each) */
#define GATE_PER_VEL 11u                        /* 127 -> 1397: 12 voices x 2 osc x 0.9 FS stay under full scale */
#else
#define NV 4u                                   /* voices */
#define GATE_PER_VEL 90u                        /* 127 -> 11430: four voices stay under full scale */
#endif
static struct {
    volatile uint32_t inc;                      /* phase step per frame: f * 2^32 / 44100 */
    volatile int32_t gate;                      /* target level, Q15: 0 = released */
    int32_t amp;                                /* the level now: glides to gate, no clicks */
    uint32_t ph, age;
#if BENCH
    uint32_t ph2;                               /* oscillator 2 (detuned) */
#endif
    uint8_t note;
} voice[NV];

#if BENCH
#include "wtbench.h"
#define NV_PLAY bench_nv                        /* keys allocate among the voices being rendered */
#else
#define NV_PLAY NV

static int32_t sine(uint32_t ph)                /* a full cycle over 2^32 -> -32767..32767 (a refined parabola, ~0.1 %) */
{
    int32_t x = (int32_t)ph >> 16, ax = x < 0 ? -x : x, y, ay;
    y = (x * (32768 - ax)) >> 13;               /* 4x(1-|x|), Q15 */
    ay = y < 0 ? -y : y;
    y += ((((y * ay) >> 15) - y) * 7373) >> 15; /* + 0.225 (y|y| - y) */
    return y > 32767 ? 32767 : y < -32767 ? -32767 : y;
}

static void engine_render(int32_t *out, uint32_t n, uint32_t t0)
{
    uint32_t v, i;
    (void)t0;
    for (v = 0; v < NV; v++) {
        uint32_t ph = voice[v].ph, step = voice[v].inc;
        int32_t amp = voice[v].amp, g = voice[v].gate;
        if (!amp && !g)
            continue;                           /* silent: costs nothing */
        for (i = 0; i < n; i++) {
            int32_t s;
            if (amp < g) { amp += 48; if (amp > g) amp = g; }        /* attack ~5 ms */
            else if (amp > g) { amp -= 6; if (amp < g) amp = g; }    /* release ~45 ms */
            ph += step;
            s = (sine(ph) * amp) >> 15;
            out[2u * i] += s;                   /* L */
            out[2u * i + 1u] += s;              /* R */
        }
        voice[v].ph = ph;
        voice[v].amp = amp;
    }
}
#endif

static void app_audio(int32_t *out, uint32_t n)
{
    static uint32_t reset_seen;
    uint32_t t0 = fm1_ticks(), dt, r;
    engine_render(out, n, t0);
    dt = fm1_ticks() - t0;                      /* includes nested timer5 ISRs (key scan, USB audio feed) */
    cpu_last = dt;
    r = cpu_reset_req;
    if (r != reset_seen) { reset_seen = r; cpu_max = 0; }
    if (dt > cpu_max) cpu_max = dt;
}

/* ---- notes (main loop) */
static const uint32_t INC_C9[12] = {            /* MIDI 120..131 (C9..B9); lower octaves shift right */
    815363807u, 863847862u, 915214929u, 969636441u, 1027294024u, 1088380105u,
    1153098554u, 1221665363u, 1294309365u, 1371273005u, 1452813141u, 1539201906u};
static uint32_t ages, last_note = 255;

static uint32_t note_inc(uint32_t note) { return INC_C9[note % 12u] >> (10u - note / 12u); }

static void play(uint32_t note, uint32_t vel)
{
    uint32_t v, pick = 0, best = 0xFFFFFFFFu, nv = NV_PLAY;
    if (note > 127u || !vel || !nv) return;
    for (v = 0; v < nv; v++) {                  /* the same note, else a free voice, else the oldest */
        uint32_t score = voice[v].note == note && voice[v].gate ? 0u
                       : voice[v].gate ? 0x80000000u + voice[v].age : (uint32_t)voice[v].amp + 1u;
        if (score < best) { best = score; pick = v; }
    }
    voice[pick].note = (uint8_t)note;
    voice[pick].age = ++ages;
    voice[pick].inc = note_inc(note);           /* pitch before gate: the ISR may run in between */
    voice[pick].gate = (int32_t)(vel * GATE_PER_VEL);
    last_note = note;
}
static void stop(uint32_t note)
{
    uint32_t v;
    for (v = 0; v < NV; v++)
        if (voice[v].note == note)
            voice[v].gate = 0;
}
static int quiet(void)                          /* nothing sounds, not even a release */
{
    uint32_t v;
    for (v = 0; v < NV; v++)
        if (voice[v].gate || voice[v].amp) return 0;
    return 1;
}
