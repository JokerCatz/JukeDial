// SPDX-License-Identifier: LGPL-2.1-or-later
/* 四子棋 rules and the minimax computer (games hub G0). */
#include "pc/pc_game_c4.h"
#include "pc_test.h"

static int at(int r, int c) { return r * PC_C4_W + c; }

/* the four cells of a line in any order */
static void check_line(const int *line, int a, int b, int c, int d)
{
    int want[4] = { a, b, c, d }, n = 0;
    for (int i = 0; i < 4; i++) for (int k = 0; k < 4; k++) if (line[i] == want[k]) { n++; break; }
    CHECK_EQ_INT(n, 4);
    CHECK(line[0] != line[1] && line[0] != line[2] && line[0] != line[3] && line[1] != line[2] && line[1] != line[3] && line[2] != line[3]);
}

static void put(pc_c4_t *g, int r, int c, int who)
{
    g->cell[at(r, c)] = (uint8_t)who;
    if (g->height[c] < PC_C4_H - r) g->height[c] = (uint8_t)(PC_C4_H - r);
    g->moves++;
}

static void test_gravity_and_columns(void)
{
    pc_c4_t g;
    pc_c4_init(&g);
    CHECK_EQ_INT(g.turn, 1);
    CHECK_EQ_INT(pc_c4_drop(&g, 3), at(5, 3));    /* the bottom row */
    CHECK_EQ_INT(g.turn, 2);
    CHECK_EQ_INT(pc_c4_drop(&g, 3), at(4, 3));    /* on top of it */
    CHECK_EQ_INT(g.cell[at(5, 3)], 1);
    CHECK_EQ_INT(g.cell[at(4, 3)], 2);
    CHECK_EQ_INT(pc_c4_drop(&g, -1), -1);
    CHECK_EQ_INT(pc_c4_drop(&g, 7), -1);
    CHECK_EQ_INT(g.moves, 2);
    CHECK_EQ_INT(g.turn, 1);                      /* refused drops change nothing */
    pc_c4_init(&g);
    for (int i = 0; i < PC_C4_H; i++) CHECK(pc_c4_drop(&g, 0) >= 0);   /* 1 2 1 2 1 2: no four */
    CHECK_EQ_INT(pc_c4_drop(&g, 0), -1);          /* a full column */
    CHECK_EQ_INT(g.cell[at(0, 0)], 2);
    CHECK_EQ_INT(pc_c4_winner(&g, NULL), 0);
}

static void test_wins(void)
{
    pc_c4_t g;
    int line[4];
    /* vertical: 1 stacks col 2, 2 stacks col 5 */
    pc_c4_init(&g);
    const int v[] = { 2, 5, 2, 5, 2, 5, 2 };
    for (int i = 0; i < 7; i++) CHECK(pc_c4_drop(&g, v[i]) >= 0);
    CHECK_EQ_INT(pc_c4_winner(&g, line), 1);
    check_line(line, at(2, 2), at(3, 2), at(4, 2), at(5, 2));
    CHECK(pc_c4_over(&g));
    CHECK_EQ_INT(pc_c4_drop(&g, 0), -1);          /* over: no more drops */
    /* horizontal for player 2 */
    pc_c4_init(&g);
    const int h[] = { 0, 1, 0, 2, 6, 3, 6, 4 };
    for (int i = 0; i < 8; i++) CHECK(pc_c4_drop(&g, h[i]) >= 0);
    CHECK_EQ_INT(pc_c4_winner(&g, line), 2);
    check_line(line, at(5, 1), at(5, 2), at(5, 3), at(5, 4));
    /* rising diagonal (bottom left to top right) for 1: (5,0) (4,1) (3,2) (2,3) */
    pc_c4_init(&g);
    const int d1[] = { 0, 1, 1, 2, 2, 3, 2, 3, 3, 6, 3 };
    for (int i = 0; i < 11; i++) CHECK(pc_c4_drop(&g, d1[i]) >= 0);
    CHECK_EQ_INT(pc_c4_winner(&g, line), 1);
    check_line(line, at(5, 0), at(4, 1), at(3, 2), at(2, 3));
    /* falling diagonal: mirror of the above */
    pc_c4_init(&g);
    const int d2[] = { 6, 5, 5, 4, 4, 3, 4, 3, 3, 0, 3 };
    for (int i = 0; i < 11; i++) CHECK(pc_c4_drop(&g, d2[i]) >= 0);
    CHECK_EQ_INT(pc_c4_winner(&g, line), 1);
    check_line(line, at(2, 3), at(3, 4), at(4, 5), at(5, 6));
}

