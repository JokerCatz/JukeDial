// SPDX-License-Identifier: LGPL-2.1-or-later
/* 骨牌 / Dominoes (ideas G4): the tile ids, the deal, the opening, legal ends and orientation, draw and pass, the end by an empty hand or a block, 五點 scoring,
 * the device's moves (complete games, tiles conserved), the golden double. */
#include "pc/pc_game_dominoes.h"
#include "pc_test.h"

static int total(const pc_dom_t *g) { return pc_dom_count(g->hand[0]) + pc_dom_count(g->hand[1]) + g->yard_n + g->chain_n; }
/* an empty table with chosen hands (a test sets the situation, the rules decide) */
static void table(pc_dom_t *g, int rule, uint32_t h0, uint32_t h1)
{
    pc_dom_init(g, rule, 1);
    g->chain_n = 0; g->yard_n = 0; g->passes = 0; g->state = PC_DOM_PLAY; g->turn = PC_DOM_YOU; g->score[0] = g->score[1] = 0; g->gold_tile = -1;
    g->hand[0] = h0; g->hand[1] = h1;
}
#define T(a, b) (1u << pc_dom_id((a), (b)))

static void test_ids(void)
{
    int seen[PC_DOM_TILES] = { 0 };
    for (int id = 0; id < PC_DOM_TILES; id++) {
        int a = pc_dom_a(id), b = pc_dom_b(id);
        CHECK(a >= 0 && a <= b && b <= 6);
        CHECK_EQ_INT(pc_dom_id(a, b), id);
        CHECK_EQ_INT(pc_dom_id(b, a), id);
        seen[id]++;
    }
    CHECK_EQ_INT(pc_dom_id(0, 0), 0);
    CHECK_EQ_INT(pc_dom_id(6, 6), 27);
    CHECK_EQ_INT(pc_dom_id(2, 5), 5 * 6 / 2 + 2);
}
static void test_faces(void)
{
    /* every oriented face a page can build (a*7+b over the pips 0..6) is in the face set; 49 and beyond, and negatives, are not (ui_cards clamps them) */
    for (int a = 0; a <= 6; a++) for (int b = 0; b <= 6; b++) CHECK(pc_dom_face_ok(a * 7 + b));
    CHECK(!pc_dom_face_ok(PC_DOM_FACES)); CHECK(!pc_dom_face_ok(-1)); CHECK(!pc_dom_face_ok(70));
    CHECK_EQ_INT(PC_DOM_FACES, 7 * 7);
}
static void test_deal(void)
{
    pc_dom_t a, b;
    pc_dom_init(&a, PC_DOM_CLASSIC, 7);
    pc_dom_init(&b, PC_DOM_CLASSIC, 7);
    CHECK_EQ_INT(a.hand[0], b.hand[0]);               /* deterministic by seed */
    CHECK_EQ_INT(total(&a), 28);
    CHECK_EQ_INT(a.chain_n, 1);                       /* the opening is played */
    CHECK_EQ_INT(a.yard_n, 14);
    CHECK_EQ_INT(pc_dom_count(a.hand[0]) + pc_dom_count(a.hand[1]), 13);
    CHECK_EQ_INT(a.turn, 1 - a.start_who);
    CHECK_EQ_INT(a.plays[a.start_who], 1);
    pc_dom_init(&a, 9, 7);
    CHECK_EQ_INT(a.rule, PC_DOM_CLASSIC);             /* an unknown rule is the classic */
    pc_dom_init(&b, PC_DOM_CLASSIC, 8);
    CHECK(a.hand[0] != b.hand[0] || a.hand[1] != b.hand[1]);
}
static void test_opening(void)
{
    /* whoever holds the highest double opens, over many deals */
    for (uint32_t s = 1; s < 200; s++) {
        pc_dom_t g;
        pc_dom_init(&g, PC_DOM_CLASSIC, s);
        const int t = g.start_tile;
        uint32_t all = g.hand[0] | g.hand[1] | (1u << t);
        for (int d = 6; d >= 0; d--) if (all & (1u << pc_dom_id(d, d))) { CHECK_EQ_INT(t, pc_dom_id(d, d)); break; }
        CHECK_EQ_INT(g.chain[0], t);
        CHECK_EQ_INT(g.cl[0], pc_dom_a(t)); CHECK_EQ_INT(g.cr[0], pc_dom_b(t));
        CHECK_EQ_INT(g.rule, PC_DOM_CLASSIC);
    }
}
static void test_legal_and_orientation(void)
{
    pc_dom_t g;
    table(&g, PC_DOM_BLOCK, T(3, 4) | T(2, 5) | T(6, 6), T(0, 1));
    g.chain[0] = (uint8_t)pc_dom_id(3, 3); g.cl[0] = 3; g.cr[0] = 3; g.chain_n = 1;
    CHECK_EQ_INT(pc_dom_left(&g), 3); CHECK_EQ_INT(pc_dom_right(&g), 3);
    CHECK_EQ_INT(pc_dom_legal(&g, pc_dom_id(3, 4)), 3);            /* a 3 fits both ends */
    CHECK_EQ_INT(pc_dom_legal(&g, pc_dom_id(2, 5)), 0);
    CHECK_EQ_INT(pc_dom_playable(&g, 0), T(3, 4));
    CHECK(!pc_dom_can_move(&g, 1));
    CHECK_EQ_INT(pc_dom_place(&g, 0, pc_dom_id(2, 5), PC_DOM_LEFT), -1);   /* no fit */
    CHECK_EQ_INT(pc_dom_place(&g, 1, pc_dom_id(0, 1), PC_DOM_LEFT), -1);   /* not the turn */
    CHECK(pc_dom_place(&g, 0, pc_dom_id(3, 4), PC_DOM_RIGHT) >= 0);
    CHECK_EQ_INT(g.chain_n, 2);
    CHECK_EQ_INT(g.cl[1], 3); CHECK_EQ_INT(g.cr[1], 4);            /* the 3 touches the end, the 4 is the new end */
    CHECK_EQ_INT(pc_dom_right(&g), 4); CHECK_EQ_INT(pc_dom_left(&g), 3);
    CHECK_EQ_INT(g.turn, 1);
    table(&g, PC_DOM_BLOCK, T(3, 4) | T(2, 5), T(0, 1));
    g.chain[0] = (uint8_t)pc_dom_id(1, 3); g.cl[0] = 1; g.cr[0] = 3; g.chain_n = 1;
    CHECK(pc_dom_place(&g, 0, pc_dom_id(3, 4), PC_DOM_LEFT) < 0);   /* the left end is 1 */
    CHECK(pc_dom_place(&g, 0, pc_dom_id(3, 4), PC_DOM_RIGHT) >= 0);
    CHECK_EQ_INT(g.cl[1], 3); CHECK_EQ_INT(g.cr[1], 4);
    g.turn = 0; g.hand[0] = T(1, 4);
    CHECK(pc_dom_place(&g, 0, pc_dom_id(1, 4), PC_DOM_LEFT) >= 0);  /* a 1 on the left end: the 4 goes outside */
    CHECK_EQ_INT(g.cl[0], 4); CHECK_EQ_INT(g.cr[0], 1);
    CHECK_EQ_INT(pc_dom_left(&g), 4);
    CHECK_EQ_INT(g.chain[0], pc_dom_id(1, 4));                      /* a left placement goes in front: chain[0] is the leftmost */
}
static void test_draw_and_pass(void)
{
    pc_dom_t g;
    table(&g, PC_DOM_CLASSIC, T(0, 1), T(5, 6));
    g.chain[0] = (uint8_t)pc_dom_id(3, 3); g.cl[0] = 3; g.cr[0] = 3; g.chain_n = 1;
    g.yard[0] = (uint8_t)pc_dom_id(2, 3); g.yard[1] = (uint8_t)pc_dom_id(4, 4); g.yard_n = 2;
    CHECK_EQ_INT(pc_dom_draw(&g, 1), -1);                          /* not its turn */
    CHECK_EQ_INT(pc_dom_draw(&g, 0), pc_dom_id(4, 4));             /* the top of the boneyard */
    CHECK_EQ_INT(g.yard_n, 1);
    CHECK_EQ_INT(pc_dom_draw(&g, 0), pc_dom_id(2, 3));
    CHECK_EQ_INT(pc_dom_draw(&g, 0), -1);                          /* it can play now */
    CHECK(!pc_dom_pass(&g, 0));
    table(&g, PC_DOM_BLOCK, T(0, 1), T(5, 6));
    g.chain[0] = (uint8_t)pc_dom_id(3, 3); g.cl[0] = 3; g.cr[0] = 3; g.chain_n = 1;
    g.yard[0] = (uint8_t)pc_dom_id(2, 3); g.yard_n = 1;
    CHECK_EQ_INT(pc_dom_draw(&g, 0), -1);                          /* the block rule never draws */
    CHECK(pc_dom_pass(&g, 0));
    CHECK_EQ_INT(g.turn, 1);
    CHECK_EQ_INT(g.passes, 1);
    CHECK(pc_dom_pass(&g, 1));                                     /* both pass: blocked; 0|1 = 1 pip against 11 */
    CHECK_EQ_INT(g.state, PC_DOM_OVER);
    CHECK_EQ_INT(g.winner, PC_DOM_YOU);
    CHECK_EQ_INT(pc_dom_place(&g, 0, pc_dom_id(0, 1), PC_DOM_LEFT), -1);   /* over */
}
static void test_blocked_draw_and_tie(void)
{
    pc_dom_t g;
    table(&g, PC_DOM_BLOCK, T(0, 5), T(1, 4));                      /* 5 pips each */
    g.chain[0] = (uint8_t)pc_dom_id(3, 3); g.cl[0] = 3; g.cr[0] = 3; g.chain_n = 1;
    CHECK(pc_dom_pass(&g, 0)); CHECK(pc_dom_pass(&g, 1));
    CHECK_EQ_INT(g.winner, PC_DOM_DRAW);
    table(&g, PC_DOM_CLASSIC, T(0, 5), T(1, 6));
    g.chain[0] = (uint8_t)pc_dom_id(3, 3); g.cl[0] = 3; g.cr[0] = 3; g.chain_n = 1;
    CHECK(pc_dom_pass(&g, 0)); CHECK(pc_dom_pass(&g, 1));           /* empty boneyard: classic passes too */
    CHECK_EQ_INT(g.winner, PC_DOM_YOU);
}
static void test_empty_hand_wins(void)
{
    pc_dom_t g;
    table(&g, PC_DOM_CLASSIC, T(3, 4), T(0, 1) | T(5, 6));
    g.chain[0] = (uint8_t)pc_dom_id(3, 3); g.cl[0] = 3; g.cr[0] = 3; g.chain_n = 1;
    CHECK(pc_dom_place(&g, 0, pc_dom_id(3, 4), PC_DOM_LEFT) >= 0);
    CHECK_EQ_INT(g.state, PC_DOM_OVER);
    CHECK_EQ_INT(g.winner, PC_DOM_YOU);
    table(&g, PC_DOM_CLASSIC, T(0, 1) | T(5, 6), T(3, 4));
    g.chain[0] = (uint8_t)pc_dom_id(3, 3); g.cl[0] = 3; g.cr[0] = 3; g.chain_n = 1; g.turn = 1;
    CHECK(pc_dom_place(&g, 1, pc_dom_id(3, 4), PC_DOM_RIGHT) >= 0);
    CHECK_EQ_INT(g.winner, PC_DOM_DEV);
}
static void test_five(void)
{
    pc_dom_t g;
    /* a lone double 5|5 counts once (10); 5|5 then 5|2: ends 5 (a double) + 2 = 12 ... use a chain whose ends sum to 10, 15, a double end counting twice */
    table(&g, PC_DOM_FIVE, T(5, 4) | T(3, 3) | T(0, 6), T(1, 1) | T(2, 2));
    g.chain[0] = (uint8_t)pc_dom_id(1, 5); g.cl[0] = 1; g.cr[0] = 5; g.chain_n = 1;
    CHECK_EQ_INT(pc_dom_ends_sum(&g), 6);
    int gain = pc_dom_place(&g, 0, pc_dom_id(5, 4), PC_DOM_RIGHT);  /* ends 1 and 4 = 5 */
    CHECK_EQ_INT(gain, 5);
    CHECK_EQ_INT(g.score[0], 5);
    g.turn = 0;
    g.chain_n = 1; g.chain[0] = (uint8_t)pc_dom_id(3, 3); g.cl[0] = 3; g.cr[0] = 3;
    CHECK_EQ_INT(pc_dom_ends_sum(&g), 6);                          /* a lone double counts once */
    g.chain[0] = (uint8_t)pc_dom_id(5, 5); g.cl[0] = 5; g.cr[0] = 5;
    CHECK_EQ_INT(pc_dom_ends_sum(&g), 10);
    g.chain_n = 2; g.chain[0] = (uint8_t)pc_dom_id(5, 5); g.cl[0] = 5; g.cr[0] = 5; g.chain[1] = (uint8_t)pc_dom_id(5, 2); g.cl[1] = 5; g.cr[1] = 2;
    CHECK_EQ_INT(pc_dom_ends_sum(&g), 5 + 5 + 2);                  /* the double at the left end counts both halves */
    /* the winner of the round gets the other side's pips rounded to 5: 1|1 + 2|2 = 6 -> 5 */
    table(&g, PC_DOM_FIVE, T(3, 3), T(1, 1) | T(2, 2));
    g.chain[0] = (uint8_t)pc_dom_id(3, 4); g.cl[0] = 3; g.cr[0] = 4; g.chain_n = 1;
    g.score[0] = 0;
    CHECK(pc_dom_place(&g, 0, pc_dom_id(3, 3), PC_DOM_LEFT) >= 0);   /* ends 3 | 4 ... left end is the new double 3 (counts 6) + 4 = 10 */
    CHECK_EQ_INT(g.state, PC_DOM_OVER);
    CHECK_EQ_INT(g.score[0], 10 + 5);
    CHECK_EQ_INT(g.winner, PC_DOM_YOU);
    /* a round won by the lower score? the higher score wins even when the other side emptied */
    table(&g, PC_DOM_FIVE, T(3, 3), T(1, 1));
    g.chain[0] = (uint8_t)pc_dom_id(3, 4); g.cl[0] = 3; g.cr[0] = 4; g.chain_n = 1;
    g.score[1] = 50;
    CHECK(pc_dom_place(&g, 0, pc_dom_id(3, 3), PC_DOM_LEFT) >= 0);
    CHECK_EQ_INT(g.winner, PC_DOM_DEV);
}
static void test_gold(void)
{
    int n = 0;
    for (uint32_t s = 1; s <= 2000; s++) {
        pc_dom_t g;
        pc_dom_init(&g, PC_DOM_CLASSIC, s);
        if (g.gold_tile >= 0) { n++; CHECK_EQ_INT(pc_dom_a(g.gold_tile), pc_dom_b(g.gold_tile)); }
    }
    CHECK(n > 130 && n < 280);                                      /* about one game in ten */
    pc_dom_t g;
    table(&g, PC_DOM_FIVE, T(3, 4), T(0, 1));
    g.chain[0] = (uint8_t)pc_dom_id(3, 3); g.cl[0] = 3; g.cr[0] = 3; g.chain_n = 1;
    g.gold_tile = (int8_t)pc_dom_id(3, 3);
    CHECK(pc_dom_place(&g, 0, pc_dom_id(3, 4), PC_DOM_LEFT) >= 0);
    CHECK(!g.last_gold);                                            /* only the golden tile itself */
    table(&g, PC_DOM_FIVE, T(3, 3) | T(0, 1), T(5, 5));
    g.chain[0] = (uint8_t)pc_dom_id(3, 4); g.cl[0] = 3; g.cr[0] = 4; g.chain_n = 1;
    g.gold_tile = (int8_t)pc_dom_id(3, 3);
    int s0 = g.score[0];
    int gain = pc_dom_place(&g, 0, pc_dom_id(3, 3), PC_DOM_LEFT);
    CHECK(g.last_gold);
    CHECK_EQ_INT(g.score[0], s0 + gain + 10);
}
/* whole games: the device plays both sides by its own rules (a draw when it must, a pass when it must), every tile stays somewhere, every game ends */
static void test_complete_games(void)
{
    int wins[3] = { 0, 0, 0 };
    for (int rule = 0; rule < PC_DOM_RULES; rule++)
        for (uint32_t s = 1; s <= 120; s++) {
            pc_dom_t g;
            pc_dom_init(&g, rule, s);
            for (int guard = 0; g.state == PC_DOM_PLAY && guard < 400; guard++) {
                int id, side;
                const int who = g.turn;
                if (pc_dom_ai(&g, &id, &side)) {
                    CHECK(pc_dom_legal(&g, id) & (1 << side));
                    CHECK(g.hand[who] & (1u << id));
                    CHECK(pc_dom_place(&g, who, id, side) >= 0);
                } else if (pc_dom_draw(&g, who) < 0) CHECK(pc_dom_pass(&g, who));
                CHECK_EQ_INT(total(&g), 28);
                CHECK(rule != PC_DOM_BLOCK || g.yard_n == 14);
            }
            CHECK_EQ_INT(g.state, PC_DOM_OVER);
            CHECK(g.winner <= PC_DOM_DRAW);
            wins[g.winner]++;
            /* the chain really is a chain: neighbours match */
            for (int i = 1; i < g.chain_n; i++) CHECK_EQ_INT(g.cr[i - 1], g.cl[i]);
            if (rule == PC_DOM_FIVE) CHECK(g.score[0] + g.score[1] > 0);
        }
    CHECK(wins[0] > 50 && wins[1] > 50);                            /* the same player on both sides: wins are shared */
}
int main(void)
{
    RUN(test_ids);
    RUN(test_faces);
    RUN(test_deal);
    RUN(test_opening);
    RUN(test_legal_and_orientation);
    RUN(test_draw_and_pass);
    RUN(test_blocked_draw_and_tie);
    RUN(test_empty_hand_wins);
    RUN(test_five);
    RUN(test_gold);
    RUN(test_complete_games);
    return 0;
}
