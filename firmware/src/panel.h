/* SPDX-License-Identifier: GPL-3.0-only */
/* panel.h: the front panel of the v1 synth (T18): what each knob, encoder and button does on each page, the factory
 * patches, the settings struct's patch overlay, and the filling of ui_view_t for ui_screens.h. Included once from
 * vavewave.c (after engine.h and ui_screens.h) when BENCH is 0. Main loop only; never from the ISR. No registers.
 *
 * Data flow: encoders / buttons -> patch_set() (patch.h) -> patch.p[] (the ISR reads it once per block). The screen
 * is filled from patch.p[], so a MIDI CC that moves a value is drawn too. Nothing here reads the voices.
 *
 * Pages (button): HOME (look 2 spectrum / 1 waterfall / 3 minimal), EDIT (OSC1 OSC2 SUB NOISE), FX (the filter),
 * ENV (amp / filter), LFO, SEL (voices), GLO (global). The four knobs are fixed per page and set (PG below). */
#pragma once

#define PN_PRESETS 8u                           /* factory patches: entries 0..7 */
#define PN_ENTRIES (PN_PRESETS + PL_IDS)         /* + the user patches of the log (patchlog.h): entries 8..39 = U01..U32 */
#define PN_NONE 0xFFu
#define SET_VER 1u

/* ---- factory patches: the defaults of patch.h (patch_reset) with these changed */
typedef struct {
    const char *name;                           /* 11 characters at most; "00N " is put in front */
    uint8_t t1, t2;
    int16_t pos1, pos2;
    uint8_t lvl2;
    int8_t semi2, fine2;
    uint8_t cut, res;
    int8_t fenv;
    uint8_t a1, d1, s1, r1, a2, d2, s2, r2;
    uint8_t sub, vmode, glide;
    int8_t warp, menv_pos;
    uint8_t lshape, lrate, lpit, lpos;
} preset_t;

static const preset_t PRESET[PN_PRESETS] = {
    /* name           t1 t2  pos1  pos2 lvl2 semi fine cut res fenv  A1  D1  S1  R1  A2  D2  S2  R2 sub vm gl warp mpos lsh lrt lpt lps */
    {"BRIGHT PAD",     0, 0, 1500, 1700, 80,   0,   9,  90, 25,  12,  70,  90, 100,  90,  70,  90,  40,  80,  0, 0,  0,   0,   0, 1, 60,  0,  0},
    {"GLASS BELL",     7, 7, 1000, 2200, 60,  12,   0, 120, 10,   0,   0, 100,   0, 100,   0,  70,   0,  70,  0, 0,  0,   0,   0, 1, 60,  0,  0},
    {"WARM VOWEL",     3, 3, 2000, 2400, 70,   0,  -9,  80, 30,   0,  80,  90, 110,  85,   0,  70,   0,  70,  0, 0,  0,   0,   0, 0, 40,  0, 20},
    {"FOLD LEAD",      5, 0, 2500,  600, 50,   0,   9, 105, 40,  20,  20,  70,  90,  70,   0,  80,   0,  70,  0, 1, 30,  20,   0, 1, 60,  0,  0},
    {"SUB BASS",       0, 0,    0,    0,  0,   0,   0,  60, 35,  30,   5,  70, 100,  50,   0,  70,   0,  60, 100, 1,  0,   0,   0, 1, 60,  0,  0},
    {"ORGAN KEYS",     6, 6, 1000, 3000, 50,  12,   0, 127,  0,   0,   8,  60, 127,  55,   0,  70,   0,  70,  0, 0,  0,   0,   0, 1, 60,  0,  0},
    {"FM PLUCK",       4, 4, 3600, 3600, 60,   0,   9, 110, 15,   0,   0,  80,  20,  70,   0,  75,   0,  70,  0, 0,  0,   0, -50, 1, 60,  0,  0},
    {"REED SOLO",     10, 10, 1200, 1400, 70,   0,  12, 100, 30,   0,  55,  80, 110,  70,   0,  70,   0,  70,  0, 0,  0,   0,   0, 0, 80,  8,  0},
};

static void preset_apply(uint32_t i)
{
    const preset_t *p = &PRESET[i % PN_PRESETS];
    patch_reset();
    patch_set(P_TABLE1, p->t1); patch_set(P_TABLE2, p->t2);
    patch_set(P_POS1, p->pos1); patch_set(P_POS2, p->pos2);
    patch_set(P_LEVEL2, p->lvl2); patch_set(P_SEMI2, p->semi2); patch_set(P_FINE2, p->fine2);
    patch_set(P_CUTOFF, p->cut); patch_set(P_RESO, p->res); patch_set(P_FENV, p->fenv);
    patch_set(P_A1, p->a1); patch_set(P_D1, p->d1); patch_set(P_S1, p->s1); patch_set(P_R1, p->r1);
    patch_set(P_A2, p->a2); patch_set(P_D2, p->d2); patch_set(P_S2, p->s2); patch_set(P_R2, p->r2);
    patch_set(P_SUB, p->sub); patch_set(P_VMODE, p->vmode); patch_set(P_GLIDE, p->glide);
    patch_set(P_WARP, p->warp); patch_set(P_MENV_POS, p->menv_pos);
    patch_set(P_LFO_SHAPE, p->lshape); patch_set(P_LFO_RATE, p->lrate);
    patch_set(P_LFO_PITCH, p->lpit); patch_set(P_LFO_POS, p->lpos);
}

/* ---- pages: four fixed knobs per page and set. NSUB[page] sets (EDIT: the four oscillators). */
enum { F_NONE, F_PCT, F_TIME, F_BIP, F_CENTS, F_FRAMES, F_RATE, F_POS, F_TXT, F_INT };
enum { TX_FMODE, TX_VMODE, TX_BDEST, TX_QUAL, TX_CURVE, TX_LSHAPE, TX_ARP, TX_ARATE, TX_N };
typedef struct { const char *lab; uint8_t id, fmt, step, tx; } kdef_t;
#define K(l, id, f, st, tx) {l, id, f, st, tx}
#define KN {"-", 0, F_NONE, 0, 0}

