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
enum { TX_FMODE, TX_VMODE, TX_BDEST, TX_QUAL, TX_CURVE, TX_LSHAPE, TX_ARP, TX_ARATE, TX_MOD, TX_ONOFF, TX_FXT, TX_N };
typedef struct { const char *lab; uint8_t id, fmt, step, tx; } kdef_t;
#define K(l, id, f, st, tx) {l, id, f, st, tx}
#define KN {"-", 0, F_NONE, 0, 0}

static const uint8_t NSUB[UI_P_N] = {1, 4, 6, 2, 2, 1, 3, 2, 2, 2};   /* ... GLO, ARP, SEQ, HELP */   /* HELP: two text pages (PG[HELP] stays empty) */
static const kdef_t PG[UI_P_N][6][4] = {
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
                  K("E>BEND", P_MENV_WARP, F_BIP, 1, 0)},
                 {K("TYPE", P_FX1_TYPE, F_TXT, 1, TX_FXT), K("A", P_FX1_A, F_INT, 1, 0), K("B", P_FX1_B, F_INT, 1, 0), K("MIX", P_FX1_MIX, F_PCT, 1, 0)},
                 {K("TYPE", P_FX2_TYPE, F_TXT, 1, TX_FXT), K("A", P_FX2_A, F_INT, 1, 0), K("B", P_FX2_B, F_INT, 1, 0), K("MIX", P_FX2_MIX, F_PCT, 1, 0)},
                 {K("TYPE", P_FX3_TYPE, F_TXT, 1, TX_FXT), K("A", P_FX3_A, F_INT, 1, 0), K("B", P_FX3_B, F_INT, 1, 0), K("MIX", P_FX3_MIX, F_PCT, 1, 0)},
                 {K("TYPE", P_FX4_TYPE, F_TXT, 1, TX_FXT), K("A", P_FX4_A, F_INT, 1, 0), K("B", P_FX4_B, F_INT, 1, 0), K("MIX", P_FX4_MIX, F_PCT, 1, 0)}},   /* sets 3..6: the effect slots (labels follow the type: pn_view_fill) */
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
                   K("TEMPO", P_TEMPO, F_INT, 1, 0)},
                  {K("TARGET", P_MOD, F_TXT, 1, TX_MOD), K("DEPTH", P_MOD_DEPTH, F_INT, 1, 0), K("GLIDE", P_MOD_GLIDE, F_TXT, 1, TX_ONOFF), KN}},       /* set 2: the modulation lane (SELECT picks the step, VALUE is drawn and edited by hand: pn_encoders) */
};

/* the tabs the strip shows (and HELP lists): a tab is one picture; it holds one or two knob sets (the page button steps
 * through the sets in order, the set tag under the tab says which). TABG: the tab of each set; TABN: the tab names */
static const uint8_t TABG[UI_P_N][6] = {
    [UI_P_EDIT] = {0, 1, 2, 3}, [UI_P_FX] = {0, 0, 1, 2, 3, 4}, [UI_P_ENV] = {0, 1}, [UI_P_LFO] = {0, 0},
    [UI_P_GLO] = {0, 1, 2}, [UI_P_ARP] = {0, 0}, [UI_P_SEQ] = {0, 1}, [UI_P_HELP] = {0, 1}};
static const char *const TABN[UI_P_N][6] = {
    [UI_P_HOME] = {"HOME"}, [UI_P_EDIT] = {"OSC1", "OSC2", "SUB", "NOISE"}, [UI_P_FX] = {"FILT", "FX1", "FX2", "FX3", "FX4"},
    [UI_P_ENV] = {"AMP", "FILTER"}, [UI_P_LFO] = {"LFO"}, [UI_P_SEL] = {"VOICE"}, [UI_P_GLO] = {"OUTPUT", "CONTROL", "BOOT"},
    [UI_P_ARP] = {"ARP"}, [UI_P_SEQ] = {"NOTES", "LANE"}, [UI_P_HELP] = {"HELP", "HELP"}};
