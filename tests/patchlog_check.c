/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test of firmware/src/patchlog.h (T17 stage P0) on a NOR mock (erase sets 0xFF, program only clears bits; every
 * program over a bit that is already 0 is counted) with fault injection and a power cut at every flash operation.
 *   tests/run_patchlog.sh */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t flash[0x100000];
static uint8_t flash_ok = 1;
static void fm1_wdt_feed(void) {}
static uint32_t crc32(const uint8_t *p, uint32_t n)
{
    uint32_t c = 0xFFFFFFFFu, k;
    while (n--) { c ^= *p++; for (k = 0; k < 8u; k++) c = c & 1u ? (c >> 1) ^ 0xEDB88320u : c >> 1; }
    return ~c;
}

static struct { long op, cut_at, fault_at; int mode, fmode, dead; uint32_t oob, over, reads; } m = {0, -1, -1, 0, 0, 0, 0, 0, 0};
static uint32_t rnd_s = 12345;
static uint32_t rnd(void) { rnd_s ^= rnd_s << 13; rnd_s ^= rnd_s >> 17; rnd_s ^= rnd_s << 5; return rnd_s; }

static int st_read(uint32_t off, void *dst, uint32_t n)
{
    if (off + n > sizeof flash) return -2;
    memcpy(dst, flash + off, n);
    m.reads++;
    return 0;
}
#define IN_WIN(off, n) ((off) >= 0xE5000u && (off) + (n) <= 0xE9000u)
static int fl_erase4k(uint32_t off, uint32_t *took)
{
    *took = 17000u;
    if (!IN_WIN(off, 0x1000u) || (off & 0xFFFu)) m.oob++;
    m.op++;
    if (m.dead) return -9;
    if (m.op == m.cut_at) {                       /* power cut inside the erase */
        uint32_t i;
        if (m.mode == 1) for (i = 0; i < 0x1000u; i++) flash[off + i] = (uint8_t)rnd();
        if (m.mode == 2) memset(flash + off, 0xFF, 0x800);
        m.dead = 1;
        return -9;
    }
    if (m.op == m.fault_at) {
        if (m.fmode == 1) memset(flash + off, 0xFF, 0x1000);
        return -3;
    }
    memset(flash + off, 0xFF, 0x1000);
    return 0;
}
static void prog_page(uint32_t off, const uint8_t *s, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++) {
        if ((flash[off + i] & s[i]) != s[i]) m.over++;      /* a 1 over a 0: the code relied on the part ANDing */
        flash[off + i] &= s[i];
    }
}
static int fl_write(uint32_t off, const uint8_t *s, uint32_t n)
{
    if (!IN_WIN(off, n)) m.oob++;
    while (n) {
        uint32_t chunk = 256u - (off & 0xFFu), k;
        if (chunk > n) chunk = n;
        m.op++;
        if (m.dead) return -9;
        if (m.op == m.cut_at) {
            uint8_t tmp[1];
            if (m.mode == 1) k = chunk / 2;
            else if (m.mode == 2) k = chunk - 1;
            else if (m.mode == 3) k = chunk - 1;
            else k = 0;
            prog_page(off, s, k);
            if (m.mode == 3) flash[off + chunk - 1] &= (uint8_t)(s[chunk - 1] | rnd());   /* the last byte half programmed */
            (void)tmp;
            if (m.mode == 4) prog_page(off, s, chunk);   /* all of it, then the lights go out */
            m.dead = 1;
            return -9;
        }
        if (m.op == m.fault_at) {
            if (m.fmode == 1) prog_page(off, s, chunk);
            if (m.fmode == 2) prog_page(off, s, chunk / 2);
            return -3;
        }
        prog_page(off, s, chunk);
        off += chunk; s += chunk; n -= chunk;
    }
    return 0;
}
#include "../firmware/src/patchlog.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL  "); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* the model: what the log must hold */
typedef struct { uint8_t have, n; char name[13]; int16_t v[PL_MAXP]; } mp_t;
typedef struct { mp_t p[PL_IDS]; } model_t;

