/* SPDX-License-Identifier: GPL-3.0-only */
/* arp.h: the arpeggiator clock and engine (T27 / T28, docs/ARP-SEQ-DESIGN.md). Runs in the audio ISR, once per block,
 * after the note events were drained; integer only, no division, no flash.
 *
 * The includer defines ARP_NOTE_ON(note, vel) and ARP_NOTE_OFF(note) (the engine's note_on / note_off) before this file.
 *
 * Who writes what:
 *   arp_cfg.*       main loop (panel, later patch)              ISR reads once per block
 *   arp_ext.*       main loop (MIDI clock 0xF8, start 0xFA)     ISR reads once per block
 *   ar.*            ISR only (notes held, what sounds, clock)
 *   arp_tap         ISR writes, main reads (the step shown on the screen)
 *
 * Time is counted in sub-ticks, 96 per quarter note (4 per MIDI clock tick). Internal clock: samples per sub-tick =
 * 44100 * 60 / (96 * bpm), done as an accumulator (n * bpm * 2 against 55125) with no division. External clock: each
 * MIDI tick adds 4 sub-ticks. Everything a block triggers sounds at the start of that block (5.8 ms grid). */
#pragma once

#define ARP_MAXN 16u                            /* notes held (keys or latch) */
#define ARP_SUB_Q 55125u                        /* accumulator units per sub-tick: 44100 * 60 / 96 * 2 */
#define ARP_EXT_IDLE 86u                        /* blocks without a clock tick (0.5 s) before the internal clock takes over */

enum { ARP_OFF, ARP_UP, ARP_DOWN, ARP_UPDN, ARP_UPDN2, ARP_RND, ARP_PLAYED, ARP_CHORD, ARP_NMODES };
enum { ARP_R4, ARP_R8, ARP_R8T, ARP_R16, ARP_R16T, ARP_R32, ARP_NRATES };
static const uint8_t ARP_RATE_SUB[ARP_NRATES] = {96, 48, 32, 24, 16, 12};   /* sub-ticks per step */

static struct {
    volatile int32_t mode;                      /* ARP_OFF .. ARP_CHORD */
    volatile int32_t rate;                      /* ARP_R4 .. ARP_R32 */
    volatile int32_t oct;                       /* 1..4 */
    volatile int32_t gate;                      /* 1..127: note length, 127 = the whole step */
    volatile int32_t swing;                     /* 0..50: percent of a step the odd steps are late */
    volatile int32_t bpm;                       /* 40..240 */
} arp_cfg = { ARP_OFF, ARP_R16, 1, 64, 0, 120 };
static struct { volatile uint32_t ticks, start; } arp_ext;      /* counters, main increments */
static struct { volatile uint32_t step, ext; } arp_tap;         /* steps played so far; 1 = following the MIDI clock */

static struct {
    uint8_t n, note[ARP_MAXN], vel[ARP_MAXN];   /* held, in the order pressed */
    uint8_t on_n, on[ARP_MAXN];                 /* sounding because of the arp */
    uint8_t active;
    uint32_t gate_left, step_left, step_idx, pos, acc, rnd;
    uint32_t ext_seen, ext_start, ext_idle, ext_ever;
} ar = { .rnd = 12345u };

static int arp_on(void) { return arp_cfg.mode != ARP_OFF; }
static int arp_sounding(uint32_t note)
{
    uint32_t i;
    for (i = 0; i < ar.on_n; i++) if (ar.on[i] == note) return 1;
    return 0;
}

static void arp_release_on(void)
{
    uint32_t i;
    for (i = 0; i < ar.on_n; i++) ARP_NOTE_OFF(ar.on[i]);
    ar.on_n = 0;
    ar.gate_left = 0;
}

/* a key goes down / up while the arp is on (events_drain calls these instead of the voices) */
static void arp_in_on(uint32_t note, uint32_t vel)
{
    uint32_t i;
    for (i = 0; i < ar.n; i++)
        if (ar.note[i] == note) { ar.vel[i] = (uint8_t)vel; return; }
    if (ar.n >= ARP_MAXN) return;
    if (!ar.n) { ar.step_left = 0; ar.pos = 0; ar.step_idx = 0; }       /* the first key: start now, from the bottom */
    ar.note[ar.n] = (uint8_t)note;
    ar.vel[ar.n] = (uint8_t)vel;
    ar.n++;
}
static void arp_in_off(uint32_t note)
{
    uint32_t i, j = 0;
    for (i = 0; i < ar.n; i++)
        if (ar.note[i] != note) { ar.note[j] = ar.note[i]; ar.vel[j] = ar.vel[i]; j++; }
    ar.n = (uint8_t)j;
}
static void arp_clear(void)                     /* all notes off / panic */
{
    arp_release_on();
    ar.n = 0;
}

static void arp_deactivate(void)                /* the arp went off: keys still down (or latched) sound as plain notes */
{
    uint32_t i;
    arp_release_on();
    for (i = 0; i < ar.n; i++) ARP_NOTE_ON(ar.note[i], ar.vel[i]);
    ar.n = 0;
    ar.active = 0;
}

