// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_dacmax.h"
#include "pc_test.h"

/* hires T6: dac_max_rate, the tiers this build can output ({48k, 96k, 192k, 384k} up to PC_AUDIO_MAX_RATE = 384000) */

static void test_dacmax_count(void) { CHECK_EQ_INT(pc_dacmax_count(), 4); }  /* hires T13: 384k selectable */

static void test_dacmax_norm(void)
{
    CHECK_EQ_INT(pc_dacmax_norm(0, 48000), 48000);        /* missing / 0: the board's default */
    CHECK_EQ_INT(pc_dacmax_norm(96000, 48000), 96000);
    CHECK_EQ_INT(pc_dacmax_norm(100000, 48000), 96000);   /* between tiers: the one below */
    CHECK_EQ_INT(pc_dacmax_norm(7, 48000), 48000);        /* garbage: minimum */
    CHECK_EQ_INT(pc_dacmax_norm(-1, 96000), 96000);       /* negative: the default */
    CHECK_EQ_INT(pc_dacmax_norm(384000, 48000), 384000);
    CHECK_EQ_INT(pc_dacmax_norm(768000, 48000), 384000);  /* above the build */
    CHECK_EQ_INT(pc_dacmax_norm(0, 768000), 384000);      /* a default above the build is normalised too */
    CHECK_EQ_INT(pc_dacmax_norm(300000, 48000), 192000);
    CHECK_EQ_INT(pc_dacmax_norm(0, 192000), 192000);
    CHECK_EQ_INT(pc_dacmax_norm(150000, 48000), 96000);
    CHECK_EQ_INT(pc_dacmax_norm(0, 7), 48000);
    CHECK_EQ_INT(pc_dacmax_norm(48000, 96000), 48000);
}

static void test_dacmax_step(void)
{
    CHECK_EQ_INT(pc_dacmax_step(48000, 1), 96000);
    CHECK_EQ_INT(pc_dacmax_step(96000, 1), 192000);
    CHECK_EQ_INT(pc_dacmax_step(192000, 1), 384000);
    CHECK_EQ_INT(pc_dacmax_step(384000, 1), 48000);       /* wraps */
    CHECK_EQ_INT(pc_dacmax_step(48000, -1), 384000);
    CHECK_EQ_INT(pc_dacmax_step(96000, -1), 48000);
    CHECK_EQ_INT(pc_dacmax_step(48000, 4), 48000);
    CHECK_EQ_INT(pc_dacmax_step(7, 1), 96000);            /* not a tier: normalised first, then stepped */
    CHECK_EQ_INT(pc_dacmax_step(96000, 0), 96000);
}

static void test_dacmax_label(void)
{
    CHECK_STR(pc_dacmax_label(48000), "48k");
    CHECK_STR(pc_dacmax_label(96000), "96k");
    CHECK_STR(pc_dacmax_label(192000), "192k");
    CHECK_STR(pc_dacmax_label(384000), "384k");
    CHECK_STR(pc_dacmax_label(7), "?");
}

int main(void)
{
    RUN(test_dacmax_count);
    RUN(test_dacmax_norm);
    RUN(test_dacmax_step);
    RUN(test_dacmax_label);
    return 0;
}
