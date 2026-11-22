// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_clockapp.h"
#include "time/clock.h"
#include "pc_test.h"

static int64_t local_utc(int y, int mo, int d, int h, int mi) { return clock_days_from_civil(y, mo, d) * 86400 + h * 3600 + mi * 60 - 8 * 3600; }   /* Taipei */
#define TZ 480

static void test_weekdays(void)
{
    pc_alarm_t a = { .on = true, .min = 7 * 60, .days = PC_DAYS_WEEKDAYS };
    int64_t mon = local_utc(2026, 11, 9, 7, 0), sat = local_utc(2026, 11, 14, 7, 0);   /* 2026-11-09 is a Monday */
    CHECK(pc_alarm_check(&a, mon - 30, mon + 10, TZ));
    CHECK(!pc_alarm_check(&a, sat - 30, sat + 10, TZ));       /* not on Saturday */
    a.days = PC_DAYS_WEEKEND;
    CHECK(pc_alarm_check(&a, sat - 30, sat + 10, TZ));
    CHECK(a.on);                                              /* repeating alarms stay on */
}

static void test_once_turns_off(void)
{
    pc_alarm_t a = { .on = true, .min = 6 * 60 + 30, .days = PC_DAYS_ONCE };
    int64_t t = local_utc(2026, 11, 10, 6, 30);
    CHECK(pc_alarm_check(&a, t - 1, t, TZ));
    CHECK(!a.on);
    CHECK(!pc_alarm_check(&a, t + 86400 - 1, t + 86400, TZ));
}

static void test_off_and_snooze(void)
{
    pc_alarm_t a = { .on = false, .min = 7 * 60, .days = PC_DAYS_EVERY };
    int64_t t = local_utc(2026, 11, 10, 7, 0);
    CHECK(!pc_alarm_check(&a, t - 1, t, TZ));
    a.on = true;
    CHECK(pc_alarm_check(&a, t - 1, t, TZ));
    pc_alarm_snooze(&a, t + 20, 9);
    CHECK(!pc_alarm_check(&a, t + 20, t + 300, TZ));
    CHECK(pc_alarm_check(&a, t + 300, t + 20 + 9 * 60, TZ));  /* 9 minutes later */
    CHECK(!pc_alarm_check(&a, t + 20 + 9 * 60, t + 20 + 9 * 60 + 5, TZ));
    /* a once-alarm switches itself off when it rings; its snooze still rings */
    pc_alarm_t o = { .on = true, .min = 7 * 60, .days = PC_DAYS_ONCE };
    CHECK(pc_alarm_check(&o, t - 1, t, TZ));
    CHECK(!o.on);
    pc_alarm_snooze(&o, t + 20, 9);
    CHECK(pc_alarm_check(&o, t + 300, t + 20 + 9 * 60, TZ));
    /* the user switches off (or edits) while a snooze is pending: it never rings */
    pc_alarm_t e = { .on = true, .min = 7 * 60, .days = PC_DAYS_EVERY };
    pc_alarm_snooze(&e, t + 20, 9);
    e.on = false;
    pc_alarm_edited(&e);
    CHECK(!pc_alarm_check(&e, t + 300, t + 20 + 9 * 60, TZ));
    CHECK_EQ_INT((int)e.snooze_until, 0);
}

static void test_pomodoro(void)
{
    pc_pomo_t p;
    pc_pomo_start(&p, 1000);
    CHECK_EQ_INT(p.phase, PC_POMO_FOCUS);
    CHECK_EQ_INT(pc_pomo_left(&p, 1000), 25 * 60);
    CHECK(!pc_pomo_tick(&p, 1000 + 25 * 60 - 1));
    int64_t t = 1000;
    for (int round = 1; round <= 4; round++) {
        t += 25 * 60;
        CHECK(pc_pomo_tick(&p, t));
        CHECK_EQ_INT(p.phase, round == 4 ? PC_POMO_LONG : PC_POMO_BREAK);
        t += round == 4 ? 15 * 60 : 5 * 60;
        CHECK(pc_pomo_tick(&p, t));
        CHECK_EQ_INT(p.phase, PC_POMO_FOCUS);
    }
    CHECK_EQ_INT(p.done, 4);
    CHECK(pc_pomo_tick(&p, t + 3 * 3600));                    /* left alone for hours: still one step, no burst */
    pc_pomo_stop(&p);
    CHECK_EQ_INT(p.phase, PC_POMO_IDLE);
    CHECK_EQ_INT(pc_pomo_left(&p, t), 0);
}

