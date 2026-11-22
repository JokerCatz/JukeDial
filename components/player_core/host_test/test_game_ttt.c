// SPDX-License-Identifier: LGPL-2.1-or-later
/* 井字棋 rules and the perfect-play computer (games hub G0). */
#include "pc/pc_game_ttt.h"
#include "pc_test.h"

static void play_all(pc_ttt_t *g, const int *mv, int n) { for (int i = 0; i < n; i++) CHECK(pc_ttt_play(g, mv[i])); }

static void test_win_lines(void)
{
    static const int L[8][3] = { {0,1,2}, {3,4,5}, {6,7,8}, {0,3,6}, {1,4,7}, {2,5,8}, {0,4,8}, {2,4,6} };
    for (int who = 1; who <= 2; who++)
        for (int l = 0; l < 8; l++) {
            pc_ttt_t g;
            pc_ttt_init(&g);
            for (int k = 0; k < 3; k++) g.cell[L[l][k]] = (uint8_t)who;
            int line[3] = { -1, -1, -1 };
            CHECK_EQ_INT(pc_ttt_winner(&g, line), who);
            for (int k = 0; k < 3; k++) CHECK_EQ_INT(line[k], L[l][k]);
            CHECK(pc_ttt_over(&g));
        }
    pc_ttt_t g;
    pc_ttt_init(&g);
    CHECK_EQ_INT(pc_ttt_winner(&g, NULL), 0);
    CHECK(!pc_ttt_over(&g));
}

static void test_play_rules(void)
{
    pc_ttt_t g;
    pc_ttt_init(&g);
    CHECK_EQ_INT(g.turn, 1);
    CHECK(pc_ttt_play(&g, 4));
    CHECK_EQ_INT(g.cell[4], 1);
    CHECK_EQ_INT(g.turn, 2);
    CHECK(!pc_ttt_play(&g, 4));                 /* taken */
    CHECK(!pc_ttt_play(&g, -1));
    CHECK(!pc_ttt_play(&g, 9));
    CHECK_EQ_INT(g.turn, 2);                    /* a refused move changes nothing */
    CHECK_EQ_INT(g.moves, 1);
    const int won[] = { 0, 3, 1, 4, 2 };         /* X takes the top row */
    pc_ttt_init(&g);
    play_all(&g, won, 5);
    CHECK_EQ_INT(pc_ttt_winner(&g, NULL), 1);
    CHECK(!pc_ttt_play(&g, 8));                 /* over: no more moves */
    const int draw[] = { 0, 1, 2, 4, 3, 5, 7, 6, 8 };   /* X O X / X O O / O X X */
    pc_ttt_init(&g);
    play_all(&g, draw, 9);
    CHECK_EQ_INT(pc_ttt_winner(&g, NULL), 0);
    CHECK(pc_ttt_over(&g));
}

/* a fake clock: every read is one microsecond, so a budget of N microseconds is N reads, PC_AI_CLOCK_EVERY nodes apart */
static int64_t s_clock;
static int64_t fake_now(void *user) { (void)user; return s_clock++; }
#define BUDGET_US 20
static const pc_ai_budget_t SLICED = { .now_us = fake_now, .budget_us = BUDGET_US };

static int ai_pick_b(const pc_ttt_t *g, uint32_t seed, const pc_ai_budget_t *b, int *max_nodes, int *slices)
{
    pc_ttt_ai_t a;
    pc_ttt_ai_begin(&a, g, seed);
    int n = 0;
    while (!pc_ttt_ai_step(&a, b)) CHECK(++n < 100000);
    if (max_nodes && a.nodes_max > *max_nodes) *max_nodes = a.nodes_max;
    if (slices && a.slices > *slices) *slices = a.slices;
    return pc_ttt_ai_move(&a);
}
static int ai_pick(const pc_ttt_t *g, uint32_t seed, int *max_nodes) { return ai_pick_b(g, seed, &SLICED, max_nodes, NULL); }

static void test_ai_takes_win_and_blocks(void)
{
    pc_ttt_t g;
    pc_ttt_init(&g);
    const int w[] = { 0, 3, 1, 4 };               /* X: 0 1, O: 3 4, X to move: 2 wins */
    play_all(&g, w, 4);
    CHECK_EQ_INT(ai_pick(&g, 1, NULL), 2);
    pc_ttt_init(&g);
    const int b[] = { 0, 4, 1 };                  /* X: 0 1, O to move must block at 2 */
    play_all(&g, b, 3);
    CHECK_EQ_INT(ai_pick(&g, 1, NULL), 2);
    pc_ttt_init(&g);
    const int c[] = { 4, 0 };                     /* X centre, O corner: X must not lose; any move is legal and chosen */
    play_all(&g, c, 2);
    int m = ai_pick(&g, 7, NULL);
    CHECK(m >= 0 && m < 9 && g.cell[m] == 0);
    pc_ttt_init(&g);
    for (int i = 0; i < 9; i++) g.cell[i] = (uint8_t)(1 + (i & 1));   /* a finished board: no move */
    g.moves = 9;
    CHECK_EQ_INT(ai_pick(&g, 1, NULL), -1);
}

