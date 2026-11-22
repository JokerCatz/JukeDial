// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_guard.h"
#include "pc_test.h"

static void test_crashes_count(void)
{
    uint32_t n = 0;
    n = pc_guard_count(n, PC_BOOT_CRASH);
    n = pc_guard_count(n, PC_BOOT_CRASH);
    CHECK(!pc_guard_safe_mode(n));
    n = pc_guard_count(n, PC_BOOT_CRASH);
    CHECK(pc_guard_safe_mode(n));
}

static void test_clean_boot_clears(void)
{
    CHECK_EQ_INT(pc_guard_count(2, PC_BOOT_CLEAN), 0);
}

static void test_brownout_neither_counts_nor_clears(void)
{
    uint32_t n = 0;
    for (int i = 0; i < 10; i++) n = pc_guard_count(n, PC_BOOT_POWER);   /* a dying battery rebooting over and over */
    CHECK_EQ_INT(n, 0);
    CHECK(!pc_guard_safe_mode(n));
    CHECK_EQ_INT(pc_guard_count(2, PC_BOOT_POWER), 2);                 /* a crash loop is not hidden by one */
}

static void test_safe_screen_window(void)   /* the screen is tried at the limit and once more; a crash inside it ends it after two more boots (spec 4.4) */
{
    CHECK(!pc_guard_safe_screen(0, false));
    CHECK(!pc_guard_safe_screen(2, false));
    CHECK(pc_guard_safe_screen(3, false));
    CHECK(pc_guard_safe_screen(4, false));
    CHECK(!pc_guard_safe_screen(5, false));
    CHECK(!pc_guard_safe_screen(UINT32_MAX, false));
    /* after a brownout (U28) the screen is skipped at every count, the plain path runs; a brownout does not move the count */
    CHECK(!pc_guard_safe_screen(3, true));
    CHECK(!pc_guard_safe_screen(4, true));
    CHECK(!pc_guard_safe_screen(0, true));
    CHECK(pc_guard_count(3, PC_BOOT_POWER) == 3);
}

static void test_bt_default_by_reset_class(void)   /* */
{
    CHECK(!pc_guard_bt_default(PC_RST_POWERON, false));
    CHECK(!pc_guard_bt_default(PC_RST_SOFTWARE, false));       /* sysmode_reboot, the 重開機 row, the web reboot: all esp_restart */
    CHECK(!pc_guard_bt_default(PC_RST_DEEPSLEEP, false));
    CHECK(pc_guard_bt_default(PC_RST_OTHER, false));           /* panic, task / interrupt watchdog, brownout, pin, ... */
    CHECK(pc_guard_bt_default(PC_RST_POWERON, true));          /* the safe mode, whatever the reason */
    CHECK(pc_guard_bt_default(PC_RST_SOFTWARE, true));
    CHECK(pc_guard_bt_default(PC_RST_OTHER, true));
}

static void test_bt_apply_and_card_missing(void)
{
    CHECK_EQ_INT(pc_guard_bt_apply(false, true, 2), PC_BTRST_NONE);          /* nothing pending: a normal restart keeps its mode */
    CHECK_EQ_INT(pc_guard_bt_apply(true, true, 2), PC_BTRST_RESET);
    CHECK_EQ_INT(pc_guard_bt_apply(true, true, 0), PC_BTRST_NONE);          /* already off: nothing to write */
    CHECK_EQ_INT(pc_guard_bt_apply(true, false, 2), PC_BTRST_SKIP_NO_CARD);  /* no card: skipped, boot goes on */
    CHECK_EQ_INT(pc_guard_bt_apply(false, false, 2), PC_BTRST_NONE);
}

static void test_arm_for_test(void)
{
    const uint32_t a = pc_guard_arm_for_test();
    CHECK(!pc_guard_safe_mode(a));                                   /* armed, not yet safe */
    CHECK(pc_guard_safe_mode(pc_guard_count(a, PC_BOOT_CRASH)));      /* the next crash reset reaches the limit */
    CHECK_EQ_INT(pc_guard_count(a, PC_BOOT_CLEAN), 0);                /* a host reset still clears it */
}

int main(void)
{
    RUN(test_crashes_count);
    RUN(test_clean_boot_clears);
    RUN(test_brownout_neither_counts_nor_clears);
    RUN(test_safe_screen_window);
    RUN(test_bt_default_by_reset_class);
    RUN(test_bt_apply_and_card_missing);
    RUN(test_arm_for_test);
    return 0;
}
