// SPDX-License-Identifier: LGPL-2.1-or-later
/* The chip bench core (pc_chipbench.c) against a fake backend: block count, the time statistics from a scripted clock, end of track,
 * the over-budget runs, silent blocks, the checksum and the failure codes. */
#include <string.h>
#include "pc/pc_chipbench.h"
#include "pc_test.h"

static uint64_t s_clock;
static int s_calls;
static uint64_t s_cost[8] = { 100, 200, 300, 12000, 13000, 100, 100, 100 };   /* per block (cycled), us */
static uint64_t now_us(void) { return s_clock; }

typedef struct { int left; int n; } fake_t;
static fake_t s_fake;

static int f_open(void **h, const pc_reader_t *r, uint32_t off) { (void)off; if (!r->mem.p) return -1; *h = &s_fake; s_fake.n = 0; return 0; }
static int f_render(void *h, float *o, int frames)
{
    fake_t *f = h;
    s_clock += s_cost[f->n % 8];                  /* the "decode" costs this long on the scripted clock */
    s_calls++;
    if (f->left == 0) return 0;
    if (f->left > 0) f->left--;
    for (int i = 0; i < 2 * frames; i++) o[i] = (f->n % 4 == 3) ? 0.0f : 0.25f;   /* every 4th block silent */
    f->n++;
    return frames;
}
static uint32_t f_rate(void *h) { (void)h; return 44100; }
static void f_close(void *h) { (void)h; }
static const pc_backend_t FAKE = { .open = f_open, .render = f_render, .rate = f_rate, .close = f_close, .name = "fake" };

static pc_chipbench_env_t env = { .now_us = now_us };

static void test_stats(void)
{
    pc_chipbench_t r;
    s_fake.left = -1;                              /* never ends */
    uint8_t d[4] = { 1, 2, 3, 4 };
    pc_chipbench_run(&FAKE, d, 4, 0, 1, &env, &r);   /* 1 s = 100 blocks of 441 frames */
    CHECK_EQ_INT(r.rc, 0);
    CHECK_EQ_INT(r.block_frames, 441);
    CHECK_EQ_INT(r.blocks, 100);
    CHECK_EQ_INT(r.ended, 0);
    CHECK_EQ_INT(r.silent_blocks, 25);
    /* the 8-cost cycle sums to 25900 us: avg over 100 blocks (12.5 cycles) */
    CHECK_EQ_INT(r.avg_us, (12 * 25900 + 100 + 200 + 300 + 12000) / 100);
    CHECK_EQ_INT(r.max_us, 13000);
    CHECK_EQ_INT(r.over_budget, 25);              /* blocks 3 and 4 of every cycle: 12 ms and 13 ms */
    CHECK_EQ_INT(r.max_run_over, 2);
    CHECK_EQ_INT(r.rt_pct10, (uint32_t)(12 * 25900 + 12600) * 1000u / 1000000u);   /* sum us / audio us (1 s) in tenths of a percent */
    CHECK(r.p99_us >= 12000);
    CHECK_EQ_INT(r.alloc_open, -1);
    char line[320];
    CHECK(pc_chipbench_fmt(&r, line, sizeof line) > 40);
    CHECK(strstr(line, "blocks=100") != NULL);
    uint32_t sum1 = r.checksum;
    s_fake.left = -1;
    pc_chipbench_run(&FAKE, d, 4, 0, 1, &env, &r);
    CHECK_EQ_INT(r.checksum, sum1);               /* same audio, same checksum */
}

static void test_end_and_errors(void)
{
    pc_chipbench_t r;
    uint8_t d[4] = { 1, 2, 3, 4 };
    s_fake.left = 7;                               /* the track ends after 7 blocks */
    pc_chipbench_run(&FAKE, d, 4, 0, 5, &env, &r);
    CHECK_EQ_INT(r.blocks, 7);
    CHECK_EQ_INT(r.ended, 1);
    pc_chipbench_run(&FAKE, d, 4, 0, 0, &env, &r);
    CHECK_EQ_INT(r.rc, -3);
    pc_chipbench_run(&FAKE, d, 4, 2, 1, &env, &r); /* sub-track on a backend without start_track */
    CHECK_EQ_INT(r.rc, -2);
    pc_chipbench_run(&FAKE, NULL, 4, 0, 1, &env, &r);
    CHECK_EQ_INT(r.rc, -3);
}

int main(void)
{
    RUN(test_stats);
    RUN(test_end_and_errors);
    return 0;
}