static const uint8_t NSUB[UI_P_N] = {1, 4, 2, 2, 2, 1, 3, 2, 1, 2};   /* ... GLO, ARP, SEQ, HELP */   /* HELP: two text pages (PG[HELP] stays empty) */
static const kdef_t PG[UI_P_N][4][4] = {
    [UI_P_HOME] = {{K("CUTOFF", P_CUTOFF, F_PCT, 1, 0), K("RESO", P_RESO, F_PCT, 1, 0), K("ATK", P_A1, F_TIME, 1, 0),
                    K("REL", P_R1, F_TIME, 1, 0)}},
    [UI_P_EDIT] = {{K("POS", P_POS1, F_POS, 8, 0), K("DETUNE", P_FINE1, F_CENTS, 1, 0), K("LEVEL", P_LEVEL1, F_PCT, 1, 0),
                    K("BEND", P_WARP, F_BIP, 1, 0)},
                   {K("POS", P_POS2, F_POS, 8, 0), K("DETUNE", P_FINE2, F_CENTS, 1, 0), K("LEVEL", P_LEVEL2, F_PCT, 1, 0),
                    K("BEND", P_WARP, F_BIP, 1, 0)},
                   {KN, KN, K("LEVEL", P_SUB, F_PCT, 1, 0), K("BEND", P_WARP, F_BIP, 1, 0)},
                   {KN, KN, K("LEVEL", P_NOISE, F_PCT, 1, 0), K("BEND", P_WARP, F_BIP, 1, 0)}},
    [UI_P_FX] = {{K("CUTOFF", P_CUTOFF, F_PCT, 1, 0), K("RESO", P_RESO, F_PCT, 1, 0), K("FENV", P_FENV, F_BIP, 1, 0),
                  K("KEYTRK", P_KEYTRK, F_PCT, 1, 0)},
                 {K("MODE", P_FMODE, F_TXT, 1, TX_FMODE), K("FVEL", P_FVEL, F_PCT, 1, 0), K("E>POS", P_MENV_POS, F_FRAMES, 1, 0),
                  K("E>BEND", P_MENV_WARP, F_BIP, 1, 0)}},
    [UI_P_ENV] = {{K("ATK", P_A1, F_TIME, 1, 0), K("DEC", P_D1, F_TIME, 1, 0), K("SUS", P_S1, F_PCT, 1, 0),
                   K("REL", P_R1, F_TIME, 1, 0)},
                  {K("ATK", P_A2, F_TIME, 1, 0), K("DEC", P_D2, F_TIME, 1, 0), K("SUS", P_S2, F_PCT, 1, 0),
                   K("REL", P_R2, F_TIME, 1, 0)}},
    [UI_P_LFO] = {{K("SHAPE", P_LFO_SHAPE, F_TXT, 1, TX_LSHAPE), K("RATE", P_LFO_RATE, F_RATE, 1, 0),
                   K(">POS", P_LFO_POS, F_PCT, 1, 0), K(">PITCH", P_LFO_PITCH, F_PCT, 1, 0)},
                  {K("SHAPE", P_LFO_SHAPE, F_TXT, 1, TX_LSHAPE), K("RATE", P_LFO_RATE, F_RATE, 1, 0),
                   K(">CUT", P_LFO_CUT, F_PCT, 1, 0), K(">AMP", P_LFO_AMP, F_PCT, 1, 0)}},
    [UI_P_SEL] = {{K("MODE", P_VMODE, F_TXT, 1, TX_VMODE), K("BEND>", P_WARP_DEST, F_TXT, 1, TX_BDEST),
                   K("QUAL", P_QUALITY, F_TXT, 1, TX_QUAL), K("CURVE", P_ECURVE, F_TXT, 1, TX_CURVE)}},
    [UI_P_GLO] = {{K("VOLUME", P_VOLUME, F_PCT, 1, 0), K("DRIVE", P_DRIVE, F_PCT, 1, 0), K("GLIDE", P_GLIDE, F_PCT, 1, 0),
                   K("PB RNG", P_BEND_RANGE, F_INT, 1, 0)},
                  {K("MW>POS", P_MW_POS, F_PCT, 1, 0), K("MW>VIB", P_MW_VIB, F_PCT, 1, 0), K("AT>CUT", P_AT_CUT, F_PCT, 1, 0),
                   K("VEL>AMP", P_VEL_AMP, F_PCT, 1, 0)},
                  {KN, KN, KN, KN}},            /* set 3: BOOT KEYS (no knobs; ui_boot_pic) */
    [UI_P_ARP] = {{K("MODE", P_ARP, F_TXT, 1, TX_ARP), K("RATE", P_ARP_RATE, F_TXT, 1, TX_ARATE), K("OCT", P_ARP_OCT, F_INT, 1, 0),
                   K("GATE", P_ARP_GATE, F_PCT, 1, 0)},
                  {K("SWING", P_ARP_SWING, F_INT, 1, 0), K("TEMPO", P_TEMPO, F_INT, 1, 0), KN, KN}},
    [UI_P_SEQ] = {{K("RATE", P_ARP_RATE, F_TXT, 1, TX_ARATE), K("GATE", P_ARP_GATE, F_PCT, 1, 0), K("SWING", P_ARP_SWING, F_INT, 1, 0),
                   K("TEMPO", P_TEMPO, F_INT, 1, 0)}},
};

static const char *const TXT[TX_N][8] = {
    {"LP", "BP", "HP"}, {"POLY", "MONO", "LEGATO"}, {"BOTH", "OSC1", "OSC2"}, {"HI", "ECO"}, {"LIN", "EXP"},
    {"SINE", "TRI", "SAW", "SQUARE", "S&H"},
    {"OFF", "UP", "DOWN", "UP-DN", "UP-DN2", "RANDOM", "PLAYED", "CHORD"}, {"1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32"}};