static void test_pomodoro_pause(void)
{
    pc_pomo_t p;
    pc_pomo_start(&p, 1000);
    CHECK(!p.paused);
    pc_pomo_pause(&p, 1000 + 600);                                /* 10 min in: 15 min left */
    CHECK(p.paused);
    CHECK_EQ_INT(pc_pomo_left(&p, 1000 + 600), 15 * 60);
    CHECK_EQ_INT(pc_pomo_left(&p, 1000 + 5 * 3600), 15 * 60);    /* time passes, the left time does not */
    CHECK(!pc_pomo_tick(&p, 1000 + 5 * 3600));                    /* and the phase does not end */
    CHECK_EQ_INT(p.phase, PC_POMO_FOCUS);
    pc_pomo_pause(&p, 1000 + 5 * 3600 + 1);                       /* pausing twice changes nothing */
    CHECK_EQ_INT(pc_pomo_left(&p, 1000 + 5 * 3600 + 1), 15 * 60);
    int64_t r = 1000 + 6 * 3600;
    pc_pomo_resume(&p, r);
    CHECK(!p.paused);
    CHECK_EQ_INT(pc_pomo_left(&p, r), 15 * 60);
    CHECK_EQ_INT(pc_pomo_left(&p, r + 60), 14 * 60);
    CHECK(!pc_pomo_tick(&p, r + 15 * 60 - 1));
    CHECK(pc_pomo_tick(&p, r + 15 * 60));                         /* ends when the remaining time is up */
    pc_pomo_resume(&p, r + 16 * 60);                              /* resuming a running timer changes nothing */
    CHECK_EQ_INT(pc_pomo_left(&p, r + 16 * 60), 4 * 60);
    pc_pomo_pause(&p, r + 16 * 60);
    pc_pomo_stop(&p);
    CHECK(!p.paused);                                             /* stopping from a pause leaves no pause behind */
    CHECK_EQ_INT(p.phase, PC_POMO_IDLE);
    pc_pomo_pause(&p, 5000);                                      /* an idle timer cannot be paused */
    CHECK(!p.paused);
}