static void test_draw(void)
{
    pc_c4_t g;
    memset(&g, 0, sizeof g);
    static const char P[6][8] = { "1122112", "2211221", "1122112", "2211221", "1122112", "2211221" };   /* pairs, shifted every row: no four anywhere */
    for (int r = 0; r < PC_C4_H; r++) for (int c = 0; c < PC_C4_W; c++) put(&g, r, c, P[r][c] - '0');
    g.turn = 1;
    CHECK_EQ_INT(g.moves, 42);
    CHECK_EQ_INT(pc_c4_winner(&g, NULL), 0);
    CHECK(pc_c4_over(&g));
    CHECK_EQ_INT(pc_c4_drop(&g, 3), -1);
}

/* a fake clock: every read is one microsecond, so a budget of N microseconds is N reads, PC_AI_CLOCK_EVERY nodes apart */
static int64_t s_clock;
static int64_t fake_now(void *user) { (void)user; return s_clock++; }
#define BUDGET_US 20
static const pc_ai_budget_t SLICED = { .now_us = fake_now, .budget_us = BUDGET_US };

static int ai_pick_b(const pc_c4_t *g, uint32_t seed, int depth, const pc_ai_budget_t *b, int *max_nodes, int *slices)
{
    pc_c4_ai_t a;
    pc_c4_ai_begin(&a, g, seed, depth);
    int n = 0;
    while (!pc_c4_ai_step(&a, b)) CHECK(++n < 1000000);
    if (max_nodes && a.nodes_max > *max_nodes) *max_nodes = a.nodes_max;
    if (slices && a.slices > *slices) *slices = a.slices;
    return pc_c4_ai_move(&a);
}
static int ai_pick(const pc_c4_t *g, uint32_t seed, int depth, int *max_nodes, int *steps) { return ai_pick_b(g, seed, depth, &SLICED, max_nodes, steps); }

static void test_ai_positions(void)
{
    pc_c4_t g;
    /* empty board: the centre */
    pc_c4_init(&g);
    CHECK_EQ_INT(ai_pick(&g, 1, 0, NULL, NULL), 3);
    /* takes a win: player 1 has three in the bottom row 0 1 2, column 3 wins; player 2 has stones above */
    pc_c4_init(&g);
    const int w[] = { 0, 0, 1, 1, 2, 2 };
    for (int i = 0; i < 6; i++) CHECK(pc_c4_drop(&g, w[i]) >= 0);
    CHECK_EQ_INT(ai_pick(&g, 1, 0, NULL, NULL), 3);
    /* blocks: player 2 (AI) must stop 1's three at 1 2 3 on the bottom row: 0 or 4; here 4 is open and 0 is open: both lose to 0/4 ... use a column threat */
    pc_c4_init(&g);
    const int b[] = { 4, 0, 4, 1, 4 };            /* player 1 has three stacked in column 4; 2 to move must cap it */
    for (int i = 0; i < 5; i++) CHECK(pc_c4_drop(&g, b[i]) >= 0);
    CHECK_EQ_INT(ai_pick(&g, 1, 0, NULL, NULL), 4);
    /* M4: the opponent (1) threatens to win on the bottom row at column 0 (1 at columns 1 2 3, column 4 taken): the computer (2) must block there */
    memset(&g, 0, sizeof g);
    put(&g, 5, 1, 1); put(&g, 5, 2, 1); put(&g, 5, 3, 1); put(&g, 4, 3, 1);
    put(&g, 5, 4, 2); put(&g, 5, 5, 2); put(&g, 4, 4, 2);
    g.turn = 2;
    CHECK_EQ_INT(pc_c4_winner(&g, NULL), 0);
    for (uint32_t seed = 1; seed <= 6; seed++) CHECK_EQ_INT(ai_pick(&g, seed, 0, NULL, NULL), 0);
    /* M4: 1 has three on row 3 at columns 0 1 2 and column 3 holds one stone: a disc of the computer's on column 3 would let 1 win on top of it, so it never drops there */
    memset(&g, 0, sizeof g);
    put(&g, 5, 0, 2); put(&g, 5, 1, 1); put(&g, 5, 2, 2); put(&g, 5, 3, 2); put(&g, 5, 4, 2);
    put(&g, 4, 0, 1); put(&g, 4, 1, 2); put(&g, 4, 2, 1);
    put(&g, 3, 0, 1); put(&g, 3, 1, 1); put(&g, 3, 2, 1);
    g.turn = 2;
    CHECK_EQ_INT(pc_c4_winner(&g, NULL), 0);
    CHECK_EQ_INT(g.height[3], 1);
    for (uint32_t seed = 1; seed <= 6; seed++) CHECK(ai_pick(&g, seed, 0, NULL, NULL) != 3);
    /* a finished game: no move */
    pc_c4_init(&g);
    const int v[] = { 2, 5, 2, 5, 2, 5, 2 };
    for (int i = 0; i < 7; i++) CHECK(pc_c4_drop(&g, v[i]) >= 0);
    CHECK_EQ_INT(ai_pick(&g, 1, 0, NULL, NULL), -1);
}

