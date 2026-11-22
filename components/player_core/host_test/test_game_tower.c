// SPDX-License-Identifier: LGPL-2.1-or-later
/* 疊塔 / Stack tower (ideas G3): the slide, the cut, the perfect drop and its combo, the rules, the golden block, the camera, the debris. */
#include "pc/pc_game_tower.h"
#include "pc_test.h"

static bool has(const pc_tw_t *g, int ev) { for (int i = 0; i < g->n_ev; i++) if (g->ev[i].kind == ev) return true; return false; }
static int top(const pc_tw_t *g) { return (g->n - 1) % PC_TW_VIS; }
/* a started game with the slider put at x, the golden roll off: a test sets the situation, the rules decide */
static void start(pc_tw_t *g, int rule, uint32_t seed) { pc_tw_init(g, rule, seed); pc_tw_tap(g); g->gold = false; }

static void test_init(void)
{
    pc_tw_t a, b;
    pc_tw_init(&a, PC_TW_CLASSIC, 5);
    pc_tw_init(&b, PC_TW_CLASSIC, 5);
    CHECK_EQ_INT(a.state, PC_TW_WAIT);
    CHECK_EQ_INT(a.n, 1);
    CHECK_EQ_INT(pc_tw_height(&a), 0);
    CHECK_NEAR(a.cx[0], 0, 0.001);
    CHECK_NEAR(a.w[0], PC_TW_W0, 0.001);
    CHECK_NEAR(a.sw, PC_TW_W0, 0.001);
    CHECK(!pc_tw_running(&a));                      /* waiting for the first tap: no step changes anything */
    CHECK_NEAR(a.sx, b.sx, 0.0001);
    CHECK_EQ_INT(a.gold, b.gold);                   /* deterministic by seed */
    pc_tw_init(&a, 77, 5);
    CHECK_EQ_INT(a.rule, PC_TW_CLASSIC);            /* an unknown rule is the classic */
}
static void test_slide(void)
{
    pc_tw_t g;
    pc_tw_init(&g, PC_TW_CLASSIC, 1);
    float x0 = g.sx;
    pc_tw_step(&g);
    CHECK_NEAR(g.sx, x0, 0.0001);                   /* waiting: it does not move */
    CHECK(pc_tw_tap(&g));
    CHECK_EQ_INT(g.state, PC_TW_PLAY);
    CHECK_EQ_INT(g.n, 1);                           /* the first tap starts, it drops nothing */
    CHECK(pc_tw_running(&g));
    float lo = 999, hi = -999;
    for (int i = 0; i < 400; i++) { pc_tw_step(&g); if (g.sx < lo) lo = g.sx; if (g.sx > hi) hi = g.sx; }
    CHECK(lo >= -PC_TW_RANGE - 0.01f && hi <= PC_TW_RANGE + 0.01f);
    CHECK(lo < -PC_TW_RANGE + 4 && hi > PC_TW_RANGE - 4);   /* it sweeps the whole range, both ways */
}
static void test_speed(void)
{
    pc_tw_t a, b;
    start(&a, PC_TW_CLASSIC, 3);
    start(&b, PC_TW_CLASSIC, 3);
    b.speed = 1.5f;
    float xa = a.sx, xb = b.sx;
    pc_tw_step(&a); pc_tw_step(&b);
    CHECK_NEAR(fabsf(b.sx - xb), 1.5f * fabsf(a.sx - xa), 0.001);
    CHECK(pc_tw_tol(&b) > pc_tw_tol(&a));           /* a faster block is more forgiving by the step it takes */
    pc_tw_init(&a, PC_TW_SWING, 3);
    CHECK_NEAR(a.speed, 1.0f, 0.0001);
}
static void test_perfect_and_combo(void)
{
    pc_tw_t g;
    start(&g, PC_TW_CLASSIC, 3);
    g.sx = 0;
    pc_tw_tap(&g);
    CHECK_EQ_INT(g.n, 2);
    CHECK_EQ_INT(g.combo, 1);
    CHECK_NEAR(g.w[top(&g)], PC_TW_W0, 0.001);      /* the first perfect: the width kept, no growth yet */
    CHECK(has(&g, PC_TW_EV_PERFECT) && has(&g, PC_TW_EV_DROP));
    CHECK(!g.deb[0].live);                          /* nothing cut off */
    /* a cut, then two perfects: the second one grows the block back */
    g.gold = false; g.sx = 10;
    pc_tw_tap(&g);
    CHECK_EQ_INT(g.combo, 0);
    CHECK_NEAR(g.w[top(&g)], 50, 0.001);
    g.gold = false; g.sx = g.cx[top(&g)];
    pc_tw_tap(&g);
    CHECK_EQ_INT(g.combo, 1);
    CHECK_NEAR(g.w[top(&g)], 50, 0.001);
    g.gold = false; g.sx = g.cx[top(&g)] + 0.5f;
    pc_tw_tap(&g);
    CHECK_EQ_INT(g.combo, 2);
    CHECK_NEAR(g.w[top(&g)], 50 + PC_TW_GROW, 0.001);
    CHECK(has(&g, PC_TW_EV_COMBO));
    CHECK_EQ_INT(g.combo_best, 2);
    CHECK_EQ_INT(g.perfects, 3);
    for (int i = 0; i < 40; i++) { g.gold = false; g.sx = g.cx[top(&g)]; pc_tw_tap(&g); }
    CHECK_NEAR(g.w[top(&g)], PC_TW_W0, 0.001);      /* grown back, never past the full width */
}
static void test_cut(void)
{
    pc_tw_t g;
    start(&g, PC_TW_CLASSIC, 3);
    g.sx = 10;                                      /* 10 units to the right: the right 10 are cut off */
    pc_tw_tap(&g);
    CHECK_EQ_INT(g.n, 2);
    CHECK_NEAR(g.w[top(&g)], 50, 0.001);
    CHECK_NEAR(g.cx[top(&g)], 5, 0.001);
    CHECK(g.deb[0].live);
    CHECK_NEAR(g.deb[0].w, 10, 0.001);
    CHECK_NEAR(g.deb[0].x, 35, 0.001);
    CHECK_NEAR(g.sw, 50, 0.001);                    /* the next block is as wide as what is left */
    g.gold = false; g.sx = g.cx[top(&g)] - 7;       /* the left side, too */
    pc_tw_tap(&g);
    CHECK_NEAR(g.w[top(&g)], 43, 0.001);
    CHECK_NEAR(g.cx[top(&g)], 5 - 3.5f, 0.001);
    CHECK(g.deb[1].live);                           /* the slider [-27, 23] against the top [-20, 30]: 7 cut off on the left */
    CHECK_NEAR(g.deb[1].w, 7, 0.001);
    CHECK_NEAR(g.deb[1].x, -23.5f, 0.001);
}
static void test_miss_over(void)
{
    pc_tw_t g;
    start(&g, PC_TW_CLASSIC, 3);
    g.sx = 55;                                      /* [35, 75] against [-30, 30]: nothing touches */
    g.sw = 40;
    pc_tw_tap(&g);
    CHECK_EQ_INT(g.state, PC_TW_OVER);
    CHECK_EQ_INT(g.n, 1);
    CHECK(has(&g, PC_TW_EV_OVER));
    CHECK(g.deb[0].live);                           /* the whole block falls */
    CHECK_NEAR(g.deb[0].w, 40, 0.001);
    CHECK(!pc_tw_tap(&g));                          /* nothing more to do */
    CHECK(pc_tw_running(&g));                       /* the debris still falls */
    for (int i = 0; i < 200; i++) pc_tw_step(&g);
    CHECK(!g.deb[0].live);
    CHECK(!pc_tw_running(&g));
}
static void test_narrow(void)
{
    pc_tw_t g;
    start(&g, PC_TW_NARROW, 3);
    CHECK_NEAR(pc_tw_cap(&g, 0), PC_TW_W0, 0.001);
    CHECK_NEAR(pc_tw_cap(&g, 10), PC_TW_W0 / 2, 0.01);          /* the start width halves every ten floors */
    CHECK_NEAR(pc_tw_cap(&g, 20), PC_TW_W0 / 4, 0.01);
    CHECK_NEAR(pc_tw_cap(&g, 90), PC_TW_MINW, 0.001);           /* with a floor */
    CHECK_NEAR(pc_tw_cap(&(pc_tw_t){ .rule = PC_TW_CLASSIC }, 50), PC_TW_W0, 0.001);
    for (int i = 0; i < 10; i++) { g.gold = false; g.sx = g.cx[top(&g)]; pc_tw_tap(&g); }
    CHECK_EQ_INT(g.n, 11);
    CHECK_NEAR(g.sw, PC_TW_W0 / 2, 0.01);           /* the 11th block starts at half (the cap by the height 10) */
}
static void test_swing(void)
{
    pc_tw_t g;
    start(&g, PC_TW_SWING, 3);
    float lo = 999, hi = -999, dmin = 0, dmax = -999;
    for (int i = 0; i < 600; i++) {
        pc_tw_step(&g);
        if (g.sx < lo) lo = g.sx;
        if (g.sx > hi) hi = g.sx;
        if (g.dy < dmin) dmin = g.dy;
        if (g.dy > dmax) dmax = g.dy;
    }
    CHECK(lo >= -PC_TW_RANGE - 0.01f && hi <= PC_TW_RANGE + 0.01f);
    CHECK(lo < -PC_TW_RANGE + 4 && hi > PC_TW_RANGE - 4);
    CHECK(dmin < -5 && dmax <= 0.001f);             /* it hangs from above: the ends are higher than the middle, never lower */
}
static void test_gold(void)
{
    pc_tw_t g;
    start(&g, PC_TW_CLASSIC, 3);
    g.sx = 10; pc_tw_tap(&g);                       /* 50 wide */
    g.gold = true; g.sx = g.cx[top(&g)] + 8;
    pc_tw_tap(&g);
    CHECK_NEAR(g.w[top(&g)], PC_TW_W0, 0.001);      /* the golden block restores the full width */
    CHECK_EQ_INT(g.kind[top(&g)], PC_TW_K_GOLD);
    CHECK(has(&g, PC_TW_EV_GOLD));
    CHECK_EQ_INT(g.golds, 1);
    CHECK(!g.deb[1].live);                          /* no cut: the overhang does not fall either */
    /* about one block in PC_TW_GOLD_N is golden; the stream is the game's own */
    int gold = 0, total = 0;
    for (uint32_t s = 1; s <= 120; s++) {
        pc_tw_init(&g, PC_TW_CLASSIC, s);
        pc_tw_tap(&g);
        for (int i = 0; i < 20; i++) {
            gold += g.gold; total++;
            g.gold = false; g.sx = g.cx[top(&g)]; pc_tw_tap(&g);
        }
    }
    CHECK(gold > total / (PC_TW_GOLD_N * 2) && gold < total * 2 / PC_TW_GOLD_N);
}
static void test_camera_and_debris(void)
{
    pc_tw_t g;
    start(&g, PC_TW_CLASSIC, 3);
    for (int i = 0; i < 12; i++) { g.gold = false; g.sx = g.cx[top(&g)]; pc_tw_tap(&g); }
    CHECK(g.cam_to > 0);
    CHECK(pc_tw_running(&g));                       /* the camera still moves */
    for (int i = 0; i < 90; i++) pc_tw_step(&g);
    CHECK_NEAR(g.cam, g.cam_to, 0.1);
    CHECK(!pc_tw_running(&g) || g.state == PC_TW_PLAY);
    /* the slider stays on the screen whatever the height: its top stays near -40 once the camera follows */
    float y = pc_tw_slider_y(&g);
    CHECK(y > -60 && y < 60);
    CHECK_NEAR(pc_tw_floor_y(&g, g.n - 1), -30, 0.5);   /* the tower's top block sits at a fixed screen height */
    for (int i = 0; i < 60; i++) { g.gold = false; g.sx = g.cx[top(&g)] + 6; pc_tw_tap(&g); for (int k = 0; k < 40; k++) pc_tw_step(&g); }
    CHECK(g.state == PC_TW_PLAY || g.state == PC_TW_OVER);
    y = pc_tw_slider_y(&g);
    CHECK(y > -60 && y < 60);
    /* debris falls and goes */
    pc_tw_init(&g, PC_TW_CLASSIC, 9);
    pc_tw_tap(&g);
    g.gold = false; g.sx = 12; pc_tw_tap(&g);
    CHECK(g.deb[0].live);
    float y0 = g.deb[0].wy;
    pc_tw_step(&g); pc_tw_step(&g);
    CHECK(g.deb[0].wy < y0);                        /* it falls (the world's y is up) */
    for (int i = 0; i < 120; i++) pc_tw_step(&g);
    CHECK(!g.deb[0].live);
}
static void test_autoplay(void)
{
    /* a player who taps when the slider is within the tolerance of the top block: every rule keeps going, the ring of floors stays consistent, it is deterministic */
    for (int rule = 0; rule < PC_TW_RULES; rule++) {
        int h1 = 0, h2 = 0;
        for (int pass = 0; pass < 2; pass++) {
            pc_tw_t g;
            pc_tw_init(&g, rule, 42);
            pc_tw_tap(&g);
            for (long st = 0; st < 40000 && g.state == PC_TW_PLAY && g.n < 300; st++) {
                pc_tw_step(&g);
                float d = g.sx - g.cx[top(&g)];
                if (d < 0) d = -d;
                if (d <= pc_tw_tol(&g) * 0.5f) pc_tw_tap(&g);
                for (int k = 0; k < PC_TW_VIS; k++) CHECK(g.w[k] >= 0 && g.w[k] <= PC_TW_W0 + 0.01f);
            }
            if (pass) h2 = pc_tw_height(&g); else h1 = pc_tw_height(&g);
        }
        CHECK(h1 >= 60);
        CHECK_EQ_INT(h1, h2);
    }
    /* a careless player (always taps at the same phase) is cut down until it is over */
    pc_tw_t g;
    pc_tw_init(&g, PC_TW_CLASSIC, 8);
    pc_tw_tap(&g);
    for (int i = 0; i < 100 && g.state == PC_TW_PLAY; i++) { g.sx = g.cx[top(&g)] + 20; pc_tw_tap(&g); }
    CHECK_EQ_INT(g.state, PC_TW_OVER);
    CHECK(pc_tw_height(&g) >= 1 && pc_tw_height(&g) <= 4);
}
static void test_milestone_level(void)
{
    pc_tw_t g;
    start(&g, PC_TW_CLASSIC, 3);
    bool mile = false;
    for (int i = 0; i < 10; i++) { g.gold = false; g.sx = g.cx[top(&g)]; pc_tw_tap(&g); if (i == 9) mile = has(&g, PC_TW_EV_MILE); else CHECK(!has(&g, PC_TW_EV_MILE)); }
    CHECK(mile);                                    /* the tenth floor */
    CHECK_EQ_INT(pc_tw_height(&g), 10);
    CHECK_EQ_INT(pc_tw_level(&g), 2);
}
int main(void)
{
    RUN(test_init);
    RUN(test_slide);
    RUN(test_speed);
    RUN(test_perfect_and_combo);
    RUN(test_cut);
    RUN(test_miss_over);
    RUN(test_narrow);
    RUN(test_swing);
    RUN(test_gold);
    RUN(test_camera_and_debris);
    RUN(test_autoplay);
    RUN(test_milestone_level);
    return 0;
}
