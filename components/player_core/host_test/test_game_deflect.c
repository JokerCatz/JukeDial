// SPDX-License-Identifier: LGPL-2.1-or-later
/* 旋轉擋板 (ideas G6): the pure rules and the guard-line geometry of both shapes (pc_game_deflect.h). */
#include "pc/pc_game_deflect.h"
#include "pc_test.h"

static float deg_of(float x, float y) { float d = atan2f(y, x) * 57.29578f; return d < 0 ? d + 360.0f : d; }
static float len(float x, float y) { return sqrtf(x * x + y * y); }
static int count_ev(const pc_df_t *g, int kind) { int n = 0; for (int i = 0; i < g->n_ev; i++) n += g->ev[i].kind == kind; return n; }

/* an empty field in PLAY, the shield at deg, no spawn for a long while */
static void field(pc_df_t *g, int rule, int shape, float deg)
{
    pc_df_init(g, rule, 1, shape, 7);
    g->state = PC_DF_PLAY;
    g->deg = deg;
    g->spawn_in = 100000;
}
static void shot(pc_df_t *g, int i, float x, float y, float vx, float vy)
{
    g->shot[i] = (pc_df_shot_t){ .x = x, .y = y, .vx = vx, .vy = vy, .live = true };
}

static void test_guard_round(void)
{
    for (int a = 0; a < 360; a += 7) {
        float x, y;
        pc_df_guard_pt(PC_DF_ROUND, (float)a, &x, &y);
        CHECK_NEAR(len(x, y), PC_DF_GUARD_R, 1e-3);
        CHECK_NEAR(pc_df_guard_dist(PC_DF_ROUND, (float)a), PC_DF_GUARD_R, 1e-5);
        float nx, ny;
        pc_df_guard_normal(PC_DF_ROUND, x, y, &nx, &ny);
        CHECK_NEAR(nx, x / PC_DF_GUARD_R, 1e-4); CHECK_NEAR(ny, y / PC_DF_GUARD_R, 1e-4);
    }
    float seg[PC_DF_SEGS][4];
    CHECK_EQ_INT(pc_df_shield_segs(PC_DF_ROUND, 270, 26, seg), 0);        /* the page draws an arc */
}
static void test_guard_square(void)
{
    for (int a = 0; a < 360; a += 3) {
        float x, y;
        pc_df_guard_pt(PC_DF_SQUARE, (float)a, &x, &y);
        float m = fabsf(x) > fabsf(y) ? fabsf(x) : fabsf(y);
        CHECK_NEAR(m, PC_DF_GUARD_Q, 1e-3);                                /* always on an edge of the square */
        CHECK(len(x, y) < PC_DF_FIELD * 0.92f);                            /* the corners stay inside the field */
        if (a % 90) CHECK_NEAR(deg_of(x, y), (float)a, 0.05);              /* on the ray it was asked for */
    }
    float nx, ny;
    pc_df_guard_normal(PC_DF_SQUARE, 64, 10, &nx, &ny); CHECK_NEAR(nx, 1, 1e-6); CHECK_NEAR(ny, 0, 1e-6);
    pc_df_guard_normal(PC_DF_SQUARE, -64, 10, &nx, &ny); CHECK_NEAR(nx, -1, 1e-6);
    pc_df_guard_normal(PC_DF_SQUARE, 10, -64, &nx, &ny); CHECK_NEAR(ny, -1, 1e-6); CHECK_NEAR(nx, 0, 1e-6);
    pc_df_guard_normal(PC_DF_SQUARE, 3, 64, &nx, &ny); CHECK_NEAR(ny, 1, 1e-6);
}
static void test_segs_square(void)
{
    float seg[PC_DF_SEGS][4];
    int n = pc_df_shield_segs(PC_DF_SQUARE, 270, 20, seg);                  /* the top edge only */
    CHECK_EQ_INT(n, 1);
    CHECK_NEAR(seg[0][1], -PC_DF_GUARD_Q, 1e-3); CHECK_NEAR(seg[0][3], -PC_DF_GUARD_Q, 1e-3);
    CHECK(seg[0][0] < seg[0][2]);                                           /* from the left end (a0 = 250) to the right (290) */
    n = pc_df_shield_segs(PC_DF_SQUARE, 315, 20, seg);                      /* across the top-right corner (315 = the diagonal) */
    CHECK_EQ_INT(n, 2);
    CHECK_NEAR(seg[0][2], seg[1][0], 1e-3); CHECK_NEAR(seg[0][3], seg[1][1], 1e-3);   /* the pieces join */
    CHECK_NEAR(seg[0][2], PC_DF_GUARD_Q, 1e-3); CHECK_NEAR(seg[0][3], -PC_DF_GUARD_Q, 1e-3);   /* at the corner */
    CHECK_NEAR(seg[0][1], -PC_DF_GUARD_Q, 1e-3);                            /* the first on the top edge, the second on the right edge */
    CHECK_NEAR(seg[1][2], PC_DF_GUARD_Q, 1e-3);
    n = pc_df_shield_segs(PC_DF_SQUARE, 0, 30, seg);                       /* the span crosses 360: a0 = -30 */
    CHECK_EQ_INT(n, 1);
    CHECK_NEAR(seg[0][0], PC_DF_GUARD_Q, 1e-3); CHECK_NEAR(seg[0][2], PC_DF_GUARD_Q, 1e-3);
    float lo = 1e9f, hi = -1e9f;
    for (int c = 0; c < 360; c += 5) {                                      /* every position: pieces chain, count in range */
        n = pc_df_shield_segs(PC_DF_SQUARE, (float)c, PC_DF_HALF_DEG + PC_DF_GROW_MAX, seg);
        CHECK(n >= 1 && n <= 2);
        for (int i = 0; i + 1 < n; i++) { CHECK_NEAR(seg[i][2], seg[i + 1][0], 1e-3); CHECK_NEAR(seg[i][3], seg[i + 1][1], 1e-3); }
        for (int i = 0; i < n; i++) { lo = fminf(lo, fminf(seg[i][0], seg[i][1])); hi = fmaxf(hi, fmaxf(seg[i][2], seg[i][3])); }
    }
    CHECK(lo >= -PC_DF_GUARD_Q - 1e-3 && hi <= PC_DF_GUARD_Q + 1e-3);
}