static uint32_t s_lcg = 12345;
static int rnd(int n) { s_lcg = s_lcg * 1664525u + 1013904223u; return (int)((s_lcg >> 16) % (uint32_t)n); }

static void test_ai_beats_random_and_is_sliced(void)
{
    int maxn = 0, maxsteps = 0, wins = 0, games = 0;
    for (int ai_side = 1; ai_side <= 2; ai_side++)
        for (int k = 0; k < 4; k++) {
            pc_c4_t g;
            pc_c4_init(&g);
            while (!pc_c4_over(&g)) {
                if (g.turn == ai_side) {
                    int m = ai_pick(&g, (uint32_t)(k + 1), 0, &maxn, &maxsteps);
                    CHECK(m >= 0);
                    CHECK(pc_c4_drop(&g, m) >= 0);
                } else {
                    int c;
                    do c = rnd(PC_C4_W); while (g.height[c] >= PC_C4_H);
                    CHECK(pc_c4_drop(&g, c) >= 0);
                }
            }
            games++;
            if (pc_c4_winner(&g, NULL) == ai_side) wins++;
        }
    CHECK(wins >= games - 1);                     /* a random player almost never survives six plies of lookahead */
    CHECK(maxn <= PC_AI_CLOCK_EVERY * (BUDGET_US + 2));   /* every slice stopped at its budget (the clock is read every 8 nodes) */
    CHECK(maxsteps > 1);                          /* the search really is cut into slices */
    printf("c4 games=%d ai_wins=%d max_nodes_per_slice=%d max_slices=%d\n", games, wins, maxn, maxsteps);
}

static void test_ai_depth_and_determinism(void)
{
    pc_c4_t g;
    pc_c4_init(&g);
    const int o[] = { 3, 2, 4 };
    for (int i = 0; i < 3; i++) CHECK(pc_c4_drop(&g, o[i]) >= 0);
    CHECK_EQ_INT(ai_pick(&g, 9, 4, NULL, NULL), ai_pick(&g, 9, 4, NULL, NULL));
    pc_c4_ai_t a4, a6;
    pc_c4_ai_begin(&a4, &g, 1, 4);
    pc_c4_ai_begin(&a6, &g, 1, 6);
    while (!pc_c4_ai_step(&a4, NULL)) {}
    while (!pc_c4_ai_step(&a6, NULL)) {}
    CHECK(a6.nodes_total > a4.nodes_total);       /* deeper means more nodes */
}

