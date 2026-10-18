/* SPDX-License-Identifier: GPL-3.0-only */
/* The arpeggiator engine (firmware/src/arp.h) on its own: a fake note sink records every on / off with the sample time.
 * Checks the modes, octaves, gate, swing, rates, tempo, external clock, latch-style holds, switching off, clears, and a
 * random storm that must never leave a note hanging. Build and run: tests/run_arp.sh */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXEV 4096
static struct { uint32_t t; int on, note; } ev[MAXEV];
static int nev;
static uint32_t now;                              /* samples */
static uint8_t snd[128];                          /* what the fake voices have on */
static int bad;
#define ARP_NOTE_ON(n, v) sink(1, (n))
#define ARP_NOTE_OFF(n) sink(0, (n))
static void sink(int on, uint32_t n)
{
    if (n > 127u) { printf("FAIL  note %u out of range\n", n); bad++; return; }
    if (nev < MAXEV) { ev[nev].t = now; ev[nev].on = on; ev[nev].note = (int)n; nev++; }
    snd[n] = (uint8_t)on;
}
#include "../firmware/src/arp.h"

static void run(uint32_t blocks) { while (blocks--) { arp_run(256); now += 256; } }
static void reset(void)
{
    memset(&ar, 0, sizeof ar); ar.rnd = 12345u; memset(snd, 0, sizeof snd);
    arp_cfg.mode = ARP_OFF; arp_cfg.rate = ARP_R16; arp_cfg.oct = 1; arp_cfg.gate = 64; arp_cfg.swing = 0; arp_cfg.bpm = 120;
    arp_ext.ticks = arp_ext.start = 0;
    nev = 0; now = 0;
}
static int fails;
static void check(int ok, const char *what) { printf("%s  %s\n", ok ? "ok  " : "FAIL", what); if (!ok) fails++; }
static int ons(int *out, int max)
{
    int i, n = 0;
    for (i = 0; i < nev && n < max; i++) if (ev[i].on) out[n++] = ev[i].note;
    return n;
}
static int seq_is(const int *want, int n)
{
    int got[64], k = ons(got, 64);
    if (k < n) return 0;
    return memcmp(got, want, (size_t)n * sizeof(int)) == 0;
}
static void key_on(uint32_t n) { if (arp_on()) arp_in_on(n, 100); else sink(1, n); }     /* what events_drain does */
static void key_off(uint32_t n) { if (arp_on() || ar.active) { arp_in_off(n); if (!arp_sounding(n)) sink(0, n); } else sink(0, n); }
static void hold3(void) { arp_in_on(60, 100); arp_in_on(64, 100); arp_in_on(67, 100); }
static uint32_t nth_on_time(int k)
{
    int i, c = 0;
    for (i = 0; i < nev; i++) if (ev[i].on && c++ == k) return ev[i].t;
    return 0;
}