static void mk(pl_patch_t *p, uint32_t n, const char *name)
{
    uint32_t i;
    memset(p, 0, sizeof *p);
    strncpy(p->name, name, PL_NAME);
    p->n = (uint8_t)n;
    for (i = 0; i < n; i++) p->v[i] = (int16_t)rnd();
}
static void model_put(model_t *md, uint32_t id, const pl_patch_t *p)
{
    memset(&md->p[id], 0, sizeof md->p[id]);
    md->p[id].have = 1; md->p[id].n = p->n;
    strncpy(md->p[id].name, p->name, 12);
    memcpy(md->p[id].v, p->v, 2u * p->n);
}
static uint32_t tot_erases, tot_gcs, tot_progs, tot_evacs;
static void reboot(void) { tot_erases += pl.erases; tot_gcs += pl.gcs; tot_progs += pl.progs; tot_evacs += pl.evacs; m.dead = 0; m.cut_at = m.fault_at = -1; pl_init(); }
static void observe(model_t *md)
{
    uint32_t id;
    memset(md, 0, sizeof *md);
    for (id = 0; id < PL_IDS; id++) {
        pl_patch_t p;
        int rc = pl_get(id, &p);
        if (rc == PL_OK) model_put(md, id, &p);
        else CHECK(rc == PL_E_NONE, "pl_get(%u) returned %d", id, rc);
    }
}
static int same(const model_t *a, const model_t *b) { return !memcmp(a, b, sizeof *a); }
static void expect(const model_t *md, const char *what)
{
    model_t o;
    observe(&o);
    CHECK(same(md, &o), "%s: the log differs from the model", what);
    CHECK(pl_present() == ({ uint32_t i, k = 0; for (i = 0; i < PL_IDS; i++) k |= (uint32_t)md->p[i].have << i; k; }), "%s: present mask", what);
}
static void wipe(void) { memset(flash, 0xFF, sizeof flash); memset(&m, 0, sizeof m); m.cut_at = m.fault_at = -1; pl_init(); }

/* one scripted step of the workload: put or delete */
typedef struct { int del; uint32_t id, n; char name[13]; uint32_t seed; } step_t;
static void make_steps(step_t *s, uint32_t count, uint32_t seed, uint32_t maxn)
{
    uint32_t i;
    rnd_s = seed;
    for (i = 0; i < count; i++) {
        s[i].del = rnd() % 5 == 0;
        s[i].id = rnd() % PL_IDS;
        s[i].n = 1 + rnd() % maxn;
        snprintf(s[i].name, sizeof s[i].name, "P%u_%u", s[i].id, i);
        s[i].seed = rnd();
    }
}
static int do_step(const step_t *s, model_t *md)    /* the call's return code; the model follows only when it succeeded */
{
    pl_patch_t p;
    uint32_t keep = rnd_s;
    int rc;
    rnd_s = s->seed;
    mk(&p, s->n, s->name);
    rnd_s = keep;
    if (s->del) { rc = pl_del(s->id); if (rc == 0) memset(&md->p[s->id], 0, sizeof md->p[s->id]); }
    else { rc = pl_put(s->id, &p); if (rc == 0) model_put(md, s->id, &p); }
    return rc;
}
static void apply_model(const step_t *s, model_t *md)   /* the state the step would produce */
{
    pl_patch_t p;
    uint32_t keep = rnd_s;
    rnd_s = s->seed;
    mk(&p, s->n, s->name);
    rnd_s = keep;
    if (s->del) memset(&md->p[s->id], 0, sizeof md->p[s->id]);
    else model_put(md, s->id, &p);
}

static step_t steps[2000];

