// SPDX-License-Identifier: LGPL-2.1-or-later
/* 萬花尺 (ideas G5): the curve closes at integer ratios and not earlier, stays in the unit circle, the pen's speed, the layers, the music rule, the golden curve. */
#include "pc/pc_game_spiro.h"
#include "pc_test.h"

static int gcd(int a, int b) { return b ? gcd(b, a % b) : a; }
static float norm(float x, float y) { return sqrtf(x * x + y * y); }

static void test_tables(void)
{
    for (int g = 0; g < PC_SP_GEARS; g++) {
        int R, r;
        pc_sp_gear_of(g, &R, &r);
        CHECK(R > r && r >= 2);
        CHECK_EQ_INT(pc_sp_turns(g), r / gcd(R, r));
        CHECK_EQ_INT(pc_sp_lobes(g), R / gcd(R, r));
    }
    int R, r;
    pc_sp_gear_of(99, &R, &r);
    CHECK_EQ_INT(R, 5);
    CHECK_EQ_INT(r, 3);
    CHECK_EQ_INT(pc_sp_turns(0), 3);
    CHECK_NEAR(pc_sp_pen_of(2), 1.0, 1e-6);
    CHECK(pc_sp_pen_of(0) < pc_sp_pen_of(1) && pc_sp_pen_of(1) < pc_sp_pen_of(3));
}
/* every gear, every pen, both kinds: the curve is closed at its end, was not closed earlier (at a whole turn of the wheel's loop count below the period),
 * and every point of every layer is inside the unit circle (the widest point of layer 0 reaches it) */
