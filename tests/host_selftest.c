/* SPDX-License-Identifier: GPL-3.0-only */
/* Self-check of the host fake (tests/host.c): encoder deltas, key edges, LED log.
 *   cc -Wall -Wno-unused-function -DDINGHY_HOST -Ifirmware/src -Ifirmware/gen -o build/host/selftest tests/host_selftest.c
 *   build/host/selftest */
#include <assert.h>
enum { BTN_OCT_DOWN = 0, BTN_OCT_UP = 1 };
#define main host_main
#include "host.c"
#undef main
static void app_audio(int32_t *out, uint32_t n) { (void)out; (void)n; }
static void app_main(void) {}

int main(void)
{
    static const host_ev_t ev[] = {
        {10, 0, -1, 0, 0, 0, 2, 3},   {20, 0, -1, 0, 0, 0, 2, -1}, {30, 0, -1, 0, 0, 0, 6, 5},
        {40, 1u << 7, -1, 0, 0, 0, 0, 0}, {50, 1u << 7 | 1u << 2, -1, 0, 0, 0, 0, 0},
    };
    uint32_t i;
    script = ev; script_n = sizeof ev / sizeof ev[0]; end_ms = 100000;
    for (i = 0; i < 9; i++) platform_poll();
    assert(fm1_enc_take(2) == 0);                 /* nothing yet */
    for (i = 9; i < 25; i++) platform_poll();     /* ms 10, 20 */
    assert(fm1_enc_take(2) == 2);                 /* +3 -1, accumulated */
    assert(fm1_enc_take(2) == 0);                 /* reset by the take */
    assert(fm1_enc_take(6) == 0);
    for (i = 25; i < 35; i++) platform_poll();
    assert(fm1_enc_take(6) == 5 && fm1_enc_take(6) == 0 && fm1_enc_take(0) == 0);
    assert(fm1_enc_take(7) == 0);                 /* out of range */
    for (i = 35; i < 45; i++) platform_poll();
    assert(fm1_input_note_edges() == 1u << 7 && fm1_input_note_edges() == 0);
    for (i = 45; i < 55; i++) platform_poll();
    assert(fm1_input_note_edges() == 1u << 2 && fm1_in.notes == (1u << 7 | 1u << 2));
    fm1_led_key(3, 1); fm1_led_key(3, 1); fm1_led_key(3, 0);   /* logs on, off, once each */
    puts("host selftest ok");
    return 0;
}
