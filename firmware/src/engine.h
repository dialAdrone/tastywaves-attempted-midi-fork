/* SPDX-License-Identifier: GPL-3.0-only */
/* engine.h: the sound. Included once from vavewave.c, after platform.c (one translation unit; no registers here).
 *   app_audio        the audio ISR entry: renders, and times itself (cpu_last / cpu_max, T10)
 *   play/stop/quiet  notes from the main loop; eng_midi() takes any MIDI channel message; eng_lit() the keys to light
 * BENCH 0 (default): the v1 wavetable engine, synth.h; its whole UI interface is patch.h.
 * BENCH 1: the wavetable oscillator CPU bench (wtbench.h), 12 voices x 2 osc, no env/filter/FX, main-loop allocation. */
#pragma once

#ifndef BENCH
#define BENCH 0
#endif

/* ---- ISR timing (T10): ticks of fm1_ticks() (24 per us) spent in app_audio per block; budget = one block */
#define AUDIO_BUDGET_TICKS (HALF_FRAMES * (FM1_TICKS_PER_US * 10000u) / (FS / 100u))   /* 256 fr: 139319 = 5805 us */
static volatile uint32_t cpu_last, cpu_max;     /* ISR writes */
static volatile uint32_t cpu_reset_req;         /* main writes: +1 = start cpu_max over */

static const uint32_t INC_C9[12] = {            /* phase steps of MIDI 120..131 (C9..B9); lower octaves shift right */
    815363807u, 863847862u, 915214929u, 969636441u, 1027294024u, 1088380105u,
    1153098554u, 1221665363u, 1294309365u, 1371273005u, 1452813141u, 1539201906u};
static uint32_t note_inc(uint32_t note) { return INC_C9[note % 12u] >> (10u - note / 12u); }
static uint32_t last_note = 255;                /* main loop: the last note played (the screen shows it) */

#if BENCH
/* ---- the bench's voices: inc / gate written by the main loop, amp / ph by the ISR */
#define NV 12u                                  /* voices (2 oscillators each) */
#define GATE_PER_VEL 11u                        /* 127 -> 1397: 12 voices x 2 osc x 0.9 FS stay under full scale */
static struct {
    volatile uint32_t inc;                      /* phase step per frame: f * 2^32 / 44100 */
    volatile int32_t gate;                      /* target level, Q15: 0 = released */
    int32_t amp;                                /* the level now */
    uint32_t ph, age;
    uint32_t ph2;                               /* oscillator 2 (detuned) */
    uint8_t note;
} voice[NV];
#include "wtbench.h"
static uint32_t ages;

static void play(uint32_t note, uint32_t vel)
{
    uint32_t v, pick = 0, best = 0xFFFFFFFFu, nv = bench_nv;
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
static uint32_t eng_lit(uint32_t base)          /* keys (bit k = note base + k) whose note sounds */
{
    uint32_t k, lit = 0;
    for (k = 0; k < NV; k++)
        if (voice[k].gate && voice[k].note >= base && voice[k].note < base + 27u)
            lit |= 1u << (voice[k].note - base);
    return lit;
}
static void eng_midi(uint32_t st, uint32_t d1, uint32_t d2)
{
    if ((st & 0xF0u) == 0x90u && d2) play(d1, d2);
    else if ((st & 0xF0u) == 0x80u || (st & 0xF0u) == 0x90u) stop(d1);
}

#else
/* ---- the v1 engine */
#include "patch.h"
#include "synth.h"

static void play(uint32_t note, uint32_t vel)   /* to the ISR through the event ring; allocation happens there */
{
    if (note > 127u || !vel) return;
    eng_push(EV_ON, note, vel > 127u ? 127u : vel);
    last_note = note;
}
static void stop(uint32_t note) { if (note <= 127u) eng_push(EV_OFF, note, 0); }
static int quiet(void) { return eng_quiet(); }
/* any channel: notes, CC (patch_cc: CC 1 mod wheel, 120 / 123 all off), pitch bend, channel and poly aftertouch
 * (poly is taken as channel aftertouch in v1) */
static void eng_midi(uint32_t st, uint32_t d1, uint32_t d2)
{
    switch (st & 0xF0u) {
    case 0x90u: if (d2) { play(d1, d2); break; } /* fall through: velocity 0 = note off */
    case 0x80u: stop(d1); break;
    case 0xB0u: patch_cc(d1, d2); break;
    case 0xE0u: eng_bend((int32_t)((d2 & 127u) << 7 | (d1 & 127u)) - 8192); break;
    case 0xD0u: eng_aftertouch((int32_t)d1); break;
    case 0xA0u: eng_aftertouch((int32_t)d2); break;
    default: break;
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
