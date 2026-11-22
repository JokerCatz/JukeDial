// SPDX-License-Identifier: LGPL-2.1-or-later
/* 旋轉迷宮 / Marble maze (G6b): wall collision, gap pass, each force mode, drift, tilt maze, no tunnelling at the top speed, the vector input. */
#include <math.h>
#include <stdlib.h>
#include "pc/pc_game_maze.h"
#include "pc_test.h"

static int wrap_gap(int a) { a %= 360; return a < 0 ? a + 360 : a; }
static bool has(const pc_mz_t *g, int e) { for (int i = 0; i < g->n_ev; i++) if (g->ev[i] == e) return true; return false; }
/* gravity at the (nearest detent to the) gap of the next ring; false when the marble is not there yet in `limit` steps */
static void aim_grav(pc_mz_t *g, int ang)
{
    int d = pc_mz_angdiff(ang, g->theta);
    d = (d + (d >= 0 ? PC_MZ_STEP_DEG / 2 : -PC_MZ_STEP_DEG / 2)) / PC_MZ_STEP_DEG;
    if (d) pc_mz_turn(g, d);
}
static int solve_grav(pc_mz_t *g, int limit)
{
    int n = 0;
    while (g->state == PC_MZ_PLAY && n < limit) {
        int k = 0;                                           /* the ring ahead: one counts as passed only when the marble is well clear of its wall */
        while (k < PC_MZ_RINGS - 1 && pc_mz_r(g) > pc_mz_ring_r(k) + PC_MZ_WALL_HALF + PC_MZ_MARBLE_R + 2.5f) k++;
        if (g->started) {                                    /* gravity at the gap, at most a quarter turn ahead of the marble (straight opposite is an unstable rest) */
            int diff = pc_mz_angdiff(g->gap[k], pc_mz_angle(g));
            diff = diff > 90 ? 90 : diff < -90 ? -90 : diff;
            aim_grav(g, pc_mz_angle(g) + diff);
        }
        else pc_mz_start(g);
        pc_mz_step(g);
        n++;
    }
    return n;
}