/* ---- panel state (main loop only) */
static struct {
    uint8_t page, look, sub, touched;
    uint8_t slot, browse, browse_pend;          /* entry loaded (0..7 factory, 8.. user); entry shown in the name line; load pending */
    uint8_t save_req, home_arm;                 /* HOME pressed and not yet decided: short press or hold */
    uint8_t seq_arm;                            /* SEQ pressed and not yet decided (short: on / off, hold: the page) */
    uint32_t t_seq, t_seq_tog;
    uint8_t arp_arm, arp_last;                  /* ARP pressed and not yet decided (short: on / off, hold: the page); the mode it switches back to */
    uint32_t t_arp, t_arp_tog;                  /* press time; time of the last switch */
    uint8_t sv, act, act_id, tgt;               /* SAVE gesture (SV_*), the request for pn_save_poll (ACT_*, user id), the chosen target id */
    uint32_t t_browse, t_flash, t_save, t_home, t_sv;
    char fmsg[12];                              /* the confirmation shown while t_flash runs */
    char uname[PL_IDS][PL_NAME + 1];            /* names of the user patches in the log */
    int16_t base[P_COUNT];                      /* the patch as loaded / saved: "modified" = differs */
    int16_t wave[256];                          /* EDIT: the frame as the engine reads it */
    uint32_t wave_key;
} pn;
static ui_view_t pn_view;
static ui_shadow_t pn_shadow;

/* the look order HOME cycles: spectrum (2) -> waterfall (1) -> minimal (3) -> spectrum */
static const uint8_t LOOK_NEXT[UI_H_N] = {UI_H_MINIMAL, UI_H_WATERFALL, UI_H_SPECTRUM};

static void pn_snapshot(void)
{
    uint32_t i;
    for (i = 0; i < P_COUNT; i++) pn.base[i] = (int16_t)patch.p[i];
}
static int pn_modified(void)
{
    uint32_t i;
    for (i = 0; i < P_COUNT; i++)
        if (patch.p[i] != pn.base[i]) return 1;
    return 0;
}

enum { SV_IDLE, SV_ARMED, SV_PICK, SV_CONFIRM, SV_DEL };       /* the SAVE gesture */
enum { ACT_NONE, ACT_SAVE, ACT_DEL };

typedef char pn_params_fit[P_COUNT <= PL_MAXP ? 1 : -1];     /* a patch must fit a log record (more: raise PL_MAXP; and PL_IDS * record size must stay under one sector, else STORE FULL comes early: widen the window, docs/PATCH-STORAGE-DESIGN.md) */
static pl_patch_t pn_tmp;                       /* one patch record in RAM (loading, saving) */
static void pn_ucache(uint32_t id)              /* the name of user patch id from the log (empty when none) */
{
    pn.uname[id][0] = 0;
    if (id < PL_IDS && pl_get(id, &pn_tmp) == PL_OK) { uint32_t i; for (i = 0; i <= PL_NAME; i++) pn.uname[id][i] = pn_tmp.name[i]; }
}
static uint32_t pn_entry_ok(uint32_t e) { return e < PN_PRESETS || (e < PN_ENTRIES && (pl_present() >> (e - PN_PRESETS) & 1u)); }

/* load entry i: a factory patch (then the user's old settings overlay when it replaces this slot) or a user patch of the
 * log; a user patch that is gone or damaged loads patch 1 */
static void pn_load_raw(uint32_t i, const app_set_t *st)
{
    uint32_t p;
    if (i >= PN_PRESETS) {
        if (i < PN_ENTRIES && pl_get(i - PN_PRESETS, &pn_tmp) == PL_OK) {
            patch_reset();                      /* parameters a record does not have (older firmware) keep their defaults */
            for (p = 0; p < pn_tmp.n && p < P_COUNT; p++) patch_set(p, pn_tmp.v[p]);
            pn_snapshot();
            pn.slot = pn.browse = (uint8_t)i;
            pn.browse_pend = 0;
            pn.save_req = 0;
            return;
        }
        i = 0;
    }
    preset_apply(i);
    if (st->uslot == i)
        for (p = 0; p < P_COUNT_V1; p++) patch_set(p, st->uval[p]);
    pn_snapshot();
    pn.slot = pn.browse = (uint8_t)i;
    pn.browse_pend = 0;
    pn.save_req = 0;
}

/* the arp is a performance switch: a patch that has it off (all the factory ones) leaves it as it is, so changing the patch
 * while an arp plays does not stop it. A patch saved with an arp mode brings its own. */
static void pn_load(uint32_t i, const app_set_t *st)
{
    int32_t arp = patch_get(P_ARP), seq = patch_get(P_SEQ);
    pn_load_raw(i, st);
    if (arp && !patch_get(P_ARP)) { patch_set(P_ARP, arp); pn.base[P_ARP] = (int16_t)arp; }
    if (seq && !patch_get(P_SEQ)) { patch_set(P_SEQ, seq); pn.base[P_SEQ] = (int16_t)seq; }
}

/* the settings read back from flash: a bad one is the defaults (version byte, ranges) */
static void pn_validate(app_set_t *st)
{
    uint32_t i, bad = st->ver != SET_VER || st->oct < -2 || st->oct > 2 || st->look >= UI_H_N || st->slot >= PN_ENTRIES;
    if (!bad && st->uslot != PN_NONE) {
        bad = st->uslot >= PN_PRESETS;
        for (i = 0; i < P_COUNT_V1 && !bad; i++)
            bad = st->uval[i] < PDESC[i].min || st->uval[i] > PDESC[i].max;
        if (bad) st->uslot = PN_NONE;           /* keep the rest of the settings, drop the patch overlay */
        bad = 0;
    }
    if (bad) {
        st->ver = SET_VER;
        st->oct = 0;
        st->look = UI_H_SPECTRUM;               /* the default look: 2 */
        st->slot = 0;
        st->uslot = PN_NONE;
        for (i = 0; i < P_COUNT_V1; i++) st->uval[i] = PDESC[i].def;
    }
}