/* a slice stops at its budget (the clock is read every PC_AI_CLOCK_EVERY nodes), and the choice does not depend on where the slices end */
static void test_slices_are_bounded_and_equal(void)
{
    int maxn = 0, slices = 0, positions = 0;
    for (uint32_t seed = 1; seed <= 3; seed++) {
        pc_ttt_t g;
        pc_ttt_init(&g);
        const int lines[][4] = { {4, -1}, {0, -1}, {1, -1}, {4, 0, -1}, {0, 8, -1}, {0, 1, 4, -1} };
        for (size_t l = 0; l < sizeof lines / sizeof lines[0]; l++) {
            pc_ttt_init(&g);
            for (int k = 0; lines[l][k] >= 0; k++) CHECK(pc_ttt_play(&g, lines[l][k]));
            int whole = ai_pick_b(&g, seed, NULL, NULL, NULL);
            CHECK_EQ_INT(ai_pick_b(&g, seed, &SLICED, &maxn, &slices), whole);
            pc_ai_budget_t cap = { .node_cap = 7 };
            int m2 = 0;
            CHECK_EQ_INT(ai_pick_b(&g, seed, &cap, &m2, NULL), whole);
            CHECK(m2 <= 7);                                  /* the second limit */
            positions++;
        }
    }
    CHECK(positions == 18);
    CHECK(maxn <= PC_AI_CLOCK_EVERY * (BUDGET_US + 2));      /* a slice stopped by its budget, not by running out of work */
    CHECK(slices > 1);                                       /* and the work really was cut into several slices */
    pc_ttt_t g;                                              /* the opening is fixed: no search at all */
    pc_ttt_init(&g);
    pc_ttt_ai_t a;
    pc_ttt_ai_begin(&a, &g, 3);
    CHECK(pc_ttt_ai_step(&a, &SLICED));
    CHECK_EQ_INT(a.nodes_total, 0);
    CHECK(a.move == 0 || a.move == 2 || a.move == 4 || a.move == 6 || a.move == 8);
}

/* the computer never loses: every sequence of the other side's moves, the computer answering by the stepper */
static long s_games, s_draws, s_ai_wins;
static int s_maxn;
static void explore(pc_ttt_t g, int ai_side, uint32_t seed)
{
    if (pc_ttt_over(&g)) {
        int w = pc_ttt_winner(&g, NULL);
        s_games++;
        CHECK(w != 3 - ai_side);                  /* the human side never wins */
        if (w == ai_side) s_ai_wins++; else s_draws++;
        return;
    }
    if (g.turn == ai_side) {
        int m = ai_pick(&g, seed + g.moves, &s_maxn);
        CHECK(m >= 0);
        CHECK(pc_ttt_play(&g, m));
        explore(g, ai_side, seed);
        return;
    }
    for (int i = 0; i < 9; i++) {
        if (g.cell[i]) continue;
        pc_ttt_t n = g;
        CHECK(pc_ttt_play(&n, i));
        explore(n, ai_side, seed);
    }
}

static void test_ai_never_loses(void)
{
    for (int side = 1; side <= 2; side++)
        for (uint32_t seed = 1; seed <= 3; seed++) {
            pc_ttt_t g;
            pc_ttt_init(&g);
            explore(g, side, seed);
        }
    CHECK(s_games > 1000);
    CHECK(s_draws > 0 && s_ai_wins > 0);
    CHECK(s_maxn <= PC_AI_CLOCK_EVERY * (BUDGET_US + 2));   /* every slice stopped at its budget */
    printf("ttt games=%ld draws=%ld ai_wins=%ld max_nodes_per_step=%d\n", s_games, s_draws, s_ai_wins, s_maxn);
}

static void test_ai_varies_by_seed(void)
{
    pc_ttt_t g;
    pc_ttt_init(&g);
    int seen[9] = { 0 }, kinds = 0;
    for (uint32_t s = 1; s <= 40; s++) { int m = ai_pick(&g, s, NULL); CHECK(m >= 0 && m < 9); if (!seen[m]++) kinds++; }
    CHECK(kinds >= 2);                            /* the opening differs between games */
    CHECK_EQ_INT(ai_pick(&g, 5, NULL), ai_pick(&g, 5, NULL));   /* the same seed, the same move */
}