static void arp_play(uint32_t note, uint32_t vel)
{
    if (note > 127u || ar.on_n >= ARP_MAXN) return;
    ARP_NOTE_OFF(note);                         /* the same note again: retrigger */
    ARP_NOTE_ON(note, vel);
    ar.on[ar.on_n++] = (uint8_t)note;
}

static void arp_step(void)
{
    uint8_t sn[ARP_MAXN], sv[ARP_MAXN];
    uint32_t i, j, n = ar.n, m, period, p, oc, len, sw, iv, gl;
    int32_t mode = arp_cfg.mode, octs = arp_cfg.oct;
    if (octs < 1) octs = 1;
    if (octs > 4) octs = 4;
    for (i = 0; i < n; i++) { sn[i] = ar.note[i]; sv[i] = ar.vel[i]; }
    if (mode != ARP_PLAYED)                     /* ascending (insertion sort, at most 16) */
        for (i = 1; i < n; i++) {
            uint8_t a = sn[i], b = sv[i];
            for (j = i; j > 0 && sn[j - 1u] > a; j--) { sn[j] = sn[j - 1u]; sv[j] = sv[j - 1u]; }
            sn[j] = a; sv[j] = b;
        }
    m = n * (uint32_t)octs;
    arp_release_on();
    switch (mode) {
    case ARP_CHORD:
        period = (uint32_t)octs;
        while (ar.pos >= period) ar.pos -= period;
        for (i = 0; i < n; i++) arp_play((uint32_t)sn[i] + 12u * ar.pos, sv[i]);
        ar.pos++;
        break;
    default:
        switch (mode) {
        case ARP_UPDN:  period = m > 1u ? 2u * m - 2u : 1u; break;
        case ARP_UPDN2: period = 2u * m; break;
        default:        period = m; break;
        }
        while (ar.pos >= period) ar.pos -= period;
        p = ar.pos;
        if (mode == ARP_RND) {
            ar.rnd = ar.rnd * 1664525u + 1013904223u;
            p = ((ar.rnd >> 16) * m) >> 16;
        } else if (mode == ARP_DOWN) p = m - 1u - p;
        else if (mode == ARP_UPDN && p >= m) p = 2u * m - 2u - p;
        else if (mode == ARP_UPDN2 && p >= m) p = 2u * m - 1u - p;
        oc = 0;
        while (p >= n) { p -= n; oc++; }
        arp_play((uint32_t)sn[p] + 12u * oc, sv[p]);
        ar.pos++;
        break;
    }
    len = ARP_RATE_SUB[(uint32_t)arp_cfg.rate < ARP_NRATES ? (uint32_t)arp_cfg.rate : ARP_R16];
    sw = (len * (uint32_t)(arp_cfg.swing < 0 ? 0 : arp_cfg.swing > 50 ? 50 : arp_cfg.swing) * 41u) >> 12;   /* x 0.01 */
    iv = (ar.step_idx & 1u) ? len - sw : len + sw;  /* the odd steps come late, so the pair still adds up to 2 steps */
    gl = (len * (uint32_t)(arp_cfg.gate < 1 ? 1 : arp_cfg.gate > 127 ? 127 : arp_cfg.gate)) >> 7;
    if (gl < 1u) gl = 1u;
    if (gl > iv) gl = iv;
    ar.step_left = iv;
    ar.gate_left = gl;
    ar.step_idx++;
    arp_tap.step = ar.step_idx;
}

static void arp_sub(void)                       /* one sub-tick */
{
    if (ar.gate_left && --ar.gate_left == 0u) arp_release_on();
    if (ar.step_left) ar.step_left--;
    if (!ar.step_left && ar.n) arp_step();
}

static void arp_run(uint32_t frames)            /* once per block, after events_drain */
{
    uint32_t sub = 0, t, d, bpm;
    if (arp_cfg.mode == ARP_OFF) {
        if (ar.active) arp_deactivate();
        return;
    }
    ar.active = 1;
    t = arp_ext.ticks;                          /* MIDI clock */
    d = t - ar.ext_seen;
    ar.ext_seen = t;
    if (d) { ar.ext_idle = 0; ar.ext_ever = 1; if (d > 24u) d = 24u; }
    else if (ar.ext_idle < 100000u) ar.ext_idle++;
    t = arp_ext.start;
    if (t != ar.ext_start) { ar.ext_start = t; ar.pos = 0; ar.step_idx = 0; ar.step_left = 0; ar.acc = 0; }
    arp_tap.ext = ar.ext_ever && ar.ext_idle < ARP_EXT_IDLE;
    if (arp_tap.ext) {
        sub = d * 4u;
        ar.acc = 0;
    } else {
        bpm = (uint32_t)(arp_cfg.bpm < 40 ? 40 : arp_cfg.bpm > 240 ? 240 : arp_cfg.bpm);
        ar.acc += frames * bpm * 2u;
        while (ar.acc >= ARP_SUB_Q) { ar.acc -= ARP_SUB_Q; sub++; }
    }
    if (!sub && ar.n && !ar.step_left && !arp_tap.ext) sub = 1u;       /* the first key does not wait for a sub-tick boundary */
    while (sub--) arp_sub();
}