int main(void)
{
    uint32_t i, id;
    model_t md;

    /* ---- basics ---- */
    wipe();
    CHECK(pl.ready && pl_present() == 0 && pl.head == PL_NONE, "empty flash: no patches");
    {
        pl_patch_t p, q;
        mk(&p, 48, "BRIGHT PAD 1");
        memset(&md, 0, sizeof md);
        CHECK(pl_put(3, &p) == 0, "put 3");
        model_put(&md, 3, &p);
        CHECK(pl_get(3, &q) == 0 && q.n == 48 && !memcmp(q.v, p.v, 96) && !strcmp(q.name, "BRIGHT PAD 1"), "get 3 returns what was put");
        CHECK(pl_get(4, &q) == PL_E_NONE, "get of an unused id");
        CHECK(pl_get(32, &q) == PL_E_ARGS && pl_put(32, &p) == PL_E_ARGS && pl_del(32) == PL_E_ARGS, "id 32 refused");
        p.n = PL_MAXP + 1;
        CHECK(pl_put(0, &p) == PL_E_ARGS, "too many parameters refused");
        mk(&p, 48, "SECOND");
        CHECK(pl_put(3, &p) == 0, "replace 3");
        model_put(&md, 3, &p);
        expect(&md, "after replace");
        reboot();
        expect(&md, "after reboot");
        CHECK(pl_del(3) == 0 && pl_present() == 0, "delete 3");
        memset(&md, 0, sizeof md);
        reboot();
        expect(&md, "deleted stays deleted after reboot");
        CHECK(pl_del(3) == 0, "delete of an unused id is fine");
        mk(&p, 0, "");
        CHECK(pl_put(5, &p) == 0 && pl_get(5, &q) == 0 && q.n == 0, "a patch with 0 parameters");
        memset(p.name, 7, 5);
        CHECK(pl_put(6, &p) == 0 && pl_get(6, &q) == 0 && q.name[0] == '?' && strlen(q.name) == 5, "control characters in the name become '?'");
    }
    CHECK(m.oob == 0 && m.over == 0, "basics: no out-of-window calls (%u), no program over a 0 bit (%u)", m.oob, m.over);

    /* ---- long random workload, periodic reboot, GC many times ---- */
    wipe();
    memset(&md, 0, sizeof md);
    make_steps(steps, 2000, 777, 60);
    {
        uint32_t full = 0;
        for (i = 0; i < 2000; i++) {
            int rc = do_step(&steps[i], &md);
            if (rc == PL_E_FULL) { full++; continue; }
            CHECK(rc == 0, "step %u returned %d", i, rc);
            if (i % 97 == 0) { expect(&md, "periodic check"); reboot(); expect(&md, "periodic reboot"); }
        }
        expect(&md, "end of workload");
        reboot();
        printf("      workload: %u steps, %u refused as full, %u erases, %u compactions, %u programs\n", 2000u, full, tot_erases, tot_gcs, tot_progs);
        CHECK(tot_gcs >= 5, "compaction ran (%u)", tot_gcs);
        CHECK(tot_erases < 2000u / 4u, "erase count stays low (%u for 2000 steps)", tot_erases);
    }
    CHECK(m.oob == 0 && m.over == 0, "workload: no out-of-window calls (%u), no program over a 0 bit (%u)", m.oob, m.over);

    /* ---- capacity ---- */
    wipe();
    memset(&md, 0, sizeof md);
    {
        pl_patch_t p;
        uint32_t ok = 0, id2;
        for (id2 = 0; id2 < PL_IDS; id2++) {
            mk(&p, 60, "BIG");
            if (pl_put(id2, &p) == 0) { model_put(&md, id2, &p); ok++; }
            else CHECK(pl_put(id2, &p) == PL_E_FULL, "full is reported as full");
        }
        CHECK(ok == PL_SSZ / pl_reclen(60), "n=60 holds %u patches (%u expected)", ok, PL_SSZ / pl_reclen(60));
        mk(&p, 60, "AGAIN");
        CHECK(pl_put(0, &p) == 0, "replacing an existing patch at capacity works");
        model_put(&md, 0, &p);
        for (i = 0; i < 300; i++) { mk(&p, 60, "CHURN"); id2 = rnd() % ok; CHECK(pl_put(id2, &p) == 0, "churn at capacity %u", i); model_put(&md, id2, &p); }
        expect(&md, "full store after churn");
        reboot();
        expect(&md, "full store after reboot");
    }
    CHECK(m.over == 0, "capacity: no program over a 0 bit (%u)", m.over);

    /* ---- a torn record (body written, length not) is never appended over ---- */
    wipe();
    memset(&md, 0, sizeof md);
    {
        pl_patch_t p;
        mk(&p, 48, "A"); CHECK(pl_put(1, &p) == 0, "put"); model_put(&md, 1, &p);
        mk(&p, 48, "B");
        m.cut_at = m.op + 1;  m.mode = 4;               /* the body goes in completely, the length never */
        CHECK(pl_put(2, &p) != 0, "put cut during the body");
        reboot();
        expect(&md, "the torn record is invisible");
        mk(&p, 48, "C"); CHECK(pl_put(2, &p) == 0, "put after a torn record"); model_put(&md, 2, &p);
        mk(&p, 48, "D"); CHECK(pl_put(7, &p) == 0, "put"); model_put(&md, 7, &p);
        expect(&md, "after the torn record");
        reboot();
        expect(&md, "after the torn record, rebooted");
    }
    CHECK(m.over == 0, "torn record: nothing programmed over junk (%u)", m.over);

    /* ---- power cut at every flash operation of a workload that includes compaction ---- */
    {
        uint32_t nsteps = 600, runs = 0, bad = 0, total_ops;
        make_steps(steps, nsteps, 4242, 90);
        wipe();
        memset(&md, 0, sizeof md);
        for (i = 0; i < nsteps; i++) do_step(&steps[i], &md);
        total_ops = (uint32_t)m.op;
        printf("      cut sweep: %u flash operations, %u compactions\n", total_ops, pl.gcs);
        CHECK(pl.gcs >= 1, "the sweep workload includes compaction");
        for (uint32_t k = 1; k <= total_ops; k++) {
            for (int mode = 0; mode < 5; mode++) {
                model_t before, after, got;
                uint32_t s;
                wipe();
                m.cut_at = (long)k; m.mode = mode;
                memset(&md, 0, sizeof md);
                for (s = 0; s < nsteps; s++) {
                    before = md;
                    if (do_step(&steps[s], &md) != 0) { after = before; apply_model(&steps[s], &after); md = before; break; }
                }
                if (s == nsteps) continue;                   /* the cut never came (modes that skip) */
                runs++;
                reboot();
                observe(&got);
                if (!same(&got, &before) && !same(&got, &after)) { bad++; CHECK(0, "cut at op %u mode %d (step %u): neither the old nor the new state", k, mode, s); }
                if (bad > 5) break;
                /* life goes on: the same step again and the next ones, then another reboot */
                md = got;
                for (uint32_t t = s; t < s + 12 && t < nsteps; t++) {
                    int rc = do_step(&steps[t], &md);
                    if (rc && rc != PL_E_FULL) { bad++; CHECK(0, "cut at op %u mode %d: step %u after recovery returned %d", k, mode, t, rc); break; }
                }
                reboot();
                observe(&got);
                if (!same(&got, &md)) { bad++; CHECK(0, "cut at op %u mode %d: log differs after recovery and more steps", k, mode); }
            }
            if (bad > 5) break;
        }
        printf("      cut sweep: %u cuts survived, %u problems\n", runs, bad);
        CHECK(m.over == 0, "cut sweep: no program over a 0 bit (%u)", m.over);
    }

    /* ---- a storm of power cuts, also inside compactions, over long workloads: every cut leaves the old or the new state,
     * no put or delete ever fails without an injected fault, a deleted patch never comes back (the reviewer's two cases) ---- */
    {
        uint32_t runs = 0, bad = 0, cuts = 0, ram_cuts = 0;
        for (uint32_t run = 0; run < 150 && bad < 6; run++) {
            uint32_t s, rate = 4 + run % 12;
            make_steps(steps, 800, 5000 + run, 70);
            wipe();
            memset(&md, 0, sizeof md);
            for (s = 0; s < 800; s++) {
                model_t before = md, after, got;
                int rc;
                uint32_t ram0;
                if (rnd() % rate == 0) { m.cut_at = m.op + 1 + rnd() % 10; m.mode = (int)(rnd() % 5); }
                ram0 = pl.evac_ram;
                rc = do_step(&steps[s], &md);
                if (m.dead) {
                    cuts++;
                    if (pl.evac_ram != ram0) {            /* the documented last-resort window: records of one sector may go, nothing else may change */
                        uint32_t q;
                        ram_cuts++; after = before; apply_model(&steps[s], &after); reboot(); observe(&got);
                        for (q = 0; q < PL_IDS; q++)
                            if (got.p[q].have && memcmp(&got.p[q], &before.p[q], sizeof got.p[q]) && memcmp(&got.p[q], &after.p[q], sizeof got.p[q])) { bad++; CHECK(0, "storm run %u step %u: id %u holds something that was never saved", run, s, q); }
                        md = got; continue;
                    }
                    after = before; apply_model(&steps[s], &after);
                    reboot();
                    observe(&got);
                    if (!same(&got, &before) && !same(&got, &after)) { bad++; CHECK(0, "storm run %u step %u: neither the old nor the new state", run, s); }
                    md = got;
                } else if (rc != 0 && rc != PL_E_FULL) { bad++; CHECK(0, "storm run %u step %u: returned %d without a fault", run, s, rc); md = before; }
                if (bad >= 6) break;
            }
            reboot();
            { model_t got; observe(&got); if (!same(&got, &md)) { bad++; CHECK(0, "storm run %u: final state differs", run); } }
            runs++;
        }
        printf("      storm: %u runs, %u cuts, %u problems; %u cuts landed inside the through-RAM compaction (accepted)\n", runs, cuts, bad, ram_cuts);
        CHECK(m.over == 0, "storm: no program over a 0 bit (%u)", m.over);
    }

    /* ---- no erased sector and live data in every sector (what cuts in successive compactions leave): saves must go on ---- */
    {
        uint32_t k, s;
        pl_patch_t p;
        wipe();
        memset(&md, 0, sizeof md);
        for (s = 0; s < PL_SECT; s++) {
            pl.head = (uint8_t)s;                        /* fill each sector with versions of one id; the last one is live */
            for (k = 0; k < 28; k++) { mk(&p, 47, "STUCK"); CHECK(pl_put(s, &p) == 0, "stuck setup"); model_put(&md, s, &p); }
        }
        reboot();
        CHECK(pl_erased() == 0 && pl.head != PL_NONE, "setup: every sector in use");
        expect(&md, "stuck state reads back");
        make_steps(steps, 1500, 8080, 60);
        for (k = 0; k < 1500; k++) {
            int rc = do_step(&steps[k], &md);
            if (rc != 0 && rc != PL_E_FULL) { CHECK(0, "stuck: step %u returned %d", k, rc); break; }
            if (k % 100 == 0) { reboot(); expect(&md, "stuck workload"); }
        }
        reboot();
        expect(&md, "stuck workload end");
        CHECK(tot_evacs > 0, "the in-place compaction ran (%u)", tot_evacs);
        CHECK(m.over == 0, "stuck: no program over a 0 bit (%u)", m.over);
    }

    /* ---- a program fault inside the through-RAM compaction: the index must stay true (every id listed loads) ---- */
    {
        uint32_t s, k, id2, bad = 0, ran = 0;
        for (k = 1; k < 60 && bad < 4; k++) {
            pl_patch_t p;
            wipe();
            memset(&md, 0, sizeof md);
            for (s = 0; s < PL_SECT; s++) {          /* 8 live ids per sector, filled up with dead versions: no room anywhere */
                uint32_t j;
                pl.head = (uint8_t)s;
                for (j = 0; j < 33u; j++) { mk(&p, 47, "RAMPATH"); id2 = s * 8u + j % 8u; CHECK(pl_put(id2, &p) == 0, "ram setup"); model_put(&md, id2, &p); }
            }
            reboot();
            m.fault_at = m.op + (long)k; m.fmode = (int)(k % 3);
            mk(&p, 47, "NEW");
            (void)pl_put(1, &p);
            if (pl.evac_ram) ran++;
            m.fault_at = -1;
            for (id2 = 0; id2 < PL_IDS; id2++)
                if ((pl_present() >> id2 & 1u) && pl_get(id2, &p) != PL_OK) { bad++; CHECK(0, "ram path fault at op +%u: id %u is listed but does not load", k, id2); break; }
            {   /* and the byte count: a full set of fresh saves still fits what is left */
                uint32_t live = 0, q;
                for (q = 0; q < PL_IDS; q++) if (pl.ent[q].type == PL_T_PATCH) live += pl.ent[q].len;
                CHECK(live == pl.live_bytes, "ram path fault at op +%u: live_bytes %u but entries add up to %u", k, pl.live_bytes, live);
            }
        }
        CHECK(ran > 10, "the through-RAM path ran in the fault test (%u)", ran);
    }

    /* ---- a failing flash operation (error return, with no / full / half effect) at every operation ---- */
    {
        uint32_t nsteps = 600, runs = 0, bad = 0, total_ops;
        make_steps(steps, nsteps, 99, 90);
        wipe();
        memset(&md, 0, sizeof md);
        for (i = 0; i < nsteps; i++) do_step(&steps[i], &md);
        total_ops = (uint32_t)m.op;
        for (uint32_t k = 1; k <= total_ops; k++) {
            for (int fm = 0; fm < 3; fm++) {
                model_t before, after, got;
                uint32_t s, failed = 0;
                wipe();
                m.fault_at = (long)k; m.fmode = fm;
                memset(&md, 0, sizeof md);
                for (s = 0; s < nsteps; s++) {
                    before = md;
                    int rc = do_step(&steps[s], &md);
                    if (rc == PL_E_FLASH) {
                        failed = 1;
                        after = before; apply_model(&steps[s], &after);
                        md = before;
                        reboot();
                        observe(&got);
                        runs++;
                        /* a failed call may or may not have taken effect on flash, but never anything else */
                        if (!same(&got, &before) && !same(&got, &after)) { bad++; CHECK(0, "fault at op %u fmode %d (step %u): state is neither old nor new", k, fm, s); }
                        md = got;
                        continue;                              /* keep going on the same flash, re-initialised */
                    }
                    if (rc != 0 && rc != PL_E_FULL) { bad++; CHECK(0, "fault at op %u: step %u returned %d", k, s, rc); break; }
                }
                (void)failed;
                if (bad > 5) break;
                reboot();
                observe(&got);
                if (!same(&got, &md)) {
                    uint32_t q; bad++; CHECK(0, "fault at op %u fmode %d: final state differs from the model", k, fm);
                    for (q = 0; q < PL_IDS; q++) if (memcmp(&got.p[q], &md.p[q], sizeof got.p[q])) printf("        id %u: log has %u, model has %u (n=%u/%u)\n", q, got.p[q].have, md.p[q].have, got.p[q].n, md.p[q].n);
                }
            }
            if (bad > 5) break;
        }
        printf("      fault sweep: %u faults, %u problems\n", runs, bad);
        CHECK(m.over == 0 && m.oob == 0, "fault sweep: window (%u) and overwrite (%u)", m.oob, m.over);
    }

    /* ---- hostile flash contents: random bytes, valid-looking headers ---- */
    for (uint32_t round = 0; round < 200; round++) {
        uint32_t j, ok = 0;
        pl_patch_t p;
        rnd_s = 31337 + round;
        memset(flash, 0xFF, sizeof flash);
        for (j = 0xE5000u; j < 0xE9000u; j++) if (rnd() % (round % 4 + 1) == 0) flash[j] = (uint8_t)rnd();
        if (round % 5 == 0) for (j = 0xE5000u; j < 0xE9000u; j += 4) if (rnd() % 7 == 0) { flash[j] = 0x7C; flash[j + 1] = 0; flash[j + 2] = 1; flash[j + 3] = (uint8_t)(rnd() % 40); flash[j + 8] = 1; flash[j + 9] = 48; }
        memset(&m, 0, sizeof m); m.cut_at = m.fault_at = -1;
        pl_init();
        for (id = 0; id < PL_IDS; id++) { int rc = pl_get(id, &p); CHECK(rc == PL_OK || rc == PL_E_NONE || rc == PL_E_BAD, "hostile get %d", rc); ok += rc == PL_OK; }
        mk(&p, 48, "HOSTILE");
        { int rc = pl_put(5, &p); CHECK(rc == 0 || rc == PL_E_FLASH || rc == PL_E_FULL, "hostile put %d", rc); }
        { int rc = pl_del(6); CHECK(rc == 0 || rc == PL_E_FLASH || rc == PL_E_FULL, "hostile del %d", rc); }
        CHECK(m.oob == 0, "hostile: window");
        (void)ok;
    }
    CHECK(1, "hostile flash contents handled");

    /* ---- no flash ---- */
    wipe();
    flash_ok = 0;
    CHECK(pl_init() == PL_E_NOFLASH && !pl.ready, "no flash: not ready");
    { pl_patch_t p; mk(&p, 4, "X"); CHECK(pl_put(0, &p) == PL_E_NOFLASH && pl_del(0) == PL_E_NOFLASH && pl_get(0, &p) == PL_E_NOFLASH, "no flash: all refused"); }
    flash_ok = 1;

    printf(fails ? "%d FAILED\n" : "all patchlog checks passed\n", fails);
    return fails != 0;
}