/* ---- G0b rule variants ---- */
/* 消失版: each side keeps three marks; placing a fourth first removes that side's oldest, then the line is checked (the removed mark never counts) */
static void test_vanish_order(void)
{
    pc_ttt_t g;
    pc_ttt_init_rule(&g, PC_TTT_VANISH);
    CHECK_EQ_INT(g.rule, PC_TTT_VANISH);
    CHECK_EQ_INT(pc_ttt_fading(&g, 1), -1);
    const int m[] = { 0, 8, 1, 7, 5, 3 };                    /* X: 0 1 5, O: 8 7 3 */
    play_all(&g, m, 6);
    CHECK_EQ_INT(pc_ttt_fading(&g, 1), 0);                   /* X's oldest goes at X's next placement: it is shown dim now */
    CHECK_EQ_INT(pc_ttt_fading(&g, 2), 8);
    CHECK_EQ_INT(g.gone, -1);
    CHECK(!pc_ttt_play(&g, 0));                              /* still taken until it vanishes */
    CHECK(pc_ttt_play(&g, 2));                               /* X: 0 goes first, then 2: 1 2 5, no line (classic would have 0 1 2) */
    CHECK_EQ_INT(g.gone, 0);
    CHECK_EQ_INT(g.cell[0], 0);
    CHECK_EQ_INT(g.cell[2], 1);
    CHECK_EQ_INT(pc_ttt_winner(&g, NULL), 0);
    CHECK(!pc_ttt_over(&g));
    CHECK_EQ_INT(pc_ttt_fading(&g, 1), 1);                   /* the next oldest */
    CHECK(pc_ttt_play(&g, 4));                               /* O: 8 goes, O: 7 3 4 */
    CHECK_EQ_INT(g.gone, 8);
    CHECK_EQ_INT(g.cell[8], 0);
    int n1 = 0, n2 = 0;
    for (int i = 0; i < 9; i++) { n1 += g.cell[i] == 1; n2 += g.cell[i] == 2; }
    CHECK_EQ_INT(n1, 3);
    CHECK_EQ_INT(n2, 3);
    /* a win where the removed mark is not in the line: X 3 1 2 (3 oldest), X places 0: 3 goes, 0 1 2 is a line */
    pc_ttt_init_rule(&g, PC_TTT_VANISH);
    const int w[] = { 3, 4, 1, 8, 2, 6 };                    /* X: 3 1 2 (no line), O: 4 8 6 */
    play_all(&g, w, 6);
    CHECK_EQ_INT(pc_ttt_winner(&g, NULL), 0);
    CHECK(pc_ttt_play(&g, 0));
    CHECK_EQ_INT(g.gone, 3);
    int line[3];
    CHECK_EQ_INT(pc_ttt_winner(&g, line), 1);
    CHECK_EQ_INT(line[0], 0); CHECK_EQ_INT(line[1], 1); CHECK_EQ_INT(line[2], 2);
    CHECK(pc_ttt_over(&g));
}

/* 消失版 has no draws: at most six marks, so a game goes on past nine moves until someone has a line */
static void test_vanish_no_draw(void)
{
    uint32_t r = 99;
    int longest = 0;
    for (int k = 0; k < 200; k++) {
        pc_ttt_t g;
        pc_ttt_init_rule(&g, PC_TTT_VANISH);
        while (!pc_ttt_over(&g) && g.moves < 400) {
            int marks = 0;
            for (int i = 0; i < 9; i++) marks += g.cell[i] != 0;
            CHECK(marks <= 6);
            int c;
            do { r = r * 1664525u + 1013904223u; c = (int)((r >> 16) % 9); } while (g.cell[c]);
            CHECK(pc_ttt_play(&g, c));
        }
        CHECK(pc_ttt_winner(&g, NULL) != 0 || g.moves >= 400);
        if (g.moves > longest) longest = g.moves;
    }
    CHECK(longest > 9);
    /* the move cap: a draw (fix round 1 M6), and the computer has no move */
    pc_ttt_t c;
    pc_ttt_init_rule(&c, PC_TTT_VANISH);
    CHECK(pc_ttt_play(&c, 4));
    c.moves = PC_TTT_MOVE_CAP;
    CHECK(pc_ttt_over(&c) && pc_ttt_winner(&c, NULL) == 0);
    CHECK(!pc_ttt_play(&c, 0));
    CHECK_EQ_INT(ai_pick(&c, 1, NULL), -1);
    /* moves == 0 with a taken cell (a wrapped count, a hand-built position): the opening never picks a taken cell */
    for (uint32_t seed = 1; seed <= 20; seed++) {
        pc_ttt_init_rule(&c, PC_TTT_VANISH);
        c.cell[0] = 1; c.cell[2] = 2; c.cell[4] = 1; c.cell[6] = 2; c.cell[8] = 2;   /* every opening cell taken, no line */
        int m = ai_pick(&c, seed, NULL);
        CHECK(m >= 0 && m < 9 && c.cell[m] == 0);
    }
}

