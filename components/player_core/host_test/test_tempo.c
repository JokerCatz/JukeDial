// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc_test.h"
#include "pc/pc_tempo.h"
#include "pc/pc_midits.h"

static void test_tap_basic(void)
{
    pc_tap_t t;
    pc_tap_reset(&t);
    CHECK_EQ_INT(pc_tap(&t, 1000), 0);
    CHECK_EQ_INT(pc_tap(&t, 1500), 0);
    CHECK_EQ_INT(pc_tap(&t, 2000), 0);
    CHECK_EQ_INT(pc_tap(&t, 2500), 120);                /* 4 taps, 500 ms apart */
    CHECK_EQ_INT(pc_tap(&t, 3010), 119);                /* keeps following: (3010 - 1000) / 4 = 502.5 ms */
    CHECK_EQ_INT(pc_tap(&t, 3530), 118);                /* (3530 - 1500) / 4 = 507.5 ms -> 118.2 */
}

static void test_tap_run_breaks(void)
{
    pc_tap_t t;
    pc_tap_reset(&t);
    pc_tap(&t, 0); pc_tap(&t, 600); pc_tap(&t, 1200);
    CHECK_EQ_INT(pc_tap(&t, 4000), 0);                  /* 2.8 s pause: a new run, this is its first tap */
    pc_tap(&t, 4400); pc_tap(&t, 4800);
    CHECK_EQ_INT(pc_tap(&t, 4850), 0);                  /* bounce ignored */
    CHECK_EQ_INT(pc_tap(&t, 5200), 150);
    pc_tap_reset(&t);
    pc_tap(&t, 0); pc_tap(&t, 500); pc_tap(&t, 1000);
    CHECK_EQ_INT(pc_tap(&t, 1200), 0);                  /* 200 ms after 500 ms intervals: new run from 1000 */
    pc_tap(&t, 1400);
    CHECK_EQ_INT(pc_tap(&t, 1600), 300);                /* 1000, 1200, 1400, 1600 */
    pc_tap_reset(&t);
    pc_tap(&t, 0); pc_tap(&t, 1900); pc_tap(&t, 3800);
    CHECK_EQ_INT(pc_tap(&t, 5700), 32);                 /* slow but still a run (< 2 s apart) */
}

/* the sender's beat grid: 24 ticks a beat; jitter in microseconds added to each tick */
static uint32_t rnd = 1;
static int jit(int amp) { rnd = rnd * 1103515245u + 12345u; return amp ? (int)((rnd >> 8) % (uint32_t)(2 * amp + 1)) - amp : 0; }

static void test_clk_follow(void)
{
    pc_clk_t c;
    pc_clk_reset(&c);
    double period = 60e6 / (120.0 * 24);                /* 20833 us */
    int64_t t0 = 10000000;
    CHECK(!pc_clk_locked(&c, t0));
    for (int i = 0; i < 200; i++) pc_clk_tick(&c, t0 + (int64_t)(i * period) + jit(1500));   /* +-1.5 ms */
    int64_t now = t0 + (int64_t)(199 * period);
    CHECK(pc_clk_locked(&c, now));
    CHECK_NEAR(pc_clk_bpm(&c), 120.0, 0.3);
    CHECK(!c.running);
    pc_clk_start(&c);                                   /* tick 200 is beat 0 */
    CHECK(c.running);
    int64_t bt;
    CHECK(pc_clk_beat_time(&c, 0, &bt));
    CHECK_NEAR((double)bt, t0 + 200 * period, 800);     /* within 0.8 ms of the true grid */
    CHECK(pc_clk_beat_time(&c, 2, &bt));
    CHECK_NEAR((double)bt, t0 + 248 * period, 1500);
    CHECK_EQ_INT(pc_clk_beat_after(&c, t0 + (int64_t)(200 * period) - 1000), 0);
    CHECK_EQ_INT(pc_clk_beat_after(&c, t0 + (int64_t)(200 * period) + 1000), 1);
    CHECK(!pc_clk_locked(&c, now + 600000));            /* ticks stopped */
    pc_clk_stop(&c);
    CHECK(!c.running);
}

