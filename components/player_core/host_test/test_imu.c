// SPDX-License-Identifier: LGPL-2.1-or-later
#include <math.h>
#include "pc/pc_imu.h"
#include "pc_test.h"

static void settle(pc_imu_t *s, float ax, float ay, float az)
{
    for (int i = 0; i < 60; i++) pc_imu_feed(s, ax, ay, az, 0, 0, 0);
}

static void test_flat_is_zero(void)
{
    pc_imu_t s; pc_imu_init(&s, false, 1, 1);
    settle(&s, 0.0f, 0.0f, 1.0f);
    float x, y; pc_imu_vector(&s, &x, &y);
    CHECK_NEAR(x, 0, 1e-4); CHECK_NEAR(y, 0, 1e-4);
    settle(&s, 0.01f, -0.02f, 1.0f);                       /* under the dead zone */
    pc_imu_vector(&s, &x, &y);
    CHECK_NEAR(x, 0, 1e-4); CHECK_NEAR(y, 0, 1e-4);
}

static void test_downhill_is_opposite_of_reading(void)
{
    pc_imu_t s; pc_imu_init(&s, false, 1, 1);
    const float t = 0.25f;                                 /* half a full tilt */
    settle(&s, -t, 0, sqrtf(1 - t * t));                   /* the +x edge lowered: the sensor reads -sin on x */
    float x, y; pc_imu_vector(&s, &x, &y);
    CHECK_NEAR(x, 0.5, 0.01); CHECK_NEAR(y, 0, 0.01);
    settle(&s, 0, t, sqrtf(1 - t * t));
    pc_imu_vector(&s, &x, &y);
    CHECK_NEAR(x, 0, 0.01); CHECK_NEAR(y, -0.5, 0.01);
}

static void test_clamped_to_unit_length(void)
{
    pc_imu_t s; pc_imu_init(&s, false, 1, 1);
    settle(&s, 0.7f, 0.7f, 0.1f);
    float x, y; pc_imu_vector(&s, &x, &y);
    CHECK_NEAR(sqrtf(x * x + y * y), 1.0, 1e-3);
    CHECK(x < 0 && y < 0);
}

static void test_mounting_map(void)
{
    pc_imu_t s; pc_imu_init(&s, true, -1, 1);              /* swapped axes, x flipped */
    settle(&s, 0.25f, 0, sqrtf(1 - 0.0625f));              /* the sensor's x is the screen's y */
    float x, y; pc_imu_vector(&s, &x, &y);
    CHECK_NEAR(x, 0, 0.01); CHECK_NEAR(y, -0.5, 0.01);
    settle(&s, 0, 0.25f, sqrtf(1 - 0.0625f));              /* the sensor's y is the screen x, sign_x -1 turns the usual "opposite of the reading" round */
    pc_imu_vector(&s, &x, &y);
    CHECK_NEAR(x, 0.5, 0.01); CHECK_NEAR(y, 0, 0.01);
    pc_imu_map(&s, false, 1, -1);
    settle(&s, 0.25f, 0.25f, 0.9f);
    pc_imu_vector(&s, &x, &y);
    CHECK(x < 0 && y > 0);
}

static void test_smoothing_and_shake(void)
{
    pc_imu_t s; pc_imu_init(&s, false, 1, 1);
    settle(&s, -0.25f, 0, 0.968f);
    float x, y; pc_imu_vector(&s, &x, &y);
    const float held = x;
    pc_imu_feed(&s, 3.0f, -2.0f, 1.0f, 200, 0, 0);          /* a knock: far over 1 g, dropped */
    pc_imu_vector(&s, &x, &y);
    CHECK_NEAR(x, held, 1e-6);
    CHECK_EQ_INT(s.shaken, 1);
    CHECK(s.rate_dps > 10.0f);                               /* but the rate saw it */
    pc_imu_feed(&s, 0.25f, 0, 0.968f, 0, 0, 0);              /* one step moves only part of the way */
    pc_imu_vector(&s, &x, &y);
    CHECK(x < held && x > held - 0.5f);
    pc_imu_feed(&s, 0, 0, 0.0f, 0, 0, 0);                    /* free fall: dropped too */
    CHECK_EQ_INT(s.shaken, 2);
}

static void test_first_sample_seeds(void)
{
    pc_imu_t s; pc_imu_init(&s, false, 1, 1);
    pc_imu_feed(&s, -0.25f, 0, 0.968f, 0, 0, 0);
    float x, y; pc_imu_vector(&s, &x, &y);
    CHECK_NEAR(x, 0.5, 0.01);                                /* no ramp from zero on the first sample */
}

int main(void)
{
    RUN(test_flat_is_zero);
    RUN(test_downhill_is_opposite_of_reading);
    RUN(test_clamped_to_unit_length);
    RUN(test_mounting_map);
    RUN(test_smoothing_and_shake);
    RUN(test_first_sample_seeds);
    return 0;
}
