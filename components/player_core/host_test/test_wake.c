// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_wake.h"
#include "pc_test.h"

static void test_wake_detent_swallowed(void)
{
    pc_wake_t w = { .screen_on = false };
    CHECK_EQ_INT(pc_wake_on_knob(&w, +1, 1000), 0);
    CHECK(w.screen_on);
    CHECK_EQ_INT(pc_wake_on_knob(&w, +3, 1000 + PC_WAKE_LOCKOUT_MS), 3);
}

static void test_wake_after_off(void)
{
    pc_wake_t w = { .screen_on = true };
    CHECK_EQ_INT(pc_wake_on_knob(&w, -1, 5000), -1);
    pc_wake_screen_off(&w);
    CHECK_EQ_INT(pc_wake_on_knob(&w, -2, 6000), 0);
    CHECK_EQ_INT(pc_wake_on_knob(&w, -2, 6000 + PC_WAKE_LOCKOUT_MS), -2);
}

/* the turn that wakes the screen usually goes on for a few detents: none of them may change the volume */
static void test_wake_lockout(void)
{
    pc_wake_t w = { .screen_on = false };
    CHECK_EQ_INT(pc_wake_on_knob(&w, +1, 10000), 0);
    CHECK_EQ_INT(pc_wake_on_knob(&w, +1, 10100), 0);
    CHECK_EQ_INT(pc_wake_on_knob(&w, +2, 10000 + PC_WAKE_LOCKOUT_MS - 1), 0);
    CHECK_EQ_INT(pc_wake_on_knob(&w, +1, 10000 + PC_WAKE_LOCKOUT_MS), 1);
    CHECK_EQ_INT(PC_WAKE_LOCKOUT_MS, 400);
}

int main(void)
{
    RUN(test_wake_detent_swallowed);
    RUN(test_wake_after_off);
    RUN(test_wake_lockout);
    return 0;
}
