// SPDX-License-Identifier: LGPL-2.1-or-later
/* 氛圍頁 / Ambient scenes (ideas G5): determinism, bounds, the scenes' own rules (merge and split, feeding, flames, the breath), energy, speed, the rare events. */
#include "pc/pc_ambient.h"
#include "pc_test.h"

static bool has(const pc_am_t *g, int kind) { for (int i = 0; i < g->n_ev; i++) if (g->ev[i].kind == kind) return true; return false; }
static void run(pc_am_t *g, int n, float e) { for (int i = 0; i < n; i++) pc_am_step(g, e); }
static float area(const pc_am_t *g)
{
    float a = 0;
    for (int i = 0; i < PC_AM_ITEMS; i++) if (g->it[i].kind == PC_AM_K_BLOB) a += g->it[i].r * g->it[i].r;
    return a;
}
static pc_am_item_t *first(pc_am_t *g, int kind) { for (int i = 0; i < PC_AM_ITEMS; i++) if (g->it[i].kind == kind) return &g->it[i]; return NULL; }

static void test_init_deterministic(void)
{
    pc_am_t a, b;
    for (int s = 0; s < PC_AM_SCENES; s++) {
        pc_am_init(&a, s, 7, 1); pc_am_init(&b, s, 7, 1);
        run(&a, 300, 0.3f); run(&b, 300, 0.3f);
        CHECK(!memcmp(&a, &b, sizeof a));
    }
    pc_am_init(&a, 99, 1, 1);
    CHECK_EQ_INT(a.scene, PC_AM_LAVA);                  /* an unknown scene is the lava lamp */
    pc_am_init(&a, PC_AM_LAVA, 1, 0);
    CHECK_NEAR(a.speed, 1.0, 0.0001);                   /* no speed: normal */
    CHECK_EQ_INT(pc_am_count(&a, PC_AM_K_BLOB), 6);
    pc_am_init(&a, PC_AM_FISH, 1, 1);
    CHECK_EQ_INT(pc_am_count(&a, PC_AM_K_FISH), 5);
}
static void test_bounds_and_prims(void)
{
    pc_am_t g;
    pc_am_prim_t p[PC_AM_MAXPRIM];
    for (int s = 0; s < PC_AM_SCENES; s++) {
        pc_am_init(&g, s, 3 + (uint32_t)s, 1);
        for (int t = 0; t < 6000; t++) {
            pc_am_step(&g, (t / 300) % 2 ? 1.0f : 0.0f);
            if (t % 37 == 0) pc_am_poke(&g, (float)((t * 7) % 140 - 70), (float)((t * 13) % 140 - 70));
            if (t % 900 == 450) pc_am_force_rare(&g);
            const int n = pc_am_draw(&g, p, PC_AM_MAXPRIM);
            CHECK(n >= 1 && n <= PC_AM_MAXPRIM);
            for (int i = 0; i < n; i++) {
                CHECK(p[i].tone < PC_AM_TONES && p[i].a > 0 && p[i].a < 130);
                CHECK(isfinite(p[i].x) && isfinite(p[i].y));
            }
        }
    }
    CHECK_EQ_INT(pc_am_draw(&g, p, 2), 2);              /* the caller's cap is kept */
}
static void test_lava(void)
{
    pc_am_t g;
    pc_am_init(&g, PC_AM_LAVA, 5, 1);
    for (int t = 0; t < 3000; t++) {
        pc_am_step(&g, 0);
        for (int i = 0; i < PC_AM_ITEMS; i++) if (g.it[i].kind == PC_AM_K_BLOB) CHECK(hypotf(g.it[i].x, g.it[i].y) <= PC_AM_FIELD - g.it[i].r + 0.5f);
        CHECK(pc_am_count(&g, PC_AM_K_BLOB) >= 1 && pc_am_count(&g, PC_AM_K_BLOB) <= 10);
    }
    float lo = 999, hi = -999;                          /* the wax travels: rises and sinks */
    pc_am_item_t *b = first(&g, PC_AM_K_BLOB);
    for (int t = 0; t < 1500; t++) { pc_am_step(&g, 0); b = first(&g, PC_AM_K_BLOB); if (b->y < lo) lo = b->y; if (b->y > hi) hi = b->y; }
    CHECK(hi - lo > 40);
    /* a tap splits the nearest blob in two (the area is kept); two blobs that touch merge into one */
    pc_am_init(&g, PC_AM_LAVA, 5, 1);
    b = first(&g, PC_AM_K_BLOB);
    b->r = 16;
    const int n0 = pc_am_count(&g, PC_AM_K_BLOB);
    const float a0 = area(&g);
    pc_am_poke(&g, b->x, b->y);
    CHECK(has(&g, PC_AM_EV_SPLIT));
    CHECK_EQ_INT(pc_am_count(&g, PC_AM_K_BLOB), n0 + 1);
    CHECK_NEAR(area(&g) / a0, 1.0, 0.1);
    for (int i = 0; i < PC_AM_ITEMS; i++) g.it[i].kind = PC_AM_K_NONE;
    pc_am_item_t *x = &g.it[0], *y = &g.it[1];
    *x = (pc_am_item_t){ .kind = PC_AM_K_BLOB, .x = 0, .y = 0, .r = 10, .tone = 1 };
    *y = (pc_am_item_t){ .kind = PC_AM_K_BLOB, .x = 3, .y = 0, .r = 10, .tone = 2 };
    g.rare_gap = 100000;
    pc_am_step(&g, 0);
    CHECK_EQ_INT(pc_am_count(&g, PC_AM_K_BLOB) >= 1, 1);
    CHECK(x->kind == PC_AM_K_NONE || y->kind == PC_AM_K_NONE);
    CHECK_NEAR(area(&g), 200, 1);
    /* a small blob only gets a kick */
    for (int i = 0; i < PC_AM_ITEMS; i++) g.it[i].kind = PC_AM_K_NONE;
    g.it[0] = (pc_am_item_t){ .kind = PC_AM_K_BLOB, .r = 6, .tone = 1 };
    pc_am_poke(&g, 0, 0);
    CHECK_EQ_INT(pc_am_count(&g, PC_AM_K_BLOB), 1);
    CHECK(g.it[0].vy < 0);
}
static void test_lava_gold(void)
{
    pc_am_t g;
    pc_am_init(&g, PC_AM_LAVA, 9, 1);
    CHECK(pc_am_force_rare(&g));
    CHECK(has(&g, PC_AM_EV_GOLD_SEEN));
    CHECK(!pc_am_force_rare(&g));                       /* one at a time */
    pc_am_item_t *gb = NULL;
    for (int i = 0; i < PC_AM_ITEMS; i++) if (g.it[i].kind == PC_AM_K_BLOB && g.it[i].tone == 3) gb = &g.it[i];
    CHECK(gb != NULL);
    pc_am_poke(&g, gb->x, gb->y);
    CHECK(has(&g, PC_AM_EV_GOLD_CAUGHT));
    int small = 0;
    for (int i = 0; i < PC_AM_ITEMS; i++) small += g.it[i].kind == PC_AM_K_BLOB && g.it[i].tone == 3 && g.it[i].r <= 5;
    CHECK_EQ_INT(small, 3);
    run(&g, 600, 0);                                    /* the gold wears off */
    for (int i = 0; i < PC_AM_ITEMS; i++) if (g.it[i].kind == PC_AM_K_BLOB) CHECK(g.it[i].tone != 3);
}
static void test_fish(void)
{
    pc_am_t g;
    pc_am_init(&g, PC_AM_FISH, 2, 1);
    run(&g, 900, 0);
    for (int i = 0; i < PC_AM_ITEMS; i++) if (g.it[i].kind == PC_AM_K_FISH) CHECK(hypotf(g.it[i].x, g.it[i].y) <= 0.86f * PC_AM_FIELD + 0.5f);
    CHECK(pc_am_count(&g, PC_AM_K_BUBBLE) > 0 && pc_am_count(&g, PC_AM_K_BUBBLE) <= 8);
    /* a tap on empty water drops food; a fish eats it and grows; a tap on a fish makes it dart */
    pc_am_item_t *f = first(&g, PC_AM_K_FISH);
    pc_am_poke(&g, 0, -60);
    CHECK(has(&g, PC_AM_EV_FEED));
    CHECK_EQ_INT(pc_am_count(&g, PC_AM_K_FOOD), 1);
    float r0 = 0;
    for (int i = 0; i < PC_AM_ITEMS; i++) if (g.it[i].kind == PC_AM_K_FISH) r0 += g.it[i].r;
    bool ate = false;
    for (int t = 0; t < 600 && !ate; t++) { pc_am_step(&g, 0); ate = has(&g, PC_AM_EV_EAT); }
    CHECK(ate);
    CHECK_EQ_INT(pc_am_count(&g, PC_AM_K_FOOD), 0);
    float r1 = 0;
    for (int i = 0; i < PC_AM_ITEMS; i++) if (g.it[i].kind == PC_AM_K_FISH) r1 += g.it[i].r;
    CHECK(r1 > r0);
    f = first(&g, PC_AM_K_FISH);
    pc_am_poke(&g, f->x, f->y);
    CHECK(has(&g, PC_AM_EV_DART));
    CHECK(f->life > 0);
    for (int k = 0; k < 8; k++) pc_am_poke(&g, -50 + 12 * (float)k, 55);    /* the food is capped */
    CHECK(pc_am_count(&g, PC_AM_K_FOOD) <= 4);
    pc_am_init(&g, PC_AM_FISH, 2, 1);                   /* the golden fish crosses and is caught */
    CHECK(pc_am_force_rare(&g));
    CHECK(!pc_am_force_rare(&g));
    run(&g, 90, 0);
    pc_am_item_t *gf = first(&g, PC_AM_K_GOLDFISH);
    CHECK(gf != NULL && gf->x > -88);
    pc_am_poke(&g, gf->x, gf->y);
    CHECK(has(&g, PC_AM_EV_GOLD_CAUGHT));
    CHECK_EQ_INT(pc_am_count(&g, PC_AM_K_GOLDFISH), 0);
    pc_am_init(&g, PC_AM_FISH, 2, 1);                   /* or it swims out by itself */
    pc_am_force_rare(&g);
    run(&g, 600, 0);
    CHECK_EQ_INT(pc_am_count(&g, PC_AM_K_GOLDFISH), 0);
}
static void test_fire(void)
{
    pc_am_t g, h;
    pc_am_init(&g, PC_AM_FIRE, 4, 1);
    run(&g, 90, 0);
    const int calm = pc_am_count(&g, PC_AM_K_FLAME);
    CHECK(calm > 10);
    pc_am_init(&h, PC_AM_FIRE, 4, 1);
    run(&h, 90, 1.0f);
    CHECK(pc_am_count(&h, PC_AM_K_FLAME) > calm);       /* music: more flames */
    pc_am_poke(&g, 10, 10);
    CHECK(has(&g, PC_AM_EV_FLARE));
    CHECK(g.flare > 0.9f);
    run(&g, 400, 0);
    CHECK(g.flare < 0.01f);
    CHECK(pc_am_count(&g, PC_AM_K_FLAME) < PC_AM_ITEMS && pc_am_count(&g, PC_AM_K_EMBER) <= 14);
    for (int t = 0; t < 2000; t++) {                    /* every flame ages out and stays over the logs' side of the field */
        pc_am_step(&g, 0);
        for (int i = 0; i < PC_AM_ITEMS; i++) if (g.it[i].kind == PC_AM_K_FLAME) CHECK(g.it[i].age < g.it[i].life + 0.001f && g.it[i].x > -40 && g.it[i].x < 40);
    }
    pc_am_init(&g, PC_AM_FIRE, 4, 1);
    CHECK(pc_am_force_rare(&g));
    CHECK(has(&g, PC_AM_EV_SPARKS));
    CHECK(pc_am_count(&g, PC_AM_K_EMBER) >= 10);
    pc_am_prim_t p[PC_AM_MAXPRIM];
    pc_am_init(&g, PC_AM_FIRE, 4, 1);
    run(&g, 60, 0);
    const int n = pc_am_draw(&g, p, PC_AM_MAXPRIM);
    CHECK(p[0].shape == PC_AM_RECT && p[1].shape == PC_AM_RECT);   /* the logs first */
    int last_tone = -1;
    for (int i = 2; i < n; i++) if (p[i].shape == PC_AM_BALL && p[i].a > 1.5f) { CHECK(p[i].tone >= last_tone || p[i].opa < 255); last_tone = p[i].tone; }
}
static void test_night(void)
{
    pc_am_t g;
    pc_am_prim_t p[PC_AM_MAXPRIM];
    pc_am_init(&g, PC_AM_NIGHT, 1, 1);
    float lo = 999, hi = -999;
    for (int t = 0; t < 400; t++) {                     /* the breath: the glow grows and shrinks, by the step count */
        pc_am_step(&g, 0);
        pc_am_draw(&g, p, PC_AM_MAXPRIM);
        if (p[0].a < lo) lo = p[0].a;
        if (p[0].a > hi) hi = p[0].a;
    }
    CHECK(hi - lo > 5 && hi < PC_AM_FIELD);
    pc_am_poke(&g, 0, 0);
    CHECK(has(&g, PC_AM_EV_PULSE));
    pc_am_draw(&g, p, PC_AM_MAXPRIM);
    const float with_pulse = p[0].a;
    run(&g, 200, 0);
    pc_am_draw(&g, p, PC_AM_MAXPRIM);
    CHECK(g.pulse < 0.01f && with_pulse > 0);
    CHECK(pc_am_force_rare(&g));
    pc_am_item_t *ff = first(&g, PC_AM_K_FIREFLY);
    CHECK(ff != NULL);
    pc_am_poke(&g, ff->x, ff->y);
    CHECK(has(&g, PC_AM_EV_BLINK));
    run(&g, 600, 0);
    CHECK_EQ_INT(pc_am_count(&g, PC_AM_K_FIREFLY), 0);  /* it drifts away */
    /* the same breath at another speed: slower speed, fewer cycles */
    pc_am_t s;
    pc_am_init(&s, PC_AM_NIGHT, 1, 0.5f);
    pc_am_init(&g, PC_AM_NIGHT, 1, 2.0f);
    run(&s, 100, 0); run(&g, 100, 0);
    CHECK(g.phase > 3.5f * s.phase);
}
static void test_energy_and_speed(void)
{
    pc_am_t a, b;
    pc_am_prim_t p[PC_AM_MAXPRIM], q[PC_AM_MAXPRIM];
    pc_am_init(&a, PC_AM_NIGHT, 1, 1); pc_am_init(&b, PC_AM_NIGHT, 1, 1);
    run(&a, 5, 0); run(&b, 5, 1);
    pc_am_draw(&a, p, PC_AM_MAXPRIM); pc_am_draw(&b, q, PC_AM_MAXPRIM);
    CHECK(q[0].a > p[0].a);                             /* music: a stronger glow */
    pc_am_init(&a, PC_AM_LAVA, 1, 1); pc_am_init(&b, PC_AM_LAVA, 1, 1);
    pc_am_draw(&a, p, PC_AM_MAXPRIM); pc_am_draw(&b, q, PC_AM_MAXPRIM);
    pc_am_step(&b, 1.0f);
    pc_am_draw(&b, q, PC_AM_MAXPRIM);
    pc_am_draw(&a, p, PC_AM_MAXPRIM);
    CHECK(q[0].a > p[0].a);                             /* ... and bigger blobs */
    pc_am_init(&a, PC_AM_FISH, 8, 0.5f); pc_am_init(&b, PC_AM_FISH, 8, 2.0f);
    run(&a, 60, 0); run(&b, 60, 0);
    float da = 0, db = 0;
    pc_am_t c;
    pc_am_init(&c, PC_AM_FISH, 8, 1);
    for (int i = 0; i < PC_AM_ITEMS; i++) if (c.it[i].kind == PC_AM_K_FISH) { da += hypotf(a.it[i].x - c.it[i].x, a.it[i].y - c.it[i].y); db += hypotf(b.it[i].x - c.it[i].x, b.it[i].y - c.it[i].y); }
    CHECK(db > da);                                     /* the speed scales the motion */
}
static void test_rare_by_itself(void)
{
    pc_am_t g;
    pc_am_init(&g, PC_AM_FISH, 11, 1);
    bool seen = false;
    for (int t = 0; t < 30000 && !seen; t++) { pc_am_step(&g, 0); seen = has(&g, PC_AM_EV_GOLD_SEEN); }
    CHECK(seen);                                        /* a rare event comes by itself, but not in the first seconds */
    pc_am_init(&g, PC_AM_FISH, 11, 1);
    run(&g, 250, 0);
    CHECK_EQ_INT(pc_am_count(&g, PC_AM_K_GOLDFISH), 0);
}
int main(void)
{
    RUN(test_init_deterministic);
    RUN(test_bounds_and_prims);
    RUN(test_lava);
    RUN(test_lava_gold);
    RUN(test_fish);
    RUN(test_fire);
    RUN(test_night);
    RUN(test_energy_and_speed);
    RUN(test_rare_by_itself);
    return 0;
}