/* 盲棋: the rules are the classic ones (the marks are hidden by the page, not by the rules) */
static void test_blind_rules(void)
{
    pc_ttt_t g;
    pc_ttt_init_rule(&g, PC_TTT_BLIND);
    CHECK(pc_ttt_play(&g, 4));
    CHECK(!pc_ttt_play(&g, 4));                              /* a hidden mark is still there */
    CHECK_EQ_INT(pc_ttt_fading(&g, 1), -1);
    pc_ttt_t d;
    pc_ttt_init_rule(&d, PC_TTT_BLIND);
    const int dd[] = { 0, 1, 2, 4, 3, 5, 7, 6, 8 };
    play_all(&d, dd, 9);
    CHECK(pc_ttt_over(&d) && pc_ttt_winner(&d, NULL) == 0);  /* a draw is possible */
}

static void test_vanish_ai(void)
{
    pc_ttt_t g;
    /* takes the win: X has 6 0 1, placing 2 removes 6 and 0 1 2 is a line */
    pc_ttt_init_rule(&g, PC_TTT_VANISH);
    const int w[] = { 6, 4, 0, 8, 1, 3 };                    /* X: 6 0 1, O: 4 8 3 (O threatens 3 4 5) */
    play_all(&g, w, 6);
    for (uint32_t seed = 1; seed <= 4; seed++) CHECK_EQ_INT(ai_pick(&g, seed, NULL), 2);
    /* O to move: X threatens 2 (its oldest, 7, is not in the line) and 2 also makes O's 2 4 6: the computer takes it */
    pc_ttt_init_rule(&g, PC_TTT_VANISH);
    const int b[] = { 7, 4, 0, 6, 1 };                       /* X: 7 0 1, O: 4 6 */
    play_all(&g, b, 5);
    for (uint32_t seed = 1; seed <= 4; seed++) CHECK_EQ_INT(ai_pick(&g, seed, NULL), 2);
    /* legal every time, against a random player, from both sides; slices bounded; the whole search per move stays small */
    int maxn = 0, slices = 0, worst = 0, ai_wins = 0, games = 0;
    uint32_t r = 7;
    for (int ai_side = 1; ai_side <= 2; ai_side++)
        for (int k = 0; k < 10; k++) {
            pc_ttt_init_rule(&g, PC_TTT_VANISH);
            while (!pc_ttt_over(&g) && g.moves < 120) {
                if (g.turn == ai_side) {
                    pc_ttt_ai_t a;
                    pc_ttt_ai_begin(&a, &g, (uint32_t)(k + 1 + g.moves));
                    while (!pc_ttt_ai_step(&a, &SLICED)) {}
                    if (a.nodes_max > maxn) maxn = a.nodes_max;
                    if (a.slices > slices) slices = a.slices;
                    if (a.nodes_total > worst) worst = a.nodes_total;
                    int m = pc_ttt_ai_move(&a);
                    CHECK(m >= 0 && m < 9 && g.cell[m] == 0);
                    CHECK(pc_ttt_play(&g, m));
                } else {
                    int c;
                    do { r = r * 1664525u + 1013904223u; c = (int)((r >> 16) % 9); } while (g.cell[c]);
                    CHECK(pc_ttt_play(&g, c));
                }
            }
            games++;
            if (pc_ttt_winner(&g, NULL) == ai_side) ai_wins++;
        }
    printf("ttt vanish games=%d ai_wins=%d max_nodes_per_slice=%d max_slices=%d max_nodes_per_move=%d\n", games, ai_wins, maxn, slices, worst);
    CHECK(ai_wins >= games - 2);
    CHECK(maxn <= PC_AI_CLOCK_EVERY * (BUDGET_US + 2));
    CHECK(worst <= PC_TTT_VANISH_NODES);                     /* the depth keeps a move's whole search small (a few ms on the board) */
    /* sliced = whole */
    pc_ttt_init_rule(&g, PC_TTT_VANISH);
    const int o[] = { 4, 0, 8 };
    play_all(&g, o, 3);
    CHECK_EQ_INT(ai_pick_b(&g, 3, &SLICED, NULL, NULL), ai_pick_b(&g, 3, NULL, NULL, NULL));
}

int main(void)
{
    RUN(test_win_lines);
    RUN(test_play_rules);
    RUN(test_ai_takes_win_and_blocks);
    RUN(test_slices_are_bounded_and_equal);
    RUN(test_ai_never_loses);
    RUN(test_ai_varies_by_seed);
    RUN(test_vanish_order);
    RUN(test_vanish_no_draw);
    RUN(test_blind_rules);
    RUN(test_vanish_ai);
    return 0;
}
