/* SPDX-License-Identifier: GPL-3.0-only */
/* VAVEWave: a wavetable synth for the M-VAVE FM-1, on the Dinghy platform (Felucca / SLOOP). Today still
 * Dinghy's 4-voice sine; the wavetable engine is being built (docs/WAVETABLE_DESIGN.md).
 * This is the file to change. Everything below it (platform.c) is done: updates, the USB rescue,
 * USB-MIDI, USB audio, keys, knobs, LEDs, the screen, a settings sector.
 *
 *   keys       play sines, 4 voices; the notes go out over USB-MIDI too
 *   OCT- OCT+  the octave (kept in flash)
 *   HOME       restart the ISR cost maximum (shown on screen: last / max per block, us and % of 5.8 ms)
 *   MASTER     the volume (platform.c)
 *   USB-MIDI   notes in play the sines, with their velocity
 * BENCH 1 (below) replaces the sine with the wavetable oscillator CPU bench (firmware/src/wtbench.h):
 *   12 voices x 2 oscillators, a held 12-note chord at start; EDIT interpolator (linear / Catmull-Rom / 8-tap sinc),
 *   SEL voices rendered (0 1 4 8 12), PLAY hold the chord on/off, FX all voices on one table / 12 tables.
 *
 * The code: engine.h (voices, app_audio, play/stop), wtbench.h (BENCH oscillators), ui.h (drawing), here the
 * main loop. One translation unit: platform.c and the headers are included, not linked.
 *
 * Rules of the road (the chip has no FPU and no libgcc): integers only, no float, no 64-bit division.
 * tests/host.c runs this file on a computer: the screen as a PNG, the sound as a WAV.
 * What platform.c offers: docs/API.md. The chip's rules: docs/RULES.md. */
#include "platform.c"

#define BENCH 1                                 /* 1 = wavetable oscillator CPU bench (throwaway), 0 = the sine synth */
#include "engine.h"
#include "ui.h"

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
#endif

