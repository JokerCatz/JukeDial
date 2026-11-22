// SPDX-License-Identifier: LGPL-2.1-or-later
#include <stdlib.h>
#include <time.h>
#include "pc/pc_lowrate.h"
#include "pc/pc_pipe_rules.h"
#include "pc_test.h"

static pc_rsq_t *Q;
/* LOWRATE: one rule, two tables (the FakePod's CS43131 and the round board's I2S), and the block stage (carry between blocks) */
#define PICK(c, src, mx, want) CHECK_EQ_INT(pc_dac_pick_rate(c, src, mx), want)
static void test_tables(void)
{
    /* the resampling pick rule is still the shared one (the round board uses it); `f` is the FakePod table as it was before DACFS (7 codes, 192k, resampling allowed) so the rule stays covered */
    static const uint32_t OLD[] = { 32000, 44100, 48000, 88200, 96000, 176400, 192000 };
    const pc_dac_caps_t fo = { 0, 0, OLD, 7, 192000, 0, 0 }, *f = &fo, *r = &pc_dac_caps_round, *n = &pc_dac_caps_fakepod;
    /* DACFS: the FakePod takes 32 .. 384 kHz natively (never resampled); 16 .. 32 kHz are upsampled by the stage; < 16 kHz refused */
    CHECK(pc_dac_has(n, 32000) && pc_dac_has(n, 192000) && pc_dac_has(n, 352800) && pc_dac_has(n, 384000) && !pc_dac_has(n, 22050) && !pc_dac_has(n, 64000) && n->native_from == 32000 && n->resample_floor == 16000 && !r->native_from);
    uint32_t nf;
    pc_pipe_t np = pc_dac_fix(n, 22050, 384000, false, (pc_pipe_t){ 22050, 0, PC_PIPE_NATIVE }, &nf);
    CHECK_EQ_INT(np.pipe, 44100); CHECK_EQ_INT(nf, 22050);                                                   /* 22.05 -> 44.1: the stage */
    np = pc_dac_fix(n, 24000, 384000, false, (pc_pipe_t){ 24000, 0, PC_PIPE_NATIVE }, &nf);
    CHECK_EQ_INT(np.pipe, 48000); CHECK_EQ_INT(nf, 24000);                                                   /* the integer multiple (same family): 24 -> 48, not 32 */
    np = pc_dac_fix(n, 16000, 384000, false, (pc_pipe_t){ 16000, 0, PC_PIPE_NATIVE }, &nf);
    CHECK_EQ_INT(np.pipe, 32000); CHECK_EQ_INT(nf, 16000);
    np = pc_dac_fix(n, 11025, 384000, false, (pc_pipe_t){ 11025, 0, PC_PIPE_NATIVE }, &nf);
    CHECK_EQ_INT(np.pipe, 11025); CHECK_EQ_INT(nf, 0); CHECK(pc_dac_unsupported(n, np.pipe, false));          /* < 16 kHz: refused with its reason, no stage */
    np = pc_dac_fix(n, 8000, 384000, false, (pc_pipe_t){ 8000, 0, PC_PIPE_NATIVE }, &nf);
    CHECK(nf == 0 && pc_dac_unsupported(n, np.pipe, false));
    /* DACFS hard limit and the halve threshold: with both at 384 kHz everything 32 .. 384 is passthrough (no halving, no stage); halve = 192 halves 384 / 352.8 */
    CHECK(!pc_dac_over_hard(n, false, 384000, 384000) && pc_dac_over_hard(n, false, 384000, 192000) && !pc_dac_over_hard(n, true, 384000, 192000) && !pc_dac_over_hard(r, false, 384000, 48000));
    static const uint32_t PR[] = { 32000, 44100, 48000, 88200, 96000, 176400, 192000, 352800, 384000 };
    for (int pi = 0; pi < 9; pi++) {
        const uint32_t pr = PR[pi];
        pc_plan_args_t pa = pc_plan_args(pr, 0, false, 384000, 384000); pc_dac_clamp_args(n, &pa);
        pc_pipe_t pp = pc_pipe_plan(pr, pa.out_max, 0, pa.limit_why); pp = pc_dac_fix(n, pr, pc_dac_fix_max(n, false, 384000, 384000), false, pp, &nf);
        CHECK_EQ_INT(pp.pipe, pr); CHECK_EQ_INT(pp.halvings, 0); CHECK_EQ_INT(nf, 0);
    }
    { pc_plan_args_t pa = pc_plan_args(384000, 0, false, 192000, 384000); pc_dac_clamp_args(n, &pa);
      pc_pipe_t pp = pc_pipe_plan(384000, pa.out_max, 0, pa.limit_why); pp = pc_dac_fix(n, 384000, pc_dac_fix_max(n, false, 192000, 384000), false, pp, &nf);
      CHECK_EQ_INT(pp.pipe, 192000); CHECK_EQ_INT(pp.halvings, 1); CHECK_EQ_INT(nf, 0); }
    np = pc_dac_fix(n, 44100, 384000, false, (pc_pipe_t){ 44100, 0, PC_PIPE_NATIVE }, &nf);
    CHECK_EQ_INT(np.pipe, 44100); CHECK_EQ_INT(nf, 0);
    np = pc_dac_fix(n, 99999, 384000, false, (pc_pipe_t){ 0, 0, PC_PIPE_NATIVE }, &nf);
    CHECK_EQ_INT(np.pipe, 0); CHECK_EQ_INT(nf, 0);
    { pc_plan_args_t na = pc_plan_args(384000, 0, false, 384000, 384000); pc_dac_clamp_args(n, &na);          /* 384 k stays 384 k: no halving, no stage */
      np = pc_pipe_plan(384000, na.out_max, 0, na.limit_why); np = pc_dac_fix(n, 384000, na.out_max, false, np, &nf);
      CHECK_EQ_INT(np.pipe, 384000); CHECK_EQ_INT(np.halvings, 0); CHECK_EQ_INT(nf, 0);
      na = pc_plan_args(352800, 0, false, 384000, 384000); pc_dac_clamp_args(n, &na);
      np = pc_pipe_plan(352800, na.out_max, 0, na.limit_why); CHECK_EQ_INT(np.pipe, 352800); }
    CHECK(pc_dac_has(f, 32000) && pc_dac_has(f, 44100) && pc_dac_has(f, 192000) && !pc_dac_has(f, 22050) && !pc_dac_has(f, 352800));
    CHECK(pc_dac_has(r, 8000) && pc_dac_has(r, 22050) && pc_dac_has(r, 48000) && !pc_dac_has(r, 64000) && pc_dac_has(r, 96000) && pc_dac_has(r, 384000) && !pc_dac_has(r, 100000) && !pc_dac_has(r, 0));
    /* FakePod: the highest at or below the source, else the lowest above (22.05 kHz: nothing below it), same family first */
    PICK(f, 22050, 192000, 44100); PICK(f, 11025, 192000, 44100);
    PICK(f, 24000, 192000, 32000); PICK(f, 16000, 192000, 32000); PICK(f, 12000, 192000, 32000); PICK(f, 8000, 192000, 32000);
    PICK(f, 7999, 192000, 0);                          /* more than 4x: nothing fits */
    PICK(f, 64000, 192000, 48000);                     /* a gap: the highest at or below, the same family (4 kHz multiples) */
    PICK(f, 50000, 192000, 48000);                     /* no family: the highest at or below */
    PICK(f, 99999, 192000, 96000);                     /* nothing above within the stage's 96k */
    PICK(f, 99999, 384000, 96000); PICK(f, 99999, 96000, 96000); PICK(f, 99999, 48000, 48000); PICK(f, 50000, 48000, 48000);   /* FIX3 I2: the ceiling never lowers the pick */
    /* down: the source above the ceiling (a halving normally gets there first; this is the odd rate and the no-stage case) */
    PICK(f, 384000, 192000, 96000); PICK(f, 352800, 192000, 88200); PICK(f, 99999, 96000, 96000); PICK(f, 200000, 192000, 96000);   /* odd / no-halving case only (halving gets 384 -> 192 first); the stage never makes more than 96k: its CPU budget */
    PICK(f, 300000, 96000, 96000);                     /* 3.1x */
    PICK(f, 500000, 96000, 0);                         /* beyond 4x */
    PICK(f, 384000, 48000, 0);
    PICK(f, 22050, 32000, 32000);                      /* the user ceiling at 32k: the other family is better than nothing */
    /* round board */
    PICK(r, 99999, 96000, 96000); PICK(r, 64000, 384000, 48000); PICK(r, 100000, 384000, 96000); PICK(r, 200000, 192000, 96000);
    PICK(r, 22050, 384000, 22050);                      /* native here: a rate in the set picks itself */
    /* the plan: native stays, a gap or a low rate resamples, a sink takes anything, halving first */
    uint32_t from;
    pc_pipe_t p = pc_dac_fix(f, 22050, 192000, false, (pc_pipe_t){ 22050, 0, PC_PIPE_NATIVE }, &from);
    CHECK_EQ_INT(p.pipe, 44100); CHECK_EQ_INT(from, 22050);
    p = pc_dac_fix(f, 44100, 192000, false, (pc_pipe_t){ 44100, 0, PC_PIPE_NATIVE }, &from);
    CHECK_EQ_INT(p.pipe, 44100); CHECK_EQ_INT(from, 0);
    p = pc_dac_fix(f, 22050, 48000, true, (pc_pipe_t){ 22050, 0, PC_PIPE_NATIVE }, &from);   /* a Bluetooth sink */
    CHECK_EQ_INT(p.pipe, 22050); CHECK_EQ_INT(from, 0);
    p = pc_dac_fix(r, 99999, 96000, false, (pc_pipe_t){ 0, 0, PC_PIPE_NATIVE }, &from);       /* odd rate: the halving plan refused it */
    CHECK_EQ_INT(p.pipe, 96000); CHECK_EQ_INT(from, 99999);
    p = pc_dac_fix(r, 22050, 384000, false, (pc_pipe_t){ 22050, 0, PC_PIPE_NATIVE }, &from);  /* round board: native */
    CHECK_EQ_INT(p.pipe, 22050); CHECK_EQ_INT(from, 0);
    p = pc_dac_fix(f, 7000, 192000, false, (pc_pipe_t){ 7000, 0, PC_PIPE_NATIVE }, &from);    /* nothing fits: as it is, unsupported */
    CHECK_EQ_INT(p.pipe, 7000); CHECK_EQ_INT(from, 0); CHECK(pc_dac_unsupported(f, p.pipe, false) && !pc_dac_unsupported(f, p.pipe, true));
    /* the full plan, as audio_svc.c plan_of and the simulator's fake make it: 384 / 352.8 kHz halve on the FakePod (never above its 192k), nothing resamples there */
    pc_plan_args_t a = pc_plan_args(384000, 0, false, 384000, 384000); pc_dac_clamp_args(f, &a);
    p = pc_pipe_plan(384000, a.out_max, 0, a.limit_why); p = pc_dac_fix(f, 384000, a.out_max, false, p, &from);
    CHECK_EQ_INT(p.pipe, 192000); CHECK_EQ_INT(p.halvings, 1); CHECK_EQ_INT(from, 0);
    a = pc_plan_args(352800, 0, false, 384000, 384000); pc_dac_clamp_args(f, &a);
    p = pc_pipe_plan(352800, a.out_max, 0, a.limit_why); CHECK_EQ_INT(p.pipe, 176400);
    a = pc_plan_args(384000, 0, false, 384000, 384000); pc_dac_clamp_args(r, &a);
    CHECK_EQ_INT(a.out_max, 384000);                   /* the round board is not clamped */
    /* every rate 8000 .. 31999 on the FakePod lands on a rate the DAC takes within 1x .. 4x */
    for (uint32_t s = 8000; s < 32000; s++) { uint32_t o = pc_dac_pick_rate(f, s, 192000); CHECK(pc_dac_has(f, o) && o >= s && o <= 4 * s && o <= PC_UP_MAX_OUT); }
}

