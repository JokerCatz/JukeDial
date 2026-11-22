// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_lowmem.h"
#include "pc_test.h"

#define K 1024

static void test_plenty_is_ok(void)
{
    pc_lowmem_t m = { 0 };
    CHECK_EQ_INT(pc_lowmem_update(&m, 240 * K, 120 * K, 0), PC_LOWMEM_OK);
}

static void test_bluetooth_level_skips_cover(void)
{
    pc_lowmem_t m = { 0 };
    CHECK_EQ_INT(pc_lowmem_update(&m, 93 * K, 60 * K, 0), PC_LOWMEM_NO_COVER);
}

static void test_fragmented_heap_skips_cover(void)
{
    pc_lowmem_t m = { 0 };
    CHECK_EQ_INT(pc_lowmem_update(&m, 150 * K, 40 * K, 0), PC_LOWMEM_NO_COVER);
}

static void test_drops_straight_to_deepest(void)
{
    pc_lowmem_t m = { 0 };
    CHECK_EQ_INT(pc_lowmem_update(&m, 40 * K, 30 * K, 0), PC_LOWMEM_STILL);
    pc_lowmem_t n = { 0 };
    CHECK_EQ_INT(pc_lowmem_update(&n, 20 * K, 10 * K, 0), PC_LOWMEM_NO_BACKGND);
}

static void test_recovery_needs_calm_and_margin(void)
{
    pc_lowmem_t m = { 0 };
    pc_lowmem_update(&m, 40 * K, 30 * K, 0);                                   /* STILL */
    CHECK_EQ_INT(pc_lowmem_update(&m, 55 * K, 50 * K, 1000), PC_LOWMEM_STILL);  /* above 48 but inside the margin */
    CHECK_EQ_INT(pc_lowmem_update(&m, 70 * K, 50 * K, 2000), PC_LOWMEM_STILL);  /* calm starts */
    CHECK_EQ_INT(pc_lowmem_update(&m, 70 * K, 50 * K, 6999), PC_LOWMEM_STILL);
    CHECK_EQ_INT(pc_lowmem_update(&m, 70 * K, 50 * K, 7000), PC_LOWMEM_NO_COVER);   /* one level up */
}

static void test_dip_resets_calm(void)
{
    pc_lowmem_t m = { 0 };
    pc_lowmem_update(&m, 40 * K, 30 * K, 0);
    pc_lowmem_update(&m, 70 * K, 50 * K, 1000);
    pc_lowmem_update(&m, 45 * K, 40 * K, 4000);                                 /* dips again */
    CHECK_EQ_INT(pc_lowmem_update(&m, 70 * K, 50 * K, 6500), PC_LOWMEM_STILL);
    CHECK_EQ_INT(pc_lowmem_update(&m, 70 * K, 50 * K, 11500), PC_LOWMEM_NO_COVER);
}

static void test_climbs_one_level_per_calm_period(void)
{
    pc_lowmem_t m = { 0 };
    pc_lowmem_update(&m, 20 * K, 10 * K, 0);                                    /* NO_BACKGND */
    CHECK_EQ_INT(pc_lowmem_update(&m, 200 * K, 100 * K, 100), PC_LOWMEM_NO_BACKGND);
    CHECK_EQ_INT(pc_lowmem_update(&m, 200 * K, 100 * K, 5100), PC_LOWMEM_STILL);
    CHECK_EQ_INT(pc_lowmem_update(&m, 200 * K, 100 * K, 10100), PC_LOWMEM_NO_COVER);
    CHECK_EQ_INT(pc_lowmem_update(&m, 200 * K, 100 * K, 15100), PC_LOWMEM_OK);
}

static void test_tick_wrap(void)
{
    pc_lowmem_t m = { 0 };
    pc_lowmem_update(&m, 93 * K, 60 * K, 0xFFFFF000u);
    pc_lowmem_update(&m, 200 * K, 100 * K, 0xFFFFF000u);
    CHECK_EQ_INT(pc_lowmem_update(&m, 200 * K, 100 * K, 0xFFFFF000u + PC_LOWMEM_CALM_MS), PC_LOWMEM_OK);
}

static void test_big_cover(void)
{
    CHECK(pc_lowmem_big_ok(244 * 1024, 128 * 1024));                       /* the FakePod's heap at the style switch */
    CHECK(!pc_lowmem_big_ok(99 * 1024, 128 * 1024));                        /* below the level-1 line: the stretched thumbnail stays */
    CHECK(!pc_lowmem_big_ok(244 * 1024, 51 * 1024));                        /* fragmented */
    CHECK(!pc_lowmem_big_esp_ok(244 * 1024, 128 * 1024));                   /* the 209 KB peak would take it to 35 KB: ours decodes it */
    CHECK(pc_lowmem_big_esp_ok(100 * 1024 + 209 * 1024, 128 * 1024));
    CHECK(!pc_lowmem_big_esp_ok(100 * 1024 + 209 * 1024 - 1, 128 * 1024));
}

int main(void)
{
    RUN(test_big_cover);
    RUN(test_plenty_is_ok);
    RUN(test_bluetooth_level_skips_cover);
    RUN(test_fragmented_heap_skips_cover);
    RUN(test_drops_straight_to_deepest);
    RUN(test_recovery_needs_calm_and_margin);
    RUN(test_dip_resets_calm);
    RUN(test_climbs_one_level_per_calm_period);
    RUN(test_tick_wrap);
    return 0;
}
