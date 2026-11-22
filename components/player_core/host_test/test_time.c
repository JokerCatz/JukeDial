// SPDX-License-Identifier: LGPL-2.1-or-later
/* Time modules ported from RP2350_player src/time (moon, sun, terms, tz, alarm, clock), checked against
 * published instants; plus the Chinese names added here. */
#include "time/alarm.h"
#include "time/clock.h"
#include "time/moon.h"
#include "time/sun.h"
#include "time/terms.h"
#include "time/tz.h"
#include "pc_test.h"

static int64_t utc(int y, int mo, int d, int h, int mi) { return clock_days_from_civil(y, mo, d) * 86400 + h * 3600 + mi * 60; }

static void test_moon(void)
{
    CHECK_EQ_INT(moon_phase8(utc(2024, 4, 8, 18, 21)), 0);     /* new moon (the total solar eclipse) */
    CHECK_EQ_INT(moon_phase8(utc(2024, 4, 23, 23, 49)), 4);    /* full moon */
    CHECK_EQ_INT(moon_phase8(utc(2024, 4, 15, 19, 13)), 2);    /* first quarter */
    CHECK_STR(moon_phase_name_zh(4), "滿月");
    CHECK_STR(moon_phase_name_zh(0), "新月");
    /* lit fraction and the next full/new moon (the clock's moon face) */
    CHECK_EQ_INT(moon_lit_pct(utc(2024, 4, 8, 18, 21)), 0);
    CHECK_EQ_INT(moon_lit_pct(utc(2024, 4, 23, 23, 49)), 100);
    CHECK_EQ_INT(moon_lit_pct(MOON_EPOCH_UTC_S + MOON_SYNODIC_S / 4), 50);   /* the mean first quarter */
    int is_full = -1;
    uint32_t s = moon_next_event_s(MOON_EPOCH_UTC_S + 3600, &is_full);        /* an hour after new: full is next, ~14.7 d away */
    CHECK_EQ_INT(is_full, 1);
    CHECK_EQ_INT((int)(s / 86400), 14);
    s = moon_next_event_s(MOON_EPOCH_UTC_S + MOON_SYNODIC_S / 2 + 3600, &is_full);                /* an hour after full: new is next */
    CHECK_EQ_INT(is_full, 0);
    CHECK_EQ_INT((int)(s / 86400), 14);
    s = moon_next_event_s(MOON_EPOCH_UTC_S, &is_full);                        /* exactly on new: the next one is full, not 0 s */
    CHECK_EQ_INT(is_full, 1);
    CHECK_EQ_INT(s > 86400, 1);
}

static void test_terms_days(void)
{
    /* Term at 2026-11-22 ~ 02:00 UTC is 10:00 in UTC+8; "now" 2026-11-10 */
    int64_t at = utc(2026, 11, 22, 2, 0);
    /* UTC+8: local day of at = Nov 22. 00:00:01 and 23:59:59 local on Nov 10 are the same count (12). */
    int64_t d0 = utc(2026, 11, 9, 16, 0) + 1, d1 = utc(2026, 11, 10, 15, 59) + 59;
    CHECK_EQ_INT(terms_days_until(d0, at, 480), 12);
    CHECK_EQ_INT(terms_days_until(d1, at, 480), 12);
    CHECK_EQ_INT(terms_days_until(d1 + 2, at, 480), 11);        /* crossed local midnight: exactly -1 */
    /* UTC date differs from local date: 2026-11-10 20:00 UTC is already Nov 11 in UTC+8 */
    CHECK_EQ_INT(terms_days_until(utc(2026, 11, 10, 20, 0), at, 480), 11);
    CHECK_EQ_INT(terms_days_until(utc(2026, 11, 10, 20, 0), at, 0), 12);
    /* UTC-5: at is still Nov 21 local (21:00); now Nov 10 02:00 UTC = Nov 9 21:00 local */
    CHECK_EQ_INT(terms_days_until(utc(2026, 11, 10, 2, 0), at, -300), 12);
    /* the day itself is 0, from early and late local hours */
    CHECK_EQ_INT(terms_days_until(utc(2026, 11, 21, 16, 0), at, 480), 0);
    CHECK_EQ_INT(terms_days_until(utc(2026, 11, 22, 1, 59), at, 480), 0);
}

static void test_terms(void)
{
    CHECK_EQ_INT(terms_index(utc(2026, 3, 21, 0, 0)), 0);      /* after the March equinox (2026-03-20 14:46 UTC): Chunfen */
    CHECK_EQ_INT(terms_index(utc(2026, 3, 20, 0, 0)), 23);     /* just before: Jingzhe */
    CHECK_EQ_INT(terms_index(utc(2026, 2, 4, 12, 0)), 21);     /* Lichun began 2026-02-04 ~02:02 UTC */
    CHECK_STR(terms_name_zh(0), "春分");
    CHECK_STR(terms_name_zh(21), "立春");
    CHECK_STR(terms_name_zh(6), "夏至");
}

static void test_sun_taipei(void)
{
    int rise, set;                                              /* Taipei 25.03 N, 121.57 E: the location is what the web page stores (0.01 deg), UTC+8 */
    CHECK_EQ_INT(sun_rise_set(2026, 6, 21, 480, 250300, 1215700, &rise, &set), 0);
    CHECK(abs(rise - (5 * 60 + 4)) <= 4);                       /* ~05:04 local */
    CHECK(abs(set - (18 * 60 + 47)) <= 4);                      /* ~18:47 local */
}

