/* SPDX-License-Identifier: GPL-3.0-only */
/* arp.h: the arpeggiator clock and engine (T27 / T28, docs/ARP-SEQ-DESIGN.md). Runs in the audio ISR, once per block,
 * after the note events were drained; integer only, no division, no flash.
 *
 * The includer defines ARP_NOTE_ON(note, vel) and ARP_NOTE_OFF(note) (the engine's note_on / note_off) before this file.
 *
 * Who writes what:
 *   arp_cfg.*       ISR (block_control copies the patch parameters once per block)
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
    volatile int32_t seq;                       /* 1: the note sequencer plays (with the arp on: the arp plays each step's chord) */
    volatile int32_t mglide;                    /* the modulation lane slides between steps */
    volatile int32_t chord;                     /* the sequencer's chord a step: CH_OFF, CH_HELD, or a fixed shape (SEQ_CHORD) */
} arp_cfg = { ARP_OFF, ARP_R16, 1, 64, 0, 120, 0, 0, 0 };

/* ---- the sequencer's chords (CHORD, SEQ page): each step plays a chord on its note. OFF: the note alone (as before CHORD).
 * HELD: the keys held (or latched) give the shape, the lowest one transposes (a single key: as OFF). The others: a fixed shape,
 * the newest held key transposes (as OFF). With the arp on, a step lasts a beat and the arp plays its chord at RATE. */
enum { CH_OFF, CH_HELD, CH_FIX };
#define SEQ_BEAT 96u                            /* sub-ticks of a step while the arp plays the sequencer (a quarter note) */
static const uint8_t SEQ_CHORD[6][3] = {{4, 7, 0}, {3, 7, 0}, {4, 7, 10}, {4, 7, 11}, {3, 7, 10}, {5, 7, 0}};   /* semitones above the root (0: none): maj min 7 maj7 min7 sus4 */

/* ---- the note sequencer (T28 stage S2): one pattern of up to 32 steps in RAM, written by the main loop only while sq.rec is
 * set (step input), read by the ISR. A step is a note (0..127), a velocity (0 = rest) and a tie flag (the previous note goes on
 * through this step). Keys held while it plays transpose it: the newest held key minus C4 (60), so C4 plays the pattern as
 * recorded; no key held plays it as recorded. It uses the arp's clock, rate, gate, swing and tempo. */
#define SEQ_MAXN 32u
static struct {
    volatile uint8_t note[SEQ_MAXN], vel[SEQ_MAXN], tie[SEQ_MAXN];
    volatile uint8_t len;                       /* steps 0..32 (0: nothing to play) */
    volatile uint8_t rec;                       /* main loop: step input is on (the ISR plays nothing and keys sound as plain notes) */
    volatile uint8_t cur;                       /* main loop: the step input writes next */
} sq;
static volatile int8_t ml[SEQ_MAXN];             /* the modulation lane: a value -64..63 a step (0 = neutral), as long as the pattern */
static volatile uint8_t sq_run = 1;           /* the sequencer's transport (PLAY while it is on): 0 = stopped, the keys only transpose */
static struct { volatile uint32_t step, on; } sq_tap;           /* the step playing (ISR writes) and whether the sequencer is running */
static struct { volatile uint32_t ticks, start; } arp_ext;      /* counters, main increments */
static struct { volatile uint32_t step, ext, beat_blocks; } arp_tap;   /* steps played so far; 1 = following the MIDI clock; blocks (256 frames) the last 24 clock ticks took (the screen: 10336 / this = BPM) */

