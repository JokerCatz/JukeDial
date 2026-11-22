// SPDX-License-Identifier: LGPL-2.1-or-later
#include <math.h>
#include "pc/pc_imucal.h"
#include "pc_test.h"

static int run(pc_imucal_t *c, float ax, float ay, float az, float gx, int n)
{
    int got = 0;
    for (int i = 0; i < n; i++) got += pc_imucal_feed(c, ax, ay, az, gx, 0, 0, 20);
    return got;
}

static void test_learns_when_still(void)
{
    pc_imucal_t c; pc_imucal_init(&c, 1.0f);
    CHECK(!c.learned);
    CHECK(run(&c, 0.0f, 0.0f, 0.8935f, 0.5f, 49) == 0);         /* under a second: nothing yet */
    CHECK(run(&c, 0.0f, 0.0f, 0.8935f, 0.5f, 3) == 1);
    CHECK(c.learned);
    CHECK_NEAR(c.scale, 1.0f / 0.8935f, 1e-4);
}
static void test_any_orientation(void)
{
    pc_imucal_t c; pc_imucal_init(&c, 1.0f);
    const float m = 0.9f;
    run(&c, m * 0.6f, m * 0.0f, m * 0.8f, 0, 60);                /* tilted: the length is what counts */
    CHECK(c.learned); CHECK_NEAR(c.scale, 1.0f / m, 1e-4);
}
static void test_moving_never_learns(void)
{
    pc_imucal_t c; pc_imucal_init(&c, 1.0f);
    CHECK(run(&c, 0, 0, 0.9f, 20.0f, 200) == 0);                 /* gyro busy */
    CHECK(!c.learned);
    for (int i = 0; i < 200; i++) pc_imucal_feed(&c, 0, 0, (i & 1) ? 0.8f : 1.0f, 0, 0, 0, 20);   /* |a| not steady */
    CHECK(!c.learned);
    run(&c, 0, 0, 0.9f, 0, 30);                                  /* a moving stretch resets the window */
    pc_imucal_feed(&c, 0, 0, 0.9f, 50.0f, 0, 0, 20);
    CHECK(run(&c, 0, 0, 0.9f, 0, 40) == 0);
}
static void test_out_of_range_kept(void)
{
    pc_imucal_t c; pc_imucal_init(&c, 1.1f);
    run(&c, 0, 0, 0.5f, 0, 80);                                  /* 1/0.5 = 2.0 > 1.25 */
    CHECK(!c.learned); CHECK_NEAR(c.scale, 1.1f, 1e-6); CHECK(c.rejected >= 1);
    run(&c, 0, 0, 1.5f, 0, 80);                                  /* 0.667 < 0.8 */
    CHECK(!c.learned); CHECK_NEAR(c.scale, 1.1f, 1e-6);
    run(&c, 0, 0, 0.98f, 0, 80);                                 /* then a good one is taken */
    CHECK(c.learned); CHECK_NEAR(c.scale, 1.0f / 0.98f, 1e-4);
}
static void test_relearns(void)
{
    pc_imucal_t c; pc_imucal_init(&c, 1.0f);
    run(&c, 0, 0, 0.9f, 0, 60);
    run(&c, 0, 0, 0.95f, 0, 60);
    CHECK_NEAR(c.scale, 1.0f / 0.95f, 1e-3);
}
static void test_rel(void)
{
    float x, y;
    pc_imucal_rel(0.5f, 0.0f, 0.5f, 0.0f, &x, &y);               /* the held pose is level */
    CHECK_NEAR(x, 0, 1e-6); CHECK_NEAR(y, 0, 1e-6);
    pc_imucal_rel(0.55f, 0.02f, 0.5f, 0.0f, &x, &y);               /* a little past it: under the dead zone (0.08) */
    CHECK_NEAR(x, 0, 1e-6);
    pc_imucal_rel(0.8f, 0.0f, 0.5f, 0.0f, &x, &y);
    CHECK_NEAR(x, 0.3f, 1e-4); CHECK_NEAR(y, 0, 1e-4);
    pc_imucal_rel(-1.0f, 0.0f, 1.0f, 0.0f, &x, &y);              /* opposite full tilt: clamped to 1 */
    CHECK_NEAR(x, -1.0f, 1e-4);
    pc_imucal_rel(0.3f, 0.4f, 0, 0, &x, &y);                     /* no neutral: the absolute vector */
    CHECK_NEAR(x, 0.3f, 1e-4); CHECK_NEAR(y, 0.4f, 1e-4);
    CHECK(pc_imucal_still(1.0f)); CHECK(!pc_imucal_still(8.0f));
}
int main(void)
{
    RUN(test_learns_when_still); RUN(test_any_orientation); RUN(test_moving_never_learns); RUN(test_out_of_range_kept); RUN(test_relearns); RUN(test_rel);
    return 0;
}
