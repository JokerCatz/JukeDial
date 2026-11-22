// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_pwrpol.h"
#include "pc_test.h"

static void test_apo_setting(void)
{
    CHECK_EQ_INT(pc_apo_norm(10), 10); CHECK_EQ_INT(pc_apo_norm(0), 0); CHECK_EQ_INT(pc_apo_norm(45), 30); CHECK_EQ_INT(pc_apo_norm(-1), 30);
    CHECK_EQ_INT(pc_apo_step(30, 1), 60); CHECK_EQ_INT(pc_apo_step(60, 1), 0); CHECK_EQ_INT(pc_apo_step(0, -1), 60); CHECK_EQ_INT(pc_apo_step(7, 1), 60);
}

static void test_apo_idle(void)
{
    pc_apo_t s = { 0 };
    uint32_t in = 0;
    bool off = false;
    for (int i = 0; i < 9 * 60 && !off; i++) { in += 1000; off = pc_apo_tick(&s, 10, 1000, false, false, in); }
    CHECK(!off);                                                        /* 9 minutes: not yet */
    for (int i = 0; i < 60 && !off; i++) { in += 1000; off = pc_apo_tick(&s, 10, 1000, false, false, in); }
    CHECK(off);                                                         /* 10 minutes of nothing */
    memset(&s, 0, sizeof s); in = 0; off = false;
    for (int i = 0; i < 20 * 60; i++) { in += 1000; if (i == 8 * 60) in = 0; off |= pc_apo_tick(&s, 10, 1000, false, false, in); }
    CHECK(off);                                                         /* a touch at 8 min: it went off 10 min later, but not at 10 */
    memset(&s, 0, sizeof s); in = 0; off = false;
    for (int i = 0; i < 12 * 60; i++) { in += 1000; if (i == 8 * 60) in = 0; off |= pc_apo_tick(&s, 10, 1000, false, false, in); }
    CHECK(!off);
    memset(&s, 0, sizeof s); off = false;
    for (int i = 0; i < 90 * 60; i++) off |= pc_apo_tick(&s, 10, 1000, true, false, 1000000);       /* playing: never */
    CHECK(!off);
    for (int i = 0; i < 90 * 60; i++) off |= pc_apo_tick(&s, 10, 1000, false, true, 1000000);        /* external power: never */
    CHECK(!off);
    for (int i = 0; i < 90 * 60; i++) off |= pc_apo_tick(&s, 0, 1000, false, false, 1000000);       /* setting off: never */
    CHECK(!off);
    s.quiet_ms = 0;
    for (int i = 0; i < 9 * 60; i++) pc_apo_tick(&s, 10, 1000, false, false, 1000000);
    CHECK(!pc_apo_tick(&s, 10, 1000, true, false, 1000000)); CHECK_EQ_INT((int)s.quiet_ms, 0);      /* playing resets the quiet time */
}

static void test_lowbat(void)
{
    CHECK(pc_lowbat_boot_refuse(1, false)); CHECK(!pc_lowbat_boot_refuse(2, false)); CHECK(!pc_lowbat_boot_refuse(0, true)); CHECK(!pc_lowbat_boot_refuse(-1, false));
    pc_lowbat_t s = { 0, false };
    CHECK_EQ_INT(pc_lowbat_tick(&s, 50, false, 1000), PC_LB_NONE);
    CHECK_EQ_INT(pc_lowbat_tick(&s, 15, false, 1000), PC_LB_WARN);
    CHECK_EQ_INT(pc_lowbat_tick(&s, 14, false, 1000), PC_LB_NONE);                  /* one warning only */
    CHECK_EQ_INT(pc_lowbat_tick(&s, 21, false, 1000), PC_LB_NONE);                  /* back above 20: re-armed */
    CHECK_EQ_INT(pc_lowbat_tick(&s, 12, false, 1000), PC_LB_WARN);
    int r = PC_LB_NONE;
    for (int i = 0; i < 59; i++) { r = pc_lowbat_tick(&s, 4, false, 1000); CHECK(r != PC_LB_SHUTDOWN); }
    CHECK_EQ_INT(pc_lowbat_tick(&s, 4, false, 1000), PC_LB_SHUTDOWN);                 /* 60 s below 5 % */
    memset(&s, 0, sizeof s);
    for (int i = 0; i < 50; i++) pc_lowbat_tick(&s, 4, false, 1000);
    pc_lowbat_tick(&s, 6, false, 1000);                                              /* 5 % or more: the run restarts */
    for (int i = 0; i < 59; i++) CHECK(pc_lowbat_tick(&s, 4, false, 1000) != PC_LB_SHUTDOWN);
    memset(&s, 0, sizeof s);
    for (int i = 0; i < 120; i++) CHECK(pc_lowbat_tick(&s, 3, true, 1000) == PC_LB_NONE);   /* external power: never */
    for (int i = 0; i < 120; i++) CHECK(pc_lowbat_tick(&s, -1, false, 1000) == PC_LB_NONE); /* no reading: never */
}

static void test_pwron(void)
{
    CHECK(pc_pwron_is_plug_only(0x04)); CHECK(!pc_pwron_is_plug_only(0x01)); CHECK(!pc_pwron_is_plug_only(0x05)); CHECK(!pc_pwron_is_plug_only(0x00));
}

static void test_oled(void)
{
    CHECK_EQ_INT((int)pc_oled_idle_limit(0), (int)PC_OLED_IDLE_MAX_MS);              /* "never" becomes the maximum */
    CHECK_EQ_INT((int)pc_oled_idle_limit(60000), 60000);
    CHECK_EQ_INT((int)pc_oled_idle_limit(PC_OLED_IDLE_MAX_MS + 1), (int)PC_OLED_IDLE_MAX_MS);
    CHECK(!pc_oled_press_stuck(119999)); CHECK(pc_oled_press_stuck(120000));
    CHECK(!pc_apo_ext_screen_off(10, true, 599999)); CHECK(pc_apo_ext_screen_off(10, true, 600000));
    CHECK(!pc_apo_ext_screen_off(10, false, 3600000));                               /* battery: the unit powers off instead (pc_apo_tick) */
    CHECK(pc_apo_ext_screen_off(0, true, 600000)); CHECK(!pc_apo_ext_screen_off(0, true, 599999));   /* auto power-off "off": still 10 min */
    CHECK(pc_apo_ext_screen_off(60, true, 3600000)); CHECK(!pc_apo_ext_screen_off(60, true, 1800000));
}

int main(void)
{
    RUN(test_apo_setting);
    RUN(test_apo_idle);
    RUN(test_lowbat);
    RUN(test_pwron);
    RUN(test_oled);
    return 0;
}