/* the single patch the old SAVE kept in the settings sector (an overlay on a factory patch) becomes user patch U01, once,
 * when the log is empty; the overlay is cleared only after the log took it */
static void pn_migrate(app_set_t *st, uint32_t *dirty)
{
    uint32_t i;
    if (!pl.ready || st->uslot >= PN_PRESETS || pl_present()) return;
    for (i = 0; i < PL_NAME && PRESET[st->uslot].name[i]; i++) pn_tmp.name[i] = PRESET[st->uslot].name[i];
    pn_tmp.name[i] = 0;
    pn_tmp.n = P_COUNT_V1;
    for (i = 0; i < P_COUNT_V1; i++) pn_tmp.v[i] = st->uval[i];
    if (pl_put(0, &pn_tmp) != PL_OK) return;     /* the overlay stays and keeps working */
    if (st->slot == st->uslot) st->slot = (uint8_t)PN_PRESETS;    /* the patch in use was that one: it is U01 now */
    st->uslot = PN_NONE;
    *dirty = 1;
}

static void pn_init(app_set_t *st)
{
    uint32_t i;
    for (i = 0; i < PL_IDS; i++) pn_ucache(i);
    pn.page = UI_P_HOME;
    pn.look = st->look;
    pn.sub = 0;
    pn.touched = 4;
    pn_load(st->slot, st);
}


/* ---- one value as text (<= 7 characters) */
static void pn_fmt(char *d, const kdef_t *k, int32_t v)
{
    switch (k->fmt) {
    case F_PCT: ui_num(d, v * 100 / 127); break;
    case F_TIME: ui_fmt_time(d, (uint32_t)v); break;
    case F_POS: ui_num(d, v * 100 / 4095); break;
    case F_CENTS: v = v * 100 / 128; if (v > 0) *d++ = '+'; ui_num(d, v); break;
    case F_FRAMES: v = v / 2; if (v > 0) *d++ = '+'; ui_num(d, v); break;
    case F_BIP: if (v > 0) *d++ = '+'; ui_num(d, v); break;
    case F_RATE: {                              /* 0.02 .. 40 Hz: 0.02 * 2^(n / 16), n = 1.383 v */
        static const uint16_t T16[16] = {256, 267, 279, 291, 304, 317, 331, 345, 362, 378, 395, 412, 431, 449, 470, 490};
        uint32_t n = ((uint32_t)v * 177u) >> 7, h = (((uint32_t)T16[n & 15u] << (n >> 4)) * 2u) >> 8;   /* x 0.01 Hz */
        if (h < 100u) { *d++ = '0'; *d++ = '.'; *d++ = (char)('0' + h / 10u); *d++ = (char)('0' + h % 10u); }
        else if (h < 1000u) { d = ui_num(d, (int32_t)(h / 100u)); *d++ = '.'; *d++ = (char)('0' + h / 10u % 10u); }
        else d = ui_num(d, (int32_t)(h / 100u));
        ui_cat(d, "HZ");
        break;
    }
    case F_TXT: ui_cat(d, TXT[k->tx][v]); break;
    case F_INT: ui_num(d, v); break;
    default: *d = 0; break;
    }
}

/* ---- EDIT's waveform: the frame the engine reads for this oscillator, morphed to the position, through the BEND
 * map. The map is eng_warp_preview() (synth.h): a pure function of (phase, warp) over const tables; it touches no
 * voice, filter or ring state, so the main loop calls it without any locking. patch.p[] reads are single 32-bit
 * loads (one writer: this loop and the MIDI CC handler, both in the main loop). */
static uint32_t pn_edit_wave(uint32_t sub)
{
    uint32_t i, key;
    if (sub >= 2u) {
        static uint32_t seed = 12345u;
        key = 0x80000000u | sub;
        if (key == pn.wave_key) return key;
        for (i = 0; i < 256u; i++) {
            if (sub == 2u) {
                int32_t a = UI_SIN64[i >> 2], b = UI_SIN64[((i >> 2) + 1u) & 63u];
                int32_t s = (a + ((b - a) * (int32_t)(i & 3u) >> 2)) * 2;
                pn.wave[i] = (int16_t)(s > 32767 ? 32767 : s < -32767 ? -32767 : s);
            } else {
                seed = seed * 1664525u + 1013904223u;
                pn.wave[i] = (int16_t)(((int32_t)(seed >> 16) - 32768) >> 1);
            }
        }
        return key;
    } else {
        uint32_t t = (uint32_t)patch_get(sub ? P_TABLE2 : P_TABLE1), pos = (uint32_t)patch_get(sub ? P_POS2 : P_POS1);
        int32_t warp = patch_get(P_WARP), dest = patch_get(P_WARP_DEST);
        int apply = warp && (dest == 0 || dest == (int32_t)sub + 1);
        key = t | pos << 5 | (uint32_t)(apply ? warp & 127 : 0) << 17 | sub << 24;      /* t 5 bits, pos 12, warp 7, sub 1 */
        if (key == pn.wave_key) return key;
        {
            uint32_t stride, P = pos * 31u >> 4, f0 = P >> 8, a = P & 255u, f1 = f0 < 31u ? f0 + 1u : f0;
            const int16_t *fr = eng_frames(t, &stride), *A = fr + f0 * stride, *B = fr + f1 * stride;
            for (i = 0; i < 256u; i++) {
                uint32_t ph = i << 24, idx, fq, j;
                int32_t s0, s1;
                if (apply) ph = eng_warp_preview(ph, warp);
                idx = ph >> 24;
                fq = ph >> 16 & 255u;
                j = (idx + 1u) & 255u;
                s0 = (A[idx] * (int32_t)(256u - a) + B[idx] * (int32_t)a) >> 8;
                s1 = (A[j] * (int32_t)(256u - a) + B[j] * (int32_t)a) >> 8;
                pn.wave[i] = (int16_t)(s0 + ((s1 - s0) * (int32_t)fq >> 8));
            }
        }
        return key;
    }
}

