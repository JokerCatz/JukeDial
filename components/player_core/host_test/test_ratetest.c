// SPDX-License-Identifier: LGPL-2.1-or-later
#include <math.h>
#include "pc/pc_ratetest.h"
#include "pc_test.h"

/* hires T11: the rate test's pure state machine (tiers, timeline, tap rules, timeout, fault) and its tone */

static void test_rt_tiers(void)
{
    pc_ratetest_t t;
    pc_ratetest_init(&t, 96000, 0);
    CHECK_EQ_INT(t.ntiers, 2);
    CHECK_EQ_INT(t.tiers[0], 48000);
    CHECK_EQ_INT(t.tiers[1], 96000);
    CHECK_EQ_INT(t.state, PC_RT_RUN);
    CHECK_EQ_INT(t.tier, 0);
    CHECK_EQ_INT(t.confirmed, 0);
    pc_ratetest_init(&t, 384000, 0);
    CHECK_EQ_INT(t.ntiers, 4);
    CHECK_EQ_INT(t.tiers[3], 384000);
    pc_ratetest_init(&t, 44100, 0);                    /* no tier fits: done at once, nothing to store */
    CHECK_EQ_INT(t.ntiers, 0);
    CHECK_EQ_INT(t.state, PC_RT_DONE);
    CHECK_EQ_INT(t.confirmed, 0);
    CHECK_EQ_INT(pc_ratetest_rate_now(&t, 0), 0);
    CHECK_EQ_INT(pc_ratetest_tone_hz(&t, 0), 0);
    CHECK(!pc_ratetest_tap(&t, 1600));
}

static void test_rt_timeline(void)
{
    pc_ratetest_t t;
    pc_ratetest_init(&t, 96000, 0);
    CHECK_EQ_INT(pc_ratetest_rate_now(&t, 0), 44100);
    CHECK_EQ_INT(pc_ratetest_rate_now(&t, 1199), 44100);
    CHECK_EQ_INT(pc_ratetest_rate_now(&t, 1200), 0);
    CHECK_EQ_INT(pc_ratetest_rate_now(&t, 1499), 0);
    CHECK_EQ_INT(pc_ratetest_rate_now(&t, 1500), 48000);
    CHECK_EQ_INT(pc_ratetest_rate_now(&t, 2699), 48000);
    CHECK_EQ_INT(pc_ratetest_rate_now(&t, 2700), 0);
    CHECK_EQ_INT(pc_ratetest_tone_hz(&t, 100), 880);
    CHECK_EQ_INT(pc_ratetest_tone_hz(&t, 1600), 1320);
    CHECK_EQ_INT(pc_ratetest_tone_hz(&t, 1300), 0);
    CHECK_EQ_INT(pc_ratetest_rate_now(&t, -1), 0);
    pc_ratetest_init(&t, 384000, 0);                    /* the families of every tier */
    t.tier = 1;
    CHECK_EQ_INT(pc_ratetest_rate_now(&t, 0), 88200);
    CHECK_EQ_INT(pc_ratetest_rate_now(&t, 1500), 96000);
    t.tier = 2;
    CHECK_EQ_INT(pc_ratetest_rate_now(&t, 0), 176400);
    CHECK_EQ_INT(pc_ratetest_rate_now(&t, 1500), 192000);
    t.tier = 3;
    CHECK_EQ_INT(pc_ratetest_rate_now(&t, 0), 352800);
    CHECK_EQ_INT(pc_ratetest_rate_now(&t, 1500), 384000);
    pc_ratetest_init(&t, 96000, 5000);                  /* the timeline is relative to the tier's start */
    CHECK_EQ_INT(pc_ratetest_rate_now(&t, 5000), 44100);
    CHECK_EQ_INT(pc_ratetest_rate_now(&t, 6500), 48000);
    CHECK_EQ_INT(pc_ratetest_rate_now(&t, 4999), 0);
}

