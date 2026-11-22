// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_power.h"
#include "pc_test.h"

static pc_power_filt_t mk(pc_power_t st) { pc_power_filt_t f = { st, 0 }; return f; }

static void test_first_sample(void)
{
    pc_power_filt_t f = mk(PC_PWR_UNKNOWN);
    CHECK_EQ_INT(pc_power_step(&f, 4030, false), PC_PWR_USB);
    f = mk(PC_PWR_UNKNOWN);
    CHECK_EQ_INT(pc_power_step(&f, 3950, false), PC_PWR_BATT);
    f = mk(PC_PWR_UNKNOWN);
    CHECK_EQ_INT(pc_power_step(&f, 3000, true), PC_PWR_USB);
}

static void low59(pc_power_filt_t *f)
{
    static const int lows[] = { 3830, 3900, 3850 };
    for (int i = 0; i < PC_PWR_LOW_RUN - 1; i++) CHECK_EQ_INT(pc_power_step(f, lows[i % 3], false), PC_PWR_USB);
}

static void test_usb_needs_a_long_low_run(void)
{
    pc_power_filt_t f = mk(PC_PWR_USB);
    low59(&f);
    CHECK_EQ_INT(pc_power_step(&f, 3830, false), PC_PWR_BATT);
}

static void test_middle_band_resets(void)
{
    pc_power_filt_t f = mk(PC_PWR_USB);
    low59(&f);
    CHECK_EQ_INT(pc_power_step(&f, 3950, false), PC_PWR_USB);
    CHECK_EQ_INT(f.low_run, 0);
    low59(&f);
}

static void test_high_resets(void)
{
    pc_power_filt_t f = mk(PC_PWR_USB);
    low59(&f);
    CHECK_EQ_INT(pc_power_step(&f, 4010, false), PC_PWR_USB);
    CHECK_EQ_INT(f.low_run, 0);
}

static void test_from_batt(void)
{
    pc_power_filt_t f = mk(PC_PWR_BATT);
    CHECK_EQ_INT(pc_power_step(&f, 3990, false), PC_PWR_BATT);
    CHECK_EQ_INT(pc_power_step(&f, 4010, false), PC_PWR_USB);
    f = mk(PC_PWR_BATT);
    CHECK_EQ_INT(pc_power_step(&f, 3000, true), PC_PWR_USB);
    CHECK_EQ_INT(f.low_run, 0);
}

static void test_measured_pattern_never_flips(void)
{
    pc_power_filt_t f = mk(PC_PWR_UNKNOWN);
    int flips = 0;
    pc_power_t last = pc_power_step(&f, 4051, false);
    CHECK_EQ_INT(last, PC_PWR_USB);
    for (int i = 0; i < 2400; i++) {
        pc_power_t n = pc_power_step(&f, i % 6 == 5 ? 4051 : 3830, false);
        if (n != last) flips++;
        last = n;
    }
    CHECK_EQ_INT(flips, 0);
    CHECK_EQ_INT(last, PC_PWR_USB);
}

static void test_low_run_saturates(void)
{
    pc_power_filt_t f = mk(PC_PWR_USB);
    for (int i = 0; i < 100000; i++) pc_power_step(&f, 3000, false);
    CHECK_EQ_INT(f.state, PC_PWR_BATT);
    CHECK_EQ_INT(f.low_run, UINT16_MAX);
    CHECK_EQ_INT(pc_power_step(&f, 3000, false), PC_PWR_BATT);
}

static void test_timeout(void)
{
    CHECK_EQ_INT(pc_screen_timeout_ms(PC_PWR_USB), 60000);
    CHECK_EQ_INT(pc_screen_timeout_ms(PC_PWR_BATT), 20000);
    CHECK_EQ_INT(pc_screen_timeout_ms(PC_PWR_UNKNOWN), 20000);
    CHECK(pc_power_allows_heavy(PC_PWR_USB));
    CHECK(!pc_power_allows_heavy(PC_PWR_BATT));
    CHECK(!pc_power_allows_heavy(PC_PWR_UNKNOWN));
}

int main(void)
{
    RUN(test_first_sample);
    RUN(test_usb_needs_a_long_low_run);
    RUN(test_middle_band_resets);
    RUN(test_high_resets);
    RUN(test_from_batt);
    RUN(test_measured_pattern_never_flips);
    RUN(test_low_run_saturates);
    RUN(test_timeout);
    return 0;
}