int main(void)
{
    int i, got[64], hung;
    { int w[] = {60, 64, 67, 60, 64, 67};
      reset(); arp_cfg.mode = ARP_UP; hold3(); run(400);
      check(seq_is(w, 6), "UP: C E G C E G"); }
    { int w[] = {67, 64, 60, 67};
      reset(); arp_cfg.mode = ARP_DOWN; hold3(); run(400); check(seq_is(w, 4), "DOWN: G E C G"); }
    { int w[] = {60, 64, 67, 64, 60, 64};
      reset(); arp_cfg.mode = ARP_UPDN; hold3(); run(400); check(seq_is(w, 6), "UP-DOWN: C E G E C E (ends not repeated)"); }
    { int w[] = {60, 64, 67, 67, 64, 60, 60};
      reset(); arp_cfg.mode = ARP_UPDN2; hold3(); run(400); check(seq_is(w, 7), "UP-DOWN 2: C E G G E C C (ends repeated)"); }
    { int w[] = {67, 60, 64, 67, 60};
      reset(); arp_cfg.mode = ARP_PLAYED; arp_in_on(67, 100); arp_in_on(60, 100); arp_in_on(64, 100); run(400);
      check(seq_is(w, 5), "PLAYED: in the order pressed (G C E G C)"); }
    { int w[] = {60, 64, 67, 72, 76, 79, 60};
      reset(); arp_cfg.mode = ARP_UP; arp_cfg.oct = 2; hold3(); run(400); check(seq_is(w, 7), "UP, 2 octaves"); }
    { int w[] = {79, 76, 72, 67};
      reset(); arp_cfg.mode = ARP_DOWN; arp_cfg.oct = 2; hold3(); run(400); check(seq_is(w, 4), "DOWN, 2 octaves"); }
    {   int c3 = 0;
        reset(); arp_cfg.mode = ARP_CHORD; hold3(); run(20);
        for (i = 0; i < nev && ev[i].t == ev[0].t; i++) c3 += ev[i].on;
        check(c3 == 3, "CHORD: all three on in the same step"); }
    {   int seen[3] = {0}, k, n;
        reset(); arp_cfg.mode = ARP_RND; arp_cfg.rate = ARP_R32; hold3(); run(400);
        n = ons(got, 64);
        for (k = 0; k < n; k++) seen[got[k] == 60 ? 0 : got[k] == 64 ? 1 : got[k] == 67 ? 2 : 0] += (got[k] == 60 || got[k] == 64 || got[k] == 67);
        check(n > 20 && seen[0] && seen[1] && seen[2], "RANDOM: only held notes, all of them appear"); }
    {   uint32_t a, b;
        reset(); arp_cfg.mode = ARP_UP; hold3(); run(400);
        a = nth_on_time(1) - nth_on_time(0); b = nth_on_time(5) - nth_on_time(4);
        check(a >= 5300 && a <= 5700 && b >= 5300 && b <= 5700, "120 BPM 1/16 = 125 ms steps (5512 samples +- one block)");
        reset(); arp_cfg.mode = ARP_UP; arp_cfg.bpm = 60; arp_cfg.rate = ARP_R8; hold3(); run(200);
        a = nth_on_time(2) - nth_on_time(1);
        check(a >= 21800 && a <= 22300, "60 BPM 1/8 = 500 ms (22050 samples)"); }
    {   uint32_t a, b;
        reset(); arp_cfg.mode = ARP_UP; arp_cfg.swing = 50; hold3(); run(200);
        a = nth_on_time(1) - nth_on_time(0); b = nth_on_time(2) - nth_on_time(1);
        check(a > b * 2 && a + b >= 10800 && a + b <= 11300, "swing 50 %: the odd step late, the pair still 2 steps long"); }
    {   uint32_t on, off; int k;
        reset(); arp_cfg.mode = ARP_UP; arp_cfg.gate = 32; hold3(); run(20);
        on = ev[0].t; off = 0;
        for (k = 1; k < nev; k++) if (!ev[k].on && ev[k].note == ev[0].note) { off = ev[k].t; break; }
        check(off > on && off - on >= 1100 && off - on <= 1900, "gate 32/128 of a step: off after about 1378 samples"); }
    {   uint32_t t1, t2, t3;
        reset(); arp_cfg.mode = ARP_UP; hold3(); arp_cfg.bpm = 120; run(50); t1 = nth_on_time(3) - nth_on_time(2);
        arp_cfg.bpm = 240; run(200); t2 = nth_on_time(12) - nth_on_time(11); t3 = 0;
        check(t1 > 5000 && t2 > 2500 && t2 < 3100 && t3 == 0, "tempo change takes effect (240 BPM: half the step)"); }
    {   int w[] = {60, 64, 67};
        reset(); arp_cfg.mode = ARP_UP; hold3(); run(60); arp_in_off(60); arp_in_off(64); arp_in_off(67); run(80);
        hung = 0; for (i = 0; i < 128; i++) hung += snd[i];
        check(hung == 0 && seq_is(w, 3), "all keys up: the last note ends, nothing hangs");
        i = nev; run(100); check(nev == i, "and the arp stays silent"); }
    {   reset(); arp_cfg.mode = ARP_UP; arp_in_on(60, 100); run(5); arp_in_off(60); run(40); arp_in_on(64, 100); run(1);
        i = ons(got, 8);
        check(i >= 2 && got[i - 1] == 64, "a new first key starts at once"); }
    {   reset(); arp_cfg.mode = ARP_UP; hold3(); run(10); arp_cfg.mode = ARP_OFF; run(1);
        check(snd[60] && snd[64] && snd[67], "arp off with keys held: they sound as plain notes");
        arp_in_off(60); run(1);
        check(snd[64] && snd[67], "(and the list is empty)"); }
    {   reset(); arp_cfg.mode = ARP_UP; hold3(); run(10); arp_clear(); run(30);
        hung = 0; for (i = 0; i < 128; i++) hung += snd[i];
        check(hung == 0 && ar.n == 0, "clear (all notes off): silence and an empty list"); }
    {   int base;
        reset(); arp_cfg.mode = ARP_UP; arp_cfg.rate = ARP_R16; hold3(); run(1);
        base = nev; arp_ext.ticks += 1; run(1); arp_ext.ticks += 1; run(1);
        check(arp_tap.ext == 1, "MIDI clock seen: following it");
        for (i = 0; i < 6; i++) { arp_ext.ticks++; run(4); }
        check(nev > base, "a step comes every 6 clock ticks");
        { int c0 = ons(got, 64); for (i = 0; i < 12; i++) { arp_ext.ticks++; run(4); }
          check(ons(got, 64) - c0 == 2, "12 ticks = 2 steps of 1/16"); }
        { uint32_t k, bb;
          for (k = 0; k < 48; k++) { arp_ext.ticks++; run(4); }
          bb = arp_tap.beat_blocks;
          check(bb == 96, "external tempo: 24 ticks of 4 blocks = a quarter of 96 blocks (10336 / 96 = 108 BPM)"); }
        run(200); check(arp_tap.ext == 0, "no clock for 0.5 s: back to the internal tempo"); }
    {   uint32_t seed = 99u; int step;
        reset(); arp_cfg.mode = ARP_UP;
        for (step = 0; step < 20000; step++) {
            seed = seed * 1664525u + 1013904223u;
            switch ((seed >> 20) % 10u) {
            case 0: case 1: key_on(48 + (seed >> 8) % 40u); break;
            case 2: case 3: key_off(48 + (seed >> 8) % 40u); break;
            case 4: arp_cfg.mode = (int32_t)((seed >> 8) % ARP_NMODES); break;
            case 5: arp_cfg.oct = 1 + (int32_t)((seed >> 8) & 3u); break;
            case 6: arp_cfg.rate = (int32_t)((seed >> 8) % ARP_NRATES); arp_cfg.gate = 1 + (int32_t)((seed >> 12) % 127u); break;
            case 7: arp_cfg.swing = (int32_t)((seed >> 8) % 51u); arp_cfg.bpm = 40 + (int32_t)((seed >> 12) % 201u); break;
            case 8: if (!((seed >> 8) % 7u)) arp_clear(); break;
            default: arp_ext.ticks += (seed >> 9) & 1u; break;
            }
            if (nev > MAXEV - 64) nev = 0;
            run(1 + (seed >> 14) % 3u);
        }
        for (i = 0; i < 128; i++) key_off((uint32_t)i); arp_cfg.mode = ARP_UP; run(1); arp_clear(); run(300);
        hung = 0; for (i = 0; i < 128; i++) hung += snd[i];
        check(hung == 0 && bad == 0, "20000 random operations: no note out of range, none hangs after a clear"); }
    printf("arp_check: %d failures\n", fails + bad);
    return fails + bad != 0;
}