static void pn_tabpos(uint32_t pg, uint32_t sub, uint8_t *tab, uint8_t *ntabs, uint8_t *tset, uint8_t *tsets)   /* where set `sub` sits in the strip */
{
    uint32_t i, t = TABG[pg][sub], f = sub, n = 0;
    while (f && TABG[pg][f - 1u] == t) f--;
    for (i = 0; i < NSUB[pg]; i++) n += TABG[pg][i] == t;
    *tab = (uint8_t)t; *ntabs = (uint8_t)(TABG[pg][NSUB[pg] - 1u] + 1u); *tset = (uint8_t)(sub - f); *tsets = (uint8_t)n;
}

static const char *const TXT[TX_N][8] = {
    {"LP", "BP", "HP"}, {"POLY", "MONO", "LEGATO"}, {"BOTH", "OSC1", "OSC2"}, {"HI", "ECO"}, {"LIN", "EXP"},
    {"SINE", "TRI", "SAW", "SQUARE", "S&H"},
    {"OFF", "UP", "DOWN", "UP-DN", "UP-DN2", "RANDOM", "PLAYED", "CHORD"}, {"1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32"},
    {"OFF", "POS", "CUTOFF", "BEND", "DRIVE"}, {"OFF", "ON"}, {"OFF", "CHORUS", "FLANGER", "PHASER", "DELAY", "REVERB"}};

/* ---- panel state (main loop only) */
static struct {
    uint8_t page, look, sub, touched;
    uint8_t slot, browse, browse_pend;          /* entry loaded (0..7 factory, 8.. user); entry shown in the name line; load pending */
    uint8_t save_req, home_arm;                 /* HOME pressed and not yet decided: short press or hold */
    uint8_t mstep;                              /* SEQ set 2: the lane step the VALUE knob edits (0..31) */
    uint8_t tab[UI_P_N];                        /* the tab each page shows when it opens again (this session only, not saved) */
    uint8_t help_from;                          /* the page HELP explains (the one HOME was pressed on) */
    uint8_t clr;                                /* REC held: "CLEAR PATTERN?" asked, REC again clears */
    uint32_t t_clr;
    uint8_t arp_arm, arp_last;                  /* ARP pressed and not yet decided (short: the page, hold: on / off); the mode it switches back to */
    uint32_t t_arp, t_arp_tog;                  /* press time; time of the last switch */
    uint8_t sv, act, act_id, tgt;               /* SAVE gesture (SV_*), the request for pn_save_poll (ACT_*, user id), the chosen target id */
    uint32_t t_browse, t_flash, t_save, t_home, t_sv;
    char fmsg[12];                              /* the confirmation shown while t_flash runs */
    char uname[PL_IDS][PL_NAME + 1];            /* names of the user patches in the log */
    int16_t base[P_COUNT];                      /* the patch as loaded / saved: "modified" = differs */
    uint32_t base_pat;                          /* the same for the note pattern and the mod lane (pn_pat_sum) */
    int16_t wave[256];                          /* EDIT: the frame as the engine reads it */
    uint32_t wave_key;
} pn;
static ui_view_t pn_view;
static ui_shadow_t pn_shadow;

/* the look order HOME cycles: spectrum (2) -> waterfall (1) -> minimal (3) -> spectrum */
static const uint8_t LOOK_NEXT[UI_H_N] = {UI_H_MINIMAL, UI_H_WATERFALL, UI_H_SPECTRUM};

/* The note pattern and the mod lane go into a patch record as pattern words (patchlog.h): word 0 the length (1..32), then a word per
 * step: note (bits 0-6), sounds (7; clear = rest), tied (8), lane value + 64 (9-15). Only a patch saved with the sequencer on and a
 * pattern recorded has them. Loading a patch with a pattern replaces the pattern in RAM; a patch without one leaves it as it is
 * (the sequencer is a performance switch, like the arp: changing the patch does not stop the tune). */
