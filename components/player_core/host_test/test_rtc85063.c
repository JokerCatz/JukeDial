// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_rtc85063.h"
#include "pc_test.h"

static void test_roundtrip(void)
{
    struct tm t = { .tm_year = 2026 - 1900, .tm_mon = 10, .tm_mday = 22, .tm_hour = 7, .tm_min = 5, .tm_sec = 59 }, o;
    uint8_t r[PC_RTC_BLOCK];
    CHECK(pc_rtc85063_encode(&t, r));
    CHECK_EQ_INT(r[0], 0x59); CHECK_EQ_INT(r[1], 0x05); CHECK_EQ_INT(r[2], 0x07); CHECK_EQ_INT(r[3], 0x22); CHECK_EQ_INT(r[4], 0);   /* 2026-11-22 is a Sunday */
    CHECK_EQ_INT(r[5], 0x11); CHECK_EQ_INT(r[6], 0x26);
    bool os = true;
    CHECK(pc_rtc85063_decode(r, &o, &os));
    CHECK(!os);
    CHECK_EQ_INT(o.tm_year, t.tm_year); CHECK_EQ_INT(o.tm_mon, 10); CHECK_EQ_INT(o.tm_mday, 22); CHECK_EQ_INT(o.tm_hour, 7); CHECK_EQ_INT(o.tm_min, 5); CHECK_EQ_INT(o.tm_sec, 59); CHECK_EQ_INT(o.tm_wday, 0);
}

static void test_os_flag(void)
{
    uint8_t r[PC_RTC_BLOCK] = { 0x80 | 0x30, 0x15, 0x23, 0x31, 3, 0x12, 0x99 };   /* oscillator stopped, 2099-12-31 23:15:30 */
    struct tm o; bool os = false;
    CHECK(pc_rtc85063_decode(r, &o, &os));                 /* the fields are fine; the flag is reported separately */
    CHECK(os); CHECK_EQ_INT(o.tm_sec, 30); CHECK_EQ_INT(o.tm_year, 199); CHECK_EQ_INT(o.tm_wday, 4);   /* a Thursday */
    uint8_t w[PC_RTC_BLOCK];
    CHECK(pc_rtc85063_encode(&o, w)); CHECK_EQ_INT(w[0] & 0x80, 0);
}

static void test_bad_fields(void)
{
    struct tm o; bool os;
    uint8_t ok[PC_RTC_BLOCK] = { 0x00, 0x00, 0x00, 0x01, 0, 0x01, 0x00 };
    CHECK(pc_rtc85063_decode(ok, &o, &os));
    uint8_t a[PC_RTC_BLOCK];
    for (int i = 0; i < PC_RTC_BLOCK; i++) a[i] = ok[i];
    a[0] = 0x60; CHECK(!pc_rtc85063_decode(a, &o, &os)); a[0] = 0x0A; CHECK(!pc_rtc85063_decode(a, &o, &os)); a[0] = 0;   /* 60 s; a non-BCD digit */
    a[1] = 0x60; CHECK(!pc_rtc85063_decode(a, &o, &os)); a[1] = 0;
    a[2] = 0x24; CHECK(!pc_rtc85063_decode(a, &o, &os)); a[2] = 0x40; CHECK(!pc_rtc85063_decode(a, &o, &os)); a[2] = 0;
    a[3] = 0x00; CHECK(!pc_rtc85063_decode(a, &o, &os)); a[3] = 0x32; CHECK(!pc_rtc85063_decode(a, &o, &os)); a[3] = 1;
    a[5] = 0x00; CHECK(!pc_rtc85063_decode(a, &o, &os)); a[5] = 0x13; CHECK(!pc_rtc85063_decode(a, &o, &os)); a[5] = 1;
    a[6] = 0xA0; CHECK(!pc_rtc85063_decode(a, &o, &os));
    uint8_t blank[PC_RTC_BLOCK] = { 0 };                   /* an all-zero block (day 0, month 0): not a date */
    CHECK(!pc_rtc85063_decode(blank, &o, &os));
    uint8_t ff[PC_RTC_BLOCK] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };   /* a floating bus */
    CHECK(!pc_rtc85063_decode(ff, &o, &os));
}

static void test_calendar(void)
{
    struct tm o; bool os;
    uint8_t feb29[PC_RTC_BLOCK] = { 0, 0, 0, 0x29, 0, 0x02, 0x28 };   /* 2028 is a leap year */
    CHECK(pc_rtc85063_decode(feb29, &o, &os));
    feb29[6] = 0x27; CHECK(!pc_rtc85063_decode(feb29, &o, &os));      /* 2027 is not */
    feb29[6] = 0x00; CHECK(pc_rtc85063_decode(feb29, &o, &os));       /* 2000 is (divisible by 400) */
    feb29[6] = 0x00; feb29[3] = 0x31; feb29[5] = 0x04; CHECK(!pc_rtc85063_decode(feb29, &o, &os));   /* April 31 */
    struct tm t = { .tm_year = 1999 - 1900, .tm_mon = 0, .tm_mday = 1 };
    uint8_t r[PC_RTC_BLOCK];
    CHECK(!pc_rtc85063_encode(&t, r)); t.tm_year = 2100 - 1900; CHECK(!pc_rtc85063_encode(&t, r));
    t.tm_year = 2025 - 1900; t.tm_mday = 29; t.tm_mon = 1; CHECK(!pc_rtc85063_encode(&t, r));
    t.tm_mon = 12; t.tm_mday = 1; CHECK(!pc_rtc85063_encode(&t, r));
}

static void test_ctrl1(void)
{
    CHECK_EQ_INT(pc_rtc85063_ctrl1_run(0x00), 0x00);
    CHECK_EQ_INT(pc_rtc85063_ctrl1_run(0x23), 0x01);   /* STOP, 12 h cleared; CAP_SEL kept */
    CHECK_EQ_INT(pc_rtc85063_ctrl1_run(0x05), 0x05);   /* CIE and CAP_SEL untouched */
}

int main(void)
{
    RUN(test_roundtrip); RUN(test_os_flag); RUN(test_bad_fields); RUN(test_calendar); RUN(test_ctrl1);
    return 0;
}
