// SPDX-License-Identifier: LGPL-2.1-or-later
/* 保險箱開鎖 / Safe cracker (G2): the combination, detents, confirm and reset, the clock, the rules. */
#include "pc/pc_game_safe.h"
#include "pc/pc_game_score.h"
#include "pc_test.h"

static bool has(const pc_safe_t *g, int ev) { for (int i = 0; i < g->n_ev; i++) if (g->ev[i] == ev) return true; return false; }
/* turn the dial to `to` the short way */
static void goto_pos(pc_safe_t *g, int to) { int d = to - g->pos; if (d > 50) d -= 100; if (d < -50) d += 100; pc_safe_turn(g, d); }

static void test_combination(void)
{
    for (int rule = 0; rule < PC_SAFE_RULES; rule++)
        for (uint32_t seed = 1; seed < 200; seed++) {
            pc_safe_t g;
            pc_safe_init(&g, rule, seed);
            CHECK_EQ_INT(g.n, rule == PC_SAFE_EXPERT ? 4 : 3);
            int prev = 0;
            for (int k = 0; k < g.n; k++) {
                CHECK(g.target[k] >= 0 && g.target[k] < PC_SAFE_RANGE);
                int d = g.target[k] > prev ? g.target[k] - prev : prev - g.target[k];
                if (d > 50) d = 100 - d;
                CHECK(d >= PC_SAFE_APART);                      /* there is always a turn to make */
                prev = g.target[k];
            }
            CHECK_EQ_INT(g.pos, 0);
            CHECK_EQ_INT(g.state, PC_SAFE_PLAY);
            CHECK(!g.started);
        }
    pc_safe_t a, b;
    pc_safe_init(&a, 0, 77);
    pc_safe_init(&b, 0, 77);
    for (int k = 0; k < 3; k++) CHECK_EQ_INT(a.target[k], b.target[k]);   /* deterministic by seed */
}
static void test_turn_and_detent(void)
{
    pc_safe_t g;
    pc_safe_init(&g, 0, 5);
    int t = g.target[0];
    pc_safe_turn(&g, -3);
    CHECK_EQ_INT(g.pos, 97);                                    /* wraps */
    CHECK(g.started);
    CHECK_EQ_INT(g.turns, 3);
    CHECK_EQ_INT(g.n_ev, 0);
    goto_pos(&g, t);
    CHECK_EQ_INT(g.pos, t);
    pc_safe_turn(&g, 0);
    pc_safe_turn(&g, 1);
    pc_safe_turn(&g, -1);
    CHECK(has(&g, PC_SAFE_EV_DETENT));                          /* turning back onto the number is a detent again */
    CHECK_EQ_INT(pc_safe_dist(&g), 0);
    pc_safe_turn(&g, 100);                                      /* a whole turn: lands on it again */
    CHECK(has(&g, PC_SAFE_EV_DETENT));
    pc_safe_turn(&g, 1);
    CHECK_EQ_INT(pc_safe_dist(&g), 1);
    CHECK(!has(&g, PC_SAFE_EV_DETENT));
}
static void test_confirm_and_reset(void)
{
    pc_safe_t g;
    pc_safe_init(&g, 0, 9);
    goto_pos(&g, g.target[0]);
    pc_safe_confirm(&g);
    CHECK(has(&g, PC_SAFE_EV_LOCK));
    CHECK_EQ_INT(g.stage, 1);
    CHECK_EQ_INT(g.attempts, 1);
    CHECK_EQ_INT(pc_safe_target(&g), g.target[1]);
    goto_pos(&g, (g.target[1] + 7) % 100);                      /* a wrong number: everything resets */
    pc_safe_confirm(&g);
    CHECK(has(&g, PC_SAFE_EV_WRONG));
    CHECK_EQ_INT(g.stage, 0);
    CHECK_EQ_INT(g.attempts, 2);
    CHECK_EQ_INT(g.state, PC_SAFE_PLAY);
    for (int k = 0; k < 3; k++) {                               /* now in order */
        goto_pos(&g, g.target[k]);
        pc_safe_confirm(&g);
    }
    CHECK(has(&g, PC_SAFE_EV_OPEN));
    CHECK_EQ_INT(g.state, PC_SAFE_OPEN);
    CHECK_EQ_INT(g.attempts, 5);
    CHECK_EQ_INT(pc_safe_target(&g), -1);
    int turns = g.turns;
    pc_safe_turn(&g, 5);                                        /* open: nothing more counts */
    pc_safe_confirm(&g);
    CHECK_EQ_INT(g.turns, turns);
    CHECK_EQ_INT(g.attempts, 5);
    CHECK_EQ_INT(g.n_ev, 0);
}
static void test_clock_and_rules(void)
{
    pc_safe_t g;
    pc_safe_init(&g, PC_SAFE_CLASSIC, 3);
    pc_safe_tick(&g, 500);
    CHECK_EQ_INT(g.ms, 0);                                      /* the clock waits for the first move */
    CHECK(!pc_safe_running(&g));
    pc_safe_turn(&g, 1);
    CHECK(pc_safe_running(&g));
    pc_safe_tick(&g, 100000);
    CHECK_EQ_INT(g.state, PC_SAFE_PLAY);                        /* classic has no time limit */
    CHECK_EQ_INT(pc_safe_left_ms(&g), 0);
    pc_safe_init(&g, PC_SAFE_TIMED, 3);
    pc_safe_confirm(&g);                                        /* a confirm starts it too (a wrong one) */
    CHECK(pc_safe_running(&g));
    CHECK_EQ_INT(pc_safe_left_ms(&g), PC_SAFE_TIME_MS);
    pc_safe_tick(&g, 59900);
    CHECK_EQ_INT(g.state, PC_SAFE_PLAY);
    CHECK_EQ_INT(pc_safe_left_ms(&g), 100);
    pc_safe_tick(&g, 100);
    CHECK_EQ_INT(g.state, PC_SAFE_TIMEUP);
    CHECK(has(&g, PC_SAFE_EV_TIMEUP));
    CHECK(!pc_safe_running(&g));
    pc_safe_turn(&g, 3);
    CHECK_EQ_INT(g.pos, 0);                                     /* over: the dial is dead */
    CHECK_EQ_INT(pc_safe_target(&g), -1);
}
static void test_reach(void)
{
    CHECK_EQ_INT(pc_safe_reach(PC_SAFE_CLASSIC, 1, false), 12);
    CHECK(pc_safe_reach(PC_SAFE_EXPERT, 1, false) < pc_safe_reach(PC_SAFE_CLASSIC, 1, false));   /* the expert's hint is weaker */
    CHECK(pc_safe_reach(PC_SAFE_CLASSIC, 0, false) > pc_safe_reach(PC_SAFE_CLASSIC, 1, false));
    CHECK(pc_safe_reach(PC_SAFE_CLASSIC, 2, false) < pc_safe_reach(PC_SAFE_CLASSIC, 1, false));
    CHECK_EQ_INT(pc_safe_reach(PC_SAFE_CLASSIC, 1, true), 24);
    CHECK(pc_safe_reach(PC_SAFE_EXPERT, 2, false) >= 1);
}
static void test_score_record(void)                              /* the result card's best: fewest attempts, then time, per rule */
{
    pc_game_score_t s;
    pc_game_score_reset(&s);
    pc_game_rec_t r = pc_game_score_record(&s, PC_SAFE_CLASSIC, PC_GAME_WIN, false, 5, 40000);
    CHECK(r.new_best);
    r = pc_game_score_record(&s, PC_SAFE_CLASSIC, PC_GAME_WIN, false, 4, 90000);
    CHECK(r.new_best && r.best_moves == 4);
    r = pc_game_score_record(&s, PC_SAFE_TIMED, PC_GAME_LOSS, false, 6, 60000);
    CHECK(!r.new_best);
}
static void test_cue_strength(void)                             /* weaker hint = weaker cue; the expert rule still gets some */
{
    CHECK(pc_safe_cue_strength(PC_SAFE_CLASSIC, 0) > pc_safe_cue_strength(PC_SAFE_CLASSIC, 1));
    CHECK(pc_safe_cue_strength(PC_SAFE_CLASSIC, 1) > pc_safe_cue_strength(PC_SAFE_CLASSIC, 2));
    CHECK(pc_safe_cue_strength(PC_SAFE_CLASSIC, 2) > 0);
    CHECK(pc_safe_cue_strength(PC_SAFE_EXPERT, 2) > 0);
    CHECK(pc_safe_cue_strength(PC_SAFE_EXPERT, 1) <= pc_safe_cue_strength(PC_SAFE_CLASSIC, 1));
    CHECK(pc_safe_cue_strength(PC_SAFE_CLASSIC, 0) <= 100);
}
int main(void)
{
    RUN(test_cue_strength);
    RUN(test_combination);
    RUN(test_turn_and_detent);
    RUN(test_confirm_and_reset);
    RUN(test_clock_and_rules);
    RUN(test_reach);
    RUN(test_score_record);
    return 0;
}