static void test_clk_tempo_jump(void)
{
    pc_clk_t c;
    pc_clk_reset(&c);
    int64_t t = 0;
    for (int i = 0; i < 100; i++) { pc_clk_tick(&c, t); t += 20833; }      /* 120 BPM */
    for (int i = 0; i < 12; i++) { pc_clk_tick(&c, t); t += 27778; }       /* 90 BPM */
    CHECK_NEAR(pc_clk_bpm(&c), 90.0, 1.0);              /* re-fitted within half a beat, not after 2 beats */
}

static void test_clk_restart_after_gap(void)
{
    pc_clk_t c;
    pc_clk_reset(&c);
    for (int i = 0; i < 50; i++) pc_clk_tick(&c, i * 20833);
    pc_clk_start(&c);
    pc_clk_tick(&c, 50 * 20833 + 3000000);              /* 3 s later */
    CHECK_EQ_INT(c.count, 1);
    CHECK(!pc_clk_locked(&c, 50 * 20833 + 3000000));
    CHECK_NEAR(pc_clk_bpm(&c), 0, 0.001);
}

static void test_clk_continue_counts_on(void)
{
    pc_clk_t c;
    pc_clk_reset(&c);
    for (int i = 0; i < 30; i++) pc_clk_tick(&c, i * 20000);
    pc_clk_start(&c);                                   /* beat 0 = tick 30 */
    for (int i = 30; i < 60; i++) pc_clk_tick(&c, i * 20000);
    pc_clk_stop(&c);
    for (int i = 60; i < 70; i++) pc_clk_tick(&c, i * 20000);
    pc_clk_continue(&c);                                /* beat grid unchanged: beat 2 is tick 78 */
    int64_t bt;
    CHECK(pc_clk_beat_time(&c, 2, &bt));
    CHECK_NEAR((double)bt, 78 * 20000, 300);
    CHECK_NEAR(pc_clk_bpm(&c), 125.0, 0.01);
}

/* BLE as on the board: ticks leave the sender on a 1 ms timestamp, arrive at the next 7.5 ms connection
 * event (plus 0..3 ms of radio retry jitter); pc_midits_map undoes most of it, the fit the rest */
static void test_clk_over_ble(void)
{
    pc_midits_t ts;
    pc_clk_t c;
    pc_midits_reset(&ts);
    pc_clk_reset(&c);
    const double period = 60e6 / (133.0 * 24);
    const int64_t off = 7000000;
    double worst = 0, worst_raw = 0;
    int64_t prev_beat = 0;
    for (int i = 0; i < 24 * 64; i++) {
        double send = 500000 + i * period;                   /* sender clock, us */
        int64_t ms = (int64_t)(send / 1000);
        int64_t ev = ((int64_t)(send + off) / 7500 + 1) * 7500 + (jit(1500) + 1500);   /* connection event */
        int64_t t = pc_midits_map(&ts, (uint16_t)(ms & 0x1FFF), ev);
        CHECK(t <= ev);                                      /* unclamped: may be before the arrival */
        if (i == 24 * 4) pc_clk_start(&c);                  /* tick 96 = beat 0 */
        pc_clk_tick(&c, t);
        if (i > 24 * 8 && i % 24 == 0) {                     /* beat (i - 96) / 24 just ticked: how far off the grid */
            int64_t bt;
            CHECK(pc_clk_beat_time(&c, (uint32_t)(i - 96) / 24 + 1, &bt));   /* the next one, predicted */
            if (prev_beat) {
                double e = fabs((double)(bt - prev_beat) - 24 * period);
                if (e > worst) worst = e;
            }
            prev_beat = bt;
            double raw = fabs((double)(ev - (int64_t)(send + off)) - 7500);
            if (raw > worst_raw) worst_raw = raw;
        }
    }
    CHECK_NEAR(pc_clk_bpm(&c), 133.0, 0.2);
    printf("clk over ble: worst beat-to-beat %.0f us, raw arrival %.0f us\n", worst, worst_raw);
    CHECK(worst < 1500);                                     /* beat to beat within 1.5 ms (raw arrival: about 7 ms) */
}

int main(void)
{
    RUN(test_clk_over_ble);
    RUN(test_tap_basic);
    RUN(test_tap_run_breaks);
    RUN(test_clk_follow);
    RUN(test_clk_tempo_jump);
    RUN(test_clk_restart_after_gap);
    RUN(test_clk_continue_counts_on);
    return 0;
}