/* ---- inputs ---- */
static int32_t pn_accel(int32_t d)              /* a quick turn goes faster: d * |d| */
{
    int32_t a = d < 0 ? -d : d;
    return a > 1 ? d * a : d;
}

static void pn_page(uint32_t page)              /* a page button: the page, or its next set when already there */
{
    if (pn.page == page) pn.sub = (uint8_t)((pn.sub + 1u) % NSUB[page]);
    else { pn.page = (uint8_t)page; pn.sub = 0; }
    pn.touched = 4;
}

/* ---- SAVE: a short press saves to the patch's own user id (or the first free one, from a factory patch); holding SAVE and
 * turning PRESETS picks another id (an occupied one asks again: press SAVE once more); holding SAVE for 2 s on a user patch
 * asks to delete it (press SAVE once more). Any other button or a PRESETS turn cancels a question, as do 5 s. */
static int pn_default_target(void)               /* user id 0..31, or -1 when the log is full of ids */
{
    uint32_t id, have = pl_present();
    if (pn.slot >= PN_PRESETS) return (int)(pn.slot - PN_PRESETS);
    for (id = 0; id < PL_IDS; id++)
        if (!(have >> id & 1u)) return (int)id;
    return -1;
}
static void pn_msg(const char *m, uint32_t id, uint32_t with_id)
{
    char *d = pn.fmsg;
    uint32_t n = 0;
    while (m[n] && n < (with_id ? 7u : 11u)) *d++ = m[n++];
    if (with_id) { *d++ = ' '; *d++ = 'U'; *d++ = (char)('0' + (id + 1u) / 10u); *d++ = (char)('0' + (id + 1u) % 10u); }
    *d = 0;
    pn.t_flash = fm1_ms + 900u;
}
static void pn_save_buttons(uint32_t b, uint32_t held)
{
    uint32_t down = held >> BTN_SAVE & 1u;
    if (b & ~(1u << BTN_SAVE) && (pn.sv == SV_CONFIRM || pn.sv == SV_DEL)) pn.sv = SV_IDLE;
    if (b & 1u << BTN_SAVE) {
        if (pn.sv == SV_CONFIRM) { pn.act = ACT_SAVE; pn.act_id = pn.tgt; pn.save_req = 1; pn.sv = SV_IDLE; return; }
        if (pn.sv == SV_DEL) { pn.act = ACT_DEL; pn.act_id = (uint8_t)(pn.slot - PN_PRESETS); pn.save_req = 1; pn.sv = SV_IDLE; return; }
        pn.sv = SV_ARMED;
        pn.t_sv = fm1_ms;
    }
    if ((pn.sv == SV_CONFIRM || pn.sv == SV_DEL) && fm1_ms - pn.t_sv > 5000u) pn.sv = SV_IDLE;
    if (pn.sv == SV_ARMED || pn.sv == SV_PICK) {
        if (!down) {                                            /* released */
            if (pn.sv == SV_ARMED) {
                int t = pn_default_target();
                if (!pn_modified()) { pn.act = ACT_NONE; pn.save_req = 1; }          /* nothing to save: the confirmation only */
                else if (t < 0) { pn_msg("STORE FULL", 0, 0); }
                else { pn.act = ACT_SAVE; pn.act_id = (uint8_t)t; pn.save_req = 1; }
                pn.sv = SV_IDLE;
            } else if (pl_present() >> pn.tgt & 1u) { pn.sv = SV_CONFIRM; pn.t_sv = fm1_ms; }
            else { pn.act = ACT_SAVE; pn.act_id = pn.tgt; pn.save_req = 1; pn.sv = SV_IDLE; }
        } else if (pn.sv == SV_ARMED && fm1_ms - pn.t_sv >= 2000u && pn.slot >= PN_PRESETS) {
            pn.sv = SV_DEL;                                     /* still held: delete this user patch? */
            pn.t_sv = fm1_ms;
        }
    }
}

/* buttons pressed (bits) and held; returns the SAVE request through pn.save_req. EDIT held + HOME resets the CPU max
 * and leaves the page alone. */
static void pn_buttons(uint32_t b, uint32_t held, app_set_t *st, uint32_t *dirty)
{
    if (b & 1u << BTN_EDIT) pn_page(UI_P_EDIT);
    if (b & 1u << BTN_FX) pn_page(UI_P_FX);
    if (b & 1u << BTN_ENV) pn_page(UI_P_ENV);
    if (b & 1u << BTN_LFO) pn_page(UI_P_LFO);
    if (b & 1u << BTN_SEL) pn_page(UI_P_SEL);
    if (b & 1u << BTN_GLO) pn_page(UI_P_GLO);
    if (b & 1u << BTN_ARP) {
        if (!pn.arp_arm && (!pn.t_arp_tog || fm1_ms - pn.t_arp_tog >= 200u)) { pn.arp_arm = 1; pn.t_arp = fm1_ms; }     /* (a bounce right after a switch is not a new press) */
    }
    if (pn.arp_arm) {
        if (!(held >> BTN_ARP & 1u)) {                              /* released: a short press switches the arp */
            pn.arp_arm = 0;
            if (patch_get(P_ARP)) { pn.arp_last = (uint8_t)patch_get(P_ARP); patch_set(P_ARP, 0); }
            else patch_set(P_ARP, pn.arp_last ? pn.arp_last : 1);
            pn.t_arp_tog = fm1_ms ? fm1_ms : 1u;
            pn.base[P_ARP] = (int16_t)patch.p[P_ARP];               /* the button is a performance switch, not a patch edit: SAVE does not blink for it */
        } else if (fm1_ms - pn.t_arp >= 500u) { pn.arp_arm = 0; pn_page(UI_P_ARP); }   /* held: its page (again: the next set) */
    }
    if (b & 1u << BTN_SEQ && !pn.seq_arm && (!pn.t_seq_tog || fm1_ms - pn.t_seq_tog >= 200u)) { pn.seq_arm = 1; pn.t_seq = fm1_ms; }
    if (pn.seq_arm) {
        if (!(held >> BTN_SEQ & 1u)) {                              /* released: a short press switches the sequencer */
            pn.seq_arm = 0;
            patch_set(P_SEQ, patch_get(P_SEQ) ? 0 : 1);
            pn.t_seq_tog = fm1_ms ? fm1_ms : 1u;
            pn.base[P_SEQ] = (int16_t)patch.p[P_SEQ];
        } else if (fm1_ms - pn.t_seq >= 500u) { pn.seq_arm = 0; pn_page(UI_P_SEQ); }
    }
    if (b & 1u << BTN_HOME) {
        if (held >> BTN_EDIT & 1u) cpu_reset_req++;                 /* the max starts over */
        else if (pn.page == UI_P_HELP) {                            /* next help page, then out */
            if (pn.sub + 1u < NSUB[UI_P_HELP]) pn.sub++;
            else { pn.page = UI_P_HOME; pn.sub = 0; pn.touched = 4; }
        } else if (pn.page == UI_P_HOME) {                          /* decided on release: a hold is HELP */
            pn.home_arm = 1;
            pn.t_home = fm1_ms;
        } else {
            pn.page = UI_P_HOME; pn.sub = 0; pn.touched = 4;
            pn.home_arm = 2;                                        /* only a hold counts */
            pn.t_home = fm1_ms;
        }
    }
    if (pn.home_arm) {
        if (!(held >> BTN_HOME & 1u)) {                             /* released: a short press */
            if (pn.home_arm == 1u) {
                pn.look = LOOK_NEXT[pn.look % UI_H_N];
                st->look = pn.look;
                *dirty = 1;
            }
            pn.home_arm = 0;
        } else if (fm1_ms - pn.t_home >= 700u) {                    /* held: the help screens */
            pn.home_arm = 0;
            pn.page = UI_P_HELP;
            pn.sub = 0;
            pn.touched = 4;
        }
    }
    pn_save_buttons(b, held);
}