/* FIX3 I2: the whole plan as audio_svc.c / the simulator make it, over every ceiling: the pick works within the user's / board's ceiling, never the plan's 48k no-clock-row one */
static uint32_t full_plan(const pc_dac_caps_t *c, uint32_t raw, uint32_t cap, uint32_t *from)
{
    pc_plan_args_t a = pc_plan_args(raw, 0, false, cap, cap);
    pc_dac_clamp_args(c, &a);
    pc_pipe_t p = pc_pipe_plan(raw, a.out_max, 0, a.limit_why);
    p = pc_dac_fix(c, raw, pc_dac_fix_max(c, false, cap, cap), false, p, from);
    return p.pipe;
}
static void test_ceilings(void)
{
    static const uint32_t caps[] = { 48000, 96000, 192000, 384000 };
    const pc_dac_caps_t *bd[2] = { &pc_dac_caps_fakepod, &pc_dac_caps_round };
    for (int b = 0; b < 2; b++) {
        const pc_dac_caps_t *c = bd[b];
        uint32_t last[4];
        for (uint32_t raw = 8000; raw <= 384000; raw += (raw < 100000 ? 7 : 101)) {
            uint32_t prev = 0;
            for (int k = 0; k < 4; k++) {
                uint32_t from = 0, pipe = full_plan(c, raw, caps[k], &from);
                if (pipe && !pc_dac_unsupported(c, pipe, false)) CHECK(pipe <= caps[k] && pipe <= c->max);
                if (from) {
                    CHECK(pipe <= PC_UP_MAX_OUT && pc_dac_has(c, pipe) && pipe <= caps[k]);
                    if (raw >= 32000) CHECK(pipe <= raw);                  /* nothing is made up above what the file holds */
                    last[k] = pipe;
                    if (prev) CHECK(pipe >= prev);                         /* a higher ceiling never gives a lower rate */
                    prev = pipe;
                }
            }
        }
        (void)last;
        uint32_t from;
        if (c->native_from) { CHECK(!pc_dac_has(c, full_plan(c, 99999, 384000, &from)) && from == 0); continue; }   /* DACFS: an odd rate is refused, never resampled */
        CHECK_EQ_INT(full_plan(c, 99999, 48000, &from), 48000);
        for (int k = 1; k < 4; k++) { CHECK_EQ_INT(full_plan(c, 99999, caps[k], &from), 96000); CHECK_EQ_INT(from, 99999); }
    }
    uint32_t from;
    for (int k = 1; k < 4; k++) { uint32_t pp = full_plan(&pc_dac_caps_fakepod, 50000, caps[k], &from); CHECK_EQ_INT(from, 0); CHECK(!pp || pc_dac_unsupported(&pc_dac_caps_fakepod, pp, false)); }   /* DACFS: never resampled to 48k */
}