/* the reference: plain minimax without pruning on the public rules and the same leaf value; the search (sliced or not) must choose a move of the best value */
#define REF_WIN 100000
static int ref_nm(const pc_c4_t *g, int depth, int ply)
{
    if (g->moves >= PC_C4_N) return 0;
    if (depth <= 0) return pc_c4_eval(g, g->turn);
    int best = -1000000;
    for (int c = 0; c < PC_C4_W; c++) {
        if (g->height[c] >= PC_C4_H) continue;
        pc_c4_t n = *g;
        int who = n.turn;
        pc_c4_drop(&n, c);
        int v = pc_c4_winner(&n, NULL) == who ? REF_WIN - ply : -ref_nm(&n, depth - 1, ply + 1);
        if (v > best) best = v;
    }
    return best;
}
static int ref_root(const pc_c4_t *g, int depth, int c)
{
    pc_c4_t n = *g;
    int who = n.turn;
    pc_c4_drop(&n, c);
    if (pc_c4_winner(&n, NULL) == who) return REF_WIN;
    if (n.moves >= PC_C4_N) return 0;
    return -ref_nm(&n, depth - 1, 1);
}
static void test_search_equals_reference_and_unsliced(void)
{
    int slices = 0, positions = 0;
    for (int k = 0; k < 8; k++) {                  /* positions from deterministic playouts of 4..11 moves */
        pc_c4_t g;
        pc_c4_init(&g);
        for (int i = 0; i < 4 + k; i++) { int c; do c = rnd(PC_C4_W); while (g.height[c] >= PC_C4_H); pc_c4_drop(&g, c); if (pc_c4_over(&g)) break; }
        if (pc_c4_over(&g)) continue;
        int best = -1000000;
        for (int c = 0; c < PC_C4_W; c++) if (g.height[c] < PC_C4_H) { int v = ref_root(&g, 4, c); if (v > best) best = v; }
        int whole = ai_pick_b(&g, 3, 4, NULL, NULL, NULL);
        CHECK_EQ_INT(ref_root(&g, 4, whole), best);               /* the pruned search finds a move of the best value */
        CHECK_EQ_INT(ai_pick_b(&g, 3, 4, &SLICED, NULL, &slices), whole);    /* slicing changes nothing */
        pc_ai_budget_t cap = { .node_cap = 37 };
        int m = 0;
        CHECK_EQ_INT(ai_pick_b(&g, 3, 4, &cap, &m, NULL), whole);
        CHECK(m <= 37);                                            /* the second limit */
        positions++;
    }
    CHECK(positions >= 6);
    CHECK(slices > 1);
}

/* ---- G0b rule variants ---- */
static void test_wrap_wins(void)
{
    pc_c4_t g;
    int line[4];
    /* across the edge on the bottom row: the first player at columns 5 6 0 1, the second stacks column 3 */
    const int h[] = { 5, 3, 6, 3, 0, 3, 1 };
    pc_c4_init(&g);
    for (int i = 0; i < 7; i++) CHECK(pc_c4_drop(&g, h[i]) >= 0);
    CHECK_EQ_INT(pc_c4_winner(&g, NULL), 0);                 /* classic: no line across the edge */
    pc_c4_init_rule(&g, PC_C4_WRAP);
    for (int i = 0; i < 6; i++) CHECK(pc_c4_drop(&g, h[i]) >= 0);
    CHECK_EQ_INT(pc_c4_winner(&g, NULL), 0);
    CHECK(pc_c4_drop(&g, 1) >= 0);
    CHECK_EQ_INT(pc_c4_winner(&g, line), 1);
    check_line(line, at(5, 5), at(5, 6), at(5, 0), at(5, 1));
    CHECK(pc_c4_over(&g));
    /* a rising diagonal through the edge: (5,5) (4,6) (3,0) (2,1) */
    memset(&g, 0, sizeof g);
    g.rule = PC_C4_WRAP;
    put(&g, 5, 5, 1); put(&g, 5, 6, 2); put(&g, 4, 6, 1);
    put(&g, 5, 0, 2); put(&g, 4, 0, 2); put(&g, 3, 0, 1);
    put(&g, 5, 1, 2); put(&g, 4, 1, 1); put(&g, 3, 1, 2);
    CHECK_EQ_INT(pc_c4_winner(&g, NULL), 0);
    put(&g, 2, 1, 1);
    CHECK_EQ_INT(pc_c4_winner(&g, line), 1);
    check_line(line, at(5, 5), at(4, 6), at(3, 0), at(2, 1));
    g.rule = PC_C4_CLASSIC;
    CHECK_EQ_INT(pc_c4_winner(&g, NULL), 0);                 /* the same discs: no line without the wrap */
    /* vertical lines do not wrap top to bottom */
    memset(&g, 0, sizeof g);
    g.rule = PC_C4_WRAP;
    put(&g, 5, 2, 1); put(&g, 4, 2, 1); put(&g, 3, 2, 2); put(&g, 2, 2, 2); put(&g, 1, 2, 1); put(&g, 0, 2, 1);
    CHECK_EQ_INT(pc_c4_winner(&g, NULL), 0);
    /* a full row of seven is one line of four (distinct cells) */
    memset(&g, 0, sizeof g);
    g.rule = PC_C4_WRAP;
    for (int c = 0; c < PC_C4_W; c++) put(&g, 5, c, 2);
    CHECK_EQ_INT(pc_c4_winner(&g, line), 2);
    CHECK(line[0] != line[1] && line[0] != line[2] && line[0] != line[3] && line[1] != line[2] && line[1] != line[3] && line[2] != line[3]);
}

