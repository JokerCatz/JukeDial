// SPDX-License-Identifier: LGPL-2.1-or-later
/* 旋鈕小玩意 / Knob toys (G6): the wind-up's distances, snap, leak, releases; the fish's zone, catch, snap, dash, determinism. */
#include "pc/pc_game_knobtoy.h"
#include "pc_test.h"

static bool has(const pc_kt_t *g, int ev) { for (int i = 0; i < g->n_ev; i++) if (g->ev[i] == ev) return true; return false; }

static void test_wind_dist(void)
{
    CHECK_EQ_INT(pc_kt_wind_cm(20, 20), 120);
    CHECK_EQ_INT(pc_kt_wind_cm(19, 20), 95);
    CHECK_EQ_INT(pc_kt_wind_cm(21, 20), 92);
    CHECK_EQ_INT(pc_kt_wind_cm(0, 20), 0);
    CHECK_EQ_INT(pc_kt_wind_cm(PC_KT_WIND_MAX, 20), 0);
    CHECK_EQ_INT(pc_kt_wind_cm(1, 33), 5);                 /* wound a little: never below 5 */
    CHECK(pc_kt_wind_cm(39, 14) >= 0 && pc_kt_wind_cm(39, 14) < 20);
    for (int s = PC_KT_SWEET_MIN; s <= PC_KT_SWEET_MAX; s++) {
        int best = 0, at = -1;
        for (int v = 0; v <= PC_KT_WIND_MAX; v++) if (pc_kt_wind_cm(v, s) > best) { best = pc_kt_wind_cm(v, s); at = v; }
        CHECK_EQ_INT(at, s);                                /* the best tension is the sweet spot */
    }
}
static void test_wind_play(void)
{
    pc_kt_t g;
    for (uint32_t seed = 1; seed < 200; seed++) {
        pc_kt_init(&g, PC_KT_WIND, seed, false);
        CHECK(g.sweet >= PC_KT_SWEET_MIN && g.sweet <= PC_KT_SWEET_MAX);
        CHECK_EQ_INT(g.v, 0);
    }
    pc_kt_init(&g, PC_KT_WIND, 5, false);
    g.sweet = 20;
    pc_kt_move(&g, 20);
    CHECK_EQ_INT(g.v, 20);
    CHECK(g.started);
    pc_kt_release(&g);
    CHECK(has(&g, PC_KT_EV_LAUNCH) && has(&g, PC_KT_EV_PERFECT));
    CHECK_EQ_INT(g.last_cm, 120);
    CHECK_EQ_INT(g.total, 120);
    CHECK_EQ_INT(g.v, 0);                                   /* the spring is slack again */
    CHECK_EQ_INT(g.launches, 1);
    CHECK_EQ_INT(g.prev_sweet, 20);                         /* the spring just released shows its sweet spot */
    CHECK(g.sweet >= PC_KT_SWEET_MIN && g.sweet <= PC_KT_SWEET_MAX);
    CHECK_EQ_INT(g.state, PC_KT_PLAY);
    pc_kt_move(&g, -5);                                     /* clamped at 0 */
    CHECK_EQ_INT(g.v, 0);
    pc_kt_release(&g);                                      /* nothing wound: 0 cm, still counts */
    CHECK_EQ_INT(g.last_cm, 0);
    CHECK_EQ_INT(g.launches, 2);
    pc_kt_move(&g, 100);                                    /* over-wound: snaps at MAX */
    CHECK(has(&g, PC_KT_EV_SNAP));
    CHECK_EQ_INT(g.v, 0);
    CHECK_EQ_INT(g.launches, 2);                            /* a snap is not a release */
    pc_kt_move(&g, 19);
    pc_kt_release(&g);
    CHECK(has(&g, PC_KT_EV_DONE));
    CHECK_EQ_INT(g.state, PC_KT_DONE);
    CHECK_EQ_INT(g.total, 120 + 0 + g.last_cm);
    pc_kt_move(&g, 3);                                      /* after the end nothing moves */
    CHECK_EQ_INT(g.v, 0);
}
static void test_wind_leak(void)
{
    pc_kt_t g;
    pc_kt_init(&g, PC_KT_WIND, 5, false);
    pc_kt_tick(&g, 5000);
    CHECK_EQ_INT(g.v, 0);                                   /* the clock waits for the first move */
    pc_kt_move(&g, 10);
    pc_kt_tick(&g, PC_KT_LEAK_MS - 100);
    CHECK_EQ_INT(g.v, 10);
    pc_kt_tick(&g, 100);
    CHECK_EQ_INT(g.v, 9);
    for (int i = 0; i < 1000; i++) pc_kt_tick(&g, 100);
    CHECK_EQ_INT(g.v, 0);
    pc_kt_move(&g, 1);                                      /* the first move after a snap or a release restarts nothing odd */
    CHECK_EQ_INT(g.v, 1);
}
static void test_wind_gold(void)
{
    pc_kt_t g;
    pc_kt_init(&g, PC_KT_WIND, 5, true);
    g.sweet = 20;
    pc_kt_move(&g, 20);
    pc_kt_release(&g);
    CHECK_EQ_INT(g.last_cm, 240);
    CHECK(g.gold);
}
static void test_fish_zone(void)
{
    pc_kt_t g;
    for (uint32_t seed = 1; seed < 200; seed++) {
        pc_kt_init(&g, PC_KT_FISH, seed, false);
        CHECK(g.fish >= 30 && g.fish <= 70);
        CHECK_EQ_INT(g.v, 50);
        CHECK(pc_kt_zone_lo(&g) >= 0 && pc_kt_zone_hi(&g) <= PC_KT_FISH_RANGE - 1);
    }
    pc_kt_init(&g, PC_KT_FISH, 3, false);
    g.fish = 2;
    CHECK_EQ_INT(pc_kt_zone_lo(&g), 0);
    g.fish = 98;
    CHECK_EQ_INT(pc_kt_zone_hi(&g), PC_KT_FISH_RANGE - 1);
    g.fish = 50; g.v = 54;
    CHECK(pc_kt_in_zone(&g));
    g.v = 55;
    CHECK(!pc_kt_in_zone(&g));
    pc_kt_move(&g, 100);
    CHECK_EQ_INT(g.v, PC_KT_FISH_RANGE - 1);
    pc_kt_move(&g, -500);
    CHECK_EQ_INT(g.v, 0);
}
static void test_fish_catch(void)
{
    pc_kt_t g;
    pc_kt_init(&g, PC_KT_FISH, 3, false);
    pc_kt_tick(&g, 3000);
    CHECK_EQ_INT(g.ms, 0);                                  /* waits for the first move */
    pc_kt_move(&g, 0);
    CHECK(g.started || g.steps == 0);
    pc_kt_move(&g, 1); pc_kt_move(&g, -1);
    CHECK(g.started);
    /* keep the rod on the fish every step: caught in 40 steps, the line never fills */
    int n = 0;
    while (g.state == PC_KT_PLAY && n < 200) {
        pc_kt_move(&g, g.fish - g.v);
        pc_kt_tick(&g, PC_KT_STEP_MS);
        n++;
    }
    CHECK_EQ_INT(g.state, PC_KT_DONE);
    CHECK(has(&g, PC_KT_EV_CAUGHT));
    CHECK(n >= 40 && n <= 60);
    CHECK_EQ_INT(g.line, 0);
    CHECK_EQ_INT(pc_kt_catch_pct(&g), 100);
    CHECK(g.ms >= 4000u);
}
static void test_fish_snap(void)
{
    pc_kt_t g;
    pc_kt_init(&g, PC_KT_FISH, 3, false);
    pc_kt_move(&g, 1);
    int n = 0, last = 0;
    while (g.state == PC_KT_PLAY && n < 400) {              /* never follow: the rod stays far from the fish */
        g.v = g.fish < 50 ? 99 : 0;
        pc_kt_tick(&g, PC_KT_STEP_MS);
        CHECK(g.line >= last || g.line == 0);
        last = g.line;
        n++;
    }
    CHECK_EQ_INT(g.state, PC_KT_LOST);
    CHECK(has(&g, PC_KT_EV_LOST));
    CHECK(n >= 15 && n <= 40);
    CHECK_EQ_INT(g.catch_, 0);
    pc_kt_tick(&g, 5000);                                   /* over: nothing moves */
    CHECK_EQ_INT(g.state, PC_KT_LOST);
}
static void test_fish_swim(void)
{
    pc_kt_t a, b;
    pc_kt_init(&a, PC_KT_FISH, 9, false);
    pc_kt_init(&b, PC_KT_FISH, 9, false);
    pc_kt_move(&a, 1); pc_kt_move(&b, 1);
    int dashes = 0, moved = 0;
    for (int i = 0; i < 3000; i++) {
        const int before = a.fish;
        a.v = a.fish; b.v = b.fish;                          /* a stays on the fish: no end */
        pc_kt_tick(&a, PC_KT_STEP_MS); pc_kt_tick(&b, PC_KT_STEP_MS);
        CHECK_EQ_INT(a.fish, b.fish);                       /* the seed decides */
        CHECK(a.fish >= 5 && a.fish <= PC_KT_FISH_RANGE - 6);
        CHECK(a.fish - before <= 2 && before - a.fish <= 2); /* no teleport */
        moved += a.fish != before;
        dashes += has(&a, PC_KT_EV_DASH);
        if (a.state != PC_KT_PLAY) { pc_kt_init(&a, PC_KT_FISH, 9 + i, false); pc_kt_init(&b, PC_KT_FISH, 9 + i, false); pc_kt_move(&a, 1); pc_kt_move(&b, 1); }
    }
    CHECK(moved > 500 && dashes > 5);
    /* the golden fish is slower */
    pc_kt_t s, f;
    int ms_ = 0, mf = 0;
    for (uint32_t seed = 1; seed <= 20; seed++) {
        pc_kt_init(&s, PC_KT_FISH, seed, false); pc_kt_init(&f, PC_KT_FISH, seed, true);
        pc_kt_move(&s, 1); pc_kt_move(&f, 1);
        for (int i = 0; i < 300; i++) {
            int a0 = s.fish, b0 = f.fish;
            s.v = s.fish; f.v = f.fish;
            s.catch_ = f.catch_ = 0;                         /* never ends */
            pc_kt_tick(&s, PC_KT_STEP_MS); pc_kt_tick(&f, PC_KT_STEP_MS);
            ms_ += s.fish != a0; mf += f.fish != b0;
        }
    }
    CHECK(mf < ms_);
}
static void test_rules(void)
{
    pc_kt_t g;
    pc_kt_init(&g, 99, 1, false);
    CHECK_EQ_INT(g.rule, PC_KT_WIND);                       /* out of range: the first */
    CHECK(pc_kt_reach(PC_KT_WIND) == 8 && pc_kt_reach(PC_KT_FISH) == 10);
    CHECK(!pc_kt_running(&g));
    pc_kt_move(&g, 1);
    CHECK(pc_kt_running(&g));
}
int main(void)
{
    RUN(test_wind_dist); RUN(test_wind_play); RUN(test_wind_leak); RUN(test_wind_gold);
    RUN(test_fish_zone); RUN(test_fish_catch); RUN(test_fish_snap); RUN(test_fish_swim); RUN(test_rules);
    return 0;
}