static struct {
    uint8_t n, note[ARP_MAXN], vel[ARP_MAXN];   /* held, in the order pressed */
    uint8_t on_n, on[ARP_MAXN];                 /* sounding because of the arp */
    uint8_t active;
    uint32_t step_len;
    uint32_t gate_left, step_left, step_idx, pos, acc, rnd;
    uint32_t seed;                              /* RANDOM: the seed of the run, taken at the first key (the screen draws the same sequence) */
    uint32_t ext_seen, ext_start, ext_idle, ext_ever, blk, mark, tk_acc;
} ar = { .rnd = 12345u };
static struct { uint8_t active, nh, h[ARP_MAXN]; int32_t trans; uint32_t pos; uint8_t arp; uint32_t left, slen; } qs;     /* the sequencer (ISR): running, keys held (for the transposition), pattern position; the arp plays it (sub-ticks to the next step, the step's length) */

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
    if (!ar.n) {                                /* the first key: start now, from the bottom; RANDOM: a new sequence (the clock's ticks into the seed) */
        ar.step_left = 0; ar.pos = 0; ar.step_idx = 0;
        ar.rnd ^= fm1_ticks() * 2654435761u;
        ar.seed = ar.rnd;
    }
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
static void arp_clear(void)                     /* all notes off / panic (the sequencer's held keys too) */
{
    arp_release_on();
    ar.n = 0;
    qs.nh = 0;
    qs.trans = 0;
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

/* ---- sequencer: keys held (transposition) */
static int seq_sounding(uint32_t note) { return arp_sounding(note); }      /* (the sequencer plays through the arp's note list ar.on) */
static void seq_trans(void) { qs.trans = qs.nh ? (int32_t)qs.h[qs.nh - 1u] - 60 : 0; }
static void seq_in_on(uint32_t note)
{
    uint32_t i;
    for (i = 0; i < qs.nh; i++)                 /* the newest press goes last */
        if (qs.h[i] == note) { for (; i + 1u < qs.nh; i++) qs.h[i] = qs.h[i + 1u]; qs.nh--; break; }
    if (qs.nh >= ARP_MAXN) { uint32_t k; for (k = 1; k < qs.nh; k++) qs.h[k - 1u] = qs.h[k]; qs.nh--; }
    qs.h[qs.nh++] = (uint8_t)note;
    seq_trans();
}
static void seq_in_off(uint32_t note)
{
    uint32_t i, j = 0;
    for (i = 0; i < qs.nh; i++) if (qs.h[i] != note) qs.h[j++] = qs.h[i];
    qs.nh = (uint8_t)j;
    seq_trans();
}
static void seq_clear(void) { arp_release_on(); qs.nh = 0; seq_trans(); }
static void seq_deactivate(void)                /* the sequencer went off (or step input began): keys still down sound as plain notes */
{
    uint32_t i;
    arp_release_on();
    for (i = 0; i < qs.nh; i++) ARP_NOTE_ON(qs.h[i], 100u);
    qs.nh = 0;
    qs.trans = 0;
    qs.active = 0;
    qs.arp = 0;
    ar.n = 0;                                   /* (the chord the arp played for the sequencer) */
    sq_tap.on = 0;
}

static uint32_t seq_chord_notes(uint32_t i, uint8_t *out)   /* the notes step i plays (transposed, the root first): 0 = a rest */
{
    uint32_t k, c = 0, ch = (uint32_t)arp_cfg.chord;
    int32_t lo = 128, root, n;
    if (!(sq.vel[i] & 127u)) return 0;
    for (k = 0; k < qs.nh; k++) if (qs.h[k] < lo) lo = qs.h[k];
    root = (int32_t)sq.note[i] + (ch == CH_HELD && qs.nh ? lo - 60 : qs.trans);
    if (root >= 0 && root <= 127) out[c++] = (uint8_t)root;
    if (ch == CH_HELD) {                        /* in the order pressed (PLAYED keeps it) */
        for (k = 0; k < qs.nh && c < ARP_MAXN; k++)
            if ((n = root + (int32_t)qs.h[k] - lo) != root && n >= 0 && n <= 127) out[c++] = (uint8_t)n;
    } else if (ch >= CH_FIX && ch < CH_FIX + 6u) {
        for (k = 0; k < 3u && SEQ_CHORD[ch - CH_FIX][k]; k++)
            if ((n = root + SEQ_CHORD[ch - CH_FIX][k]) <= 127) out[c++] = (uint8_t)n;
    }
    return c;
}

static void seq_step(void)
{
    uint32_t len = sq.len > SEQ_MAXN ? SEQ_MAXN : sq.len, i = qs.pos, nx, rate, rl, sw, iv, gl, vel, c, k;
    uint8_t cn[ARP_MAXN];
    if (i >= len) i = 0;
    nx = i + 1u >= len ? 0u : i + 1u;
    rate = (uint32_t)arp_cfg.rate < ARP_NRATES ? (uint32_t)arp_cfg.rate : ARP_R16;
    rl = ARP_RATE_SUB[rate];
    sw = (rl * (uint32_t)(arp_cfg.swing < 0 ? 0 : arp_cfg.swing > 50 ? 50 : arp_cfg.swing) * 41u) >> 12;
    iv = (ar.step_idx & 1u) ? rl - sw : rl + sw;
    gl = (rl * (uint32_t)(arp_cfg.gate < 1 ? 1 : arp_cfg.gate > 127 ? 127 : arp_cfg.gate)) >> 7;
    if (gl < 1u) gl = 1u;
    if (gl > iv) gl = iv;
    vel = sq.vel[i] & 127u;
    if (sq.tie[i] && ar.on_n && vel) {          /* tied: the note from the step before goes on */
        ar.gate_left = gl;
    } else {
        arp_release_on();
        c = seq_chord_notes(i, cn);             /* (CHORD off: the note alone) */
        for (k = 0; k < c; k++) arp_play(cn[k], vel);
        ar.gate_left = ar.on_n ? ((sq.tie[nx] && sq.vel[nx] && len > 1u) ? iv + 1u : gl) : 0u;      /* the next step is tied: no gap */
    }
    ar.step_left = iv;
    ar.step_len = iv;
    ar.step_idx++;
    qs.pos = nx;
    sq_tap.step = i;
}
static void seq_sub(void)
{
    if (ar.gate_left && --ar.gate_left == 0u) arp_release_on();
    if (ar.step_left) ar.step_left--;
    if (!ar.step_left && sq.len) seq_step();
}

/* the sequencer with the arp on: a step a beat; its chord becomes the arp's notes, the arp plays them at RATE from the
 * pattern's start (a tied step: the arp goes on with the chord before; a rest: silence) */
static void seqa_step(void)
{
    uint32_t len = sq.len > SEQ_MAXN ? SEQ_MAXN : sq.len, i = qs.pos, c, k;
    uint8_t cn[ARP_MAXN];
    if (i >= len) i = 0;
    if (!(sq.tie[i] && sq.vel[i] && ar.n)) {
        c = seq_chord_notes(i, cn);
        for (k = 0; k < c; k++) { ar.note[k] = cn[k]; ar.vel[k] = (uint8_t)(sq.vel[i] & 127u); }
        ar.n = (uint8_t)c;
        ar.pos = 0; ar.step_idx = 0; ar.step_left = 0;
        if (!c) arp_release_on();
    }
    qs.left = qs.slen = SEQ_BEAT;
    qs.pos = i + 1u >= len ? 0u : i + 1u;
    sq_tap.step = i;
}
static void seqa_sub(void)
{
    if (qs.left) qs.left--;
    if (!qs.left && sq.len) seqa_step();
    arp_sub();
}

static int32_t seq_mod_q8(void)                 /* the lane's value now, Q8, -64..63 (0 when the sequencer is not playing); once per block, 32-bit division only */
{
    uint32_t len = sq.len > SEQ_MAXN ? SEQ_MAXN : sq.len, i = sq_tap.step, nx;
    uint32_t sl = qs.arp ? qs.slen : ar.step_len, lf = qs.arp ? qs.left : ar.step_left;     /* (the arp plays it: the beat's clock) */
    int32_t v;
    if (!qs.active || !sq_run || !len) return 0;
    if (i >= len) i = 0;
    v = (int32_t)ml[i] * 256;
    if (arp_cfg.mglide && sl) {
        int32_t prog = (int32_t)(sl - (lf < sl ? lf : sl));
        nx = i + 1u >= len ? 0u : i + 1u;
        v += ((int32_t)ml[nx] - (int32_t)ml[i]) * 256 * prog / (int32_t)sl;
    }
    return v;
}

static void arp_run(uint32_t frames)            /* once per block, after events_drain */
{
    uint32_t sub = 0, t, d, bpm;
    t = arp_ext.ticks;                          /* MIDI clock */
    d = t - ar.ext_seen;
    ar.ext_seen = t;
    ar.blk++;
    if (d) {
        ar.tk_acc += d;
        while (ar.tk_acc >= 24u) { ar.tk_acc -= 24u; arp_tap.beat_blocks = ar.blk - ar.mark; ar.mark = ar.blk; }     /* one quarter note of clock */
    }
    if (d) { ar.ext_idle = 0; ar.ext_ever = 1; if (d > 6u) d = 6u; }       /* (a burst after a stalled main loop: at most one 1/16) */
    else if (ar.ext_idle < 100000u) ar.ext_idle++;
    t = arp_ext.start;
    if (t != ar.ext_start) { ar.ext_start = t; sq_run = 1; ar.pos = 0; qs.pos = 0; ar.step_idx = 0; ar.step_left = 0; ar.acc = 0; }
    arp_tap.ext = ar.ext_ever && ar.ext_idle < ARP_EXT_IDLE;      /* (tracked while the arp is off too: the page shows it) */
    if (sq.rec) {                               /* step input: nothing plays from the clock, keys are plain notes */
        if (ar.active) arp_deactivate();
        if (qs.active) seq_deactivate();
        return;
    }
    if (arp_cfg.seq && sq.len) {                /* the sequencer (a pattern with no steps plays nothing: the keys and the arp work as usual): the arp's held notes become its transposition keys */
        if (!qs.active) {
            uint32_t i;
            arp_release_on();
            for (i = 0; i < ar.n; i++) seq_in_on(ar.note[i]);
            ar.n = 0; ar.active = 0;
            qs.active = 1; qs.pos = 0; ar.step_left = 0; ar.step_idx = 0; sq_tap.step = 0;
            sq_tap.on = 1;
        }
    } else {
        if (qs.active) seq_deactivate();
        if (arp_cfg.mode == ARP_OFF) {
            if (ar.active) arp_deactivate();
            return;
        }
        ar.active = 1;
    }
    if (qs.active && (uint8_t)(arp_cfg.mode != ARP_OFF) != qs.arp) {    /* the arp came on / went off under the sequencer: the step's time goes on */
        arp_release_on();
        ar.n = 0;
        if (!qs.arp) { qs.left = ar.step_left; qs.slen = ar.step_len; ar.step_left = 0; }
        else { ar.step_left = qs.left; ar.step_len = qs.slen; }
        qs.arp ^= 1u;
    }
    if (qs.active && !sq_run) {                 /* stopped: silent, the next start begins at step 1 */
        arp_release_on();
        qs.pos = 0; ar.step_left = 0; ar.step_idx = 0; ar.gate_left = 0; ar.acc = 0; sq_tap.step = 0;
        qs.left = 0; ar.n = 0;
        sq_tap.on = 0;
        return;
    }
    if (qs.active) sq_tap.on = 1;
    if (arp_tap.ext) {
        sub = d * 4u;
        ar.acc = 0;
    } else {
        bpm = (uint32_t)(arp_cfg.bpm < 40 ? 40 : arp_cfg.bpm > 240 ? 240 : arp_cfg.bpm);
        ar.acc += frames * bpm * 2u;
        while (ar.acc >= ARP_SUB_Q) { ar.acc -= ARP_SUB_Q; sub++; }
    }
    if (!sub && !(qs.active && qs.arp ? qs.left : ar.step_left) && !arp_tap.ext && (qs.active ? sq.len != 0u : ar.n != 0u)) sub = 1u;       /* the first key (or the start of the sequence) does not wait for a sub-tick boundary */
    while (sub--) { if (qs.active) { if (qs.arp) seqa_sub(); else seq_sub(); } else arp_sub(); }
}
