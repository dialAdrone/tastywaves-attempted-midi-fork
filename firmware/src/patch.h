/* SPDX-License-Identifier: GPL-3.0-only */
/* patch.h: the interface between the main loop (UI, MIDI, presets) and the v1 engine (synth.h). Included from
 * engine.h when BENCH is 0. No registers, no drawing. Everything the UI needs is here; it never touches voices.
 *
 * Who writes what (one writer each, 32-bit fields, so every store is atomic):
 *   patch.p[P_*]   main loop (patch_set / patch_cc / presets)    ISR reads once per block (256 frames), smooths the
 *                  continuous ones over ~20 ms (cutoff, reso, levels, positions, BEND, env amount, drive, volume)
 *   eng_ctl        main loop (eng_bend / eng_aftertouch / CC 1)   performance state, not part of a patch
 *   note events    main loop pushes (eng_push via play / stop / eng_midi), ISR pops: a 64-entry single-producer /
 *                  single-consumer ring of packed uint32 (type << 16 | note << 8 | vel). Voice allocation and
 *                  stealing run in the ISR, so voice state has one writer. A full ring drops the event (counted).
 *   eng_tap        ISR (once per block)                            what sounds, the position, the peak, the shedding
 *
 * Units: every parameter is a small integer with the range in PDESC (UI, MIDI CC map and the preset serialiser can
 * all use the same table; values fit an int16, all but the positions an int8). Values out of range are clamped by
 * patch_set and again by the ISR's reads, so a corrupted preset cannot make the ISR index out of a table. */
#pragma once
#include "usertab.h"                             /* table 12..19 = user slot 0..7 */

#define ENG_NV 12u                              /* voices */
#define P_COUNT_V1 47u                          /* parameters in a patch record before the arpeggiator (P_ARP): records never get shorter than this */