static void test_covers(void)
{
    pc_df_t g;
    field(&g, PC_DF_CLASSIC, PC_DF_ROUND, 270);
    CHECK(pc_df_covers(&g, 270)); CHECK(pc_df_covers(&g, 270 + 27)); CHECK(!pc_df_covers(&g, 270 + 30)); CHECK(!pc_df_covers(&g, 90));
    CHECK(pc_df_covers(&g, 270 - 27)); CHECK(!pc_df_covers(&g, 270 - 30));
    field(&g, PC_DF_TWIN, PC_DF_ROUND, 270);
    CHECK_EQ_INT(pc_df_shields(&g), 2);
    CHECK(pc_df_covers(&g, 90)); CHECK(pc_df_covers(&g, 270)); CHECK(!pc_df_covers(&g, 0));
    CHECK(!pc_df_covers(&g, 270 + 20));                                       /* narrower: 17 + 2 */
    field(&g, PC_DF_CLASSIC, PC_DF_ROUND, 5);
    CHECK(pc_df_covers(&g, 355) && pc_df_covers(&g, 25));                      /* wraps round 0 */
}

static void test_bounce_round(void)
{
    pc_df_t g;
    field(&g, PC_DF_CLASSIC, PC_DF_ROUND, 270);
    shot(&g, 0, 0, -70, 0, 1.5f);                                             /* straight down the shield's middle */
    int n = 0;
    while (!g.shot[0].back && n++ < 20) pc_df_step(&g);
    CHECK(g.shot[0].back && g.shot[0].live);
    CHECK_NEAR(g.shot[0].vx, 0, 1e-4); CHECK_NEAR(g.shot[0].vy, -1.5, 1e-4);   /* straight back, speed kept */
    CHECK_EQ_INT(g.score, 10); CHECK_EQ_INT(g.combo, 1); CHECK_EQ_INT(g.bounces, 1);
    CHECK_EQ_INT(count_ev(&g, PC_DF_EV_BOUNCE), 1);
    CHECK(len(g.shot[0].x, g.shot[0].y) > PC_DF_GUARD_R);                      /* outside the line: no second contact */
    for (int i = 0; i < 80; i++) pc_df_step(&g);
    CHECK(!g.shot[0].live);                                                    /* leaves the field */
    CHECK_EQ_INT(g.lives, 3);
    /* oblique: mirrored about the radial normal */
    field(&g, PC_DF_CLASSIC, PC_DF_ROUND, 270);
    shot(&g, 0, 20, -75, -0.4f, 1.2f);
    for (int i = 0; i < 20 && !g.shot[0].back; i++) pc_df_step(&g);
    CHECK(g.shot[0].back);
    CHECK_NEAR(len(g.shot[0].vx, g.shot[0].vy), len(0.4f, 1.2f), 1e-3);
    CHECK(g.shot[0].vy < 0);
}
static void test_bounce_square(void)
{
    pc_df_t g;
    field(&g, PC_DF_CLASSIC, PC_DF_SQUARE, 270);
    shot(&g, 0, 5, -85, 0.3f, 1.4f);                                          /* the top edge: normal (0, -1) */
    for (int i = 0; i < 40 && !g.shot[0].back; i++) pc_df_step(&g);
    CHECK(g.shot[0].back);
    CHECK_NEAR(g.shot[0].vx, 0.3, 1e-4); CHECK_NEAR(g.shot[0].vy, -1.4, 1e-4);   /* only vy flips */
    CHECK(g.shot[0].y < -PC_DF_GUARD_Q);                                      /* outside the edge */
    field(&g, PC_DF_CLASSIC, PC_DF_SQUARE, 0);                                /* the right edge */
    shot(&g, 0, 85, 12, -1.4f, -0.2f);
    for (int i = 0; i < 40 && !g.shot[0].back; i++) pc_df_step(&g);
    CHECK(g.shot[0].back);
    CHECK_NEAR(g.shot[0].vx, 1.4, 1e-4); CHECK_NEAR(g.shot[0].vy, -0.2, 1e-4);
    field(&g, PC_DF_CLASSIC, PC_DF_SQUARE, 180);                              /* the shield is on the left: a shot at the right edge goes on to the core */
    shot(&g, 0, 85, 0, -1.4f, 0);
    for (int i = 0; i < 80 && g.lives == 3; i++) pc_df_step(&g);
    CHECK_EQ_INT(g.lives, 2);
}
static void test_miss(void)
{
    pc_df_t g;
    field(&g, PC_DF_CLASSIC, PC_DF_ROUND, 90);
    g.combo = 4;
    shot(&g, 0, 0, -90, 0, 1.5f);
    for (int i = 0; i < 100 && g.lives == 3; i++) pc_df_step(&g);
    CHECK_EQ_INT(g.lives, 2); CHECK_EQ_INT(g.combo, 0); CHECK_EQ_INT(g.misses, 1);
    CHECK_EQ_INT(count_ev(&g, PC_DF_EV_MISS), 1);
    CHECK(!g.shot[0].live); CHECK_EQ_INT(g.score, 0);
    CHECK_EQ_INT(g.state, PC_DF_PLAY);
}
static void test_gold(void)
{
    pc_df_t g;
    field(&g, PC_DF_CLASSIC, PC_DF_ROUND, 270);
    g.lives = 2;
    shot(&g, 0, 0, -80, 0, 1.5f); g.shot[0].gold = true;
    for (int i = 0; i < 20 && !g.shot[0].back; i++) pc_df_step(&g);
    CHECK_EQ_INT(g.lives, 3); CHECK_EQ_INT(g.score, 60); CHECK_EQ_INT(g.golds, 1);
    CHECK_EQ_INT(count_ev(&g, PC_DF_EV_LIFE), 1); CHECK_EQ_INT(count_ev(&g, PC_DF_EV_GOLD), 1);
    field(&g, PC_DF_CLASSIC, PC_DF_ROUND, 270);
    g.lives = PC_DF_LIVES_MAX;
    shot(&g, 0, 0, -80, 0, 1.5f); g.shot[0].gold = true;
    for (int i = 0; i < 20 && !g.shot[0].back; i++) pc_df_step(&g);
    CHECK_EQ_INT(g.lives, PC_DF_LIVES_MAX);                                    /* the cap */
    field(&g, PC_DF_CLASSIC, PC_DF_ROUND, 90);                                 /* a golden shot missed: free */
    shot(&g, 0, 0, -90, 0, 1.5f); g.shot[0].gold = true;
    for (int i = 0; i < 100 && g.shot[0].live; i++) pc_df_step(&g);
    CHECK_EQ_INT(g.lives, 3); CHECK_EQ_INT(g.misses, 0);
}
static void test_combo_grows(void)
{
    pc_df_t g;
    field(&g, PC_DF_CLASSIC, PC_DF_ROUND, 270);
    CHECK_NEAR(pc_df_half(&g), PC_DF_HALF_DEG, 1e-4);
    int hits = 0;
    for (int k = 0; k < 12; k++) {
        shot(&g, 0, 0, -85, 0, 1.5f);
        for (int i = 0; i < 30 && !g.shot[0].back; i++) pc_df_step(&g);
        hits += g.shot[0].back;
        g.shot[0].live = false;
    }
    CHECK_EQ_INT(hits, 12); CHECK_EQ_INT(g.combo, 12);
    CHECK_NEAR(pc_df_half(&g), PC_DF_HALF_DEG + PC_DF_GROW_MAX, 1e-4);          /* capped */
    CHECK_EQ_INT(g.level, 2);                                                  /* 10 bounces: level 2 */
    CHECK_EQ_INT(g.score, 10 * 4 + 20 * 5 + 30 * 3);                           /* combos 1-4 x1, 5-9 x2, 10-12 x3 */
    shot(&g, 0, 0, -85, 0, 1.5f);
    g.deg = 90;
    for (int i = 0; i < 100 && g.lives == 3; i++) pc_df_step(&g);
    CHECK_EQ_INT(g.combo, 0); CHECK_NEAR(pc_df_half(&g), PC_DF_HALF_DEG, 1e-4);
}
static void test_combo_event(void)
{
    pc_df_t g;
    field(&g, PC_DF_CLASSIC, PC_DF_ROUND, 270);
    int seen = 0;
    for (int k = 0; k < 5; k++) {
        shot(&g, 0, 0, -85, 0, 1.5f);
        for (int i = 0; i < 30 && !g.shot[0].back; i++) { pc_df_step(&g); seen += count_ev(&g, PC_DF_EV_COMBO); }
        g.shot[0].live = false;
    }
    CHECK_EQ_INT(seen, 1);
}
static void test_rebound(void)
{
    pc_df_t g;
    field(&g, PC_DF_REBOUND, PC_DF_ROUND, 270);
    shot(&g, 0, 0, -75, 0, 1.2f);                                              /* will bounce */
    shot(&g, 1, 0, -92, 0, 0);                                                 /* a standing shot out there: the bounced one runs into it */
    bool pop = false;
    for (int i = 0; i < 60; i++) { pc_df_step(&g); if (count_ev(&g, PC_DF_EV_POP)) { pop = true; break; } }
    CHECK(pop);
    CHECK(!g.shot[0].live && !g.shot[1].live);
    CHECK_EQ_INT(g.score, 10 + PC_DF_BONUS);
    field(&g, PC_DF_CLASSIC, PC_DF_ROUND, 270);                                 /* classic: it flies through */
    shot(&g, 0, 0, -75, 0, 1.2f);
    shot(&g, 1, 0, -92, 0, 0);
    for (int i = 0; i < 60; i++) { pc_df_step(&g); CHECK_EQ_INT(count_ev(&g, PC_DF_EV_POP), 0); }
    CHECK(g.shot[1].live);
}
static void test_twin(void)
{
    pc_df_t g;
    field(&g, PC_DF_TWIN, PC_DF_ROUND, 270);
    shot(&g, 0, 0, 80, 0, -1.5f);                                              /* from below: the second shield (90) takes it */
    for (int i = 0; i < 30 && !g.shot[0].back; i++) pc_df_step(&g);
    CHECK(g.shot[0].back);
    CHECK_NEAR(pc_df_half(&g), PC_DF_HALF_TWIN + PC_DF_GROW_DEG, 1e-4);   /* the one bounce widened it */
    pc_df_move(&g, 90);
    CHECK_NEAR(g.deg, 0, 1e-4);
}
static void test_over(void)
{
    pc_df_t g;
    field(&g, PC_DF_CLASSIC, PC_DF_ROUND, 90);
    for (int k = 0; k < 3; k++) {
        shot(&g, 0, 0, -90, 0, 1.5f);
        for (int i = 0; i < 100 && g.shot[0].live; i++) pc_df_step(&g);
    }
    CHECK_EQ_INT(g.state, PC_DF_OVER); CHECK_EQ_INT(g.lives, 0);
    CHECK_EQ_INT(count_ev(&g, PC_DF_EV_OVER), 1);                              /* with the third miss */
    pc_df_t h;
    field(&h, PC_DF_CLASSIC, PC_DF_ROUND, 90);
    h.lives = 1;
    shot(&h, 0, 0, -90, 0, 1.5f);
    bool over = false;
    for (int i = 0; i < 100 && !over; i++) { pc_df_step(&h); over = count_ev(&h, PC_DF_EV_OVER) == 1; }
    CHECK(over);
    uint32_t t = h.ticks;
    pc_df_step(&h);
    CHECK_EQ_INT(h.ticks, t); CHECK(!pc_df_running(&h));
}
static void test_start(void)
{
    pc_df_t g;
    pc_df_init(&g, 0, 1, PC_DF_ROUND, 3);
    CHECK_EQ_INT(g.state, PC_DF_SERVE); CHECK_NEAR(g.deg, 270, 1e-6);
    pc_df_step(&g);
    CHECK_EQ_INT(g.ticks, 0);                                                  /* nothing moves before the start */
    CHECK(pc_df_start(&g)); CHECK_EQ_INT(count_ev(&g, PC_DF_EV_START), 1);
    CHECK(!pc_df_start(&g));
    CHECK(pc_df_running(&g));
    pc_df_move(&g, -15); CHECK_NEAR(g.deg, 255, 1e-4);
    pc_df_move(&g, 360 + 30); CHECK_NEAR(g.deg, 285, 1e-3);
    pc_df_move(&g, -300); CHECK_NEAR(g.deg, 345, 1e-3);
}
static void test_spawn(void)
{
    pc_df_t g;
    pc_df_init(&g, 0, 1, PC_DF_ROUND, 11);
    pc_df_start(&g);
    g.deg = 90;                                                                 /* nothing defended: just watch the spawns */
    bool seen = false;
    for (int i = 0; i < 25 && !seen; i++) {
        pc_df_step(&g);
        for (int k = 0; k < PC_DF_SHOTS; k++) if (g.shot[k].live) {
            seen = true;
            CHECK(len(g.shot[k].x, g.shot[k].y) <= PC_DF_SPAWN_R + 1.5f && len(g.shot[k].x, g.shot[k].y) > PC_DF_SPAWN_R - 2.0f);
            CHECK_NEAR(len(g.shot[k].vx, g.shot[k].vy), pc_df_speed(&g), 1e-3);
            CHECK(g.shot[k].vx * g.shot[k].x + g.shot[k].vy * g.shot[k].y < 0);   /* inward */
        }
    }
    CHECK(seen);
    pc_df_t f; pc_df_init(&f, 0, 2, PC_DF_ROUND, 1); pc_df_t s; pc_df_init(&s, 0, 0, PC_DF_ROUND, 1);
    CHECK(pc_df_speed(&f) > pc_df_speed(&g) && pc_df_speed(&g) > pc_df_speed(&s));
    f.level = 100; CHECK_NEAR(pc_df_speed(&f), 2.0f * 1.5f, 1e-4);              /* capped at 2x */
}
/* an autoplayer that points the shield at the nearest inbound shot */
static void autoplay(pc_df_t *g)
{
    float best = 1e9f;
    for (int i = 0; i < PC_DF_SHOTS; i++) {
        const pc_df_shot_t *s = &g->shot[i];
        if (!s->live || s->back || s->past) continue;
        float r = len(s->x, s->y);
        if (r < best) { best = r; g->deg = deg_of(s->x, s->y); }
    }
}
static void test_autoplayer(void)
{
    for (int shape = 0; shape < 2; shape++)
        for (int rule = 0; rule < PC_DF_RULES; rule++)
            for (int sp = 0; sp < PC_DF_SPEEDS; sp++) {
                pc_df_t g;
                pc_df_init(&g, rule, sp, shape, 1234u + (uint32_t)rule);
                pc_df_start(&g);
                for (int i = 0; i < 3000; i++) { autoplay(&g); pc_df_step(&g); }
                CHECK(g.bounces >= 30);
                CHECK(g.state == PC_DF_PLAY);
                CHECK(g.level >= 2);
            }
}
static void test_determinism(void)
{
    pc_df_t a, b, c;
    pc_df_init(&a, 2, 1, 0, 99); pc_df_init(&b, 2, 1, 0, 99); pc_df_init(&c, 2, 1, 0, 100);
    pc_df_start(&a); pc_df_start(&b); pc_df_start(&c);
    for (int i = 0; i < 400; i++) { autoplay(&a); autoplay(&b); autoplay(&c); pc_df_step(&a); pc_df_step(&b); pc_df_step(&c); }
    CHECK(!memcmp(&a, &b, sizeof a));
    CHECK(memcmp(a.shot, c.shot, sizeof a.shot) || a.score != c.score || a.rng != c.rng);
}
static void test_no_tunnel(void)
{
    pc_df_t g;
    field(&g, PC_DF_CLASSIC, PC_DF_ROUND, 270);
    shot(&g, 0, 0, -70, 0, 6.0f);                                              /* far faster than a game gets: still caught */
    pc_df_step(&g);
    CHECK(g.shot[0].back);
    field(&g, PC_DF_CLASSIC, PC_DF_SQUARE, 270);
    shot(&g, 0, 0, -70, 0, 6.0f);
    pc_df_step(&g);
    CHECK(g.shot[0].back);
}

int main(void)
{
    RUN(test_guard_round);
    RUN(test_guard_square);
    RUN(test_segs_square);
    RUN(test_covers);
    RUN(test_bounce_round);
    RUN(test_bounce_square);
    RUN(test_miss);
    RUN(test_gold);
    RUN(test_combo_grows);
    RUN(test_combo_event);
    RUN(test_rebound);
    RUN(test_twin);
    RUN(test_over);
    RUN(test_start);
    RUN(test_spawn);
    RUN(test_autoplayer);
    RUN(test_determinism);
    RUN(test_no_tunnel);
    return 0;
}
