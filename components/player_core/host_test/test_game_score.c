// SPDX-License-Identifier: LGPL-2.1-or-later
/* The session score of a game page (G0b): best this session, the win streak, the rare golden game. */
#include "pc/pc_game_score.h"
#include "pc_test.h"

static void test_best_and_streak(void)
{
    pc_game_score_t s;
    pc_game_score_reset(&s);
    pc_game_rec_t r = pc_game_score_record(&s, 0, PC_GAME_WIN, true, 9, 20000);
    CHECK(r.new_best);
    CHECK_EQ_INT(r.best_moves, 9);
    CHECK_EQ_INT(r.streak, 1);
    CHECK(!r.streak_hit);
    r = pc_game_score_record(&s, 0, PC_GAME_WIN, true, 11, 9000);   /* more moves: not a best, even if faster */
    CHECK(!r.new_best);
    CHECK_EQ_INT(r.best_moves, 9);
    CHECK_EQ_INT(r.best_ms, 20000);
    r = pc_game_score_record(&s, 0, PC_GAME_WIN, true, 9, 15000);   /* the same moves, faster: a best */
    CHECK(r.new_best);
    CHECK_EQ_INT(r.best_ms, 15000);
    CHECK_EQ_INT(r.streak, 3);
    CHECK(r.streak_hit);                                            /* three in a row */
    r = pc_game_score_record(&s, 0, PC_GAME_WIN, true, 13, 1000);
    CHECK_EQ_INT(r.streak, 4);
    CHECK(r.streak_hit);                                            /* and every win after it while it lasts */
    r = pc_game_score_record(&s, 1, PC_GAME_WIN, true, 7, 1000);    /* another rule: its own best */
    CHECK(r.new_best);
    CHECK_EQ_INT(r.best_moves, 7);
    CHECK_EQ_INT(pc_game_score_record(&s, 0, PC_GAME_DRAW, true, 9, 1).best_moves, 9);
    CHECK_EQ_INT(s.streak, 0);                                      /* a draw ends the streak */
    CHECK_EQ_INT(s.best_streak, 5);
    r = pc_game_score_record(&s, 0, PC_GAME_LOSS, true, 5, 1);
    CHECK(!r.new_best && r.streak == 0);                            /* a loss is never a best */
    pc_game_score_record(&s, 0, PC_GAME_WIN, true, 9, 1);
    r = pc_game_score_record(&s, 0, PC_GAME_WIN, false, 5, 1);     /* two players: a best, but no streak change */
    CHECK(r.new_best);
    CHECK_EQ_INT(r.streak, 1);
    CHECK_EQ_INT(s.played, 9);
    CHECK_EQ_INT(s.wins, 7);
    r = pc_game_score_record(&s, 99, PC_GAME_WIN, true, 3, 1);     /* a variant out of range: counted, no best */
    CHECK(!r.new_best && r.best_moves == 0);
}

static void test_gold_is_rare(void)
{
    int n = 0;
    for (uint32_t i = 0; i < 30000; i++) n += pc_game_gold(i * 2654435761u + 17u);
    CHECK(n > 700 && n < 1300);                                     /* about one in PC_GAME_GOLD_N */
    CHECK_EQ_INT(pc_game_gold(12345), pc_game_gold(12345));
}

/* a score game (G1 四色記憶: the length reached): the highest is the best, no streak, every run is counted */
static void test_high_score(void)
{
    pc_game_score_t s;
    pc_game_score_reset(&s);
    pc_game_rec_t r = pc_game_score_record_high(&s, 0, 0, 5000);   /* nothing reached: not a best */
    CHECK(!r.new_best);
    CHECK_EQ_INT(r.best_moves, 0);
    r = pc_game_score_record_high(&s, 0, 7, 30000);
    CHECK(r.new_best);
    CHECK_EQ_INT(r.best_moves, 7);
    CHECK_EQ_INT(r.best_ms, 30000);
    r = pc_game_score_record_high(&s, 0, 7, 1000);                 /* the same length again is not a new best (even faster) */
    CHECK(!r.new_best);
    CHECK_EQ_INT(r.best_ms, 30000);
    r = pc_game_score_record_high(&s, 0, 4, 1000);
    CHECK(!r.new_best);
    CHECK_EQ_INT(r.best_moves, 7);
    r = pc_game_score_record_high(&s, 0, 12, 50000);
    CHECK(r.new_best);
    CHECK_EQ_INT(r.best_moves, 12);
    r = pc_game_score_record_high(&s, 3, 2, 1);                    /* another rule: its own best */
    CHECK(r.new_best && r.best_moves == 2);
    CHECK_EQ_INT(s.best_moves[0], 12);
    CHECK_EQ_INT(r.streak, 0);
    CHECK_EQ_INT(s.streak, 0);
    CHECK_EQ_INT(s.played, 6);
    r = pc_game_score_record_high(&s, PC_GAME_VARIANTS, 50, 1);    /* a variant out of range: counted, no best */
    CHECK(!r.new_best && r.best_moves == 0);
    CHECK_EQ_INT(s.played, 7);
    r = pc_game_score_record_high(&s, 0, 70000, 1);                /* held at 65535 */
    CHECK(r.new_best && r.best_moves == 65535);
}

/* a score game (G1 環形打磚塊): the best is the most points of each rule; the streak is not touched */
static void test_points(void)
{
    pc_game_score_t s;
    pc_game_score_reset(&s);
    s.streak = 2;
    pc_game_rec_t r = pc_game_score_points(&s, 0, 300, 60000);
    CHECK(r.new_best);
    CHECK_EQ_INT(r.best_points, 300);
    CHECK_EQ_INT(r.streak, 2);
    CHECK(!r.streak_hit);
    r = pc_game_score_points(&s, 0, 200, 1000);                     /* fewer points: not a best, however fast */
    CHECK(!r.new_best);
    CHECK_EQ_INT(r.best_points, 300);
    CHECK_EQ_INT(r.best_ms, 60000);
    r = pc_game_score_points(&s, 0, 300, 1000);                     /* the same points: not a new best either */
    CHECK(!r.new_best);
    r = pc_game_score_points(&s, 0, 301, 90000);
    CHECK(r.new_best);
    CHECK_EQ_INT(r.best_points, 301);
    r = pc_game_score_points(&s, 2, 10, 1000);                      /* another rule: its own best */
    CHECK(r.new_best);
    CHECK_EQ_INT(r.best_points, 10);
    CHECK(!pc_game_score_points(&s, 1, 0, 1000).new_best);          /* nothing scored: never a best */
    CHECK(!pc_game_score_points(&s, PC_GAME_VARIANTS, 999, 1).new_best);
    CHECK_EQ_INT(s.played, 7);
    CHECK_EQ_INT(s.streak, 2);
    CHECK_EQ_INT(s.best_moves[0], 0);                               /* the moves bests are another game kind's */
}

int main(void)
{
    RUN(test_best_and_streak);
    RUN(test_points);
    RUN(test_gold_is_rare);
    RUN(test_high_score);
    return 0;
}