static void test_pop_legality(void)
{
    pc_c4_t g;
    pc_c4_init(&g);
    CHECK(pc_c4_drop(&g, 2) >= 0);                           /* 1 at the bottom of column 2 */
    CHECK(!pc_c4_can_pop(&g, 2));                            /* classic: no pops */
    CHECK_EQ_INT(pc_c4_pop(&g, 2), -1);
    pc_c4_init_rule(&g, PC_C4_POPOUT);
    CHECK(!pc_c4_can_pop(&g, 2));                            /* empty column */
    CHECK(pc_c4_drop(&g, 2) >= 0);                           /* 1 bottom */
    CHECK(pc_c4_drop(&g, 2) >= 0);                           /* 2 on it */
    CHECK(pc_c4_can_pop(&g, 2));                             /* 1 to move, its own disc at the bottom */
    CHECK(!pc_c4_legal(&g, PC_C4_POP_MOVE(3)));
    CHECK(pc_c4_legal(&g, PC_C4_POP_MOVE(2)));
    CHECK(!pc_c4_legal(&g, PC_C4_POP_MOVE(7)) && !pc_c4_legal(&g, -1) && !pc_c4_legal(&g, 14));
    CHECK_EQ_INT(pc_c4_pop(&g, 2), at(5, 2));
    CHECK_EQ_INT(g.cell[at(5, 2)], 2);                       /* the column fell one cell */
    CHECK_EQ_INT(g.cell[at(4, 2)], 0);
    CHECK_EQ_INT(g.height[2], 1);
    CHECK_EQ_INT(g.turn, 2);
    CHECK_EQ_INT(g.moves, 3);
    CHECK(!pc_c4_can_pop(&g, 2) || g.cell[at(5, 2)] == g.turn);
    CHECK(pc_c4_can_pop(&g, 2));                             /* now 2's own bottom disc */
    CHECK_EQ_INT(pc_c4_play(&g, PC_C4_POP_MOVE(2)), at(5, 2));
    CHECK_EQ_INT(g.height[2], 0);
    CHECK_EQ_INT(pc_c4_play(&g, 4), at(5, 4));               /* a drop through the same call */
    /* a full board is not the end when a pop is possible; the game is drawn only when the side to move has no move */
    memset(&g, 0, sizeof g);
    g.rule = PC_C4_POPOUT;
    static const char P[6][8] = { "1122112", "2211221", "1122112", "2211221", "1122112", "2211221" };
    for (int r = 0; r < PC_C4_H; r++) for (int c = 0; c < PC_C4_W; c++) put(&g, r, c, P[r][c] - '0');
    g.turn = 1;
    CHECK_EQ_INT(pc_c4_winner(&g, NULL), 0);
    CHECK(!pc_c4_over(&g));                                  /* 1 can pop columns 2 3 6 */
    CHECK(pc_c4_can_pop(&g, 2) && !pc_c4_can_pop(&g, 0));
    /* (a full board with no four always has discs of both sides in its bottom row, so the side to move can always pop: such a game ends by the move cap) */
    /* the move cap: a pop game that goes on and on is a draw */
    pc_c4_init_rule(&g, PC_C4_POPOUT);
    g.moves = PC_C4_MOVE_CAP;
    CHECK(pc_c4_over(&g));
    CHECK_EQ_INT(pc_c4_drop(&g, 0), -1);
}