/* ALGORITHM steps through the 12 factory tables, then the user tables that exist (empty slots and damaged ones are skipped) */
static uint32_t ut_listed(uint32_t t) { return t < UT_FIRST || (t < UT_NTABLES && wt_dir[t - UT_FIRST].valid && wt_dir[t - UT_FIRST].state != UT_BAD); }
static void pn_table_step(uint32_t id, int32_t d)
{
    int32_t t = patch_get(id), step = d > 0 ? 1 : -1, n = d > 0 ? d : -d;
    while (n--) {
        int32_t u = t;
        do u += step; while (u >= 0 && u < (int32_t)UT_NTABLES && !ut_listed((uint32_t)u));
        if (u < 0 || u >= (int32_t)UT_NTABLES) break;       /* the ends stop, as before */
        t = u;
    }
    patch_set(id, t);
}
static const char *pn_tname(uint32_t t)
{
    if (t < UT_FIRST) return WTG_NAME[t % WTG_TABLES];
    if (t >= UT_NTABLES || !wt_dir[t - UT_FIRST].valid) return "EMPTY";
    return wt_dir[t - UT_FIRST].state == UT_BAD ? "BAD" : wt_dir[t - UT_FIRST].name;
}

/* the oscillator the table / position encoders act on: OSC2 only while its EDIT chip is shown */
static uint32_t pn_osc(void) { return pn.page == UI_P_EDIT && pn.sub == 1u; }

static void pn_encoders(void)
{
    uint32_t i, o = pn_osc();
    int32_t d;
    if ((d = fm1_enc_take(ENC_ALGORITHM)) != 0) pn_table_step(o ? P_TABLE2 : P_TABLE1, d);
    if ((d = fm1_enc_take(ENC_SELECT)) != 0) patch_add(o ? P_POS2 : P_POS1, pn_accel(d) * 32);
    for (i = 0; i < 4u; i++) {
        const kdef_t *k = &PG[pn.page][pn.sub][i];
        d = fm1_enc_take(ENC_KNOB1 + i);
        if (!d || k->fmt == F_NONE) continue;
        patch_add(k->id, (k->fmt == F_TXT ? d : pn_accel(d)) * k->step);
        pn.touched = (uint8_t)i;
    }
    if ((d = fm1_enc_take(ENC_PRESETS)) != 0) {
        if ((pn.sv == SV_ARMED || pn.sv == SV_PICK || pn.sv == SV_DEL) && (fm1_in.buttons >> BTN_SAVE & 1u)) {   /* SAVE held: choose the target id */
            int32_t t = pn.sv == SV_PICK ? (int32_t)pn.tgt : pn_default_target() < 0 ? 0 : pn_default_target();
            t += d > 0 ? 1 : -1;
            pn.tgt = (uint8_t)(t < 0 ? 0 : t >= (int32_t)PL_IDS ? (int32_t)PL_IDS - 1 : t);
            pn.sv = SV_PICK;
        } else {                                     /* browse: the name now, the sound after 400 ms of quiet */
            int32_t n = (int32_t)pn.browse, step = d > 0 ? 1 : -1, k = d > 4 ? 4 : d < -4 ? 4 : d < 0 ? -d : d;
            if (pn.sv == SV_CONFIRM || pn.sv == SV_DEL) pn.sv = SV_IDLE;
            while (k--) {                            /* factory patches, then the user patches that exist; the ends stop */
                int32_t u = n;
                do u += step; while (u >= 0 && u < (int32_t)PN_ENTRIES && !pn_entry_ok((uint32_t)u));
                if (u < 0 || u >= (int32_t)PN_ENTRIES) break;
                n = u;
            }
            if (n != (int32_t)pn.browse) { pn.browse = (uint8_t)n; pn.browse_pend = 1; }
            pn.t_browse = fm1_ms;
        }
    }
}

