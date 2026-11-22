// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_rtc85063.h"

static int bcd2(uint8_t v, int hi_mask, int *out)   /* two BCD digits under a mask; false for a digit above 9 */
{
    const int tens = (v & hi_mask) >> 4, ones = v & 0x0F;
    if (tens > 9 || ones > 9) return 0;
    *out = tens * 10 + ones;
    return 1;
}
static uint8_t to_bcd(int v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }
static int leap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }
static int dim(int y, int m) { static const int d[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 }; return m == 2 && leap(y) ? 29 : d[m - 1]; }
static int wday(int y, int m, int d)   /* Sunday = 0 (Sakamoto) */
{
    static const int t[12] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    if (m < 3) y--;
    return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

bool pc_rtc85063_decode(const uint8_t r[PC_RTC_BLOCK], struct tm *t, bool *os)
{
    if (os) *os = (r[0] & 0x80) != 0;
    int s, mi, h, d, mo, y;
    if (!bcd2(r[0], 0x70, &s) || !bcd2(r[1], 0x70, &mi) || !bcd2(r[2], 0x30, &h) || !bcd2(r[3], 0x30, &d) || !bcd2(r[5], 0x10, &mo) || !bcd2(r[6], 0xF0, &y)) return false;
    if (r[1] & 0x80) return false;                                         /* bit 7 of minutes is not part of the value */
    if (r[2] & 0xC0) return false;                                         /* 24 h mode: bits 7:6 are 0 */
    const int year = 2000 + y;
    if (s > 59 || mi > 59 || h > 23 || mo < 1 || mo > 12 || d < 1 || d > dim(year, mo)) return false;
    if (t) {
        *t = (struct tm){ 0 };
        t->tm_sec = s; t->tm_min = mi; t->tm_hour = h; t->tm_mday = d; t->tm_mon = mo - 1; t->tm_year = year - 1900; t->tm_wday = wday(year, mo, d);
    }
    return true;
}

bool pc_rtc85063_encode(const struct tm *t, uint8_t r[PC_RTC_BLOCK])
{
    const int year = t->tm_year + 1900;
    if (year < 2000 || year > 2099 || t->tm_mon < 0 || t->tm_mon > 11 || t->tm_mday < 1 || t->tm_mday > dim(year, t->tm_mon + 1) ||
        t->tm_hour < 0 || t->tm_hour > 23 || t->tm_min < 0 || t->tm_min > 59 || t->tm_sec < 0 || t->tm_sec > 59) return false;
    r[0] = to_bcd(t->tm_sec);                                              /* bit 7 = 0: the OS flag is cleared by this write */
    r[1] = to_bcd(t->tm_min);
    r[2] = to_bcd(t->tm_hour);
    r[3] = to_bcd(t->tm_mday);
    r[4] = (uint8_t)wday(year, t->tm_mon + 1, t->tm_mday);
    r[5] = to_bcd(t->tm_mon + 1);
    r[6] = to_bcd(year - 2000);
    return true;
}

uint8_t pc_rtc85063_ctrl1_run(uint8_t c) { return (uint8_t)(c & ~(PC_RTC_CTRL1_STOP | PC_RTC_CTRL1_12_24)); }