static void app_main(void)
{
    struct { int8_t oct; } set = {0};           /* what survives a power-off (add yours, up to 256 bytes) */
    uint32_t prev = 0, k, t_change = 0, dirty = 0, lit_drawn = ~0u, t_cost = 0, last_drawn = ~0u, max_drawn = ~0u;
    int32_t oct_drawn = 99;
    uint8_t held[27] = {0};                     /* the note each key plays */
#if BENCH
    static const uint8_t NVS[] = {0, 1, 4, 8, NV};
    uint32_t ip = IP_LIN, nvi = sizeof NVS - 1u, same = 0, hold = 1, cfg_dirty = 1, shed_base = 0, shed_drawn = ~0u;
#else
    uint32_t note_drawn = ~0u;
#endif
    settings_load(&set, sizeof set);
    if (set.oct < -2 || set.oct > 2) set.oct = 0;

#if BENCH
    draw_bench_static();
    bench_ip = ip; bench_nv = NVS[nvi]; bench_same = same;
    bench_hold(1);
#else
    lcd_fill(0, 0, 240, 240, C_BLACK);
    draw_text_box(0, 18, 240, &FONT_L, APP_NAME, C_ACC, 1);
    draw_text_box(0, 52, 240, &FONT_S, "wavetable synth  v" APP_VER, C_OFF, 1);
#endif

    for (;;) {
        uint32_t keys, down, up, rel, b, lit = 0, base = (uint32_t)(KEY0 + 12 * set.oct);
        uint8_t st, d1, d2;
        platform_poll();

        keys = fm1_in.notes;                    /* bit k: key k (0 = F3 .. 26 = G5) */
        down = keys & ~prev;
        up = prev & ~keys;
        prev = keys;
        for (k = 0; k < 27u; k++) {                 /* (a key stops the note it started: the octave may change meanwhile) */
            if (down >> k & 1u) { held[k] = (uint8_t)(base + k); play(held[k], 100); midi_send(0x90, held[k], 100); }
            if (up >> k & 1u) { stop(held[k]); midi_send(0x80, held[k], 0); }
        }

        b = fm1_input_edges(&rel);              /* buttons pressed since the last call (bit = button id) */
        if (b & (1u << BTN_OCT_DOWN) && set.oct > -2) { set.oct--; dirty = 1; t_change = fm1_ms; }
        if (b & (1u << BTN_OCT_UP) && set.oct < 2) { set.oct++; dirty = 1; t_change = fm1_ms; }
        if (b & (1u << BTN_HOME)) cpu_reset_req++;   /* the max starts over */
#if BENCH
        if (b & (1u << BTN_EDIT)) { ip = (ip + 1u) % IP_N; bench_ip = ip; cfg_dirty = 1; }
        if (b & (1u << BTN_SEL)) { nvi = (nvi + 1u) % sizeof NVS; bench_nv = NVS[nvi]; cfg_dirty = 1; }
        if (b & (1u << BTN_FX)) { same ^= 1u; bench_same = same; cfg_dirty = 1; }
        if (b & (1u << BTN_PLAY)) { hold ^= 1u; bench_hold((int)hold); cfg_dirty = 1; }
        if (b & (1u << BTN_HOME)) shed_base = bench_shed_blocks;
        if (cfg_dirty || set.oct != oct_drawn) {     /* a new setting: a new maximum */
            if (cfg_dirty) { cpu_reset_req++; shed_base = bench_shed_blocks; }
            draw_bench_cfg(ip, NVS[nvi], same, hold, set.oct);
            cfg_dirty = 0;
            oct_drawn = set.oct;
        }
#endif

        while (midi_get(&st, &d1, &d2)) {       /* USB-MIDI in, any channel */
            if ((st & 0xF0u) == 0x90u && d2) play(d1, d2);
            else if ((st & 0xF0u) == 0x80u || (st & 0xF0u) == 0x90u) stop(d1);
        }

        base = (uint32_t)(KEY0 + 12 * set.oct);
        for (k = 0; k < NV; k++)                /* what sounds, on the keys of this octave */
            if (voice[k].gate && voice[k].note >= base && voice[k].note < base + 27u)
                lit |= 1u << (voice[k].note - base);
        for (k = 0; k < 27u; k++)
            fm1_led_key(KEY_LED(k), lit >> k & 1u);
        fm1_led_key(BTN_OCT_DOWN, set.oct < 0);
        fm1_led_key(BTN_OCT_UP, set.oct > 0);
#if BENCH
        fm1_led_key(BTN_PLAY, (int)hold);
        fm1_led_key(BTN_FX, (int)same);
#endif

        if (lit != lit_drawn) { lit_drawn = lit; draw_keys(lit); }   /* redraw only what changed */
        if (fm1_ms - t_cost >= 250u) {          /* the ISR cost, 4 times a second, when it changed */
            uint32_t l = cpu_last, m = cpu_max;
            t_cost = fm1_ms;
#if BENCH
            if (l != last_drawn || m != max_drawn || bench_shed_blocks - shed_base != shed_drawn) {
                shed_drawn = bench_shed_blocks - shed_base;
                draw_bench_cost(l, m, bench_shed, shed_drawn);
#else
            if (l != last_drawn || m != max_drawn) {
                draw_cpu(l, m);
#endif
                last_drawn = l;
                max_drawn = m;
            }
        }
#if !BENCH
        if (last_note != note_drawn) { note_drawn = last_note; draw_note(); }
        if (set.oct != oct_drawn) { oct_drawn = set.oct; draw_status(set.oct); }
#endif

        if (dirty && quiet() && fm1_ms - t_change > 2000u) {   /* 2 s after the change, when silent (an erase stalls the sound) */
            settings_save(&set, sizeof set);
            dirty = 0;
        }
    }
}
