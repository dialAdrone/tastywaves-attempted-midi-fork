/* SPDX-License-Identifier: GPL-3.0-only */
/* VAVEWave: a wavetable synth for the M-VAVE FM-1, on the Dinghy platform (Felucca / SLOOP). The v1 engine
 * (firmware/src/synth.h, 12 voices x 2 wavetable osc, SVF, 2 ADSR, LFO, BEND, drive; docs/WAVETABLE_DESIGN.md) is
 * driven through patch.h; the front panel is panel.h (what each control does) and ui_screens.h (the screens).
 * This is the file to change. Everything below it (platform.c) is done: updates, the USB rescue,
 * USB-MIDI, USB audio, keys, knobs, LEDs, the screen, a settings sector.
 *
 *   keys       play, 12 voices; the notes go out over USB-MIDI too (EDIT held: OP1..OP4 black keys pick the oscillator)
 *   OCT- OCT+  the octave (kept in flash)
 *   GLO x3     third set: BOOT KEYS, the key scan at power-on (docs/UI-DESIGN.md)
 *   HOME EDIT FX ENV LFO SEL GLO   the page; the same button again: next set / HOME look (2 -> 1 -> 3, kept in flash)
 *   KNOB1-4    the page's four values; ALGORITHM = table; SELECT = position; PRESETS = patch (loads after 400 ms)
 *   SAVE       the patch into the settings sector (a stub for T17: one user patch overlay; see panel.h)
 *   EDIT+HOME  restart the ISR cost maximum (GLO page shows it; the status line shows the live CPU %)
 *   MASTER     the volume (platform.c)
 *   USB-MIDI   notes (velocity), CCs (patch.h patch_cc), pitch bend, aftertouch
 * BENCH 1 (below) replaces the engine with the wavetable oscillator CPU bench (firmware/src/wtbench.h):
 *   12 voices x 2 oscillators, a held 12-note chord at start; EDIT interpolator (linear / Catmull-Rom / 8-tap sinc),
 *   SEL voices rendered (0 1 4 8 12), PLAY hold the chord on/off, FX all voices on one table / 12 tables.
 *
 * The code: engine.h (voices, app_audio, play/stop), wtbench.h (BENCH oscillators), ui.h (BENCH drawing),
 * ui_screens.h + panel.h (the UI), here the main loop. One translation unit: platform.c and the headers are
 * included, not linked.
 *
 * Rules of the road (the chip has no FPU and no libgcc): integers only, no float, no 64-bit division.
 * tests/host.c runs this file on a computer: the screen as a PNG, the sound as a WAV.
 * What platform.c offers: docs/API.md. The chip's rules: docs/RULES.md. */
#include "platform.c"

#ifndef BENCH
#define BENCH 0                                 /* 1 = wavetable oscillator CPU bench (throwaway), 0 = the v1 engine */
#endif
#include "engine.h"
#include "ui.h"
#include "ui_screens.h"

#if BENCH
static const uint8_t CHORD[NV] = {36, 43, 48, 52, 55, 60, 64, 67, 72, 76, 79, 84};   /* C2 .. C6, mip levels 0..2 */
static void bench_hold(int on)
{
    uint32_t v;
    for (v = 0; v < NV; v++) {
        if (on) {
            voice[v].note = CHORD[v];
            voice[v].age = ++ages;
            voice[v].inc = note_inc(CHORD[v]);  /* pitch before gate */
            voice[v].gate = (int32_t)(100u * GATE_PER_VEL);
        } else
            voice[v].gate = 0;
    }
}

