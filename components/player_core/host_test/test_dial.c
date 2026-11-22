// SPDX-License-Identifier: LGPL-2.1-or-later
/* The pure parts of the shared dial (ui_dial, G2): wrap/clamp, distance, haptic grade, the needle's angle, the meter. */
#include "pc/pc_dial.h"
#include "pc_test.h"

static void test_wrap_clamp(void)
{
    CHECK_EQ_INT(pc_dial_wrap(100, 0, 99, true), 0);
    CHECK_EQ_INT(pc_dial_wrap(-1, 0, 99, true), 99);
    CHECK_EQ_INT(pc_dial_wrap(-201, 0, 99, true), 99);
    CHECK_EQ_INT(pc_dial_wrap(250, 0, 99, true), 50);
    CHECK_EQ_INT(pc_dial_wrap(-5, 10, 19, true), 15);
    CHECK_EQ_INT(pc_dial_wrap(150, 0, 99, false), 99);
    CHECK_EQ_INT(pc_dial_wrap(-9, 0, 99, false), 0);
    CHECK_EQ_INT(pc_dial_step(98, 5, 0, 99, true), 3);
    CHECK_EQ_INT(pc_dial_step(2, -5, 0, 99, true), 97);
    CHECK_EQ_INT(pc_dial_step(98, 5, 0, 99, false), 99);
}
static void test_wrapi(void)
{
    CHECK_EQ_INT(pc_wrapi(0, 5), 0);
    CHECK_EQ_INT(pc_wrapi(7, 5), 2);
    CHECK_EQ_INT(pc_wrapi(-1, 5), 4);
    CHECK_EQ_INT(pc_wrapi(-10, 5), 0);
    CHECK_EQ_INT(pc_wrapi(-11, 5), 4);
    CHECK_EQ_INT(pc_wrapi(3, 1), 0);
    CHECK_EQ_INT(pc_wrapi_step(4, 1, 5), 0);
    CHECK_EQ_INT(pc_wrapi_step(0, -1, 5), 4);
    CHECK_EQ_INT(pc_wrapi_step(2, 13, 5), 0);     /* |d| > n */
    CHECK_EQ_INT(pc_wrapi_step(2, -13, 5), 4);
    CHECK_EQ_INT(pc_wrapi_step(-3, -4, 5), 3);    /* negative v and d */
}
static void test_dist(void)
{
    CHECK_EQ_INT(pc_dial_dist(5, 5, 0, 99, true), 0);
    CHECK_EQ_INT(pc_dial_dist(2, 98, 0, 99, true), 4);        /* the short way round */
    CHECK_EQ_INT(pc_dial_dist(98, 2, 0, 99, true), 4);
    CHECK_EQ_INT(pc_dial_dist(0, 50, 0, 99, true), 50);
    CHECK_EQ_INT(pc_dial_dist(0, 51, 0, 99, true), 49);
    CHECK_EQ_INT(pc_dial_dist(2, 98, 0, 99, false), 96);      /* a gauge: no way round */
}
static void test_grade(void)
{
    CHECK_EQ_INT(pc_dial_grade(0, 12), 3);
    CHECK_EQ_INT(pc_dial_grade(1, 12), 2);
    CHECK_EQ_INT(pc_dial_grade(4, 12), 2);
    CHECK_EQ_INT(pc_dial_grade(5, 12), 1);
    CHECK_EQ_INT(pc_dial_grade(12, 12), 1);
    CHECK_EQ_INT(pc_dial_grade(13, 12), 0);
    CHECK_EQ_INT(pc_dial_grade(1, 2), 2);                      /* a small reach: the near band is at least 1 */
    CHECK_EQ_INT(pc_dial_grade(2, 2), 1);
    CHECK_EQ_INT(pc_dial_grade(1, 0), 0);                      /* no reach: only the detent */
    CHECK_EQ_INT(pc_dial_grade(0, 0), 3);
    for (int d = 1; d < 30; d++) CHECK(pc_dial_grade(d, 12) >= pc_dial_grade(d + 1, 12));   /* closer is never weaker */
}
static void test_angle(void)
{
    CHECK_EQ_INT(pc_dial_angle10(0, 0, 99, true, 270, 360), 2700);          /* a lock: 0 at 12 o'clock, a full turn is 100 steps */
    CHECK_EQ_INT(pc_dial_angle10(25, 0, 99, true, 270, 360), 3600);
    CHECK_EQ_INT(pc_dial_angle10(99, 0, 99, true, 270, 360), 2700 + 3564);
    CHECK_EQ_INT(pc_dial_angle10(0, 0, 100, false, 135, 270), 1350);        /* a gauge: lo and hi are the ends */
    CHECK_EQ_INT(pc_dial_angle10(100, 0, 100, false, 135, 270), 4050);
    CHECK_EQ_INT(pc_dial_angle10(50, 0, 100, false, 135, 270), 2700);
    CHECK_EQ_INT(pc_dial_angle10(500, 0, 100, false, 135, 270), 4050);      /* clamped */
}
static void test_meter(void)
{
    pc_meter_t m;
    pc_meter_init(&m, 0);
    CHECK(!pc_meter_step(&m));                                 /* nothing moves at rest */
    pc_meter_feed(&m, 80);
    int steps = 0, last = 0;
    while (pc_meter_step(&m) && steps < 500) { steps++; CHECK(pc_meter_shown(&m) >= last); last = pc_meter_shown(&m); }   /* smooth: up, never past the target */
    CHECK(steps > 5 && steps < 200);
    CHECK_EQ_INT(pc_meter_shown(&m), 80);
    CHECK_EQ_INT(pc_meter_peak(&m), 80);
    pc_meter_feed(&m, 20);                                     /* down: the peak holds, then falls */
    for (int i = 0; i < 20; i++) pc_meter_step(&m);
    CHECK(pc_meter_shown(&m) < 80);
    CHECK_EQ_INT(pc_meter_peak(&m), 80);                       /* still inside the hold */
    int held = 0;
    for (int i = 0; i < 400 && pc_meter_step(&m); i++) held = i;
    CHECK(held > 10);
    CHECK_EQ_INT(pc_meter_shown(&m), 20);
    CHECK_EQ_INT(pc_meter_peak(&m), 20);                       /* the peak came down to the value and the meter rests */
    CHECK(!pc_meter_step(&m));
    pc_meter_feed(&m, 30);
    pc_meter_step(&m);
    CHECK(pc_meter_peak(&m) >= pc_meter_shown(&m));            /* the peak is never under the value */
}
static void test_meter_config(void)
{
    pc_meter_t slow, fast, np;
    pc_meter_init(&slow, 0); pc_meter_config(&slow, 4, true);
    pc_meter_init(&fast, 0); pc_meter_config(&fast, 1, true);
    pc_meter_feed(&slow, 100); pc_meter_feed(&fast, 100);
    pc_meter_step(&slow); pc_meter_step(&fast);
    CHECK(pc_meter_shown(&fast) > pc_meter_shown(&slow));            /* a smaller shift is faster */
    CHECK_EQ_INT(pc_meter_shown(&slow), 6);                          /* 1/16 of 100 */
    CHECK_EQ_INT(pc_meter_shown(&fast), 50);
    pc_meter_config(&fast, 0, true);                                 /* 0 = the default, a quarter */
    CHECK_EQ_INT(fast.shift, 2);
    pc_meter_init(&np, 0); pc_meter_config(&np, 0, false);           /* no peak: the marker follows the value and never holds */
    pc_meter_feed(&np, 80);
    while (pc_meter_step(&np)) { }
    pc_meter_feed(&np, 10);
    for (int i = 0; i < 100 && pc_meter_step(&np); i++) CHECK_EQ_INT(np.peak, np.shown);
    CHECK_EQ_INT(pc_meter_peak(&np), 10);
    CHECK(!pc_meter_step(&np));
}
static void test_cue(void)                                       /* the proximity cue: 0 outside the reach, full strength on the target, linear between */
{
    CHECK_EQ_INT(pc_dial_cue(0, 12, 100), 255);
    CHECK_EQ_INT(pc_dial_cue(12, 12, 100), 0);
    CHECK_EQ_INT(pc_dial_cue(30, 12, 100), 0);
    CHECK_EQ_INT(pc_dial_cue(6, 12, 100), 127);
    CHECK_EQ_INT(pc_dial_cue(0, 12, 50), 127);                   /* a weaker strength caps the level */
    CHECK_EQ_INT(pc_dial_cue(0, 12, 0), 0);                      /* strength 0: off */
    CHECK_EQ_INT(pc_dial_cue(0, 0, 100), 0);                     /* no reach: nothing to approach */
    CHECK_EQ_INT(pc_dial_cue(-3, 12, 100), 0);
    CHECK_EQ_INT(pc_dial_cue(0, 12, 250), 255);                  /* strength is clamped to 100 */
    int last = -1;
    for (int d = 11; d >= 0; d--) { int c = pc_dial_cue(d, 12, 70); CHECK(c >= last); last = c; }   /* the closer, the brighter */
}
int main(void)
{
    RUN(test_cue);
    RUN(test_meter_config);
    RUN(test_wrap_clamp);
    RUN(test_wrapi);
    RUN(test_dist);
    RUN(test_grade);
    RUN(test_angle);
    RUN(test_meter);
    return 0;
}