/* FIX3 I1: the stage's source buffer holds PC_UP_SBUF_FRAMES; the audio task reads carry + pc_up_want frames into it. Exactly that size on the heap (ASan), the exact
 * rates that overflowed 3840 (383,801 .. 383,999 Hz at 96k), every carry the resampler leaves. */
static void test_sbuf_bound(void)
{
    const pc_dac_caps_t *bd[2] = { &pc_dac_caps_fakepod, &pc_dac_caps_round };
    static const uint32_t caps[] = { 96000, 192000, 384000 };
    int tried = 0;
    for (int b = 0; b < 2; b++)
        for (uint32_t raw = 370000; raw <= 384000; raw++)
            for (int k = 0; k < 3; k++) {
                uint32_t from = 0, pipe = full_plan(bd[b], raw, caps[k], &from);
                if (!from) continue;
                int frames = (int)(pipe / 100);
                tried++;
                for (int carry = 0; carry <= 4; carry++) {
                    int sf = pc_up_want(carry, frames, from, pipe);
                    CHECK(carry + sf <= PC_UP_SBUF_FRAMES && sf >= 0);
                    CHECK(carry + sf >= pc_up_need(frames, from, pipe));   /* and it is enough for a whole block */
                }
            }
    CHECK(tried > 0);
    for (uint32_t in = 383801; in <= 383999; in += 7) {                    /* the blocks themselves, on a buffer of exactly the size */
        Q = malloc(pc_rsq_size());
        CHECK(Q && pc_rsq_init(Q, in, 96000) == 0);
        float *sb = calloc((size_t)PC_UP_SBUF_FRAMES * 2, sizeof(float)), *ob = calloc(960 * 2, sizeof(float));
        int carry = 0;
        for (int blk = 0; blk < 40; blk++) {
            int sf = pc_up_want(carry, 960, in, 96000), nc;
            CHECK(carry + sf <= PC_UP_SBUF_FRAMES);
            for (int i = 0; i < sf; i++) sb[2 * (carry + i)] = sb[2 * (carry + i) + 1] = 0.25f * (float)((i + blk) % 7);
            int n = pc_up_run(Q, sb, carry, sf, ob, 960, &nc);
            CHECK_EQ_INT(n, 960);
            CHECK(nc >= 0 && nc <= 8);
            carry = nc;
        }
        free(sb); free(ob); free(Q);
    }
    Q = NULL;
}