static void app_main(void)
{
    static const uint8_t NVS[] = {0, 1, 4, 8, NV};
    struct { int8_t oct; } set = {0};           /* what survives a power-off */
    uint32_t prev = 0, k, t_change = 0, dirty = 0, lit_drawn = ~0u, t_cost = 0, last_drawn = ~0u, max_drawn = ~0u;
    uint32_t ip = IP_LIN, nvi = sizeof NVS - 1u, same = 0, hold = 1, cfg_dirty = 1, shed_base = 0, shed_drawn = ~0u;
    int32_t oct_drawn = 99;
    uint8_t held[27] = {0};                     /* the note each key plays */
    settings_load(&set, sizeof set);
    if (set.oct < -2 || set.oct > 2) set.oct = 0;
    draw_bench_static();
    bench_ip = ip; bench_nv = NVS[nvi]; bench_same = same;
    bench_hold(1);

    for (;;) {
        uint32_t keys, down, up, rel, b, lit = 0, base = (uint32_t)(KEY0 + 12 * set.oct);
        uint8_t st, d1, d2;
        platform_poll();

        keys = fm1_in.notes;
        down = keys & ~prev;
        up = prev & ~keys;
        prev = keys;
        for (k = 0; k < 27u; k++) {
            if (down >> k & 1u) { held[k] = (uint8_t)(base + k); play(held[k], 100); midi_send(0x90, held[k], 100); }
            if (up >> k & 1u) { stop(held[k]); midi_send(0x80, held[k], 0); }
        }

        b = fm1_input_edges(&rel);
        if (b & (1u << BTN_OCT_DOWN) && set.oct > -2) { set.oct--; dirty = 1; t_change = fm1_ms; }
        if (b & (1u << BTN_OCT_UP) && set.oct < 2) { set.oct++; dirty = 1; t_change = fm1_ms; }
        if (b & (1u << BTN_EDIT)) { ip = (ip + 1u) % IP_N; bench_ip = ip; cfg_dirty = 1; }
        if (b & (1u << BTN_SEL)) { nvi = (nvi + 1u) % sizeof NVS; bench_nv = NVS[nvi]; cfg_dirty = 1; }
        if (b & (1u << BTN_FX)) { same ^= 1u; bench_same = same; cfg_dirty = 1; }
        if (b & (1u << BTN_PLAY)) { hold ^= 1u; bench_hold((int)hold); cfg_dirty = 1; }
        if (b & (1u << BTN_HOME)) { cpu_reset_req++; shed_base = bench_shed_blocks; }
        if (cfg_dirty || set.oct != oct_drawn) {     /* a new setting: a new maximum */
            if (cfg_dirty) { cpu_reset_req++; shed_base = bench_shed_blocks; }
            draw_bench_cfg(ip, NVS[nvi], same, hold, set.oct);
            cfg_dirty = 0;
            oct_drawn = set.oct;
        }

        while (midi_get(&st, &d1, &d2))
            eng_midi(st, d1, d2);

        base = (uint32_t)(KEY0 + 12 * set.oct);
        lit = eng_lit(base);
        for (k = 0; k < 27u; k++)
            fm1_led_key(KEY_LED(k), lit >> k & 1u);
        fm1_led_key(BTN_OCT_DOWN, set.oct < 0);
        fm1_led_key(BTN_OCT_UP, set.oct > 0);
        fm1_led_key(BTN_PLAY, (int)hold);
        fm1_led_key(BTN_FX, (int)same);

        if (lit != lit_drawn) { lit_drawn = lit; draw_keys(lit); }
        if (fm1_ms - t_cost >= 250u) {
            uint32_t l = cpu_last, m = cpu_max;
            t_cost = fm1_ms;
            if (l != last_drawn || m != max_drawn || bench_shed_blocks - shed_base != shed_drawn) {
                shed_drawn = bench_shed_blocks - shed_base;
                draw_bench_cost(l, m, bench_shed, shed_drawn);
                last_drawn = l;
                max_drawn = m;
            }
        }
        if (dirty && quiet() && fm1_ms - t_change > 2000u) {
            settings_save(&set, sizeof set);
            dirty = 0;
        }
    }
}

#else   /* ---- the v1 synth */

/* what survives a power-off: validated after the load (version byte, ranges), 102 of the 256 bytes */
typedef struct {
    uint8_t ver;                                /* SET_VER (panel.h): another version = the defaults */
    int8_t oct;                                 /* -2..2 */
    uint8_t look;                               /* HOME look UI_H_*: 0 waterfall, 1 spectrum (default), 2 minimal */
    uint8_t slot;                               /* the entry last loaded: 0..7 factory, 8..39 user patch U01..U32 (panel.h) */
    uint8_t uslot;                              /* the old single-patch overlay: the factory slot it replaced, 0xFF none; migrated to U01 at boot */
    uint8_t pad[3];
    int16_t uval[P_COUNT_V1];                    /* the old overlay's values (read once for the migration, never written again) */
} app_set_t;
typedef char app_set_fits[sizeof(app_set_t) <= 256u ? 1 : -1];