static void test_rt_tap_rules(void)
{
    pc_ratetest_t t;
    pc_ratetest_init(&t, 96000, 0);
    CHECK(!pc_ratetest_tap(&t, 1000));                  /* before tone 2 starts: ignored */
    CHECK(!pc_ratetest_tap(&t, 1499));
    CHECK_EQ_INT(t.confirmed, 0);
    CHECK(pc_ratetest_tap(&t, 1600));
    CHECK_EQ_INT(t.confirmed, 48000);
    CHECK_EQ_INT(t.tier, 1);
    CHECK_EQ_INT(t.t0_ms, 1600);
    CHECK_EQ_INT(t.state, PC_RT_RUN);
    CHECK(!pc_ratetest_tap(&t, 1600 + 1000));
    CHECK(pc_ratetest_tap(&t, 1600 + 1600));            /* the top tier of this build */
    CHECK_EQ_INT(t.state, PC_RT_DONE);
    CHECK_EQ_INT(t.end, PC_RT_END_TOP);
    CHECK_EQ_INT(t.confirmed, 96000);
    CHECK(!pc_ratetest_tap(&t, 9000));                  /* done: taps are ignored */
    CHECK_EQ_INT(t.confirmed, 96000);
    pc_ratetest_init(&t, 96000, 0);                     /* a tap after the tones ended is still a tap (until the timeout) */
    CHECK(pc_ratetest_tap(&t, 5000));
    CHECK_EQ_INT(t.confirmed, 48000);
    CHECK_EQ_INT(t.t0_ms, 5000);
}

static void test_rt_timeout(void)
{
    pc_ratetest_t t;
    pc_ratetest_init(&t, 96000, 0);
    pc_ratetest_tick(&t, 7999);
    CHECK_EQ_INT(t.state, PC_RT_RUN);
    pc_ratetest_tick(&t, 8000);
    CHECK_EQ_INT(t.state, PC_RT_DONE);
    CHECK_EQ_INT(t.end, PC_RT_END_TIMEOUT);
    CHECK_EQ_INT(t.confirmed, 0);
    pc_ratetest_init(&t, 96000, 0);
    CHECK(pc_ratetest_tap(&t, 1600));
    pc_ratetest_tick(&t, 9599);
    CHECK_EQ_INT(t.state, PC_RT_RUN);
    pc_ratetest_tick(&t, 9600);
    CHECK_EQ_INT(t.state, PC_RT_DONE);
    CHECK_EQ_INT(t.end, PC_RT_END_TIMEOUT);
    CHECK_EQ_INT(t.confirmed, 48000);
    CHECK(!pc_ratetest_tap(&t, 9700));                  /* a tap after the end changes nothing */
    CHECK_EQ_INT(t.confirmed, 48000);
}

static void test_rt_fault(void)
{
    pc_ratetest_t t;
    pc_ratetest_init(&t, 384000, 0);
    CHECK(pc_ratetest_tap(&t, 1600));
    pc_ratetest_fault(&t);                              /* tier 2 (96k) underruns */
    CHECK_EQ_INT(t.state, PC_RT_DONE);
    CHECK_EQ_INT(t.end, PC_RT_END_FAULT);
    CHECK_EQ_INT(t.confirmed, 48000);
    pc_ratetest_fault(&t);                              /* already done: nothing moves */
    pc_ratetest_leave(&t);
    CHECK_EQ_INT(t.end, PC_RT_END_FAULT);
    pc_ratetest_init(&t, 96000, 0);                     /* a fault in the first tier confirms nothing */
    pc_ratetest_fault(&t);
    CHECK_EQ_INT(t.end, PC_RT_END_FAULT);
    CHECK_EQ_INT(t.confirmed, 0);
}

static void test_rt_leave(void)
{
    pc_ratetest_t t;
    pc_ratetest_init(&t, 96000, 0);
    CHECK(pc_ratetest_tap(&t, 1600));
    pc_ratetest_leave(&t);
    CHECK_EQ_INT(t.state, PC_RT_DONE);
    CHECK_EQ_INT(t.end, PC_RT_END_LEFT);
    pc_ratetest_tick(&t, 99999);
    CHECK_EQ_INT(t.end, PC_RT_END_LEFT);
    pc_ratetest_init(&t, 96000, 0);
    pc_ratetest_tap(&t, 1600);
    pc_ratetest_tap(&t, 3300);
    pc_ratetest_leave(&t);                              /* done with TOP: leaving afterwards does not rewrite why */
    CHECK_EQ_INT(t.end, PC_RT_END_TOP);
}