enum {
    /* oscillator 1 / 2: wavetable (0..11 factory, 12..19 user slots 0..7), position 0..4095 over the 32 frames (12 bit, any frame, morphs between
     * neighbours), level, coarse (semitones) and fine (1/128 semitone: +-64 = +-50 cents) tune */
    P_TABLE1, P_POS1, P_LEVEL1, P_SEMI1, P_FINE1,
    P_TABLE2, P_POS2, P_LEVEL2, P_SEMI2, P_FINE2,
    P_SUB,          /* sine one octave under osc 1, level 0..127 (0 = off, costs nothing) */
    P_NOISE,        /* white noise level 0..127 (0 = off) */
    P_WARP,         /* BEND: smooth phase warp of the read, -64..63 (0 = off); + squeezes the first half of the cycle */
    P_WARP_DEST,    /* 0 both oscillators, 1 osc 1, 2 osc 2 */
    P_QUALITY,      /* 0 8-tap sinc (63 dBc alias rejection), 1 eco: Catmull-Rom 4-tap (cheaper, ~24 dBc) */
    /* filter: 2-pole state variable (trapezoidal) */
    P_CUTOFF,       /* 0..127 = about 20 Hz .. 18 kHz (0.94 semitone per step) */
    P_RESO,         /* 0..127: flat (Q 0.7) .. close to self-oscillation */
    P_FMODE,        /* 0 low pass, 1 band pass, 2 high pass */
    P_KEYTRK,       /* key tracking 0..127 (128 = 100 %: cutoff follows the note) */
    P_FENV,         /* mod envelope -> cutoff, -64..63 = -64..63 semitones at full envelope */
    P_FVEL,         /* velocity -> cutoff, 0..127 = 0..+48 semitones at velocity 127 */
    /* envelopes: amp (1) and mod (2); times 0..127 = 0.5 ms .. 10 s (exponential), sustain 0..127 */
    P_A1, P_D1, P_S1, P_R1,
    P_A2, P_D2, P_S2, P_R2,
    P_ECURVE,       /* 0 linear decay/release, 1 exponential (attack is linear in both) */
    P_MENV_POS,     /* mod envelope -> position, -64..63 = -32..+32 frames */
    P_MENV_WARP,    /* mod envelope -> BEND, -64..63 */
    /* LFO (one per patch, free running) */
    P_LFO_SHAPE,    /* 0 sine, 1 triangle, 2 saw, 3 square, 4 sample & hold */
    P_LFO_RATE,     /* 0..127 = 0.02 .. 40 Hz */
    P_LFO_POS,      /* depth -> position, 0..127 = +-16 frames */
    P_LFO_PITCH,    /* depth -> pitch, 0..127 (quadratic: 10 = 7 cents, 127 = +-12 semitones) */
    P_LFO_CUT,      /* depth -> cutoff, 0..127 = +-63 semitones */
    P_LFO_AMP,      /* depth -> amplitude (tremolo), 0..127 */
    /* performance */
    P_MW_POS,       /* mod wheel -> position, 0..127 = 0..+16 frames */
    P_MW_VIB,       /* mod wheel -> vibrato (LFO -> pitch), 0..127 as P_LFO_PITCH */
    P_AT_CUT,       /* aftertouch -> cutoff, 0..127 = 0..+48 semitones */
    P_VEL_AMP,      /* velocity -> level, 0 (none) .. 127 (full) */
    P_BEND_RANGE,   /* pitch bend range, semitones 0..24 */
    P_VMODE,        /* 0 poly (12 voices), 1 mono, 2 legato */
    P_GLIDE,        /* portamento 0 (off) .. 127 (5 s); poly glides from the last note played */
    /* output */
    P_DRIVE,        /* master drive into the soft clip, 0..127 = gain 1 .. 16 */
    P_VOLUME,       /* voice level 0..127 */
    /* arpeggiator (arp.h), appended after the patch format of the first user patches: P_COUNT_V1 values were all a record had */
    P_ARP,          /* 0 off, 1 up, 2 down, 3 up-down, 4 up-down (ends repeat), 5 random, 6 played, 7 chord */
    P_ARP_RATE,     /* 0 1/4, 1 1/8, 2 1/8T, 3 1/16, 4 1/16T, 5 1/32 */
    P_ARP_OCT,      /* octaves 1..4 */
    P_ARP_GATE,     /* note length 1..127 of 128 of a step */
    P_ARP_SWING,    /* 0..50 % of a step: the odd steps are late */
    P_TEMPO,        /* BPM 40..240 (the arp and the sequencers; follows MIDI clock when there is one) */
    P_SEQ,          /* 0 off, 1 the note sequencer plays (with the arp on: the arp plays each step's chord, a step a beat) */
    P_MOD,          /* modulation lane target: 0 off, 1 table position, 2 cutoff, 3 BEND, 4 drive */
    P_MOD_DEPTH,    /* -100..100 % of the target's range for a lane value at its extreme */
    P_MOD_GLIDE,    /* 1: the value slides to the next step's over the step (else it jumps; the smoother rounds the edge) */
    /* effects chain (fx.h): 4 slots in series, each TYPE (0 off, 1 chorus, 2 flanger, 3 phaser, 4 delay, 5 reverb), A, B (meaning by type), MIX */
    P_FX1_TYPE, P_FX1_A, P_FX1_B, P_FX1_MIX, P_FX2_TYPE, P_FX2_A, P_FX2_B, P_FX2_MIX,
    P_FX3_TYPE, P_FX3_A, P_FX3_B, P_FX3_MIX, P_FX4_TYPE, P_FX4_A, P_FX4_B, P_FX4_MIX,
    P_CHORD,        /* the sequencer's chord on each step: 0 off (the note alone), 1 the held keys' shape, 2 maj 3 min 4 7 5 maj7 6 min7 7 sus4 (arp.h) */
    /* the second pattern (SEQ2): its own arp, rate, gate, swing, chord; both patterns' levels. Saved in the patch's companion record (panel.h), not the patch record */
    P_ARP2, P_ARP2_RATE, P_ARP2_OCT, P_ARP2_GATE, P_ARP2_SWING, P_CHORD2,
    P_SEQ_LVL1,     /* pattern 1's level, 0..127 = its velocities x 0..1 */
    P_SEQ_LVL2,     /* pattern 2's level */
    P_COUNT
};

