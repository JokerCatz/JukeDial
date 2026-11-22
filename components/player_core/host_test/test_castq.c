// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_castq.h"
#include "pc_test.h"

static void test_normalise(void)
{
    const int in[] = { -1, 0, 1, 2, 3, 4, 100 }, want[] = { 1, 0, 1, 2, 1, 1, 1 };   /* out of range reads as the default, 48k stereo */
    for (unsigned i = 0; i < sizeof in / sizeof in[0]; i++) CHECK_EQ_INT(pc_cast_quality(in[i]), want[i]);
}

static void test_axis_order_and_wrap(void)
{
    /* display order = value order: 24k stereo -> 48k stereo -> 48k high, wrapping */
    CHECK_EQ_INT(pc_cast_q_step(0, 1), 1);
    CHECK_EQ_INT(pc_cast_q_step(1, 1), 2);
    CHECK_EQ_INT(pc_cast_q_step(2, 1), 0);
    CHECK_EQ_INT(pc_cast_q_step(0, -1), 2);
    CHECK_EQ_INT(pc_cast_q_step(2, -1), 1);
    CHECK_EQ_INT(pc_cast_q_step(1, -1), 0);
    CHECK_EQ_INT(pc_cast_q_step(3, 1), 2);   /* out of range counts as 1 */
    CHECK_EQ_INT(pc_cast_q_step(1, 4), 2);
}

static void test_params(void)
{
    CHECK_EQ_INT(pc_cast_q_rate(0), 24000); CHECK_EQ_INT(pc_cast_q_nbyte(0), 60);
    CHECK_EQ_INT(pc_cast_q_rate(1), 48000); CHECK_EQ_INT(pc_cast_q_nbyte(1), 120);
    CHECK_EQ_INT(pc_cast_q_rate(2), 48000); CHECK_EQ_INT(pc_cast_q_nbyte(2), 155);
    CHECK_EQ_INT(pc_cast_q_rate(7), 48000); CHECK_EQ_INT(pc_cast_q_nbyte(7), 120);
}

static void test_default(void)
{
    CHECK_EQ_INT(pc_cast_quality(PC_CAST_Q_DEFAULT), PC_CAST_Q_DEFAULT);
    CHECK_EQ_INT(pc_cast_q_rate(PC_CAST_Q_DEFAULT), 48000);
    CHECK_EQ_INT(pc_cast_q_nbyte(PC_CAST_Q_DEFAULT), 120);
    CHECK_STR(pc_cast_q_label(PC_CAST_Q_DEFAULT, false), "48k stereo");
}

int main(void)
{
    RUN(test_normalise);
    RUN(test_axis_order_and_wrap);
    RUN(test_params);
    RUN(test_default);
    return 0;
}