/* what the key scan saw at boot (the recovery_key() scan, copied first thing in app_main): shown on GLO page 3 */
static uint32_t boot_btn, boot_frames;
static uint8_t boot_raw[11];

#include "panel.h"

/* the sequencer stops: SEQ off (keys play plain notes, or the arp), the pattern kept. A performance switch, not a patch edit */
static void seq_stop(void)
{
    sq_run = 0;
    patch_set(P_SEQ, 0);
    pn.base[P_SEQ] = (int16_t)patch.p[P_SEQ];
}

static void app_main(void)
{
    app_set_t set;
    uint32_t prev = 0, started = 0, k, t_change = 0, dirty = 0, t_cpu = 0, cpu = 0, cpu_mx = 0, gen_seen = 0;
    uint8_t held[27] = {0};                     /* the note each key plays */
    uint32_t route_prev = 0, t_rejoin = 0, t_rec = 0, rec_arm = 0;   /* where keys go (1 arp, 2 sequencer, 0 plain), last pass; REC pressed and not yet decided */
    uint8_t latch = 0, lat[16] = {0};           /* LATCH (the PLAY button): notes kept sounding after the key is up, 1 bit per note */

    boot_btn = fm1_in.buttons;                  /* before anything else */
    boot_frames = fm1_in.frames;
    for (k = 0; k < 11u; k++) boot_raw[k] = fm1_in.raw[k];
    memset(&set, 0, sizeof set);
    if (settings_load(&set, sizeof set)) set.ver = 0;       /* nothing (or another size): the defaults */
    pn_validate(&set);
    pl_init();                                  /* the patch log: a scan, no write */
    pn_migrate(&set, &dirty);                   /* the old single saved patch becomes U01 */
    if (dirty) { settings_save(&set, sizeof set); dirty = 0; }      /* now: the overlay must not outlive its copy (boot is quiet) */
    pn_init(&set);
    if (set.slot != pn.slot) { set.slot = pn.slot; dirty = 1; t_change = fm1_ms; }   /* a user patch that is gone: patch 1 */

    for (;;) {
        uint32_t keys, down, up, rel, b, hb, lit, base, edit_held, m, save_shown, rec_pass;
        uint8_t st, d1, d2;
        platform_poll();
        if (pl_link_dirty) {                    /* the computer wrote or deleted patches (patchlink.h): the names the panel shows */
            uint32_t pm = pl_link_dirty, pi;
            pl_link_dirty = 0;
            for (pi = 0; pi < PL_IDS; pi++) if (pm >> pi & 1u) pn_ucache(pi);
            if (pn.slot >= PN_PRESETS && (pm >> (pn.slot - PN_PRESETS) & 1u)) pn_load(pn.slot, &set);     /* the loaded patch was replaced or deleted from the computer: show and play what the log holds (else SAVE would name and replace the wrong one) */
        }

        {
            uint32_t w = wt_ui_poll();          /* a table upload (wtlink.h) owns the screen */
            if (w == 1u) {                      /* keys, knobs, buttons and MIDI are ignored until it ends */
                uint8_t s0, d0, d1x;
                (void)fm1_input_edges(&rel);
                for (k = 0; k < 7u; k++) (void)fm1_enc_take(k);
                while (midi_get(&s0, &d0, &d1x)) ;
                prev = fm1_in.notes;
                continue;
            }
            if (w == 2u) memset(&pn_shadow, 0, sizeof pn_shadow);     /* the next ui_draw repaints everything */
            wt_user_poll((uint32_t)patch_get(P_TABLE1), (uint32_t)patch_get(P_TABLE2));   /* a user table the patch uses: CRC first */
            if (wt_gen != gen_seen) {           /* what a table index plays changed: draw the table pictures again */
                gen_seen = wt_gen;
                memset(&pn_shadow, 0, sizeof pn_shadow);
                pn.wave_key = ~0u;
            }
        }

        hb = fm1_in.buttons;                    /* held now */
        edit_held = hb >> BTN_EDIT & 1u;
        b = fm1_input_edges(&rel);              /* pressed since the last pass */
        {   int shift = 0;
            if (b & (1u << BTN_OCT_DOWN) && set.oct > -2) { set.oct--; shift -= 12; dirty = 1; t_change = fm1_ms; }
            if (b & (1u << BTN_OCT_UP) && set.oct < 2) { set.oct++; shift += 12; dirty = 1; t_change = fm1_ms; }
            if (shift && latch) {               /* latched notes move with the octave: let them go, start them again shifted */
                uint8_t nl[16] = {0};
                for (k = 0; k < 128u; k++)
                    if (lat[k >> 3] >> (k & 7u) & 1u) { stop((uint8_t)k); midi_send(0x80, k, 0); }
                for (k = 0; k < 128u; k++)
                    if (lat[k >> 3] >> (k & 7u) & 1u) {
                        int n = (int)k + shift;
                        if (n < 0 || n > 127) continue;
                        nl[n >> 3] |= (uint8_t)(1u << (n & 7));
                        play((uint8_t)n, 100); midi_send(0x90, (uint32_t)n, 100);
                    }
                for (k = 0; k < 16u; k++) lat[k] = nl[k];
            }
        }
        if (pn.clr && (b & ~(1u << BTN_REC) || fm1_ms - pn.t_clr > 5000u)) pn.clr = 0;    /* "CLEAR PATTERN?": any other button or 5 s cancels */
        if (b & (1u << BTN_REC) && pn.clr) {    /* REC again: clear the pattern */
            pn.clr = 0;
            sq.len = 0; sq.cur = 0;
            for (k = 0; k < SEQ_MAXN; k++) { sq.note[k] = 0; sq.vel[k] = 0; sq.tie[k] = 0; ml[k] = 0; }
            pn_msg("CLEARED", 0, 0);
        } else if (b & (1u << BTN_REC)) { rec_arm = 1; t_rec = fm1_ms; }
        if (rec_arm) {
            if (!(hb >> BTN_REC & 1u)) {        /* a short press: step input on / off (off keeps what was entered as the pattern) */
                rec_arm = 0;
                if (!sq.rec) { sq.cur = 0; sq.rec = 1; pn_goto(UI_P_SEQ, 0); }   /* (and its page shows the steps) */
                else { if (sq.cur) sq.len = sq.cur; sq.rec = 0; seq_stop(); }      /* the pattern waits: PLAY on the SEQ page starts it */
            } else if (fm1_ms - t_rec >= 700u) {    /* held: ask first (REC again clears the pattern) */
                rec_arm = 0;
                if (sq.len || sq.rec) { pn.clr = 1; pn.t_clr = fm1_ms; }
            }
        }
        rec_pass = sq.rec;                      /* (a button that ends or enters the last step does nothing else in this pass) */
        if (sq.rec && b & (1u << BTN_PLAY) && sq.cur < SEQ_MAXN) {         /* step input: PLAY is a rest */
            sq.note[sq.cur] = 0; sq.vel[sq.cur] = 0; sq.tie[sq.cur] = 0;
            sq.cur++;
        }
        if (sq.rec && b & (1u << BTN_SEQ) && sq.cur && sq.cur < SEQ_MAXN) {   /* step input: SEQ ties the step before */
            sq.note[sq.cur] = sq.note[sq.cur - 1u]; sq.vel[sq.cur] = sq.vel[sq.cur - 1u]; sq.tie[sq.cur] = 1;
            sq.cur++;
        }
        if (sq.rec && sq.cur >= SEQ_MAXN) { sq.len = SEQ_MAXN; sq.rec = 0; seq_stop(); }  /* 32 steps: full */
        if (!rec_pass && (b & (1u << BTN_PLAY)) && pn.page == UI_P_SEQ) {  /* on the SEQ page PLAY starts / stops the sequencer (elsewhere it is LATCH) */
            if (patch_get(P_SEQ) && sq_run) seq_stop();
            else if (sq.len) { patch_set(P_SEQ, 1); pn.base[P_SEQ] = (int16_t)patch.p[P_SEQ]; sq_run = 1; }
            else pn_msg("NO PATTERN", 0, 0);
        }
        else if (!rec_pass && (b & (1u << BTN_PLAY))) {             /* LATCH on / off: on keeps the keys held now; off lets every latched note go */
            latch ^= 1u;
            for (k = 0; k < 27u; k++)
                if (latch && (started >> k & 1u)) { lat[held[k] >> 3] |= (uint8_t)(1u << (held[k] & 7u)); started &= ~(1u << k); }
            if (!latch)
                for (k = 0; k < 128u; k++)
                    if (lat[k >> 3] >> (k & 7u) & 1u) { stop((uint8_t)k); midi_send(0x80, k, 0); }
            if (!latch) for (k = 0; k < 16u; k++) lat[k] = 0;
        }
        {
            uint8_t look = set.look;
            uint32_t d0 = dirty;
            pn_buttons(rec_pass ? b & ~(1u << BTN_SEQ) : b, hb, &set, &dirty);
            if (look != set.look || dirty != d0) t_change = fm1_ms;
        }
        {   uint32_t am = sq.rec ? 0u : patch_get(P_SEQ) && sq.len ? 2u : patch_get(P_ARP) ? 1u : 0u;
            if (am == 2u && route_prev != 2u) sq_run = 1;
            if (am && am != route_prev) t_rejoin = fm1_ms + 15u;   /* the arp or the sequencer came on over sounding notes (held keys, latched): after a block or two they become its notes */
            route_prev = am;
            if (t_rejoin && fm1_ms >= t_rejoin) {
                t_rejoin = 0;
                if (am) {
                    for (k = 0; k < 128u; k++)
                        if (latch && lat[k >> 3] >> (k & 7u) & 1u) { stop((uint8_t)k); play((uint8_t)k, 100); }   /* (the plain voice ends, the arp takes the note) */
                    for (k = 0; k < 27u; k++)
                        if (started >> k & 1u) { stop(held[k]); play(held[k], 100); }       /* keys still down */
                }
            }
        }

        keys = fm1_in.notes;                    /* bit k: key k (0 = F3 .. 26 = G5) */
        down = keys & ~prev;
        up = prev & ~keys;
        prev = keys;
        base = (uint32_t)(KEY0 + 12 * set.oct);
        for (k = 0; k < 27u; k++) {             /* (a key stops the note it started: the octave may change meanwhile) */
            if (down >> k & 1u) {
                int chip = k == 1u ? 0 : k == 3u ? 1 : k == 5u ? 2 : k == 8u ? 3 : -1;   /* the OP1..OP4 black keys */
                if (edit_held && chip >= 0) pn_goto(UI_P_EDIT, (uint32_t)chip);
                else if (latch && !rec_pass) {    /* latched: the same note again lets it go; the key's release does nothing */
                    uint32_t n = base + k;
                    if (lat[n >> 3] >> (n & 7u) & 1u) { lat[n >> 3] &= (uint8_t)~(1u << (n & 7u)); stop((uint8_t)n); midi_send(0x80, n, 0); }
                    else { lat[n >> 3] |= (uint8_t)(1u << (n & 7u)); play((uint8_t)n, 100); midi_send(0x90, n, 100); }
                }
                else {
                    started |= 1u << k; held[k] = (uint8_t)(base + k); play(held[k], 100); midi_send(0x90, held[k], 100);
                    if (rec_pass) lat[held[k] >> 3] &= (uint8_t)~(1u << (held[k] & 7u));     /* (a latched note played again as a step is not latched any more) */
                    if (sq.rec && sq.cur < SEQ_MAXN) { sq.note[sq.cur] = held[k]; sq.vel[sq.cur] = 100; sq.tie[sq.cur] = 0; sq.cur++; }    /* step input */
                }
            }
            if (up >> k & 1u && started >> k & 1u) { started &= ~(1u << k); stop(held[k]); midi_send(0x80, held[k], 0); }
        }

        pn_encoders();
        fx_sync();
        if (pn.browse_pend && fm1_ms - pn.t_browse >= 400u) {   /* the patch name turned, then 400 ms of quiet */
            pn_load(pn.browse, &set);
            if (latch)                          /* a new patch may release the voices (voice mode): the latched notes start again on it */
                for (k = 0; k < 128u; k++)
                    if (lat[k >> 3] >> (k & 7u) & 1u) play((uint8_t)k, 100);
            if (set.slot != pn.slot) { set.slot = pn.slot; dirty = 1; t_change = fm1_ms; }
        }
        save_shown = pn_save_poll(&set, &dirty);
        if (save_shown) t_change = fm1_ms;      /* (a changed last patch is written to the settings 2 s later) */

        while (midi_get(&st, &d1, &d2)) {       /* USB-MIDI in, any channel */
            if ((st & 0xF0u) == 0xB0u && (d1 == 120u || d1 == 123u))     /* all sound / notes off: nothing is latched any more */
                for (k = 0; k < 16u; k++) lat[k] = 0;
            if (st == 0xF8u) arp_ext.ticks++;                            /* MIDI clock and start: the arp follows them */
            else if (st == 0xFAu) arp_ext.start++;
            eng_midi(st, d1, d2);
        }

        if (fm1_ms - t_cpu >= 250u) {           /* the ISR cost, 4 times a second */
            uint32_t t = cpu_max > 4000000u ? 4000000u : cpu_max;
            t_cpu = fm1_ms;
            cpu = (eng_cpu_permille() + 5u) / 10u;
            cpu_mx = (t * 1000u / AUDIO_BUDGET_TICKS + 5u) / 10u;
        }
        pn_view_fill(cpu, cpu_mx, eng_voices_busy(), fm1_ms < pn.t_flash);
        pn_view.latch = latch;
        ui_draw(&pn_view, &pn_shadow);          /* redraws only what changed */

        lit = eng_lit(base);                    /* LEDs: what sounds, on the keys of this octave */
        if (sq.rec) {                           /* step input: the steps entered, the next one blinking */
            lit = sq.cur >= 27u ? 0x7FFFFFFu : (1u << sq.cur) - 1u;
            if (sq.cur < 27u && fm1_ms >> 8 & 1u) lit |= 1u << sq.cur;
        } else if (pn.page == UI_P_SEQ && sq_tap.on && sq.len && sq_tap.step < 27u) lit = 1u << sq_tap.step;      /* the SEQ page: the step playing */
        for (k = 0; k < 27u; k++) {
            uint32_t on = lit >> k & 1u;
            if (edit_held && (k == 1u || k == 3u || k == 5u || k == 8u))     /* the oscillator chips */
                on = pn.sub == (k == 1u ? 0u : k == 3u ? 1u : k == 5u ? 2u : 3u);
            fm1_led_key(KEY_LED(k), (int)on);
        }
        m = ui_button_leds(&pn_view, fm1_ms >> 8 & 1u);
        if (fm1_ms < pn.t_flash) m |= 1u << BTN_SAVE;
        if (latch) m |= 1u << BTN_PLAY;
        if (patch_get(P_ARP)) m |= 1u << BTN_ARP;
        if (patch_get(P_SEQ)) m |= 1u << BTN_SEQ;      /* (lit with no pattern too: nothing plays until REC has entered steps) */
        if (sq.rec) m |= 1u << BTN_REC;
        if (set.oct < 0) m |= 1u << BTN_OCT_DOWN;
        if (set.oct > 0) m |= 1u << BTN_OCT_UP;
        for (k = 0; k < 14u; k++) fm1_led_key(k, (int)(m >> k & 1u));

        if (dirty && quiet() && fm1_ms - t_change > 2000u) {   /* 2 s after the change, when silent (an erase stalls the sound) */
            settings_save(&set, sizeof set);
            dirty = 0;
        }
    }
}
#endif