static int64_t utc_at(int y, int mo, int d, int h, int mi) { return clock_days_from_civil(y, mo, d) * 86400 + h * 3600 + mi * 60; }
static void test_dst_alarm(void)
{
    pc_tz_t ny, syd;
    CHECK(pc_tz_parse("EST5EDT,M3.2.0,M11.1.0", &ny));
    CHECK(pc_tz_parse("AEST-10AEDT,M10.1.0,M4.1.0/3", &syd));
    /* spring (US 2026-03-08, 02:00 -> 03:00): an alarm at 02:30 is in the skipped hour and rings at the first tick after it, once */
    pc_alarm_t a = { .on = true, .min = 2 * 60 + 30, .days = PC_DAYS_EVERY };
    int64_t t = utc_at(2026, 3, 8, 7, 0);
    CHECK(!pc_alarm_check_tz(&a, t - 3600, t - 1, &ny));
    CHECK(pc_alarm_check_tz(&a, t - 1, t + 1, &ny));
    CHECK(!pc_alarm_check_tz(&a, t + 1, t + 3600, &ny));
    CHECK(!pc_alarm_check_tz(&a, t + 86400 - 100, t + 86400 - 99, &ny));       /* not twice that day */
    CHECK(pc_alarm_check_tz(&a, utc_at(2026, 3, 9, 6, 29), utc_at(2026, 3, 9, 6, 31), &ny));   /* the next day at 02:30 EDT = 06:30 UTC */
    /* autumn (US 2026-11-01, 02:00 EDT -> 01:00 EST): 01:30 happens twice, the alarm rings once */
    pc_alarm_t b = { .on = true, .min = 60 + 30, .days = PC_DAYS_EVERY };
    CHECK(pc_alarm_check_tz(&b, utc_at(2026, 11, 1, 5, 29), utc_at(2026, 11, 1, 5, 31), &ny));      /* 01:30 EDT */
    CHECK(!pc_alarm_check_tz(&b, utc_at(2026, 11, 1, 6, 29), utc_at(2026, 11, 1, 6, 31), &ny));     /* 01:30 EST: the same occurrence */
    CHECK(!pc_alarm_check_tz(&b, utc_at(2026, 11, 1, 5, 59), utc_at(2026, 11, 1, 6, 1), &ny));      /* the wall clock stepped back over the tick */
    CHECK(pc_alarm_check_tz(&b, utc_at(2026, 11, 2, 6, 29), utc_at(2026, 11, 2, 6, 31), &ny));      /* next day 01:30 EST */
    /* southern hemisphere (Sydney): spring forward 2026-10-04 02:00 -> 03:00 (Oct 3 16:00 UTC), back 2026-04-05 03:00 -> 02:00 (Apr 4 16:00 UTC) */
    pc_alarm_t c = { .on = true, .min = 2 * 60 + 30, .days = PC_DAYS_EVERY };
    CHECK(pc_alarm_check_tz(&c, utc_at(2026, 10, 3, 15, 59), utc_at(2026, 10, 3, 16, 1), &syd));
    pc_alarm_t d = { .on = true, .min = 2 * 60 + 30, .days = PC_DAYS_EVERY };
    CHECK(pc_alarm_check_tz(&d, utc_at(2026, 4, 4, 15, 29), utc_at(2026, 4, 4, 15, 31), &syd));    /* 02:30 AEDT */
    CHECK(!pc_alarm_check_tz(&d, utc_at(2026, 4, 4, 16, 29), utc_at(2026, 4, 4, 16, 31), &syd));   /* 02:30 AEST again */
    /* a once-alarm in the skipped hour rings and turns itself off */
    pc_alarm_t o = { .on = true, .min = 2 * 60 + 30, .days = PC_DAYS_ONCE };
    CHECK(pc_alarm_check_tz(&o, t - 1, t + 1, &ny) && !o.on);
}

static void test_local_to_utc(void)
{
    pc_tz_t tp, ny;
    CHECK(pc_tz_parse(pc_tz_default(), &tp));
    CHECK(pc_tz_parse("EST5EDT,M3.2.0,M11.1.0", &ny));
    CHECK(clock_days_in_month(2026, 2) == 28 && clock_days_in_month(2028, 2) == 29 && clock_days_in_month(1900, 2) == 28 && clock_days_in_month(2000, 2) == 29);
    CHECK(clock_days_in_month(2026, 4) == 30 && clock_days_in_month(2026, 12) == 31);
    int64_t l = clock_days_from_civil(2026, 11, 22) * 86400 + 15 * 3600 + 30 * 60;
    CHECK(pc_tz_local_to_utc(&tp, l) == l - 8 * 3600);                                  /* "UTC-8" = UTC+8, the project default */
    CHECK(pc_tz_local_to_utc(&ny, l) == l + 5 * 3600);                                  /* winter: EST */
    int64_t s = clock_days_from_civil(2026, 7, 1) * 86400 + 12 * 3600;
    CHECK(pc_tz_local_to_utc(&ny, s) == s + 4 * 3600);                                  /* summer: EDT */
    int64_t rep = clock_days_from_civil(2026, 11, 1) * 86400 + 90 * 60;                 /* 01:30 happens twice: the first (EDT) */
    CHECK(pc_tz_local_to_utc(&ny, rep) == rep + 4 * 3600);
    int64_t gap = clock_days_from_civil(2026, 3, 8) * 86400 + 2 * 3600 + 30 * 60;       /* 02:30 does not exist: lands on 03:30 EDT */
    clock_civil_t c;
    clock_civil_from_utc(pc_tz_local_to_utc(&ny, gap), pc_tz_offset_min(&ny, pc_tz_local_to_utc(&ny, gap)), &c);
    CHECK(c.hour == 3 && c.minute == 30);
}

int main(void)
{
    RUN(test_local_to_utc);
    RUN(test_weekdays);
    RUN(test_dst_alarm);
    RUN(test_once_turns_off);
    RUN(test_off_and_snooze);
    RUN(test_pomodoro);
    RUN(test_pomodoro_pause);
    return 0;
}
