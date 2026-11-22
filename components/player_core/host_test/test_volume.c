// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_volume.h"
#include "pc_test.h"

static void test_volume_step(void)
{
    CHECK_EQ_INT(PC_VOL_DEFAULT, 40);
    CHECK_EQ_INT(pc_volume_step(40, 1, 100), 42);
    CHECK_EQ_INT(pc_volume_step(99, 1, 100), 100);
    CHECK_EQ_INT(pc_volume_step(1, -1, 100), 0);
    CHECK_EQ_INT(pc_volume_step(79, 3, 80), 80);
}

static void test_volume_gain(void)
{
    CHECK(pc_volume_gain(0) == 0.0f);
    CHECK_NEAR(pc_volume_gain(100), 1.0, 1e-5);
    CHECK_NEAR(pc_volume_gain(50), 0.031623, 1e-4);
    for (int v = 1; v <= 100; v++) CHECK(pc_volume_gain(v) > pc_volume_gain(v - 1));
}

int main(void)
{
    RUN(test_volume_step);
    RUN(test_volume_gain);
    return 0;
}