/* the patch the name line shows */
static void pn_uid(char *d, uint32_t id)         /* "U05" */
{
    *d++ = 'U'; *d++ = (char)('0' + (id + 1u) / 10u); *d++ = (char)('0' + (id + 1u) % 10u); *d = 0;
}
static void pn_name(char *d, uint32_t e)
{
    if (e >= PN_PRESETS) {                      /* "U05 NAME" */
        pn_uid(d, e - PN_PRESETS);
        d[3] = ' ';
        ui_copy(d + 4, pn.uname[(e - PN_PRESETS) % PL_IDS], 12);
        return;
    }
    *d++ = '0'; *d++ = '0'; *d++ = (char)('1' + e); *d++ = ' ';
    ui_copy(d, PRESET[e].name, 12);
}

/* ---- the view (ui_screens.h reads it): everything the screen shows, from the patch and the page */
static void pn_view_fill(uint32_t cpu, uint32_t cpu_max, uint32_t voices, uint32_t flash)
{
    ui_view_t *v = &pn_view;
    uint32_t i, o = pn_osc(), t, stride, pos, kbip = 0, ksmall = 0;
    const int16_t *fr;
    v->page = pn.page;
    v->variant = pn.look;
    v->sub = pn.sub;
    v->touched = pn.touched;
    v->voices = (uint8_t)(voices > 99u ? 99u : voices);
    v->voices_max = ENG_NV;
    v->cpu = (uint8_t)(cpu > 255u ? 255u : cpu);
    v->cpu_max = (uint8_t)(cpu_max > 255u ? 255u : cpu_max);
    v->modified = (uint8_t)(!pn.browse_pend && pn_modified());
    v->flash = (uint8_t)flash;
    v->ms = fm1_ms;
    v->nframes = 32;
    v->stride = (uint16_t)WTG_FRAME_LEN;

    t = (uint32_t)patch_get(o ? P_TABLE2 : P_TABLE1);
    pos = (uint32_t)patch_get(o ? P_POS2 : P_POS1);
    fr = eng_frames(t, &stride);
    v->table = (uint8_t)t;
    v->pos = (uint16_t)pos;
    v->frame = (uint8_t)((pos * 31u + 2048u) >> 12);        /* the nearest of the 32 frames (the engine maps pos * 31 / 4096) */
    v->frames = fr;
    v->wave2 = fr + (uint32_t)v->frame * stride + WTG_LEVEL_OFF[2];
    ui_copy(v->tname, pn_tname(t), 9);
    if (pn.sv == SV_PICK) { char *d = ui_cat(v->patch, "SAVE TO "); pn_uid(d, pn.tgt); if (pl_present() >> pn.tgt & 1u) ui_cat(d + 3, "*"); }
    else if (pn.sv == SV_CONFIRM) { char *d = ui_cat(v->patch, "REPLACE "); d = ui_cat(d, "") ; pn_uid(d, pn.tgt); ui_cat(d + 3, "?"); }
    else if (pn.sv == SV_DEL) { char *d = ui_cat(v->patch, "DELETE "); pn_uid(d, pn.slot - PN_PRESETS); ui_cat(d + 3, "?"); }
    else pn_name(v->patch, pn.browse);
    ui_copy(v->fmsg, pn.fmsg, 12);

    for (i = 0; i < 4u; i++) {
        const kdef_t *k = &PG[pn.page][pn.sub][i];
        int32_t val;
        v->klabel[i] = k->lab;
        if (k->fmt == F_NONE) { v->kval[i][0] = 0; v->kfrac[i] = 0; continue; }
        val = patch_get(k->id);
        pn_fmt(v->kval[i], k, val);
        v->kfrac[i] = (uint8_t)(((val - PDESC[k->id].min) * 255) / (PDESC[k->id].max - PDESC[k->id].min));
        if (PDESC[k->id].min < 0) kbip |= 1u << i;
        if (k->fmt == F_TIME || k->fmt == F_TXT || k->fmt == F_RATE) ksmall |= 1u << i;
    }
    {   uint32_t m = 0, n = sq.rec ? sq.cur : sq.len;       /* recording: the steps entered so far */
        for (i = 0; i < n && i < 32u; i++) if (sq.vel[i]) m |= 1u << i;
        v->seq_mask = m; v->seq_len = (uint8_t)n; v->seq_rec = sq.rec; v->seq_on = (uint8_t)(sq_tap.on != 0u && sq.len != 0u);
        v->seq_pos = sq.rec ? sq.cur : (uint8_t)sq_tap.step;
    }
    v->kbip = (uint8_t)kbip;
    v->ksmall = (uint8_t)ksmall;

    v->env[0][0] = (uint8_t)patch_get(P_A1); v->env[0][1] = (uint8_t)patch_get(P_D1);
    v->env[0][2] = (uint8_t)patch_get(P_S1); v->env[0][3] = (uint8_t)patch_get(P_R1);
    v->env[1][0] = (uint8_t)patch_get(P_A2); v->env[1][1] = (uint8_t)patch_get(P_D2);
    v->env[1][2] = (uint8_t)patch_get(P_S2); v->env[1][3] = (uint8_t)patch_get(P_R2);

    v->wave = 0;
    if (pn.page == UI_P_EDIT) {
        pn.wave_key = pn_edit_wave(pn.sub);
        v->wave = pn.wave;
        v->wave_key = pn.wave_key;
    }
    {   /* GLO set 3: BOOT KEYS, the scan app_main copied at start */
        static const char HX[] = "0123456789ABCDEF";
        char *d = v->boot[0];
        uint32_t n;
        if (pn.page == UI_P_GLO && pn.sub == 2u) {
            d = ui_cat(d, "BTN 0x");
            for (n = 0; n < 4u; n++) *d++ = HX[boot_btn >> (12u - 4u * n) & 15u];
            d = ui_cat(d, " FR ");
            ui_num(d, (int32_t)(boot_frames > 99999u ? 99999u : boot_frames));
            d = ui_cat(v->boot[1], "RAW ");
            for (n = 0; n < 11u; n++) { *d++ = HX[boot_raw[n] >> 4]; *d++ = HX[boot_raw[n] & 15u]; }
            *d = 0;
        }
    }
    if (pn.page == UI_P_FX || pn.page == UI_P_LFO || pn.page == UI_P_SEL || pn.page == UI_P_GLO || pn.page == UI_P_ARP || pn.page == UI_P_SEQ) {
        uint32_t s, n = NSUB[pn.page];
        v->nsets = (uint8_t)n;
        v->set = pn.sub;
        v->flt_cut = (uint8_t)patch_get(P_CUTOFF);
        v->flt_res = (uint8_t)patch_get(P_RESO);
        v->flt_mode = (uint8_t)patch_get(P_FMODE);
        v->lfo_shape = (uint8_t)patch_get(P_LFO_SHAPE);
        v->drive = (uint8_t)patch_get(P_DRIVE);
        ui_copy(v->ptxt, pn.page == UI_P_ARP ? TXT[TX_ARP][patch_get(P_ARP)] : TXT[TX_VMODE][patch_get(P_VMODE)], 8);
        if (pn.page == UI_P_SEQ) {
            char *d = v->info;
            ui_copy(v->ptxt, sq.rec ? "RECORD" : patch_get(P_SEQ) ? "PLAY" : "OFF", 8);
            if (sq.rec) { d = ui_cat(d, "STEP "); d = ui_num(d, (int32_t)sq.cur + 1); d = ui_cat(d, " OF 32  PLAY=REST SEQ=TIE"); }
            else if (!sq.len) ui_cat(d, "NO PATTERN  REC TO ENTER STEPS");
            else { d = ui_cat(d, "LEN "); d = ui_num(d, sq.len); d = ui_cat(d, "  "); if (arp_tap.ext) d = ui_cat(d, "EXT"); else { d = ui_num(d, patch_get(P_TEMPO)); d = ui_cat(d, " BPM"); } }
        }
        if (pn.page == UI_P_ARP) {
            char *d = v->info;
            if (arp_tap.ext) {                  /* the clock's tempo, measured over the last quarter note */
                uint32_t bb = arp_tap.beat_blocks;
                d = ui_cat(d, "EXT ");
                if (bb >= 20u && bb < 400u) d = ui_num(d, (int32_t)((10336u + bb / 2u) / bb));
                else d = ui_cat(d, "--");
                d = ui_cat(d, " BPM");
            }
            else { d = ui_num(d, patch_get(P_TEMPO)); d = ui_cat(d, " BPM"); }
            d = ui_cat(d, "  ");
            d = ui_cat(d, TXT[TX_ARATE][patch_get(P_ARP_RATE)]);
        }
        for (s = 0; s < 2u; s++)
            for (i = 0; i < 4u; i++) {
                char *d = v->list[s * 4u + i];
                const kdef_t *k = &PG[pn.page][s < n ? s : 0][i];
                if (s >= n || k->fmt == F_NONE) { d[0] = 0; continue; }
                {
                    uint32_t l = 0;
                    while (k->lab[l] && l < 7u) { d[l] = k->lab[l]; l++; }
                    while (l < 8u) d[l++] = ' ';                /* label 8, value <= 6: 14 characters, 112 px */
                    pn_fmt(d + l, k, patch_get(k->id));
                }
            }
    }
}