static void test_rt_render(void)
{
    const uint32_t rate = 48000, len = rate * 1200 / 1000;
    static float buf[2 * 48000 * 2];
    double phase = 0;
    uint32_t pos = 0;
    int ramp = rate * 20 / 1000;
    for (uint32_t done = 0; done < len;) {               /* in 10 ms blocks, as the audio task asks */
        int n = 480;
        pc_ratetest_render(buf + 2 * done, n, rate, &phase, &pos, len, 880.0);
        done += (uint32_t)n;
    }
    CHECK_EQ_INT(pos, len);
    float peak = 0;
    double sq = 0;
    int cnt = 0;
    for (uint32_t i = 0; i < len; i++) {
        CHECK(buf[2 * i] == buf[2 * i + 1]);
        float a = fabsf(buf[2 * i]);
        if (a > peak) peak = a;
        if (i >= (uint32_t)ramp && i < len - (uint32_t)ramp) { sq += (double)buf[2 * i] * buf[2 * i]; cnt++; }
    }
    CHECK(peak <= 0.0317f);
    CHECK(peak > 0.03f);
    CHECK(buf[0] == 0.0f);                              /* the ramp starts at silence */
    CHECK(fabsf(buf[2 * (len - 1)]) < 1e-4f);           /* and ends in it */
    CHECK(fabsf(buf[2 * (ramp / 2)]) < 0.0317f * 0.9f + 1e-3f);   /* inside the ramp the tone is below its full level */
    double rms = sqrt(sq / cnt);
    CHECK_NEAR(rms, 0.0316 / sqrt(2.0), 0.0316 / sqrt(2.0) * 0.01);
    float more[8] = { 1, 1, 1, 1, 1, 1, 1, 1 };         /* past len: silence, pos stays */
    pc_ratetest_render(more, 4, rate, &phase, &pos, len, 880.0);
    for (int i = 0; i < 8; i++) CHECK(more[i] == 0.0f);
    CHECK_EQ_INT(pos, len);
}

static void test_rt_render_rates(void)                  /* every tier's clock: same level and length rule at 384 kHz */
{
    const uint32_t rate = 384000, len = rate * 1200 / 1000;
    static float buf[2 * 460800];
    double phase = 0;
    uint32_t pos = 0;
    pc_ratetest_render(buf, (int)len, rate, &phase, &pos, len, 1320.0);
    float peak = 0;
    for (uint32_t i = 0; i < len; i++) { float a = fabsf(buf[2 * i]); if (a > peak) peak = a; }
    CHECK(peak <= 0.0317f && peak > 0.03f);
    CHECK(buf[0] == 0.0f);
}

static void test_rt_baseline_at(void)                   /* the tier's underrun baseline: once per tier, 300 ms in (the reconfigure has settled) */
{
    pc_ratetest_t t;
    pc_ratetest_init(&t, 96000, 1000);
    CHECK(!pc_ratetest_baseline_at(&t, 1000));
    CHECK(!pc_ratetest_baseline_at(&t, 1299));
    CHECK(pc_ratetest_baseline_at(&t, 1300));
    CHECK(!pc_ratetest_baseline_at(&t, 1350));           /* once */
    CHECK(!pc_ratetest_baseline_at(&t, 5000));
    CHECK(pc_ratetest_tap(&t, 2600));                    /* the next tier has its own */
    CHECK(!pc_ratetest_baseline_at(&t, 2800));
    CHECK(pc_ratetest_baseline_at(&t, 2900));
    CHECK(!pc_ratetest_baseline_at(&t, 2950));
    pc_ratetest_init(&t, 96000, 0);
    CHECK(pc_ratetest_baseline_at(&t, 7000));            /* a late first poll still gets it once */
    pc_ratetest_leave(&t);
    CHECK(!pc_ratetest_baseline_at(&t, 7100));
    pc_ratetest_init(&t, 44100, 0);
    CHECK(!pc_ratetest_baseline_at(&t, 400));
}

int main(void)
{
    RUN(test_rt_baseline_at);
    RUN(test_rt_tiers);
    RUN(test_rt_timeline);
    RUN(test_rt_tap_rules);
    RUN(test_rt_timeout);
    RUN(test_rt_fault);
    RUN(test_rt_leave);
    RUN(test_rt_render);
    RUN(test_rt_render_rates);
    return 0;
}