typedef struct { char name[6]; int16_t min, max, def; } pdesc_t;
static const pdesc_t PDESC[P_COUNT] = {
    {"TBL1", 0, UT_NTABLES - 1, 0},   {"POS1", 0, 4095, 0},  {"LVL1", 0, 127, 100}, {"SEMI1", -24, 24, 0}, {"FINE1", -64, 63, 0},
    {"TBL2", 0, UT_NTABLES - 1, 0},   {"POS2", 0, 4095, 0},  {"LVL2", 0, 127, 80},  {"SEMI2", -24, 24, 0}, {"FINE2", -64, 63, 9},
    {"SUB", 0, 127, 0},   {"NOISE", 0, 127, 0},
    {"BEND", -64, 63, 0}, {"BDEST", 0, 2, 0},    {"ECO", 0, 1, 0},
    {"CUT", 0, 127, 110}, {"RES", 0, 127, 20},   {"FMODE", 0, 2, 0},    {"KTRK", 0, 127, 64},  {"FENV", -64, 63, 0},
    {"FVEL", 0, 127, 0},
    {"A1", 0, 127, 10},   {"D1", 0, 127, 80},    {"S1", 0, 127, 100},   {"R1", 0, 127, 70},
    {"A2", 0, 127, 0},    {"D2", 0, 127, 70},    {"S2", 0, 127, 0},     {"R2", 0, 127, 70},
    {"CURVE", 0, 1, 1},   {"EPOS", -64, 63, 0},  {"EBEND", -64, 63, 0},
    {"LSHP", 0, 4, 1},    {"LRATE", 0, 127, 60}, {"LPOS", 0, 127, 0},   {"LPIT", 0, 127, 0},   {"LCUT", 0, 127, 0},
    {"LAMP", 0, 127, 0},
    {"MWPOS", 0, 127, 64},{"MWVIB", 0, 127, 20}, {"ATCUT", 0, 127, 0},  {"VEL", 0, 127, 64},   {"PBR", 0, 24, 2},
    {"MODE", 0, 2, 0},    {"GLIDE", 0, 127, 0},
    {"DRIVE", 0, 127, 0}, {"VOL", 0, 127, 100},
    {"ARP", 0, 7, 0},     {"RATE", 0, 5, 3},     {"OCT", 1, 4, 1},      {"GATE", 1, 127, 64},  {"SWING", 0, 50, 0},
    {"TEMPO", 40, 240, 120}, {"SEQ", 0, 1, 0},
    {"MOD", 0, 4, 0},     {"MDEPTH", -100, 100, 100}, {"MGLIDE", 0, 1, 0},
    {"FXT", 0, 5, 0},      {"FXA", 0, 127, 64},   {"FXB", 0, 127, 64},   {"FXMIX", 0, 127, 64},
    {"FXT", 0, 5, 0},      {"FXA", 0, 127, 64},   {"FXB", 0, 127, 64},   {"FXMIX", 0, 127, 64},
    {"FXT", 0, 5, 0},      {"FXA", 0, 127, 64},   {"FXB", 0, 127, 64},   {"FXMIX", 0, 127, 64},
    {"FXT", 0, 5, 0},      {"FXA", 0, 127, 64},   {"FXB", 0, 127, 64},   {"FXMIX", 0, 127, 64},
    {"CHORD", 0, 7, 0},
    {"ARP2", 0, 7, 0},    {"RATE2", 0, 5, 3},    {"OCT2", 1, 4, 1},     {"GATE2", 1, 127, 64}, {"SWING2", 0, 50, 0},
    {"CHORD2", 0, 7, 0},  {"LVL1", 0, 127, 127}, {"LVL2", 0, 127, 127},
};
#define P_MAIN P_ARP2                           /* the parameters in the patch record; P_MAIN .. P_COUNT - 1 go in its companion with pattern 2 */

