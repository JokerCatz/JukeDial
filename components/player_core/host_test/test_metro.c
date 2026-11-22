// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc_test.h"
#include "pc/pc_metro.h"

#define RATE 48000
#define BLK 64

/* run the planner and renderer block by block like the engine; record where each click starts */
static int run(pc_metro_t *m, int64_t *pos, int blocks, int64_t horizon, int64_t *starts, uint8_t *accent, int max)
{
    int n = 0;
    int32_t mix[BLK * 2];
    for (int b = 0; b < blocks; b++) {
        pc_beat_t bt;
        if (pc_metro_plan(m, *pos, *pos + BLK + horizon, &bt) && n < max) { starts[n] = bt.pos; accent[n] = bt.accent; n++; }
        memset(mix, 0, sizeof mix);
        pc_metro_render(m, mix, BLK, *pos, 32768);
        *pos += BLK;
    }
    return n;
}

static void test_grid(void)
{
    pc_metro_t m;
    pc_metro_init(&m, RATE);
    pc_metro_set_bpm(&m, 120);                          /* 24000 samples a beat */
    pc_metro_set_bpb(&m, 3);
    int64_t pos = 1000, st[16];
    uint8_t ac[16];
    pc_metro_start(&m, 5000);
    int n = run(&m, &pos, 48000 * 3 / BLK, 1200, st, ac, 16);
    CHECK_EQ_INT(n, 6);                                 /* 3 s: beats at 0, .5 .. 2.5 s */
    for (int i = 0; i < n; i++) {
        CHECK_EQ_INT(st[i], 5000 + i * 24000);          /* exact sample positions, no block rounding */
        CHECK_EQ_INT(ac[i], i % 3 == 0);
    }
}

static void test_click_sample_accurate(void)
{
    pc_metro_t m;
    pc_metro_init(&m, RATE);
    pc_metro_set_bpm(&m, 100);
    pc_metro_start(&m, 1000 + 37);                      /* inside a block */
    pc_beat_t b;
    int32_t mix[BLK * 2];
    int64_t pos = 1000;
    CHECK_EQ_INT(pc_metro_plan(&m, pos, pos + BLK, &b), 1);
    memset(mix, 0, sizeof mix);
    pc_metro_render(&m, mix, BLK, pos, 32768);
    for (int f = 0; f < 37; f++) CHECK_EQ_INT(mix[2 * f], 0);
    CHECK(mix[2 * 37] > 6000);                          /* the onset is the transient */
    CHECK_EQ_INT(mix[2 * 37], mix[2 * 37 + 1]);
    /* the 20 ms click runs on over the next blocks and ends */
    long energy = 0;
    for (int k = 1; k < 30; k++) {
        memset(mix, 0, sizeof mix);
        pc_metro_render(&m, mix, BLK, pos + k * BLK, 32768);
        for (int f = 0; f < BLK; f++) energy += labs(mix[2 * f]);
        if (k > 16) for (int f = 0; f < BLK; f++) CHECK_EQ_INT(mix[2 * f], 0);   /* 960 samples, ~15 blocks */
    }
    CHECK(energy > 0);
}

static void test_tempo_change_and_stop(void)
{
    pc_metro_t m;
    pc_metro_init(&m, RATE);
    pc_metro_set_bpm(&m, 60);
    pc_metro_start(&m, 0);
    pc_beat_t b;
    CHECK_EQ_INT(pc_metro_plan(&m, 0, 100, &b), 1);
    CHECK_EQ_INT(b.pos, 0);
    CHECK_EQ_INT(pc_metro_plan(&m, 0, 100, &b), 0);     /* one pending at a time */
    int32_t mix[BLK * 2] = { 0 };
    pc_metro_render(&m, mix, BLK, 0, 32768);
    pc_metro_set_bpm(&m, 120);                          /* from the last committed beat: next at 24000, not 48000 */
    CHECK_EQ_INT(pc_metro_plan(&m, 64, 30000, &b), 1);
    CHECK_EQ_INT(b.pos, 24000);
    CHECK_EQ_INT(b.idx, 1);
    pc_metro_stop(&m);
    CHECK(!m.pending);
    CHECK_EQ_INT(pc_metro_plan(&m, 64, 1000000, &b), 0);
    /* a beat already behind (tempo raised a lot after a long one) goes to now, not into the past */
    pc_metro_set_bpm(&m, 30);
    pc_metro_start(&m, 0);
    pc_metro_plan(&m, 0, 10, &b);
    pc_metro_render(&m, mix, BLK, 0, 32768);
    pc_metro_set_bpm(&m, 300);                          /* next would be 9600, but now is 50000 */
    CHECK_EQ_INT(pc_metro_plan(&m, 50000, 51000, &b), 1);
    CHECK_EQ_INT(b.pos, 50000);
}

static void test_external_commit(void)
{
    pc_metro_t m;
    pc_metro_init(&m, RATE);
    pc_metro_set_bpb(&m, 4);
    pc_metro_start(&m, 0);
    pc_beat_t b;
    CHECK_EQ_INT(pc_metro_commit(&m, 100, 500, 5, &b), 1);
    CHECK_EQ_INT(b.beat, 1);
    CHECK_EQ_INT(b.accent, 0);
    CHECK_EQ_INT(pc_metro_commit(&m, 100, 900, 6, &b), 0);   /* still pending */
    int32_t mix[1024 * 2] = { 0 };
    pc_metro_render(&m, mix, 1024, 0, 32768);
    CHECK(mix[2 * 500] != 0);
    CHECK_EQ_INT(pc_metro_commit(&m, 2000, 1500, 8, &b), 1);  /* late: at now */
    CHECK_EQ_INT(b.pos, 2000);
    CHECK_EQ_INT(b.accent, 1);
}

static void test_aclock(void)
{
    pc_aclock_t a;
    pc_aclock_init(&a, RATE, 4000);
    /* blocks of 64 written; writes return 1333 us apart, some late */
    int64_t base = 3000000;
    for (int i = 1; i <= 3000; i++) {
        int64_t pos = (int64_t)i * BLK;
        int64_t t = base + pos * 1000000 / RATE + (i % 7 == 0 ? 400 : 0) + (i % 50 == 0 ? 3000 : 0);
        pc_aclock_mark(&a, pos, t);
    }
    int64_t p = 3000 * BLK;
    CHECK_NEAR((double)pc_aclock_time(&a, p), (double)(base + p * 1000000 / RATE + 4000), 30);
    CHECK_NEAR((double)pc_aclock_pos(&a, pc_aclock_time(&a, p + 4800)), (double)(p + 4800), 1);
}

int main(void)
{
    RUN(test_grid);
    RUN(test_click_sample_accurate);
    RUN(test_tempo_change_and_stop);
    RUN(test_external_commit);
    RUN(test_aclock);
    return 0;
}
