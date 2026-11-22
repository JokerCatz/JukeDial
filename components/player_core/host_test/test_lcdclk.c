// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_lcdclk.h"
#include "pc_test.h"

static void test_norm(void)
{
    CHECK_EQ_INT(pc_lcdclk_default(true), 50); CHECK_EQ_INT(pc_lcdclk_default(false), 40);
    CHECK_EQ_INT(pc_lcdclk_norm(50, true), 50); CHECK_EQ_INT(pc_lcdclk_norm(80, true), 80);
    CHECK_EQ_INT(pc_lcdclk_norm(60, true), 50); CHECK_EQ_INT(pc_lcdclk_norm(40, true), 50);
    CHECK_EQ_INT(pc_lcdclk_norm(40, false), 40); CHECK_EQ_INT(pc_lcdclk_norm(60, false), 60); CHECK_EQ_INT(pc_lcdclk_norm(80, false), 80);
    CHECK_EQ_INT(pc_lcdclk_norm(50, false), 40);
    CHECK_EQ_INT(pc_lcdclk_norm(0, true), 50); CHECK_EQ_INT(pc_lcdclk_norm(-80, false), 40); CHECK_EQ_INT(pc_lcdclk_norm(120, false), 40);
}

static void test_frame(void)
{
    CHECK_EQ_INT(pc_lcdclk_frame_us(460, 460, 40), 21160);   /* 423 KB over 4 lanes */
    CHECK_EQ_INT(pc_lcdclk_frame_us(460, 460, 80), 10580);
    CHECK_EQ_INT(pc_lcdclk_frame_us(360, 360, 50), 10368);
    CHECK_EQ_INT(pc_lcdclk_frame_us(0, 360, 50), 0);
}

int main(void)
{
    RUN(test_norm); RUN(test_frame);
    return 0;
}