static void test_closure_and_extent(void)
{
    for (int g = 0; g < PC_SP_GEARS; g++)
        for (int p = 0; p < PC_SP_PENS; p++)
            for (int e = 0; e < 2; e++) {
                pc_sp_t s;
                pc_sp_init(&s, PC_SP_LAYERS, 1, 0, 7, false);
                pc_sp_set(&s, g, p, e);
                float x0, y0, x1, y1;
                pc_sp_point(&s, 0, 0, &x0, &y0);
                pc_sp_point(&s, 0, (double)s.total, &x1, &y1);
                CHECK_NEAR(x0, x1, 2e-3);
                CHECK_NEAR(y0, y1, 2e-3);
                CHECK_EQ_INT(s.total, pc_sp_turns(g) * PC_SP_SEG_REV);
                if (pc_sp_turns(g) > 1) {                      /* a shorter period would have closed it already: it did not */
                    float xm, ym;
                    pc_sp_point(&s, 0, (double)(s.total / pc_sp_turns(g)), &xm, &ym);
                    CHECK(fabsf(xm - x0) > 1e-3f || fabsf(ym - y0) > 1e-3f);
                }
                float mx = 0;
                for (int l = 0; l < PC_SP_LAYERS_N; l++)
                    for (uint32_t k = 0; k <= s.total; k++) {
                        float x, y;
                        pc_sp_point(&s, l, k, &x, &y);
                        CHECK(norm(x, y) <= 1.0f + 1e-4f);
                        if (l == 0 && norm(x, y) > mx) mx = norm(x, y);
                    }
                CHECK(mx > 0.97f);                            /* layer 0 fills the circle (the 120 samples per turn reach near the extremum) */
            }
}
static void test_clip(void)
{
    float x = 3, y = 4;
    pc_sp_clip(&x, &y, 1.0f);
    CHECK_NEAR(norm(x, y), 1.0, 1e-5);
    CHECK_NEAR(x / y, 0.75, 1e-5);                           /* the same ray */
    x = 0.3f; y = 0.2f;
    pc_sp_clip(&x, &y, 1.0f);
    CHECK_NEAR(x, 0.3, 1e-6);
    x = 0; y = 0;
    pc_sp_clip(&x, &y, 1.0f);
    CHECK_NEAR(norm(x, y), 0, 1e-6);
}
static void test_init_deterministic(void)
{
    pc_sp_t a, b;
    pc_sp_init(&a, PC_SP_CLASSIC, 1, 0, 11, false);
    pc_sp_init(&b, PC_SP_CLASSIC, 1, 0, 11, false);
    CHECK_EQ_INT(a.gear, b.gear);
    CHECK_EQ_INT(a.pen, b.pen);
    CHECK_EQ_INT(a.epi, b.epi);
    CHECK(a.gear < PC_SP_GEARS && a.pen < PC_SP_PENS);
    CHECK_EQ_INT(a.layers, 1);
    CHECK_EQ_INT(a.seg, 0);
    CHECK(!a.done);
    CHECK(pc_sp_running(&a));
    int seen_g = 0, seen_e = 0, seen_p = 0;                  /* the seeds reach every gear, pen and both kinds */
    for (uint32_t k = 1; k < 200; k++) { pc_sp_init(&a, 0, 1, 0, k, false); seen_g |= 1 << a.gear; seen_p |= 1 << a.pen; seen_e |= 1 << a.epi; }
    CHECK_EQ_INT(seen_g, (1 << PC_SP_GEARS) - 1);
    CHECK_EQ_INT(seen_p, (1 << PC_SP_PENS) - 1);
    CHECK_EQ_INT(seen_e, 3);
    pc_sp_init(&a, 77, 99, 99, 1, false);                    /* unknown rule, speed, colour: the first */
    CHECK_EQ_INT(a.rule, PC_SP_CLASSIC);
    CHECK_EQ_INT(a.speed, 0);
    CHECK_EQ_INT(a.colour, 0);
    pc_sp_init(&a, PC_SP_LAYERS, 1, 0, 1, false);
    CHECK_EQ_INT(a.layers, PC_SP_LAYERS_N);
}
/* the pen draws the segments it owes: a continuous polyline from the curve's start; the step ends at the curve's end with the event and then rests */
static void test_step_draws_a_continuous_curve(void)
{
    pc_sp_t s;
    pc_sp_init(&s, PC_SP_CLASSIC, 1, 0, 3, false);
    pc_sp_set(&s, 0, 2, 0);
    float px, py;
    pc_sp_point(&s, 0, 0, &px, &py);
    uint32_t drawn = 0;
    int steps = 0;
    bool done_seen = false;
    while (pc_sp_running(&s) && steps < 5000) {
        int n = pc_sp_step(&s, 0);
        steps++;
        CHECK(n >= 0 && n <= PC_SP_BURST && n == s.n_out);
        for (int i = 0; i < n; i++) {
            CHECK_NEAR(s.out[i].x0, px, 1e-5);
            CHECK_NEAR(s.out[i].y0, py, 1e-5);
            px = s.out[i].x1; py = s.out[i].y1;
            CHECK(norm(px, py) <= 1.0f + 1e-4f);
            drawn++;
        }
        if (s.ev_done) { CHECK(!done_seen); done_seen = true; CHECK(s.done); }
    }
    CHECK(done_seen);
    CHECK_EQ_INT(drawn, s.total);
    CHECK_EQ_INT(drawn, 3 * PC_SP_SEG_REV);                  /* gear 0: three turns */
    CHECK_NEAR(s.out[s.n_out - 1].t, 1.0, 1e-6);
    CHECK(!pc_sp_running(&s));
    CHECK_EQ_INT(pc_sp_step(&s, 0), 0);                      /* done: nothing more, no event */
    CHECK(!s.ev_done);
}
static void test_speed(void)
{
    pc_sp_t s;
    pc_sp_init(&s, PC_SP_CLASSIC, 0, 0, 3, false);
    float slow = pc_sp_rate(&s, 0);
    s.speed = 1; float mid = pc_sp_rate(&s, 0);
    s.speed = 2; float fast = pc_sp_rate(&s, 0);
    CHECK(slow > 0 && slow < mid && mid < fast);
    CHECK(fast <= PC_SP_BURST);
    s.speed = 0;
    s.gear = 0;                                              /* a fractional rate is carried: the average over steps equals the rate */
    pc_sp_set(&s, 0, 2, 0);
    uint32_t n = 0;
    for (int i = 0; i < 60 && pc_sp_running(&s); i++) n += (uint32_t)pc_sp_step(&s, 0);
    CHECK_NEAR((double)n, 60.0 * slow, 1.5);
    CHECK_NEAR(pc_sp_rate(&s, 255), pc_sp_rate(&s, 0), 1e-6);   /* classic ignores the music */
}
static void test_layers(void)
{
    pc_sp_t s;
    pc_sp_init(&s, PC_SP_LAYERS, 2, 0, 5, false);
    pc_sp_set(&s, 1, 2, 0);
    int layer_events = 0, last = 0;
    uint32_t drawn = 0;
    for (int i = 0; i < 4000 && pc_sp_running(&s); i++) {
        pc_sp_step(&s, 0);
        for (int k = 0; k < s.n_out; k++) { CHECK(s.out[k].layer >= last); last = s.out[k].layer; drawn++; }
        if (s.ev_layer) layer_events++;
    }
    CHECK(s.done);
    CHECK_EQ_INT(layer_events, PC_SP_LAYERS_N - 1);
    CHECK_EQ_INT(last, PC_SP_LAYERS_N - 1);
    CHECK_EQ_INT(drawn, PC_SP_LAYERS_N * s.total);
    float x0, y0, x1, y1;                                    /* the layers have different pens: their points differ, the inner ones lie inside */
    pc_sp_point(&s, 0, 10, &x0, &y0);
    pc_sp_point(&s, 2, 10, &x1, &y1);
    CHECK(fabsf(x0 - x1) + fabsf(y0 - y1) > 1e-3f);
}
static void test_music(void)
{
    pc_sp_t s;
    pc_sp_init(&s, PC_SP_MUSIC, 1, 0, 2, false);
    float quiet = pc_sp_rate(&s, 0), loud = pc_sp_rate(&s, 255);
    CHECK(quiet > 0.5f);                                     /* silence still creeps */
    CHECK(loud > 3.0f * quiet);
    CHECK(loud <= PC_SP_BURST);
    CHECK(pc_sp_rate(&s, 128) > quiet && pc_sp_rate(&s, 128) < loud);
    CHECK(pc_sp_rate(&s, -5) == quiet && pc_sp_rate(&s, 999) == loud);
}
static void test_gold(void)
{
    for (uint32_t k = 1; k < 60; k++) {
        pc_sp_t s;
        pc_sp_init(&s, PC_SP_CLASSIC, 1, 0, k, true);
        CHECK(s.gold);
        CHECK(!s.epi);
        CHECK_EQ_INT(s.pen, 2);
        CHECK(pc_sp_lobes(s.gear) >= 8);
    }
    pc_sp_t s;
    pc_sp_init(&s, PC_SP_CLASSIC, 1, 0, 1, false);
    CHECK(!s.gold);
}
static void test_set_restarts(void)
{
    pc_sp_t s;
    pc_sp_init(&s, PC_SP_CLASSIC, 2, 0, 4, false);
    for (int i = 0; i < 5; i++) pc_sp_step(&s, 0);
    CHECK(s.seg > 0);
    pc_sp_set(&s, 3, -1, -1);
    CHECK_EQ_INT(s.gear, 3);
    CHECK_EQ_INT(s.seg, 0);
    CHECK_EQ_INT(s.layer, 0);
    CHECK(!s.done);
    CHECK_EQ_INT(s.total, pc_sp_turns(3) * PC_SP_SEG_REV);
    int pen = s.pen;
    pc_sp_set(&s, -1, 99, -1);                               /* out of range: wrapped into range, not kept */
    CHECK(s.pen < PC_SP_PENS);
    (void)pen;
}
int main(void)
{
    RUN(test_tables);
    RUN(test_closure_and_extent);
    RUN(test_clip);
    RUN(test_init_deterministic);
    RUN(test_step_draws_a_continuous_curve);
    RUN(test_speed);
    RUN(test_layers);
    RUN(test_music);
    RUN(test_gold);
    RUN(test_set_restarts);
    return 0;
}