/* ---- SAVE and DELETE, as pn_save_buttons decided (docs/PATCH-STORAGE-DESIGN.md section 7): the record goes into the
 * patch log (patchlog.h). The write does not wait for quiet (a latched note or the arp would block it; a compaction erase is one 17 ms gap) and happens at most once a second;
 * ACT_NONE (nothing changed since loaded / saved) only shows the confirmation. Returns 1 when a message is shown. A failed
 * write shows SAVE FAILED, keeps the patch modified and drops the request (no retry loop). */
static int pn_save_poll(app_set_t *st, uint32_t *dirty)
{
    uint32_t i, id;
    if (!pn.save_req) return 0;
    if (pn.act != ACT_NONE) {
        if (fm1_ms - pn.t_save < 1000u) return 0;                  /* try again in a later pass; the LED keeps blinking (not waiting for quiet: with a latched note or the arp it would never come; a compaction erase costs one 17 ms gap) */
        pn.t_save = fm1_ms;
        id = pn.act_id % PL_IDS;
        if (pn.act == ACT_SAVE) {
            int rc;
            const char *src = pn.slot >= PN_PRESETS ? pn.uname[(pn.slot - PN_PRESETS) % PL_IDS] : PRESET[pn.slot % PN_PRESETS].name;
            for (i = 0; i < PL_NAME && src[i]; i++) pn_tmp.name[i] = src[i];       /* named after the patch it was made from */
            pn_tmp.name[i] = 0;
            for (i = 0; i < P_COUNT; i++) pn_tmp.v[i] = (int16_t)patch.p[i];
            pn_tmp.n = P_COUNT;
            while (pn_tmp.n > P_COUNT_V1 && pn_tmp.v[pn_tmp.n - 1u] == PDESC[pn_tmp.n - 1u].def) pn_tmp.n--;     /* trailing defaults are not stored: a patch without arp keeps its 124 bytes */
            rc = pl_put(id, &pn_tmp);
            pn.save_req = 0;
            if (rc != PL_OK) { pn_msg(rc == PL_E_FULL ? "STORE FULL" : "SAVE FAILED", 0, 0); return 1; }
            pn_ucache(id);
            pn_snapshot();
            pn.slot = pn.browse = (uint8_t)(PN_PRESETS + id);
            pn.browse_pend = 0;
            st->slot = pn.slot;
            *dirty = 1;
            pn_msg("SAVED", id, 1);
            return 1;
        } else {
            int32_t e = (int32_t)(PN_PRESETS + id);
            pn.save_req = 0;
            if (pl_del(id) != PL_OK) { pn_msg("DEL FAILED", 0, 0); return 1; }
            pn_ucache(id);
            do e--; while (e > 0 && !pn_entry_ok((uint32_t)e));      /* the patch before it in the list */
            pn_load((uint32_t)e, st);
            st->slot = pn.slot;
            *dirty = 1;
            pn_msg("DELETED", id, 1);
            return 1;
        }
    }
    pn.save_req = 0;
    pn_msg("SAVED", 0, 0);
    return 1;
}