static void run_rate(uint32_t in, uint32_t outr, int secs, double *us_block)
{
    uint32_t out = outr;
    int frames = (int)(out / 100);
    CHECK(Q && pc_rsq_init(Q, in, out) == 0);
    int need = pc_up_need(frames, in, out);
    float *sb = calloc((size_t)(need + 8) * 2, sizeof(float)), *ob = calloc((size_t)frames * 2, sizeof(float));
    int carry = 0, blocks = secs * 100;
    double phase = 0, maxc = 0, peak = 0;
    uint64_t pulled = 0;
    clock_t t0 = clock();
    for (int b = 0; b < blocks; b++) {
        int ns = need - carry;
        for (int i = 0; i < ns; i++) { float v = 0.5f * sinf((float)phase); phase += 2 * 3.14159265358979 * 1000.0 / in; sb[2 * (carry + i)] = sb[2 * (carry + i) + 1] = v; }
        pulled += (uint64_t)ns;
        int nc, n = pc_up_run(Q, sb, carry, ns, ob, frames, &nc);
        CHECK_EQ_INT(n, frames);                       /* a full block every time */
        carry = nc;
        if (carry > maxc) maxc = carry;
        if (b > 2) for (int i = 0; i < n; i++) if (fabsf(ob[2 * i]) > peak) peak = fabsf(ob[2 * i]);
    }
    double us = (double)(clock() - t0) * 1e6 / CLOCKS_PER_SEC / blocks;
    CHECK(maxc <= 4);                                  /* the carry stays a few frames */
    CHECK_NEAR(peak, 0.5, 0.01);                       /* unity gain: a 0.5 sine stays 0.5 */
    /* source frames pulled over the run = output time at the source rate (within the carry) */
    CHECK(llabs((long long)pulled - (long long)secs * in) <= 3);
    if (us_block) *us_block = us;
    printf("  %6u -> %6u: %.1f us per 10 ms block on this host, carry max %.0f\n", in, out, us, maxc);
    free(sb); free(ob);
}

static void test_blocks(void)
{
    Q = malloc(pc_rsq_size());
    static const uint32_t up[][2] = { { 8000, 32000 }, { 11025, 44100 }, { 12000, 32000 }, { 16000, 32000 }, { 22050, 44100 }, { 24000, 32000 }, { 99999, 176400 } };
    for (unsigned i = 0; i < sizeof up / sizeof *up; i++) run_rate(up[i][0], up[i][1], 3, NULL);
    run_rate(99999, 96000, 3, NULL);                   /* down, odd */
    run_rate(384000, 192000, 2, NULL);                 /* the cost the halving stage avoids (bench) */
    run_rate(352800, 176400, 2, NULL);
    free(Q);
}

int main(void)
{
    RUN(test_tables);
    RUN(test_ceilings);
    RUN(test_sbuf_bound);
    RUN(test_blocks);
    return 0;
}