static uint32_t pn_pat_words(int16_t *w, uint32_t any)    /* the words for the pattern now (0: none; `any` 0 also gives none with the sequencer off) */
{
    uint32_t n = sq.len > SEQ_MAXN ? SEQ_MAXN : sq.len, i;
    if ((!any && !patch_get(P_SEQ)) || !n) return 0;
    if (w) {
        w[0] = (int16_t)n;
        for (i = 0; i < n; i++) {
            uint32_t v = (sq.note[i] & 127u) | (sq.vel[i] ? 128u : 0u) | (sq.tie[i] ? 256u : 0u) | (uint32_t)((int32_t)ml[i] + 64) << 9;
            w[1u + i] = (int16_t)(uint16_t)v;
        }
    }
    return 1u + n;
}
static uint32_t pn_pat_sum(void)                 /* a cheap fingerprint of the pattern (0: none): a changed pattern is an edit, SEQ on or off */
{
    int16_t w[PL_MAXPW];
    uint32_t n = pn_pat_words(w, 1u), i, h = 0;
    for (i = 0; i < n; i++) h = (h * 31u + (uint16_t)w[i]) ^ (h >> 27);
    return h;
}
static void pn_pat_apply(const pl_patch_t *t)    /* a loaded user patch's pattern becomes the pattern; a patch saved without one empties it */
{
    uint32_t n, i;
    n = t->pw ? (uint16_t)t->pv[0] : 0u;
    if (t->pw && (n < 1u || n > SEQ_MAXN || t->pw < 1u + n)) return;       /* a damaged pattern: leave the one in RAM */
    sq.len = 0;                                  /* the ISR plays nothing while the arrays change */
    __asm__ volatile("" ::: "memory");
    sq.rec = 0;
    for (i = 0; i < n; i++) {
        uint32_t v = (uint16_t)t->pv[1u + i];
        sq.note[i] = (uint8_t)(v & 127u);
        sq.vel[i] = v & 128u ? 100u : 0u;
        sq.tie[i] = (v & 256u) != 0u;
        ml[i] = (int8_t)((int32_t)(v >> 9) - 64);
    }
    for (; i < SEQ_MAXN; i++) { sq.note[i] = 0; sq.vel[i] = 0; sq.tie[i] = 0; ml[i] = 0; }
    sq.cur = (uint8_t)n;
    __asm__ volatile("" ::: "memory");
    sq.len = (uint8_t)n;                         /* 0 = no pattern: the sequencer is silent until one is recorded */
}

