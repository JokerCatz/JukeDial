// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_wifihold.h"
#include "pc_test.h"

static void test_hold_release_grace(void)
{
    pc_wifihold_t h = { 0 };
    CHECK(!h.on);
    CHECK(pc_wh_hold(&h, PC_WH_RADIO));
    CHECK_EQ_INT(h.holders, 1);
    CHECK(pc_wh_release(&h, PC_WH_RADIO, 1000));      /* still on inside the grace */
    CHECK(pc_wh_tick(&h, 1000 + PC_WH_GRACE_MS - 1));
    CHECK(!pc_wh_tick(&h, 1000 + PC_WH_GRACE_MS));
    CHECK_EQ_INT(h.holders, 0);
    CHECK(!pc_wh_tick(&h, 1000 + 10 * PC_WH_GRACE_MS)); /* stays off */
}

static void test_hold_in_grace_cancels(void)
{
    pc_wifihold_t h = { 0 };
    pc_wh_hold(&h, PC_WH_RADIO);
    pc_wh_release(&h, PC_WH_RADIO, 0);
    CHECK(pc_wh_pending(&h));
    CHECK(pc_wh_hold(&h, PC_WH_CLOCK));
    CHECK(!pc_wh_pending(&h));
    CHECK(pc_wh_tick(&h, 5 * PC_WH_GRACE_MS));        /* past the old deadline: still on */
    pc_wh_release(&h, PC_WH_CLOCK, 5 * PC_WH_GRACE_MS);
    CHECK(pc_wh_tick(&h, 5 * PC_WH_GRACE_MS + PC_WH_GRACE_MS - 1));
    CHECK(!pc_wh_tick(&h, 6 * PC_WH_GRACE_MS));       /* the new grace counts from the new release */
}

static void test_two_holders(void)
{
    pc_wifihold_t h = { 0 };
    pc_wh_hold(&h, PC_WH_RADIO);
    pc_wh_hold(&h, PC_WH_MANUAL);
    CHECK(pc_wh_release(&h, PC_WH_RADIO, 0));
    CHECK(!pc_wh_pending(&h));
    CHECK(pc_wh_tick(&h, 10 * PC_WH_GRACE_MS));       /* the other still holds: no off */
    CHECK_EQ_INT(h.holders, PC_WH_MANUAL);
    pc_wh_release(&h, PC_WH_MANUAL, 10 * PC_WH_GRACE_MS);
    CHECK(!pc_wh_tick(&h, 11 * PC_WH_GRACE_MS));
}

static void test_release_of_non_holder(void)
{
    pc_wifihold_t h = { 0 };
    CHECK(!pc_wh_release(&h, PC_WH_RADIO, 5));        /* off stays off */
    CHECK(!pc_wh_pending(&h));
    pc_wh_hold(&h, PC_WH_CLOCK);
    CHECK(pc_wh_release(&h, PC_WH_RADIO, 5));         /* no change: the clock still holds */
    CHECK_EQ_INT(h.holders, PC_WH_CLOCK);
    CHECK(!pc_wh_pending(&h));
    pc_wh_release(&h, PC_WH_CLOCK, 100);
    uint32_t d = h.off_at_ms;
    pc_wh_release(&h, PC_WH_CLOCK, 50000);            /* a second release does not push the deadline */
    CHECK_EQ_INT(h.off_at_ms, d);
}

static void test_wrap(void)
{
    pc_wifihold_t h = { 0 };
    pc_wh_hold(&h, PC_WH_RADIO);
    uint32_t t = 0xFFFFFFFFu - 1000;                  /* the deadline lands after the wrap */
    pc_wh_release(&h, PC_WH_RADIO, t);
    CHECK(h.off_at_ms < t);
    CHECK(pc_wh_tick(&h, 0xFFFFFFFFu));
    CHECK(pc_wh_tick(&h, PC_WH_GRACE_MS - 1002));
    CHECK(!pc_wh_tick(&h, PC_WH_GRACE_MS - 1000));
}

static void test_force_off(void)
{
    pc_wifihold_t h = { 0 };
    pc_wh_hold(&h, PC_WH_RADIO);
    pc_wh_hold(&h, PC_WH_MANUAL);
    pc_wh_force_off(&h);
    CHECK(!h.on);
    CHECK_EQ_INT(h.holders, 0);
    CHECK(!pc_wh_tick(&h, 1));
    CHECK(pc_wh_hold(&h, PC_WH_PROBE));               /* usable again */
    CHECK_EQ_INT(h.holders, PC_WH_PROBE);
}

int main(void)
{
    RUN(test_hold_release_grace);
    RUN(test_hold_in_grace_cancels);
    RUN(test_two_holders);
    RUN(test_release_of_non_holder);
    RUN(test_wrap);
    RUN(test_force_off);
    return 0;
}
