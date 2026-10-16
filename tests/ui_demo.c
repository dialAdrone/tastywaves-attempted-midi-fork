/* SPDX-License-Identifier: GPL-3.0-only */
/* UI design demo (T18): drives firmware/src/ui_screens.h with fake display values on the host fake FM-1. No engine,
 * no sound. Controls as on the panel: HOME cycles the three home looks, EDIT / ENV pages (ENV again: filter envelope),
 * SAVE clears "modified", ALGORITHM = table, SELECT = position, PRESETS = patch name, KNOB1-4 = the page's values,
 * keys = voices. Build and run: tests/run_ui.sh (script: tests/scripts/ui_demo.txt). */
#include <stdint.h>
static void patch_set(uint32_t id, int32_t v) { (void)id; (void)v; }   /* (the host script's "patch" line; no engine here) */
#include "platform.c"
#include "wavetables.h"
#include "ui_screens.h"

static void app_audio(int32_t *out, uint32_t n) { (void)out; (void)n; }

static const char *const PATCH[] = {"001 BRIGHT PAD", "002 GLASS BELL", "003 WARM VOWEL", "004 FOLD LEAD", "005 SUB BASS"};
#define NPATCH 5
static const char *const HOME_L[4] = {"CUTOFF", "RESO", "ATK", "REL"};
static const char *const EDIT_L[4] = {"POS", "DETUNE", "LEVEL", "BEND"};
static const char *const ENV_L[4] = {"ATK", "DEC", "SUS", "REL"};

static int32_t clampi(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }

static void app_main(void)
{
    static ui_view_t v;
    static ui_shadow_t sh;
    uint8_t home[4] = {90, 40, 10, 60}, edit_lvl = 100, edit_bend = 0;
    int8_t detune = 7;
    uint32_t patch = 0, prev_keys = 0, t_cpu = 0;
    int32_t pos = 1500;
    uint32_t i;

    v.page = UI_P_HOME;
    v.variant = UI_H_WATERFALL;
    v.touched = 4;
    v.voices_max = 12;
    v.nframes = 32;
    v.stride = WT_FRAME_LEN;
    v.env[0][0] = 6; v.env[0][1] = 60; v.env[0][2] = 80; v.env[0][3] = 45;
    v.env[1][0] = 30; v.env[1][1] = 90; v.env[1][2] = 40; v.env[1][3] = 70;
    v.table = 0;

    for (;;) {
        uint32_t b, rel, keys, k, nv = 0;
        int32_t d;
        platform_poll();

        b = fm1_input_edges(&rel);
        if (b & 1u << BTN_HOME) {
            if (v.page == UI_P_HOME) v.variant = (uint8_t)((v.variant + 1u) % UI_H_N);
            v.page = UI_P_HOME;
            v.sub = 0;
        }
        if (b & 1u << BTN_EDIT) { v.sub = v.page == UI_P_EDIT ? (uint8_t)((v.sub + 1u) & 3u) : 0; v.page = UI_P_EDIT; }
        if (b & 1u << BTN_ENV) { v.sub = v.page == UI_P_ENV ? (uint8_t)(v.sub ^ 1u) : 0; v.page = UI_P_ENV; }
        if (b & 1u << BTN_SAVE) v.modified = 0;

        d = fm1_enc_take(ENC_SELECT);
        if (d) { pos = clampi(pos + d * 32, 0, 4095); v.modified = 1; }
        d = fm1_enc_take(ENC_ALGORITHM);
        if (d) { v.table = (uint8_t)clampi((int32_t)v.table + d, 0, (int32_t)WT_TABLES - 1); v.modified = 1; }
        d = fm1_enc_take(ENC_PRESETS);
        if (d) { patch = (uint32_t)clampi((int32_t)patch + d, 0, NPATCH - 1); v.modified = 0; }
        for (i = 0; i < 4u; i++) {
            d = fm1_enc_take(ENC_KNOB1 + i);
            if (!d) continue;
            v.touched = (uint8_t)i;
            v.modified = 1;
            if (v.page == UI_P_ENV) v.env[v.sub][i] = (uint8_t)clampi(v.env[v.sub][i] + d * 2, 0, 127);
            else if (v.page == UI_P_EDIT) {
                if (i == 0) pos = clampi(pos + d * 8, 0, 4095);
                else if (i == 1) detune = (int8_t)clampi(detune + d, -50, 50);
                else if (i == 2) edit_lvl = (uint8_t)clampi(edit_lvl + d, 0, 100);
                else edit_bend = (uint8_t)clampi(edit_bend + d * 2, 0, 127);
            } else
                home[i] = (uint8_t)clampi(home[i] + d * 2, 0, 127);
        }

        keys = fm1_in.notes;
        for (k = 0; k < 27u; k++) nv += keys >> k & 1u;
        if (keys != prev_keys) { prev_keys = keys; }
        v.voices = (uint8_t)nv;
        if (fm1_ms - t_cpu >= 250u) { t_cpu = fm1_ms; v.cpu = (uint8_t)(nv ? 12u + nv * 3u + (fm1_ms >> 8) % 3u : 2u); }

        /* fill the view from the state: strings, fractions, the frame */
        ui_copy(v.patch, PATCH[patch], 16);
        ui_copy(v.tname, WT_DIR[v.table].name, 8);
        v.pos = (uint16_t)pos;
        v.frame = (uint8_t)clampi((pos + 64) >> 7, 0, 31);
        v.frames = WT_DATA + WT_DIR[v.table].offset;
        v.wave2 = v.frames + (uint32_t)v.frame * WT_FRAME_LEN + WT_LEVEL_OFF[2];
        v.kbip = 0;
        v.ksmall = 0;
        if (v.page == UI_P_EDIT) {
            char *p;
            for (i = 0; i < 4u; i++) v.klabel[i] = EDIT_L[i];
            ui_num(v.kval[0], pos >> 5);
            p = v.kval[1];
            if (detune > 0) *p++ = '+';
            ui_num(p, detune);
            ui_num(v.kval[2], edit_lvl);
            ui_num(v.kval[3], edit_bend);
            v.kfrac[0] = (uint8_t)(pos >> 4);
            v.kfrac[1] = (uint8_t)((detune + 50) * 255 / 100);
            v.kfrac[2] = (uint8_t)(edit_lvl * 255 / 100);
            v.kfrac[3] = (uint8_t)(edit_bend * 2);
            v.kbip = 2;
        } else if (v.page == UI_P_ENV) {
            const uint8_t *e = v.env[v.sub];
            for (i = 0; i < 4u; i++) v.klabel[i] = ENV_L[i];
            ui_fmt_time(v.kval[0], e[0]);
            ui_fmt_time(v.kval[1], e[1]);
            ui_num(v.kval[2], e[2] * 100 / 127);
            ui_fmt_time(v.kval[3], e[3]);
            for (i = 0; i < 4u; i++) v.kfrac[i] = (uint8_t)(e[i] * 2);
            v.ksmall = 15;
        } else {
            for (i = 0; i < 4u; i++) { v.klabel[i] = HOME_L[i]; ui_num(v.kval[i], home[i]); v.kfrac[i] = (uint8_t)(home[i] * 2); }
        }

        ui_draw(&v, &sh);

        {
            uint32_t led = ui_button_leds(&v, fm1_ms >> 8 & 1u);
            for (i = 0; i < 14u; i++) fm1_led_key(i, (led >> i) & 1u);
            for (k = 0; k < 27u; k++) fm1_led_key(KEY_LED(k), keys >> k & 1u);
        }
    }
}