static struct { volatile int32_t p[P_COUNT]; } patch;                       /* main writes */
static struct { volatile int32_t bend, mod, at; } eng_ctl;                  /* main writes: -8192..8191, 0..127, 0..127 */

static int32_t pclamp(uint32_t id, int32_t v)
{
    if (id >= P_COUNT) return 0;
    return v < PDESC[id].min ? PDESC[id].min : v > PDESC[id].max ? PDESC[id].max : v;
}
static void patch_set(uint32_t id, int32_t v) { if (id < P_COUNT) patch.p[id] = pclamp(id, v); }
static int32_t patch_get(uint32_t id) { return id < P_COUNT ? patch.p[id] : 0; }
static void patch_add(uint32_t id, int32_t d) { if (id < P_COUNT && d) patch_set(id, patch.p[id] + d); }   /* encoders */
static void patch_reset(void)
{
    uint32_t i;
    for (i = 0; i < P_COUNT; i++) patch.p[i] = PDESC[i].def;
}

/* ---- note events (main -> ISR) */
#define EV_N 64u
enum { EV_ON = 1, EV_OFF = 2, EV_ALLOFF = 3, EV_PANIC = 4 };
static volatile uint32_t ev_ring[EV_N];
static volatile uint32_t ev_head, ev_dropped;   /* main writes */
static volatile uint32_t ev_tail;               /* ISR writes */
static int eng_push(uint32_t type, uint32_t note, uint32_t vel)
{
    uint32_t h = ev_head;
    if (h - ev_tail >= EV_N) { ev_dropped++; return -1; }
    ev_ring[h % EV_N] = type << 16 | (note & 127u) << 8 | (vel & 127u);
    ev_head = h + 1u;                           /* the slot first, then the head: the ISR never sees a half event */
    return 0;
}
static void eng_bend(int32_t v) { eng_ctl.bend = v < -8192 ? -8192 : v > 8191 ? 8191 : v; }
static void eng_aftertouch(int32_t v) { eng_ctl.at = v < 0 ? 0 : v > 127 ? 127 : v; }

