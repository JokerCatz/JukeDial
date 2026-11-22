// SPDX-License-Identifier: LGPL-2.1-or-later
/* 呼吸教練 / Breathing coach (ideas G7): the phase sequence of each pattern, skipped phases, the level curve, the countdown, the session end, the noise bound. */
#include "pc/pc_breath.h"
#include "pc_test.h"

static void test_sequence(void)
{
    static const int want[PC_BR_PATTERNS][4] = { { 4, 7, 8, 0 }, { 4, 4, 4, 4 }, { 5, 0, 5, 0 } };
    for (int p = 0; p < PC_BR_PATTERNS; p++) {
        pc_br_t b;
        pc_br_init(&b, p, 0);
        for (int ph = 0; ph < PC_BR_PHASES; ph++) CHECK_EQ_INT(pc_br_phase_s(p, ph), want[p][ph]);
        int len = 0, steps[PC_BR_PHASES] = { 0 };
        for (int ph = 0; ph < PC_BR_PHASES; ph++) len += want[p][ph];
        for (int i = 0; i < len * PC_BR_FPS; i++) { steps[b.phase]++; pc_br_step(&b); }
        for (int ph = 0; ph < PC_BR_PHASES; ph++) CHECK_EQ_INT(steps[ph], want[p][ph] * PC_BR_FPS);   /* each phase as long as the pattern says, a 0 s one never shows */
        CHECK_EQ_INT(b.cycles, 1);
        CHECK_EQ_INT(b.phase, PC_BR_IN);
        CHECK_EQ_INT(b.phase_tick, 0);
    }
}
static void test_box_order(void)
{
    pc_br_t b;
    pc_br_init(&b, PC_BR_BOX, 0);
    int ph[4], n = 0;
    for (int i = 0; i < 16 * 30 && n < 4; i++) if (pc_br_step(&b) & PC_BR_EV_PHASE) ph[n++] = b.phase;
    CHECK_EQ_INT(n, 4);
    CHECK_EQ_INT(ph[0], PC_BR_HOLD); CHECK_EQ_INT(ph[1], PC_BR_OUT); CHECK_EQ_INT(ph[2], PC_BR_REST); CHECK_EQ_INT(ph[3], PC_BR_IN);
    pc_br_init(&b, PC_BR_55, 0);
    n = 0;
    for (int i = 0; i < 10 * 30 && n < 2; i++) if (pc_br_step(&b) & PC_BR_EV_PHASE) ph[n++] = b.phase;
    CHECK_EQ_INT(ph[0], PC_BR_OUT); CHECK_EQ_INT(ph[1], PC_BR_IN);               /* 5-5 has no hold phases */
}
static void test_level(void)
{
    pc_br_t b;
    pc_br_init(&b, PC_BR_BOX, 0);
    CHECK_EQ_INT(pc_br_level(&b), 0);
    int prev = 0;
    for (int i = 0; i < 119; i++) { pc_br_step(&b); const int l = pc_br_level(&b); CHECK(l >= prev && l <= 1000); prev = l; }
    pc_br_step(&b);                                                              /* the 120th step: the hold */
    CHECK_EQ_INT(b.phase, PC_BR_HOLD);
    CHECK_EQ_INT(pc_br_level(&b), 1000);
    for (int i = 0; i < 120; i++) pc_br_step(&b);
    CHECK_EQ_INT(b.phase, PC_BR_OUT);
    prev = 1000;
    for (int i = 0; i < 119; i++) { pc_br_step(&b); const int l = pc_br_level(&b); CHECK(l <= prev && l >= 0); prev = l; }
    pc_br_step(&b);
    CHECK_EQ_INT(b.phase, PC_BR_REST);
    CHECK_EQ_INT(pc_br_level(&b), 0);
}
static void test_countdown_and_end(void)
{
    pc_br_t b;
    pc_br_init(&b, PC_BR_55, 0);
    CHECK_EQ_INT(pc_br_phase_left_s(&b), 5);
    CHECK_EQ_INT(pc_br_left_s(&b), 180);
    pc_br_step(&b);
    CHECK_EQ_INT(pc_br_phase_left_s(&b), 5);
    for (int i = 0; i < 29; i++) pc_br_step(&b);
    CHECK_EQ_INT(pc_br_phase_left_s(&b), 4);
    CHECK_EQ_INT(pc_br_left_s(&b), 179);
    static const int mins[PC_BR_SESSIONS] = { 180, 300, 600 };
    for (int s = 0; s < PC_BR_SESSIONS; s++) {
        pc_br_init(&b, PC_BR_478, s);
        int n = 0, ev = 0;
        while (!b.done) { ev = pc_br_step(&b); n++; CHECK(n <= mins[s] * 30); }
        CHECK_EQ_INT(n, mins[s] * 30);
        CHECK(ev & PC_BR_EV_DONE);
        CHECK_EQ_INT(pc_br_left_s(&b), 0);
        CHECK_EQ_INT(pc_br_step(&b), 0);                                         /* nothing after the end */
        CHECK_EQ_INT(b.elapsed, mins[s] * 30);
    }
    CHECK_EQ_INT(pc_br_session_s(9), 180);
    pc_br_init(&b, 77, 77);                                                      /* unknown values: 4-7-8, 3 min */
    CHECK_EQ_INT(b.pattern, PC_BR_478);
    CHECK_EQ_INT(b.total, 180 * 30);
    CHECK_STR(pc_br_pattern_id(PC_BR_BOX), "box");
}
static void test_noise_bound(void)
{
    for (int depth = -100; depth <= 900; depth += 50)
        for (int level = -50; level <= 1100; level += 25) {
            const int g = pc_br_noise_pm(level, depth);
            CHECK(g <= 1000);
            CHECK(g >= 1000 - PC_BR_DEPTH_MAX);
        }
    CHECK_EQ_INT(pc_br_noise_pm(1000, 500), 1000);
    CHECK_EQ_INT(pc_br_noise_pm(0, 500), 500);
    CHECK_EQ_INT(pc_br_noise_pm(500, 400), 800);
    CHECK_EQ_INT(pc_br_noise_pm(0, 0), 1000);
}
static void test_hold(void)
{
    pc_br_hold_t h = { 0 };
    CHECK(!pc_br_hold_on(&h, 0, 5000000, 1000000));                  /* never asked: off */
    CHECK(pc_br_hold_on(&h, 1, 5000000, 1000000));                   /* a new count renews */
    CHECK(pc_br_hold_on(&h, 1, 5900000, 1000000));
    CHECK(!pc_br_hold_on(&h, 1, 6000001, 1000000));                  /* expired */
    CHECK(!pc_br_hold_on(&h, 1, 6000001 + 3000000000LL * 1000, 1000000));   /* far later (past a 2^31 ms wrap): still off, the old cut does not return */
    CHECK(pc_br_hold_on(&h, 2, 9000000000000LL, 1000000));          /* a new count renews at any clock */
    h.seen = 0xffffffffu;
    CHECK(pc_br_hold_on(&h, 0, 100, 1000000));                       /* the counter wraps: still a new count */
}
int main(void)
{
    RUN(test_sequence);
    RUN(test_box_order);
    RUN(test_level);
    RUN(test_countdown_and_end);
    RUN(test_noise_bound);
    RUN(test_hold);
    return 0;
}
