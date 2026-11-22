// SPDX-License-Identifier: LGPL-2.1-or-later
/* 15 拼圖 / 15 puzzle (ideas G4): the shuffle is solvable (parity checked against an exhaustive search of the 3x3 states), moves, solved, par, determinism. */
#include <stdlib.h>
#include <string.h>
#include "pc/pc_game_puzzle15.h"
#include "pc_test.h"

/* the rank of a 3x3 permutation (Lehmer code): the index into the reachable set */
static int rank9(const uint8_t *t)
{
    int r = 0;
    for (int i = 0; i < 9; i++) { int c = 0; for (int j = i + 1; j < 9; j++) if (t[j] < t[i]) c++; int f = 1; for (int k = 2; k <= 8 - i; k++) f *= k; r += c * f; }
    return r;
}
static uint8_t *reach;                              /* 362880 flags: the states reachable from solved by sliding */
static void build_reach(void)
{
    static const int D[4] = { -3, 3, -1, 1 };
    reach = calloc(362880, 1);
    typedef struct { uint8_t t[9]; } st_t;
    st_t *q = malloc(sizeof(st_t) * 181440 * 2);
    int h = 0, n = 0;
    pc_p15_t g;
    pc_p15_solved_state(&g, PC_P15_R3);
    memcpy(q[n++].t, g.t, 9);
    reach[rank9(g.t)] = 1;
    while (h < n) {
        st_t s = q[h++];
        int b = 0; while (s.t[b]) b++;
        for (int k = 0; k < 4; k++) {
            int to = b + D[k];
            if (to < 0 || to > 8 || (k >= 2 && to / 3 != b / 3)) continue;
            st_t x = s; x.t[b] = x.t[to]; x.t[to] = 0;
            int r = rank9(x.t);
            if (!reach[r]) { reach[r] = 1; q[n++] = x; }
        }
    }
    CHECK_EQ_INT(n, 181440);                        /* half of 9! */
    free(q);
}

static void test_solved(void)
{
    pc_p15_t g;
    for (int r = 0; r < PC_P15_RULES; r++) {
        pc_p15_solved_state(&g, r);
        int n = pc_p15_side(r);
        CHECK(pc_p15_solved(&g));
        CHECK_EQ_INT(g.blank, n * n - 1);
        CHECK_EQ_INT(g.t[0], 1);
        CHECK_EQ_INT(g.t[n * n - 2], n * n - 1);
        CHECK(pc_p15_solvable(g.t, n));
        CHECK_EQ_INT(pc_p15_manhattan(&g), 0);
    }
    CHECK_EQ_INT(pc_p15_side(PC_P15_R4), 4);
    CHECK_EQ_INT(pc_p15_side(PC_P15_R3), 3);
    CHECK_EQ_INT(pc_p15_side(PC_P15_R5), 5);
    CHECK_EQ_INT(pc_p15_side(99), 4);               /* an unknown rule is the classic */
}

static void test_parity(void)
{
    pc_p15_t g;
    for (int r = 0; r < PC_P15_RULES; r++) {        /* swapping two tiles of a solved board makes an unsolvable one; the blank in another row too */
        int n = pc_p15_side(r);
        pc_p15_solved_state(&g, r);
        uint8_t x = g.t[0]; g.t[0] = g.t[1]; g.t[1] = x;
        CHECK(!pc_p15_solvable(g.t, n));
        g.t[1] = g.t[0]; g.t[0] = x;
        CHECK(pc_p15_solvable(g.t, n));
    }
    /* the well-known 15 puzzle: 14 and 15 swapped is the unsolvable one */
    pc_p15_solved_state(&g, PC_P15_R4);
    g.t[13] = 15; g.t[14] = 14;
    CHECK(!pc_p15_solvable(g.t, 4));
}

static void test_shuffles(void)
{
    pc_p15_t g;
    for (int r = 0; r < PC_P15_RULES; r++)
        for (uint32_t s = 0; s < 300; s++) {
            pc_p15_init(&g, r, s);
            int n = pc_p15_side(r), N = n * n;
            CHECK(!pc_p15_solved(&g));
            CHECK(pc_p15_solvable(g.t, n));
            CHECK_EQ_INT(g.moves, 0);
            int seen[26] = { 0 }, ok = 1;
            for (int i = 0; i < N; i++) { if (g.t[i] >= N) ok = 0; else seen[g.t[i]]++; }
            for (int v = 0; v < N; v++) if (seen[v] != 1) ok = 0;
            CHECK(ok);                              /* every tile once, one blank */
            CHECK_EQ_INT(g.t[g.blank], 0);
        }
    pc_p15_t a, b;
    pc_p15_init(&a, PC_P15_R4, 7); pc_p15_init(&b, PC_P15_R4, 7);
    CHECK(!memcmp(a.t, b.t, sizeof a.t));           /* deterministic */
    pc_p15_init(&b, PC_P15_R4, 8);
    CHECK(memcmp(a.t, b.t, sizeof a.t));
}