/* ---- MIDI CC map (after Felucca's convention; docs/research/wavetable-engine.md 4.3). Unmapped CCs are ignored. */
static void patch_cc(uint32_t cc, uint32_t v)
{
    int32_t x = (int32_t)(v & 127u), bi = x - 64, pos = x * 32 + (x >> 2);   /* 0..127 -> 0..4095 */
    switch (cc) {
    case 1: eng_ctl.mod = x; break;
    case 5: patch_set(P_GLIDE, x); break;
    case 7: patch_set(P_VOLUME, x); break;
    case 12: patch_set(P_WARP, bi); break;
    case 13: patch_set(P_FMODE, x * 3 >> 7); break;
    case 14: patch_set(P_VMODE, x * 3 >> 7); break;
    case 15: patch_set(P_QUALITY, x >= 64); break;
    case 16: patch_set(P_LFO_SHAPE, x * 5 >> 7); break;
    case 17: patch_set(P_LFO_PITCH, x); break;
    case 18: patch_set(P_LFO_CUT, x); break;
    case 19: patch_set(P_LFO_AMP, x); break;
    case 20: patch_set(P_TABLE1, x * 12 >> 7); break;
    case 21: patch_set(P_TABLE2, x * 12 >> 7); break;
    case 22: patch_set(P_POS2, pos); break;
    case 23: patch_set(P_LEVEL2, x); break;
    case 24: patch_set(P_SUB, x); break;
    case 25: patch_set(P_NOISE, x); break;
    case 26: patch_set(P_LFO_RATE, x); break;
    case 27: patch_set(P_LFO_POS, x); break;
    case 28: patch_set(P_FENV, bi); break;
    case 29: patch_set(P_DRIVE, x); break;
    case 30: patch_set(P_SEMI2, (x * 49 >> 7) - 24); break;
    case 31: patch_set(P_FINE2, bi); break;
    case 70: patch_set(P_POS1, pos); break;
    case 71: patch_set(P_RESO, x); break;
    case 72: patch_set(P_R1, x); break;
    case 73: patch_set(P_A1, x); break;
    case 74: patch_set(P_CUTOFF, x); break;
    case 75: patch_set(P_D1, x); break;
    case 76: patch_set(P_S1, x); break;
    case 77: patch_set(P_A2, x); break;
    case 78: patch_set(P_D2, x); break;
    case 79: patch_set(P_S2, x); break;
    case 80: patch_set(P_R2, x); break;
    case 81: patch_set(P_MENV_POS, bi); break;
    case 82: patch_set(P_MENV_WARP, bi); break;
    case 83: patch_set(P_KEYTRK, x); break;
    case 84: patch_set(P_ECURVE, x >= 64); break;
    case 85: patch_set(P_AT_CUT, x); break;
    case 86: patch_set(P_LEVEL1, x); break;
    case 87: patch_set(P_SEMI1, (x * 49 >> 7) - 24); break;
    case 88: patch_set(P_FINE1, bi); break;
    case 89: patch_set(P_WARP_DEST, x * 3 >> 7); break;
    case 90: patch_set(P_FVEL, x); break;
    case 102: patch_set(P_BEND_RANGE, x * 25 >> 7); break;
    case 103: patch_set(P_MW_POS, x); break;
    case 104: patch_set(P_MW_VIB, x); break;
    case 105: patch_set(P_VEL_AMP, x); break;
    case 120: eng_push(EV_PANIC, 0, 0); break;  /* all sound off */
    case 123: eng_push(EV_ALLOFF, 0, 0); break; /* all notes off */
    default: break;
    }
}

/* ---- taps (ISR -> main), written once per block */
static volatile struct {
    uint32_t busy;                              /* bit v: voice v sounds (attack .. end of release) */
    uint32_t vnote[ENG_NV];                     /* note | 0x100 while its key is held | 0x200 while it sounds */
    uint32_t pos;                               /* osc 1 position of the newest voice, 0..4095 (with modulation) */
    uint32_t peak;                              /* largest |output| of the last block, 0..32767 */
    uint32_t shed, shed_blocks;                 /* voices skipped by the overload guard (last block / blocks since boot) */
} eng_tap;

static uint32_t eng_voices_busy(void)           /* how many voices sound */
{
    uint32_t b = eng_tap.busy, n = 0;
    while (b) { n += b & 1u; b >>= 1; }
    return n;
}
static uint32_t eng_pos_now(void) { return eng_tap.pos; }
static uint32_t eng_peak(void) { return eng_tap.peak; }
static uint32_t eng_cpu_permille(void)          /* the last block's ISR time, per mille of the 5.8 ms budget */
{
    uint32_t t = cpu_last;
    if (t > 4000000u) t = 4000000u;
    return t * 1000u / AUDIO_BUDGET_TICKS;
}
static uint32_t eng_lit(uint32_t base)          /* keys (bit k = note base + k, 27 keys) whose note is held and sounds */
{
    uint32_t v, lit = 0;
    for (v = 0; v < ENG_NV; v++) {
        uint32_t t = eng_tap.vnote[v], n = t & 127u;
        if ((t & 0x300u) == 0x300u && n >= base && n < base + 27u) lit |= 1u << (n - base);
    }
    return lit;
}
static int eng_quiet(void) { return !eng_tap.busy && ev_head == ev_tail; }
/* frame 0, level 0 (256 samples) of a table, for drawing; frames are WTG_FRAME_LEN int16 apart */
static const int16_t *eng_frames(uint32_t table, uint32_t *stride);
/* the BEND phase map, for a preview of the warped frame: phase 0..2^32 -> warped phase, warp -64..63 */
static uint32_t eng_warp_preview(uint32_t ph, int32_t warp);