static void test_ring_wall(void)
{
    pc_mz_t g;
    pc_mz_init(&g, PC_MZ_GRAV, 0, 3, false);
    g.gap[0] = 270;                                         /* the gap is not where gravity (down = 90) points */
    pc_mz_start(&g);
    for (int i = 0; i < 200; i++) pc_mz_step(&g);
    CHECK_NEAR(pc_mz_r(&g), pc_mz_ring_r(0) - PC_MZ_WALL_HALF - PC_MZ_MARBLE_R, 0.6);   /* it rests on the wall's inner face, down */
    CHECK_EQ_INT(g.level, 0);
    CHECK(abs(pc_mz_angdiff(pc_mz_angle(&g), 90)) <= 2);
    CHECK_EQ_INT(g.state, PC_MZ_PLAY);
}
static void test_gap_pass(void)
{
    pc_mz_t g;
    pc_mz_init(&g, PC_MZ_GRAV, 0, 3, false);
    g.gap[0] = 90;                                          /* the gap where it falls */
    pc_mz_start(&g);
    bool gap_ev = false;
    for (int i = 0; i < 200; i++) { pc_mz_step(&g); gap_ev |= has(&g, PC_MZ_EV_GAP); }
    CHECK(gap_ev && g.level >= 1);
    CHECK(pc_mz_r(&g) > pc_mz_ring_r(0));
    /* a gap right next to it (outside the window) stops it */
    pc_mz_init(&g, PC_MZ_GRAV, 0, 3, false);
    g.gap[0] = 90 + PC_MZ_GAP_HALF + 4;
    pc_mz_start(&g);
    for (int i = 0; i < 200; i++) pc_mz_step(&g);
    CHECK_EQ_INT(g.level, 0);
}
static void test_grav_solve(void)
{
    pc_mz_t g;
    for (uint32_t seed = 1; seed <= 60; seed++) {
        pc_mz_init(&g, PC_MZ_GRAV, 0, seed, false);
        for (int k = 0; k < PC_MZ_RINGS; k++) CHECK(g.gap[k] % 10 == 0 && abs(pc_mz_angdiff(g.gap[k], k ? g.gap[k - 1] : 90)) >= 60);
        const int n = solve_grav(&g, 3000);
        if (g.state != PC_MZ_DONE) { fprintf(stderr, "seed %u not solved in %d steps (level %d)\n", (unsigned)seed, n, g.level); }
        CHECK_EQ_INT(g.state, PC_MZ_DONE);
        CHECK(pc_mz_r(&g) >= PC_MZ_EXIT_R);
        CHECK(g.turns > 0 && g.ms > 0);
    }
}
static void test_grav_golden_drift(void)
{
    pc_mz_t g;
    pc_mz_init(&g, PC_MZ_GRAV, 0, 4, true);
    CHECK_EQ_INT(pc_mz_gap_half(&g), PC_MZ_GAP_HALF + 4);
    for (int drift = 1; drift <= 2; drift++)
        for (uint32_t seed = 1; seed <= 30; seed++) {
            pc_mz_init(&g, PC_MZ_GRAV, drift, seed, false);
            const int g0 = g.gap[3];
            for (int i = 0; i < 100; i++) { pc_mz_start(&g); pc_mz_step(&g); }   /* no gravity input yet: it just sits down */
            CHECK(g.gap[3] != g0);                          /* a gap drifts */
            solve_grav(&g, 6000);
            CHECK_EQ_INT(g.state, PC_MZ_DONE);              /* and still can be followed */
        }
}
static void test_repel(void)
{
    pc_mz_t g;
    for (uint32_t seed = 1; seed <= 40; seed++) {
        pc_mz_init(&g, PC_MZ_REPEL, 0, seed, false);
        CHECK(pc_mz_r(&g) < 12);
        pc_mz_start(&g);
        for (int i = 0; i < 60; i++) pc_mz_step(&g);
        CHECK_EQ_INT(g.level, 0);                           /* pushed out to the first wall, held there */
        CHECK_NEAR(pc_mz_r(&g), pc_mz_ring_r(0) - (PC_MZ_WALL_HALF + PC_MZ_MARBLE_R), 0.7);
        int n = 0;
        while (g.state == PC_MZ_PLAY && n < 3000) {
            const int k = g.level < PC_MZ_RINGS ? g.level : PC_MZ_RINGS - 1;
            const int d = pc_mz_angdiff(g.gap[k], pc_mz_angle(&g));
            if (abs(d) > 3) pc_mz_turn(&g, d > 0 ? 1 : -1);
            pc_mz_step(&g);
            n++;
        }
        CHECK_EQ_INT(g.state, PC_MZ_DONE);
        CHECK(g.turns > 0);
    }
}
static void test_attract(void)
{
    pc_mz_t g;
    for (uint32_t seed = 1; seed <= 40; seed++) {
        pc_mz_init(&g, PC_MZ_ATTRACT, 0, seed, false);
        CHECK(pc_mz_r(&g) > 90 && g.level == PC_MZ_RINGS);
        pc_mz_start(&g);
        for (int i = 0; i < 60; i++) pc_mz_step(&g);
        CHECK_EQ_INT(g.level, PC_MZ_RINGS);                 /* held by the outer wall's outer face */
        int n = 0;
        while (g.state == PC_MZ_PLAY && n < 3000) {
            const int k = g.level > 0 ? g.level - 1 : 0;
            const int d = pc_mz_angdiff(g.gap[k], pc_mz_angle(&g));
            if (abs(d) > 3) pc_mz_turn(&g, d > 0 ? 1 : -1);
            pc_mz_step(&g);
            n++;
        }
        CHECK_EQ_INT(g.state, PC_MZ_DONE);
        CHECK(pc_mz_r(&g) <= PC_MZ_CENTRE_R);
    }
}
static void test_no_tunnel(void)
{
    pc_mz_t g;
    /* the top speed straight at a wall, from every side of every ring, no gap anywhere near */
    for (int k = 0; k < PC_MZ_RINGS; k++)
        for (int side = -1; side <= 1; side += 2)
            for (int a = 0; a < 360; a += 15) {
                pc_mz_init(&g, PC_MZ_GRAV, 0, 1, false);
                for (int j = 0; j < PC_MZ_RINGS; j++) g.gap[j] = wrap_gap(a + 180);
                const float R = pc_mz_ring_r(k), r0 = R + (float)side * 12.0f, c = cosf((float)a * 0.0174533f), s = sinf((float)a * 0.0174533f);
                g.x = r0 * c; g.y = r0 * s;
                g.vx = -(float)side * c * PC_MZ_VMAX; g.vy = -(float)side * s * PC_MZ_VMAX;
                g.gx = -(float)side * c; g.gy = -(float)side * s;       /* and still pushing */
                pc_mz_start(&g);
                for (int i = 0; i < 60; i++) {
                    pc_mz_step(&g);
                    const float r = pc_mz_r(&g);
                    CHECK(side > 0 ? r > R : r < R);        /* never to the other side of the wall */
                }
            }
}
static void test_tilt(void)
{
    pc_mz_t g;
    for (uint32_t seed = 1; seed <= 30; seed++) {
        pc_mz_init(&g, PC_MZ_TILT, 0, seed, false);
        CHECK(g.n_walls >= 4 && g.n_walls <= PC_MZ_WALLS_MAX && g.n_holes >= 1);
        CHECK(pc_mz_cell_of(g.x) == 0 && pc_mz_cell_of(g.y) == 0 && pc_mz_cell_of(g.goal[0]) == PC_MZ_N - 1);
        pc_mz_turn(&g, 5);
        CHECK(!g.started && g.turns == 0);                  /* the knob does nothing here */
        pc_mz_set_grav(&g, 0.05f, 0);
        CHECK(!g.started);
        pc_mz_set_grav(&g, 0.5f, 0.5f);
        CHECK(g.started);
        int falls = 0;
        const float lim = (float)PC_MZ_N * PC_MZ_CELL * 0.5f;
        for (int i = 0; i < 3000; i++) {                    /* wandering gravity: it never leaves the grid */
            if (i % 40 == 0) pc_mz_set_grav(&g, (float)((i / 40) % 3 - 1), (float)((i / 120) % 3 - 1));
            pc_mz_step(&g);
            falls += has(&g, PC_MZ_EV_FALL);
            CHECK(fabsf(g.x) < lim && fabsf(g.y) < lim);
            if (g.state == PC_MZ_DONE) break;
        }
        CHECK_EQ_INT(g.falls, falls);
    }
    /* straight down from the start: stops on the bottom wall of the first column or falls in a hole, never through the outer wall */
    pc_mz_init(&g, PC_MZ_TILT, 0, 7, false);
    pc_mz_set_grav(&g, 0, 1);
    for (int i = 0; i < 400; i++) pc_mz_step(&g);
    CHECK(g.y < PC_MZ_N * PC_MZ_CELL * 0.5f);
    /* a wall rest: gravity up from the start sits on the top wall */
    pc_mz_init(&g, PC_MZ_TILT, 0, 7, false);
    pc_mz_set_grav(&g, 0, -1);
    for (int i = 0; i < 100; i++) pc_mz_step(&g);
    CHECK_NEAR(g.y, -PC_MZ_N * PC_MZ_CELL * 0.5f + PC_MZ_WALL_T * 0.5f + PC_MZ_MARBLE_R, 0.3);
}
static void test_vector_input(void)
{
    pc_mz_t g;
    pc_mz_init(&g, PC_MZ_GRAV, 0, 2, false);
    pc_mz_set_theta(&g, 270);
    CHECK_EQ_INT(g.theta, 270);
    pc_mz_turn(&g, 1);
    CHECK_EQ_INT(g.theta, 280);
    CHECK_NEAR(g.gx, cos(280 * 0.0174533), 0.01);
    CHECK_EQ_INT(pc_mz_angdiff(10, 350), 20);
    /* the tilt vector reaches only 傾斜: in 重力 a zero (or any) vector keeps the knob's force and does not start the clock */
    pc_mz_set_grav(&g, 0, 0);
    CHECK_NEAR(g.gx, cos(280 * 0.0174533), 0.01);
    pc_mz_set_grav(&g, 1, 0);
    CHECK_NEAR(g.gx, cos(280 * 0.0174533), 0.01);
    CHECK_EQ_INT(g.theta, 280);
    pc_mz_init(&g, PC_MZ_GRAV, 0, 2, false);
    pc_mz_set_grav(&g, 1, 0);
    CHECK(!g.started && g.theta == 90);
    pc_mz_init(&g, PC_MZ_REPEL, 0, 2, false);
    pc_mz_set_grav(&g, 1, 1);
    CHECK(!g.started && g.gx == 0.0f && g.gy == 1.0f);   /* the init vector, untouched */
    pc_mz_init(&g, PC_MZ_ATTRACT, 0, 2, false);
    pc_mz_set_grav(&g, 1, 1);
    CHECK(!g.started);
    pc_mz_init(&g, PC_MZ_TILT, 0, 2, false);
    pc_mz_set_grav(&g, 3, 4);                               /* length clamped to 1 */
    CHECK_NEAR(hypot(g.gx, g.gy), 1.0, 0.001);
    CHECK(g.started);
}
static void test_rules(void)
{
    pc_mz_t g;
    pc_mz_init(&g, 99, 9, 1, false);
    CHECK_EQ_INT(g.rule, PC_MZ_GRAV);
    CHECK_EQ_INT(g.drift, 2);
    CHECK(!pc_mz_running(&g));
    pc_mz_step(&g);
    CHECK_EQ_INT(g.steps, 0);                               /* the clock waits for the first move */
    pc_mz_turn(&g, 1);
    CHECK(pc_mz_running(&g));
    pc_mz_init(&g, PC_MZ_GRAV, 0, 9, false);
    pc_mz_init(&g, PC_MZ_GRAV, 0, 9, false);
    pc_mz_t h;
    pc_mz_init(&h, PC_MZ_GRAV, 0, 9, false);
    CHECK(g.gap[0] == h.gap[0] && g.gap[3] == h.gap[3]);    /* the seed decides */
}
int main(void)
{
    RUN(test_ring_wall); RUN(test_gap_pass); RUN(test_grav_solve); RUN(test_grav_golden_drift); RUN(test_repel); RUN(test_attract);
    RUN(test_no_tunnel); RUN(test_tilt); RUN(test_vector_input); RUN(test_rules);
    return 0;
}