static void test_tz_parse(void)
{
    pc_tz_t z;
    CHECK(pc_tz_parse("UTC-8", &z) && z.std_min == 480 && !z.has_dst);
    CHECK(pc_tz_parse("UTC0", &z) && z.std_min == 0);
    CHECK(pc_tz_parse("<+0545>-5:45", &z) && z.std_min == 345);
    CHECK(pc_tz_parse("UTC3:30", &z) && z.std_min == -210);
    CHECK(pc_tz_parse("<-05>5<-04>,M3.2.0,M11.1.0", &z) && z.has_dst && z.std_min == -300 && z.dst_min == -240);
    CHECK(z.start.m == 3 && z.start.w == 2 && z.start.d == 0 && z.start.time_s == 7200 && z.end.m == 11 && z.end.w == 1);
    CHECK(pc_tz_parse("AEST-10AEDT,M10.1.0,M4.1.0/3", &z) && z.std_min == 600 && z.dst_min == 660 && z.end.time_s == 10800);
    CHECK(pc_tz_parse("<+1030>-10:30<+11>-11,M10.1.0,M4.1.0", &z) && z.std_min == 630 && z.dst_min == 660);   /* Lord Howe: 30 min */
    CHECK(!pc_tz_parse("", &z) && !pc_tz_parse("UTC", &z) && !pc_tz_parse("UTC-8X", &z) && !pc_tz_parse(NULL, &z));
    CHECK(!pc_tz_parse("EST5EDT", &z) && !pc_tz_parse("EST5EDT,J60,J300", &z) && !pc_tz_parse("EST5EDT,M13.1.0,M11.1.0", &z));
    CHECK(!pc_tz_parse("EST5EDT,M3.2.0,M11.1.0,", &z) && !pc_tz_parse("<+08-8", &z));
}

static void test_tz_dst(void)
{
    pc_tz_t ny, syd;
    CHECK(pc_tz_parse("EST5EDT,M3.2.0,M11.1.0", &ny));
    CHECK_EQ_INT(pc_tz_offset_min(&ny, utc(2026, 3, 8, 6, 59)), -300);     /* the second Sunday of March 2026 is the 8th: 02:00 EST = 07:00 UTC */
    CHECK_EQ_INT(pc_tz_offset_min(&ny, utc(2026, 3, 8, 7, 0)), -240);
    CHECK_EQ_INT(pc_tz_offset_min(&ny, utc(2026, 11, 1, 5, 59)), -240);    /* 02:00 EDT = 06:00 UTC on Nov 1 */
    CHECK_EQ_INT(pc_tz_offset_min(&ny, utc(2026, 11, 1, 6, 0)), -300);
    CHECK_EQ_INT(pc_tz_offset_min(&ny, utc(2026, 7, 1, 12, 0)), -240);
    CHECK_EQ_INT(pc_tz_offset_min(&ny, utc(2026, 1, 1, 12, 0)), -300);
    CHECK(pc_tz_parse("AEST-10AEDT,M10.1.0,M4.1.0/3", &syd));              /* southern: DST spans the new year */
    CHECK_EQ_INT(pc_tz_offset_min(&syd, utc(2026, 1, 1, 0, 0)), 660);
    CHECK_EQ_INT(pc_tz_offset_min(&syd, utc(2026, 7, 1, 0, 0)), 600);
    CHECK_EQ_INT(pc_tz_offset_min(&syd, utc(2026, 4, 4, 15, 59)), 660);    /* Apr 5 03:00 AEDT = Apr 4 16:00 UTC */
    CHECK_EQ_INT(pc_tz_offset_min(&syd, utc(2026, 4, 4, 16, 0)), 600);
    CHECK_EQ_INT(pc_tz_offset_min(&syd, utc(2026, 10, 3, 15, 59)), 600);   /* Oct 4 02:00 AEST = Oct 3 16:00 UTC */
    CHECK_EQ_INT(pc_tz_offset_min(&syd, utc(2026, 10, 3, 16, 0)), 660);
    pc_tz_t lon;                                                           /* "last Sunday": week 5 in a month whose last Sunday is the 4th */
    CHECK(pc_tz_parse("GMT0BST,M3.5.0/1,M10.5.0", &lon));
    CHECK_EQ_INT(pc_tz_offset_min(&lon, utc(2026, 3, 29, 0, 59)), 0);      /* the last Sunday of March 2026 is the 29th, 01:00 UTC */
    CHECK_EQ_INT(pc_tz_offset_min(&lon, utc(2026, 3, 29, 1, 0)), 60);
    CHECK_EQ_INT(pc_tz_offset_min(&lon, utc(2026, 10, 25, 0, 59)), 60);    /* 02:00 BST = 01:00 UTC; the rule says 02:00 local */
}

static void test_tz_default(void)
{
    pc_tz_t z;
    CHECK(pc_tz_parse(pc_tz_default(), &z) && z.std_min == 480 && !z.has_dst);
}

static void test_alarm_once(void)
{
    int32_t tz = 480;                                           /* Taipei; 07:00 local = 23:00 UTC the day before */
    int64_t t = utc(2026, 11, 9, 22, 59) + 30;
    CHECK(alarm_crossed(t, t + 40, tz, 7 * 60));                /* 06:59:30 -> 07:00:10 */
    CHECK(!alarm_crossed(t + 40, t + 80, tz, 7 * 60));          /* the next tick: not again */
    CHECK(!alarm_crossed(t - 3600, t - 3500, tz, 7 * 60));
}

int main(void)
{
    RUN(test_tz_parse);
    RUN(test_tz_dst);
    RUN(test_tz_default);
    RUN(test_moon);
    RUN(test_terms);
    RUN(test_terms_days);
    RUN(test_sun_taipei);
    RUN(test_alarm_once);
    return 0;
}