/* a pop that makes four for both sides at once: the side that popped wins (the rule, stated in the 說明); four for the other side only: the other side wins */
static void test_pop_wins(void)
{
    pc_c4_t g;
    int line[4];
    /* column 3 bottom-up 1 2 1 2; rows 4 and 3 hold three of 1 / of 2 at columns 0..2; 1 pops column 3: row 4 = 1 1 1 1 and row 3 = 2 2 2 2 */
    memset(&g, 0, sizeof g);
    g.rule = PC_C4_POPOUT;
    put(&g, 5, 0, 2); put(&g, 5, 1, 1); put(&g, 5, 2, 2);
    put(&g, 4, 0, 1); put(&g, 4, 1, 1); put(&g, 4, 2, 1);
    put(&g, 3, 0, 2); put(&g, 3, 1, 2); put(&g, 3, 2, 2);
    put(&g, 5, 3, 1); put(&g, 4, 3, 2); put(&g, 3, 3, 1); put(&g, 2, 3, 2);
    g.turn = 1;
    CHECK_EQ_INT(pc_c4_winner(&g, NULL), 0);
    pc_c4_t g2 = g;
    CHECK(pc_c4_pop(&g, 3) >= 0);
    CHECK_EQ_INT(pc_c4_winner(&g, line), 1);                 /* both have four: the popper */
    check_line(line, at(4, 0), at(4, 1), at(4, 2), at(4, 3));
    CHECK(pc_c4_over(&g));
    /* the same with 1's row broken: only 2 has four after 1's pop, 2 wins */
    g = g2;
    g.cell[at(4, 2)] = 2;
    CHECK_EQ_INT(pc_c4_winner(&g, NULL), 0);
    CHECK(pc_c4_pop(&g, 3) >= 0);
    CHECK_EQ_INT(pc_c4_winner(&g, line), 2);
    check_line(line, at(3, 0), at(3, 1), at(3, 2), at(3, 3));
    /* the computer as 1 here takes the pop win; as 2 to move (its own disc not at the bottom) it must not hand it over */
    g = g2;
    for (uint32_t seed = 1; seed <= 4; seed++) CHECK_EQ_INT(ai_pick(&g, seed, 0, NULL, NULL), PC_C4_POP_MOVE(3));
}

static void ai_legal_playouts(int rule, int *wins_out, int *maxn_out, int *slices_out)
{
    int wins = 0, maxn = 0, slices = 0;
    for (int ai_side = 1; ai_side <= 2; ai_side++)
        for (int k = 0; k < 3; k++) {
            pc_c4_t g;
            pc_c4_init_rule(&g, rule);
            while (!pc_c4_over(&g)) {
                if (g.turn == ai_side) {
                    int m = ai_pick(&g, (uint32_t)(k + 7), 0, &maxn, &slices);
                    CHECK(pc_c4_legal(&g, m));
                    CHECK(pc_c4_play(&g, m) >= 0);
                } else {
                    int m;
                    do m = rnd(rule == PC_C4_POPOUT ? 2 * PC_C4_W : PC_C4_W); while (!pc_c4_legal(&g, m));
                    CHECK(pc_c4_play(&g, m) >= 0);
                }
            }
            if (pc_c4_winner(&g, NULL) == ai_side) wins++;
        }
    *wins_out = wins; *maxn_out = maxn; *slices_out = slices;
}

static void test_ai_variants(void)
{
    for (int rule = 0; rule < PC_C4_RULES; rule++) {
        int wins, maxn, slices;
        ai_legal_playouts(rule, &wins, &maxn, &slices);
        printf("c4 rule=%d ai_wins=%d/6 max_nodes_per_slice=%d max_slices=%d\n", rule, wins, maxn, slices);
        CHECK(wins >= 5);
        CHECK(maxn <= PC_AI_CLOCK_EVERY * (BUDGET_US + 2));
    }
    /* wrap: the computer takes a win across the edge (1 at columns 5 6 0 on the bottom row: column 1) */
    pc_c4_t g;
    pc_c4_init_rule(&g, PC_C4_WRAP);
    const int h[] = { 5, 4, 6, 3, 0, 3 };                    /* the second player blocks column 4 (4 5 6 0 would win too) */
    for (int i = 0; i < 6; i++) CHECK(pc_c4_drop(&g, h[i]) >= 0);
    for (uint32_t seed = 1; seed <= 4; seed++) CHECK_EQ_INT(ai_pick(&g, seed, 0, NULL, NULL), 1);
    /* sliced = whole, per variant */
    for (int rule = 0; rule < PC_C4_RULES; rule++) {
        pc_c4_init_rule(&g, rule);
        const int o[] = { 3, 2, 4, 3 };
        for (int i = 0; i < 4; i++) CHECK(pc_c4_drop(&g, o[i]) >= 0);
        int s = 0;
        CHECK_EQ_INT(ai_pick_b(&g, 5, 0, &SLICED, NULL, &s), ai_pick_b(&g, 5, 0, NULL, NULL, NULL));
        CHECK(s > 1);
    }
}

int main(void)
{
    RUN(test_gravity_and_columns);
    RUN(test_wins);
    RUN(test_draw);
    RUN(test_ai_positions);
    RUN(test_ai_beats_random_and_is_sliced);
    RUN(test_ai_depth_and_determinism);
    RUN(test_search_equals_reference_and_unsliced);
    RUN(test_wrap_wins);
    RUN(test_pop_legality);
    RUN(test_pop_wins);
    RUN(test_ai_variants);
    return 0;
}