/* the solver check: every shuffle of 3x3 is a state reachable from solved (exhaustive search), and an unsolvable one is not */
static void test_solver_check(void)
{
    build_reach();
    pc_p15_t g;
    for (uint32_t s = 0; s < 2000; s++) {
        pc_p15_init(&g, PC_P15_R3, s * 7919u + 1);
        CHECK(reach[rank9(g.t)]);
    }
    for (uint32_t s = 0; s < 200; s++) {            /* swap two tiles of a shuffle: the parity flips, and the search never reaches it */
        pc_p15_init(&g, PC_P15_R3, s);
        int a = 0, b = 1;
        while (!g.t[a]) a++;
        b = a + 1; while (!g.t[b]) b++;
        uint8_t x = g.t[a]; g.t[a] = g.t[b]; g.t[b] = x;
        CHECK(!pc_p15_solvable(g.t, 3));
        CHECK(!reach[rank9(g.t)]);
    }
    free(reach);
}

static void test_moves(void)
{
    pc_p15_t g;
    pc_p15_solved_state(&g, PC_P15_R3);              /* blank at 8: its neighbours are 5 and 7 */
    int nb[4];
    CHECK_EQ_INT(pc_p15_neighbours(&g, nb), 2);
    CHECK_EQ_INT(nb[0], 5); CHECK_EQ_INT(nb[1], 7);
    CHECK(pc_p15_movable(&g, 5) && pc_p15_movable(&g, 7));
    CHECK(!pc_p15_movable(&g, 4) && !pc_p15_movable(&g, 8) && !pc_p15_movable(&g, 6));   /* a diagonal, the blank itself, a far cell */
    CHECK(!pc_p15_movable(&g, -1) && !pc_p15_movable(&g, 9));
    CHECK(!pc_p15_move(&g, 0));
    CHECK_EQ_INT(g.moves, 0);
    CHECK(pc_p15_move(&g, 5));                       /* tile 6 slides down */
    CHECK_EQ_INT(g.t[8], 6); CHECK_EQ_INT(g.t[5], 0); CHECK_EQ_INT(g.blank, 5);
    CHECK_EQ_INT(g.moves, 1);
    CHECK(!pc_p15_solved(&g));
    CHECK_EQ_INT(pc_p15_manhattan(&g), 1);
    CHECK(!pc_p15_home(&g, 8) && pc_p15_home(&g, 0));
    CHECK(pc_p15_move(&g, 8));                       /* and back: solved again */
    CHECK(pc_p15_solved(&g));
    CHECK_EQ_INT(g.moves, 2);
    CHECK(!pc_p15_move(&g, 8));                      /* the blank is there: not a tile */
    /* the left edge does not wrap: cell 3's right neighbour is not cell 2 */
    pc_p15_solved_state(&g, PC_P15_R3);
    g.t[8] = g.t[3]; g.t[3] = 0; g.blank = 3;        /* a blank at the left edge */
    CHECK(!pc_p15_movable(&g, 2));
    CHECK(pc_p15_movable(&g, 0) && pc_p15_movable(&g, 4) && pc_p15_movable(&g, 6));
    CHECK_EQ_INT(pc_p15_neighbours(&g, nb), 3);
}

static void test_par(void)
{
    CHECK(pc_p15_par(PC_P15_R3) < pc_p15_par(PC_P15_R4));
    CHECK(pc_p15_par(PC_P15_R4) < pc_p15_par(PC_P15_R5));
    pc_p15_t g;
    pc_p15_init(&g, PC_P15_R4, 1);
    CHECK(pc_p15_manhattan(&g) > 0);
}

int main(void)
{
    RUN(test_solved);
    RUN(test_parity);
    RUN(test_shuffles);
    RUN(test_solver_check);
    RUN(test_moves);
    RUN(test_par);
    return 0;
}
