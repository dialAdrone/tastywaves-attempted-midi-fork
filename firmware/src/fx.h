/* SPDX-License-Identifier: GPL-3.0-only */
/* Effects chain (docs/EFFECTS-DESIGN.md): 4 slots in series on the stereo bus (synth.h fx_chain hooks, before the drive and
 * the soft clip). A slot has a TYPE and three values (A, B, MIX) in the patch (P_FXn_*). Included by synth.h before
 * engine_render. The audio ISR runs fx_slot_run for a slot whose `on` is set; the main loop (fx_sync) changes a slot's type
 * while `on` is 0: the ISR always finishes a block before the main loop runs, so it never sees a half-cleared slot.
 * Types so far: 1 CHORUS, 2 FLANGER (one delay line, two taps; the right tap's LFO is a quarter cycle behind).
 * Bus samples are Q15 x 32 (+-2^20 full scale): the delay line stores them >> 5 as int16. */
#pragma once

#define FXT_NONE 0u
#define FXT_CHORUS 1u
#define FXT_FLANGER 2u
#define FX_DLEN 2048u                           /* samples of the modulated delay (46 ms) */

typedef struct {
    volatile uint8_t type;                      /* 0 = the slot passes the bus through */
    uint8_t idx;
    uint32_t ph, w;                             /* LFO phase, write position */
    int32_t mix_s;                              /* smoothed mix, Q8 of 0..128 */
    int32_t fb;                                 /* flanger: the last wet sample (feedback) */
    int16_t buf[FX_DLEN];
} fx_slot_t;

#ifdef DINGHY_HOST
static fx_slot_t fxs[FX_SLOTS];
#else
static fx_slot_t fxs[FX_SLOTS] __attribute__((section(".pool")));
#endif

static void fx_slot_run(int32_t *lr, uint32_t n, void *state)
{
    fx_slot_t *s = (fx_slot_t *)state;
    const int32_t *p = es.p + P_FX1_TYPE + 4u * s->idx;
    uint32_t type = s->type, inc, i, w = s->w, ph = s->ph;
    int32_t a = p[1], b = p[2], mt = p[3] + (p[3] >> 6), ms = s->mix_s, fbk = 0, d0, dq;
    if (!type) return;
    if (type == FXT_CHORUS) {                   /* A rate 0.02 .. 4.4 Hz, B depth +-0..254 samples around 15 ms */
        inc = LFO_INC[(a * 90) >> 7] >> 5;
        d0 = 661; dq = b * 2;
    } else {                                    /* flanger: A rate, B feedback up to 0.79; delay 1.5 ms +- 60 samples */
        inc = LFO_INC[(a * 90) >> 7] >> 5;
        d0 = 70; dq = 60; fbk = (b * 205) >> 7;
    }
    for (i = 0; i < n; i++) {
        int32_t inl = lr[2u * i], inr = lr[2u * i + 1u], mono = (inl + inr) >> 1, wet[2], k, dl, x;
        ms += (mt * 256 - ms) >> 10;
        ph += inc;
        for (k = 0; k < 2; k++) {
            int32_t lfo = sine(ph + (k ? 0x40000000u : 0u)), pos, fr, i0, i1;
            dl = d0 * 256 + ((lfo * dq) >> 7);              /* Q8 samples */
            pos = ((int32_t)w << 8) - dl;
            pos &= (int32_t)(FX_DLEN << 8) - 1;
            i0 = pos >> 8; fr = pos & 255;
            i1 = (i0 + 1) & (int32_t)(FX_DLEN - 1u);
            wet[k] = (((int32_t)s->buf[i0] * (256 - fr) + (int32_t)s->buf[i1] * fr) >> 8) << 5;
        }
        x = (mono + (type == FXT_FLANGER ? (s->fb * fbk) >> 8 : 0)) >> 5;
        s->buf[w] = (int16_t)(x > 32767 ? 32767 : x < -32768 ? -32768 : x);
        s->fb = wet[0];
        w = (w + 1u) & (FX_DLEN - 1u);
        {   int32_t m = ms >> 8;
            lr[2u * i] = inl + (((wet[0] - inl) * m) >> 7);
            lr[2u * i + 1u] = inr + (((wet[1] - inr) * m) >> 7);
        }
    }
    s->w = w; s->ph = ph; s->mix_s = ms;
}

/* main loop: bring each slot to the patch's type (clear the delay line, the mix starts from 0 and ramps in) */
static void fx_sync(void)
{
    uint32_t i;
    for (i = 0; i < FX_SLOTS; i++) {
        uint32_t want = (uint32_t)patch_get(P_FX1_TYPE + 4u * i);
        fx_slot_t *s = &fxs[i];
        if (want == s->type && fx_chain[i].fn) continue;
        fx_chain[i].on = 0;
        s->type = FXT_NONE;
        { uint32_t j; for (j = 0; j < FX_DLEN; j++) s->buf[j] = 0; }
        s->w = 0; s->ph = 0; s->mix_s = 0; s->fb = 0; s->idx = (uint8_t)i;
        fx_chain[i].fn = fx_slot_run;
        fx_chain[i].state = s;
        s->type = (uint8_t)want;
        fx_chain[i].on = want != FXT_NONE;
    }
}