static void pn_snapshot(void)
{
    uint32_t i;
    for (i = 0; i < P_COUNT; i++) pn.base[i] = (int16_t)patch.p[i];
    pn.base_pat = pn_pat_sum();
}
static int pn_modified(void)
{
    uint32_t i;
    for (i = 0; i < P_COUNT; i++)
        if (patch.p[i] != pn.base[i]) return 1;
    return pn_pat_sum() != pn.base_pat;
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
            pn_pat_apply(&pn_tmp);
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
    if (seq && patch_get(P_ARP) != arp) { patch_set(P_ARP, arp); pn.base[P_ARP] = (int16_t)arp; }       /* the sequencer plays: the arp stays as it was (a patch's own arp does not come on under it) */
    else if (arp && !patch_get(P_ARP)) { patch_set(P_ARP, arp); pn.base[P_ARP] = (int16_t)arp; }
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
    pn_tmp.pw = 0;
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
/* a quick turn goes faster, by detents a second (not by detents a pass: a slow pass, e.g. a big picture drawn, must not
 * multiply a turn): under 20 / s x1, 20..40 x2, above x4 */
static int32_t pn_accel_i(uint32_t enc, int32_t d)
{
    static uint32_t t_last[7];
    uint32_t a = (uint32_t)(d < 0 ? -d : d), dt = fm1_ms - t_last[enc], rate;
    t_last[enc] = fm1_ms;
    if (dt < 1u) dt = 1u;
    if (dt > 1000u) dt = 1000u;
    rate = a * 1000u / dt;
    return d * (rate >= 40u ? 4 : rate >= 20u ? 2 : 1);
}

static void pn_goto(uint32_t page, uint32_t sub) /* show this page and tab; the tab is remembered for the page */
{
    pn.page = (uint8_t)page;
    pn.sub = (uint8_t)(sub < NSUB[page] ? sub : 0u);
    pn.tab[page] = pn.sub;
    pn.touched = 4;
}
static void pn_page(uint32_t page)              /* a page button: the page on the tab it was left on, or its next tab when already there */
{
    if (pn.page == page) pn_goto(page, (pn.sub + 1u) % NSUB[page]);
    else pn_goto(page, pn.tab[page]);
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
/* a page button: its page; on HELP, that page's help (HELP stays), and again: the page */
static void pn_page_btn(uint32_t page)
{
    if (pn.page != UI_P_HELP) pn_page(page);
    else if (pn.help_from == page && pn.sub == 0u) pn_goto(page, pn.tab[page]);
    else { pn.help_from = (uint8_t)page; pn.sub = 0; }
}

static void pn_buttons(uint32_t b, uint32_t held, app_set_t *st, uint32_t *dirty)
{
    if (b & 1u << BTN_EDIT) pn_page_btn(UI_P_EDIT);
    if (b & 1u << BTN_FX) pn_page_btn(UI_P_FX);
    if (b & 1u << BTN_ENV) pn_page_btn(UI_P_ENV);
    if (b & 1u << BTN_LFO) pn_page_btn(UI_P_LFO);
    if (b & 1u << BTN_SEL) pn_page_btn(UI_P_SEL);
    if (b & 1u << BTN_GLO) pn_page_btn(UI_P_GLO);
    if (b & 1u << BTN_ARP) {
        if (!pn.arp_arm && (!pn.t_arp_tog || fm1_ms - pn.t_arp_tog >= 200u)) { pn.arp_arm = 1; pn.t_arp = fm1_ms; }     /* (a bounce right after a switch is not a new press) */
    }
    if (pn.arp_arm) {
        if (!(held >> BTN_ARP & 1u)) { pn.arp_arm = 0; pn_page_btn(UI_P_ARP); }     /* released: a short press opens its page (again: the next set), as every page button */
        else if (fm1_ms - pn.t_arp >= 500u) {                       /* held: switches the arp on / off (at once, the release does nothing more) */
            pn.arp_arm = 0;
            if (patch_get(P_ARP)) { pn.arp_last = (uint8_t)patch_get(P_ARP); patch_set(P_ARP, 0); }
            else patch_set(P_ARP, pn.arp_last ? pn.arp_last : 1);
            pn.t_arp_tog = fm1_ms ? fm1_ms : 1u;
            pn.base[P_ARP] = (int16_t)patch.p[P_ARP];               /* the button is a performance switch, not a patch edit: SAVE does not blink for it */
        }
    }
    if (b & 1u << BTN_SEQ) pn_page_btn(UI_P_SEQ);               /* SEQ only opens its page (again: NOTES / LANE); PLAY on that page starts / stops it */
    if (b & 1u << BTN_HOME) {
        if (held >> BTN_EDIT & 1u) cpu_reset_req++;                 /* the max starts over */
        else if (pn.page == UI_P_HELP) {                            /* next help page, then out */
            if (pn.sub + 1u < NSUB[UI_P_HELP]) pn.sub++;
            else { pn.page = UI_P_HOME; pn.sub = 0; pn.touched = 4; }
        } else if (pn.page == UI_P_HOME) {                          /* decided on release: a hold is HELP */
            pn.home_arm = 1;
            pn.help_from = UI_P_HOME;
            pn.t_home = fm1_ms;
        } else {
            pn.help_from = pn.page;                                 /* a hold explains the page it was pressed on */
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

static uint32_t pn_fx_taken(uint32_t id, int32_t ty)       /* another slot already has this type */
{
    uint32_t j;
    for (j = 0; j < 4u; j++)
        if (P_FX1_TYPE + 4u * j != id && patch_get(P_FX1_TYPE + 4u * j) == ty) return 1;
    return 0;
}
static void pn_fx_type(uint32_t id, int32_t d)
{
    int32_t cur = patch_get(id), dir = d > 0 ? 1 : -1, n = d < 0 ? -d : d, v;
    while (n-- > 0) {
        v = cur + dir;
        while ((v == 4 || v == 5) && pn_fx_taken(id, v)) v += dir;
        if (v < 0 || v > 5) break;
        cur = v;
    }
    patch_set(id, cur);
}

static void pn_encoders(void)
{
    uint32_t i, o = pn_osc();
    int32_t d;
    if ((d = fm1_enc_take(ENC_ALGORITHM)) != 0) pn_table_step(o ? P_TABLE2 : P_TABLE1, d);
    if (pn.page == UI_P_SEQ && pn.sub == 1u) {  /* the modulation lane: SELECT picks the step, VALUE sets it (RAM only until patterns are saved) */
        int32_t n = sq.len ? (int32_t)sq.len : 32, m = (int32_t)pn.mstep, x;
        if ((d = fm1_enc_take(ENC_SELECT)) != 0) m += d > 0 ? 1 : -1;
        m = m < 0 ? 0 : m >= n ? n - 1 : m;
        pn.mstep = (uint8_t)m;
        if ((d = fm1_enc_take(ENC_KNOB4)) != 0) {
            x = (int32_t)ml[m] + pn_accel_i(ENC_KNOB4, d) * 4;       /* 4 values a detent: the lane's 128 values are a long way for one knob */
            ml[m] = (int8_t)(x < -64 ? -64 : x > 63 ? 63 : x);
            pn.touched = 3;
        }
    }
    if ((d = fm1_enc_take(ENC_SELECT)) != 0) patch_add(o ? P_POS2 : P_POS1, pn_accel_i(ENC_SELECT, d) * 32);
    for (i = 0; i < 4u; i++) {
        const kdef_t *k = &PG[pn.page][pn.sub][i];
        d = fm1_enc_take(ENC_KNOB1 + i);
        if (!d || k->fmt == F_NONE) continue;
        if (pn.page == UI_P_FX && pn.sub >= 2u && i == 0u) pn_fx_type(k->id, d);    /* an effect slot's TYPE: delay and reverb are used once, the taken one is skipped */
        else patch_add(k->id, (k->fmt == F_TXT ? d : pn_accel_i(ENC_KNOB1 + i, d)) * k->step);
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

/* ---- HELP: page 1 explains the page HOME was held on (its tabs and knobs, from PG, so it cannot go stale), page 2 the buttons */
#define PN_HW 29u                                /* characters a help line holds (240 px, 8 px each, 4 px margin) */
static void pn_hl(char *d, const char *lab, const char *txt)       /* "LABEL  TEXT" in one line */
{
    uint32_t n = 0;
    while (*lab && n < PN_HW) d[n++] = *lab++;
    if (*txt) { d[n++] = ' '; d[n++] = ' '; }
    while (*txt && n < PN_HW) d[n++] = *txt++;
    d[n] = 0;
}
static void pn_help(ui_view_t *v, uint32_t pg, uint32_t sub)
{
    static const char *const PNAME[UI_P_N] = {"HOME", "EDIT: OSCILLATORS", "FX: FILTER, EFFECTS", "ENV: ENVELOPES", "LFO", "SEL: VOICE", "GLO: GLOBAL", "ARP", "SEQ: SEQUENCER", ""};
    static const char *const BTN[UI_P_N] = {"HOME", "EDIT", "FX", "ENV", "LFO", "SEL", "GLO", "ARP", "SEQ", ""};
    static const char *const GEN[13] = {
        "HELP 2/2  BUTTONS",
        "HOME  BACK HOME, HOLD: HELP",
        "PAGE BUTTON  OPEN, AGAIN: TAB",
        "ARP  PAGE, HOLD: ON/OFF",
        "SAVE  STORE (HOLD: PICK, DEL)",
        "PLAY  LATCH; SEQ PAGE: START",
        "REC  STEPS (SEQ PAGE ONLY)",
        "OCT- OCT+  OCTAVE DOWN, UP",
        "ALGORITHM  WAVETABLE",
        "SELECT  POSITION IN TABLE",
        "PRESETS  FACTORY + USER",
        "KNOB 1-4  THE 4 VALUES ON TOP",
        "HOME: NEXT PAGE, THEN EXIT"};
    uint32_t i, l = 0, t;
    char txt[40], *d;
    for (i = 0; i < 14u; i++) v->help[i][0] = 0;
    if (sub) {
        for (i = 0; i < 13u; i++) pn_hl(v->help[i], GEN[i], "");
        return;
    }
    if (pg >= UI_P_HELP) pg = UI_P_HOME;
    d = ui_cat(txt, "HELP 1/2  ");
    ui_cat(d, PNAME[pg]);
    pn_hl(v->help[l++], txt, "");
    pn_hl(v->help[12], "BUTTON", "ITS HELP, AGAIN: OPEN");       /* (lines 12 and 13: the same on every page) */
    pn_hl(v->help[13], "HOME: NEXT PAGE, THEN EXIT", "");
    if (pg == UI_P_HOME) {                                   /* the HOME screen: its knobs are its own, the rest acts everywhere */
        pn_hl(v->help[l++], "KNOB 1-4", "CUTOFF RESO ATK REL");
        pn_hl(v->help[l++], " (ON HOME; EACH PAGE HAS", "");
        pn_hl(v->help[l++], " ITS OWN, SHOWN ON TOP)", "");
        pn_hl(v->help[l++], "ALGORITHM", "CHANGES WAVETABLE");
        pn_hl(v->help[l++], "SELECT", "MOVES IN WAVETABLE");
        pn_hl(v->help[l++], "PRESETS", "CHANGES PATCH");
        pn_hl(v->help[l++], "HOME AGAIN", "NEXT WAVE VIEW");
        pn_hl(v->help[l++], "HOLD HOME", "THIS HELP");
        pn_hl(v->help[l++], "PAGE BUTTONS", "EDIT FX ENV LFO");
        pn_hl(v->help[l++], " SEL GLO ARP SEQ", "");
        return;
    }
    for (t = 0; t < NSUB[pg] && l < 9u; t++) {             /* a line per tab: its name, then the four knobs */
        d = txt;
        for (i = 0; i < 4u; i++) {
            const char *lab = PG[pg][t][i].fmt == F_NONE ? 0 : PG[pg][t][i].lab;
            if (pg == UI_P_FX && t >= 2u) lab = i ? (i == 3u ? "MIX" : i == 1u ? "A" : "B") : "TYPE";
            if (pg == UI_P_SEQ && t == 1u && i == 3u) lab = "VALUE";
            if (!lab) continue;
            if (d != txt) *d++ = ' ';
            d = ui_cat(d, lab);
        }
        if (pg == UI_P_GLO && t == 2u) ui_cat(txt, "KEY SCAN AT POWER-ON");
        if (pg == UI_P_FX && t >= 3u) { if (t == 3u) { pn_hl(v->help[l++], "FX2-FX4", "THE SAME"); } continue; }
        {   char lab[16];
            uint32_t ll = 0, tl = 0;
            uint8_t tb, nt, ts, nts;
            pn_tabpos(pg, t, &tb, &nt, &ts, &nts);
            d = ui_cat(lab, pg == UI_P_HOME ? "KNOBS" : TABN[pg][tb]);
            if (nts > 1u) { *d++ = ' '; d = ui_num(d, ts + 1); }       /* a tab of two sets: "FILTER 1", "FILTER 2" */
            while (lab[ll]) ll++;
            while (txt[tl]) tl++;
            if (ll + 2u + tl <= PN_HW) pn_hl(v->help[l++], lab, txt);
            else { pn_hl(v->help[l++], lab, " "); v->help[l][0] = ' '; pn_hl(v->help[l] + 1, txt, ""); l++; }      /* too long: the knobs on the next line (a leading space: plain text) */
        }
    }
    if (pg != UI_P_HOME && NSUB[pg] > 1u) { d = ui_cat(txt, BTN[pg]); ui_cat(d, " AGAIN"); pn_hl(v->help[l++], txt, "NEXT TAB"); }
    switch (pg) {
    case UI_P_EDIT: pn_hl(v->help[l++], "ALGORITHM", "TABLE OF THIS OSC"); pn_hl(v->help[l++], "SELECT", "POSITION OF THIS OSC"); pn_hl(v->help[l++], "EDIT+F# G# A# C#", "PICK OSC"); break;
    case UI_P_FX: pn_hl(v->help[l++], "DELAY REVERB", "ONE SLOT EACH"); pn_hl(v->help[l++], "CHAIN", "SLOT 1 > 2 > 3 > 4"); break;
    case UI_P_ENV: pn_hl(v->help[l++], "FILTER ENV", "ALSO E>POS E>BEND (FX)"); break;
    case UI_P_GLO: pn_hl(v->help[l++], "EDIT+HOME", "RESET CPU MAX"); break;
    case UI_P_ARP: pn_hl(v->help[l++], "HOLD ARP", "ARP ON / OFF"); break;
    case UI_P_SEQ:
        pn_hl(v->help[l++], "PLAY", "START / STOP");
        pn_hl(v->help[l++], "REC", "STEP INPUT ON / OFF");
        pn_hl(v->help[l++], "HOLD REC", "CLEAR (REC AGAIN)");
        pn_hl(v->help[l++], "LANE", "SELECT PICKS THE STEP");
        break;
    default: break;
    }
    if (pg != UI_P_EDIT && pg != UI_P_SEQ && l < 11u) {     /* (when both fit above the two last lines) */
        pn_hl(v->help[l++], "ALGORITHM", "CHANGES WAVETABLE");
        pn_hl(v->help[l++], "SELECT", "MOVES IN WAVETABLE");
    }
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
    v->tabn = TABN[pn.page];
    pn_tabpos(pn.page, pn.sub, &v->tab, &v->ntabs, &v->tset, &v->tsets);
    if (pn.page == UI_P_HELP) pn_help(v, pn.help_from, pn.sub);
    v->touched = pn.touched;
    v->voices = (uint8_t)(voices > 99u ? 99u : voices);
    v->voices_max = ENG_NV;
    v->cpu = (uint8_t)(cpu > 255u ? 255u : cpu);
    v->cpu_max = (uint8_t)(cpu_max > 255u ? 255u : cpu_max);
    v->modified = (uint8_t)(!pn.browse_pend && pn_modified());
    v->flash = (uint8_t)flash;
    v->drops = (uint16_t)(eng_tap.shed_blocks > 9999u ? 9999u : eng_tap.shed_blocks);
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
    else if (pn.clr) ui_copy(v->patch, "CLEAR PATTERN?", 16);
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
        {   uint32_t tm = 0;                            /* the tiles show the stored notes; a held key's transposition is not shown */
            for (i = 0; i < n && i < 32u; i++) {
                v->seq_note[i] = (uint8_t)(sq.vel[i] && sq.note[i] <= 127u ? sq.note[i] + 1 : 0);
                if (sq.tie[i] && sq.vel[i]) tm |= 1u << i;
            }
            for (; i < 32u; i++) v->seq_note[i] = 0;
            v->seq_tie = tm; v->seq_tr = 0;
        }
    }
    if (pn.page == UI_P_FX && pn.sub >= 2u) {           /* an effect slot: the labels follow its type, an empty slot shows nothing */
        static const char *const CN[6][3] = {{"-", "-", "-"}, {"RATE", "DEPTH", "MIX"}, {"RATE", "FEEDBK", "MIX"}, {"RATE", "FEEDBK", "MIX"}, {"TIME", "FEEDBK", "MIX"}, {"SIZE", "DAMP", "MIX"}};
        uint32_t ty = (uint32_t)patch_get(P_FX1_TYPE + 4u * (pn.sub - 2u)), c;
        if (ty > 5u) ty = 0;
        for (c = 0; c < 3u; c++) {
            v->klabel[1u + c] = CN[ty][c];
            if (!ty) { v->kval[1u + c][0] = 0; v->kfrac[1u + c] = 0; }
        }
    }
    if (pn.page == UI_P_SEQ && pn.sub == 1u) {          /* STEP and VALUE of the lane */
        uint32_t m = pn.mstep < 32u ? pn.mstep : 31u;
        v->klabel[3] = "VALUE";
        ui_num(v->kval[3], (int32_t)ml[m] + 64);
        v->kfrac[3] = (uint8_t)(((int32_t)ml[m] + 64) * 2);
    }
    v->seq_set = (uint8_t)(pn.page == UI_P_SEQ ? pn.sub : 0u);       /* 1: the lane is shown and edited */
    v->seq_cur = pn.mstep;
    for (i = 0; i < 32u; i++) v->seq_mod[i] = (uint8_t)((int32_t)ml[i] + 64);
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
        v->btn_now = (uint16_t)fm1_in.buttons;
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
        v->flt_cut = (uint8_t)patch_get(P_CUTOFF);
        v->flt_res = (uint8_t)patch_get(P_RESO);
        v->flt_mode = (uint8_t)patch_get(P_FMODE);
        v->lfo_shape = (uint8_t)patch_get(P_LFO_SHAPE);
        v->lfo_rate = (uint8_t)patch_get(P_LFO_RATE);
        v->drive = (uint8_t)patch_get(P_DRIVE);
        v->flt_env = (int8_t)patch_get(P_FENV);
        v->mod[0] = (uint8_t)patch_get(P_MW_POS); v->mod[1] = (uint8_t)patch_get(P_MW_VIB);
        v->mod[2] = (uint8_t)patch_get(P_AT_CUT); v->mod[3] = (uint8_t)patch_get(P_VEL_AMP);
        v->vmode = (uint8_t)patch_get(P_VMODE); v->wdest = (uint8_t)patch_get(P_WARP_DEST);
        v->qual = (uint8_t)patch_get(P_QUALITY); v->ecurve = (uint8_t)patch_get(P_ECURVE);
        v->arp_mode = (uint8_t)patch_get(P_ARP); v->arp_oct = (uint8_t)patch_get(P_ARP_OCT);
        v->arp_gate = (uint8_t)patch_get(P_ARP_GATE); v->arp_swing = (uint8_t)patch_get(P_ARP_SWING);
        if (pn.page == UI_P_FX && pn.sub >= 2u) {      /* the chain of slots; where delay and reverb are */
            uint32_t c;
            for (c = 0; c < 4u; c++) v->fxt[c] = (uint8_t)patch_get(P_FX1_TYPE + 4u * c);
            {   char *e = v->info2;                 /* the once-only effects: where they are, or free */
                uint32_t t, own;
                for (t = 4; t < 6u; t++) {
                    own = 0;
                    for (c = 4; c-- > 0u; ) if ((uint32_t)patch_get(P_FX1_TYPE + 4u * c) == t) own = c + 1u;      /* (the first slot owns it) */
                    e = ui_cat(e, t == 4u ? "DELAY " : "  REVERB ");
                    if (own) { e = ui_cat(e, "SLOT "); e = ui_num(e, (int32_t)own); } else e = ui_cat(e, "FREE");
                }
            }
        }
        if (pn.page == UI_P_SEQ) {
            char *d = v->info;
            ui_copy(v->ptxt, sq.rec ? "RECORD" : !sq.len ? "EMPTY" : patch_get(P_SEQ) && sq_run ? "PLAY" : "STOP", 8);
            if (pn.sub == 1u) ui_copy(v->ptxt, TXT[TX_MOD][patch_get(P_MOD)], 8);           /* the lane: its target, big */

            if (pn.sub == 1u) { d = ui_cat(d, "STEP "); d = ui_num(d, (int32_t)(pn.mstep < 32u ? pn.mstep : 31u) + 1); d = ui_cat(d, "  SELECT KNOB MOVES"); }
            else if (sq.rec) { d = ui_cat(d, "STEP "); d = ui_num(d, (int32_t)sq.cur + 1); d = ui_cat(d, "/32  PLAY=REST SEQ=TIE"); }
            else if (!sq.len) ui_cat(d, "NO PATTERN: PRESS REC");
            else { d = ui_cat(d, "LEN "); d = ui_num(d, sq.len); d = ui_cat(d, "  "); if (arp_tap.ext) d = ui_cat(d, "EXT"); else { d = ui_num(d, patch_get(P_TEMPO)); d = ui_cat(d, " BPM"); }
                   if (!(patch_get(P_SEQ) && sq_run)) ui_cat(d, "  PLAY=START"); }
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
            pn_tmp.pw = (uint8_t)pn_pat_words(pn_tmp.pv, 1u);
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
