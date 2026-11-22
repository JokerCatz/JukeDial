// SPDX-License-Identifier: LGPL-2.1-or-later
/* pc_habit (ideas G7): local day / minute, the drink reminder's rules, the habit streaks. */
#include "pc/pc_habit.h"
#include "pc_test.h"

#define TZ 480                                   /* Taipei, UTC+8, no DST */
static int64_t at(int day, int h, int m) { return (int64_t)day * 86400 + (h * 60 + m) * 60 - TZ * 60; }   /* the UTC second of local day `day`, h:m */

static void test_local_time(void)
{
    CHECK_EQ_INT(pc_hb_local_day(0, 0), 0);
    CHECK_EQ_INT(pc_hb_local_day(86399, 0), 0);
    CHECK_EQ_INT(pc_hb_local_day(86400, 0), 1);
    CHECK_EQ_INT(pc_hb_local_day(-1, 0), -1);                       /* before 1970: floor */
    CHECK_EQ_INT(pc_hb_local_day(at(20000, 0, 0), TZ), 20000);      /* local midnight */
    CHECK_EQ_INT(pc_hb_local_day(at(20000, 0, 0) - 1, TZ), 19999);
    CHECK_EQ_INT(pc_hb_local_minute(at(20000, 23, 59), TZ), 23 * 60 + 59);
    CHECK_EQ_INT(pc_hb_local_minute(at(20000, 0, 0), TZ), 0);
    CHECK_EQ_INT(pc_hb_local_minute(-1, 0), 24 * 60 - 1);
    CHECK_EQ_INT(pc_hb_local_day(1700000000, 480), 19676);          /* 2023-11-15 06:13 Taipei */
}

static void test_cfg(void)
{
    pc_drink_cfg_t c;
    pc_drink_cfg_default(&c);
    CHECK(c.on == 0 && c.interval == 2 && c.start_h == 9 && c.end_h == 21 && c.goal == 8);
    CHECK_EQ_INT(pc_drink_interval_min(0), 30);
    CHECK_EQ_INT(pc_drink_interval_min(1), 45);
    CHECK_EQ_INT(pc_drink_interval_min(2), 60);
    CHECK_EQ_INT(pc_drink_interval_min(3), 90);
    CHECK_EQ_INT(pc_drink_interval_min(9), 90);
    pc_drink_step_interval(&c, 1); CHECK_EQ_INT(c.interval, 3);
    pc_drink_step_interval(&c, 1); CHECK_EQ_INT(c.interval, 0);     /* wraps */
    pc_drink_step_interval(&c, -1); CHECK_EQ_INT(c.interval, 3);
    pc_drink_step_start(&c, 20); CHECK(c.start_h == 23 && c.end_h == 24);   /* the end follows */
    pc_drink_step_start(&c, -50); CHECK_EQ_INT(c.start_h, 0);
    pc_drink_step_end(&c, -50); CHECK(c.end_h == 1 && c.start_h == 0);
    pc_drink_step_end(&c, 99); CHECK_EQ_INT(c.end_h, 24);
    c.start_h = 10; c.end_h = 12;
    pc_drink_step_end(&c, -5); CHECK(c.end_h == 7 && c.start_h == 6);       /* the start follows */
    pc_drink_step_goal(&c, -99); CHECK_EQ_INT(c.goal, PC_DRINK_GOAL_MIN);
    pc_drink_step_goal(&c, 99); CHECK_EQ_INT(c.goal, PC_DRINK_GOAL_MAX);
    c = (pc_drink_cfg_t){ .on = 7, .interval = 200, .start_h = 30, .end_h = 0, .goal = 0 };
    pc_drink_cfg_fix(&c);
    CHECK(c.on == 1 && c.interval == 3 && c.start_h == 23 && c.end_h == 24 && c.goal == 1);
    pc_drink_cfg_default(&c);
    CHECK(!pc_drink_in_window(&c, 9 * 60 - 1) && pc_drink_in_window(&c, 9 * 60) && pc_drink_in_window(&c, 21 * 60 - 1) && !pc_drink_in_window(&c, 21 * 60));
    c.end_h = 24;
    CHECK(pc_drink_in_window(&c, 24 * 60 - 1));
}

static void test_drink_reminder(void)
{
    pc_drink_cfg_t c;
    pc_drink_cfg_default(&c);                    /* 60 min, 09-21, 8 cups */
    CHECK(!pc_drink_tick(&(pc_drink_t){ .day = 1 }, &c, at(20000, 10, 0), TZ));   /* off: never */
    c.on = 1;
    pc_drink_t s = { 0 };
    int d = 20000;
    CHECK(pc_drink_sync_day(&s, d) && s.day == d && s.cups == 0);
    CHECK(!pc_drink_sync_day(&s, d));
    CHECK(!pc_drink_tick(&s, &c, at(d, 8, 0), TZ) && s.next_due == 0);            /* before the window: nothing pending */
    CHECK(!pc_drink_tick(&s, &c, at(d, 9, 0), TZ) && s.next_due == at(d, 10, 0)); /* the window opens: the first one an interval later */
    CHECK(!pc_drink_tick(&s, &c, at(d, 9, 59), TZ));
    CHECK(pc_drink_tick(&s, &c, at(d, 10, 0), TZ) && s.next_due == at(d, 11, 0));
    CHECK(!pc_drink_tick(&s, &c, at(d, 10, 0), TZ));                              /* once */
    pc_drink_cup(&s, &c, at(d, 10, 5));
    CHECK(s.cups == 1 && s.next_due == at(d, 11, 5));                              /* a cup moves it */
    CHECK(!pc_drink_tick(&s, &c, at(d, 11, 0), TZ));
    CHECK(pc_drink_tick(&s, &c, at(d, 11, 5), TZ));
    pc_drink_snooze(&s, at(d, 11, 6));
    CHECK(s.next_due == at(d, 11, 16));
    CHECK(!pc_drink_tick(&s, &c, at(d, 11, 15), TZ) && pc_drink_tick(&s, &c, at(d, 11, 16), TZ));
    /* the window closes: nothing, and the pending one is dropped; it reopens a day later with the new day's rules */
    CHECK(!pc_drink_tick(&s, &c, at(d, 21, 0), TZ) && s.next_due == 0);
    CHECK(!pc_drink_tick(&s, &c, at(d, 23, 59), TZ));
    CHECK(pc_drink_sync_day(&s, d + 1) && s.cups == 0 && s.next_due == 0);          /* midnight: a new day */
    /* the goal reached: no more reminders */
    s.cups = 8;
    CHECK(!pc_drink_tick(&s, &c, at(d + 1, 12, 0), TZ) && !pc_drink_tick(&s, &c, at(d + 1, 20, 0), TZ));
    pc_drink_undo(&s);
    CHECK_EQ_INT(s.cups, 7);
    CHECK(!pc_drink_tick(&s, &c, at(d + 1, 12, 0), TZ) && pc_drink_tick(&s, &c, at(d + 1, 13, 0), TZ));
    /* the clock went back (a resync): the next one is never more than an interval away */
    s.next_due = at(d + 1, 20, 0);
    CHECK(!pc_drink_tick(&s, &c, at(d + 1, 14, 0), TZ) && s.next_due == at(d + 1, 15, 0));
    /* caps */
    s.cups = PC_DRINK_CUPS_MAX;
    pc_drink_cup(&s, &c, at(d + 1, 14, 0));
    CHECK_EQ_INT(s.cups, PC_DRINK_CUPS_MAX);
    s.cups = 0; pc_drink_undo(&s); CHECK_EQ_INT(s.cups, 0);
    /* a 30 minute interval in a window that ends at 24 */
    c.interval = 0; c.end_h = 24;
    s = (pc_drink_t){ .day = d };
    CHECK(!pc_drink_tick(&s, &c, at(d, 23, 0), TZ));
    CHECK(pc_drink_tick(&s, &c, at(d, 23, 30), TZ));
    CHECK(!pc_drink_tick(&s, &c, at(d, 23, 59), TZ));
}

static void test_habit_streak(void)
{
    pc_hb_t h = { 0 };
    int t = 20000;
    CHECK(!pc_hb_done(&h, t) && pc_hb_streak(&h, t) == 0 && !pc_hb_on(&h, t, 0));
    CHECK(pc_hb_mark(&h, t));
    CHECK(!pc_hb_mark(&h, t));                                      /* one a day */
    CHECK(pc_hb_done(&h, t) && pc_hb_streak(&h, t) == 1);
    CHECK(pc_hb_mark(&h, t + 1) && pc_hb_mark(&h, t + 2));
    CHECK_EQ_INT(pc_hb_streak(&h, t + 2), 3);
    CHECK_EQ_INT(pc_hb_streak(&h, t + 3), 3);                       /* tomorrow's not over: the streak stands */
    CHECK_EQ_INT(pc_hb_streak(&h, t + 4), 0);                       /* a day was missed: broken */
    CHECK(pc_hb_on(&h, t + 2, 0) && pc_hb_on(&h, t + 2, 1) && pc_hb_on(&h, t + 2, 2) && !pc_hb_on(&h, t + 2, 3));
    CHECK(pc_hb_mark(&h, t + 4));                                   /* after the gap: a new streak of 1, the old days still in the strip */
    CHECK_EQ_INT(pc_hb_streak(&h, t + 4), 1);
    CHECK(!pc_hb_on(&h, t + 4, 1) && pc_hb_on(&h, t + 4, 2) && pc_hb_on(&h, t + 4, 3) && pc_hb_on(&h, t + 4, 4));
    char s[7];
    CHECK_EQ_INT(pc_hb_strip(&h, t + 4, s, sizeof s), 0);           /* too small */
    char b[16];
    CHECK_EQ_INT(pc_hb_strip(&h, t + 4, b, sizeof b), 7);
    CHECK_STR(b, "..###.#");                                      /* t-2 .. t+4 */
    /* the clock one day back never marks or breaks (a zone change); further back the record restarts (test_clock_back) */
    CHECK(!pc_hb_mark(&h, t + 3));
    CHECK_EQ_INT(pc_hb_streak(&h, t + 3), 1);
    /* a long gap clears the mask, a streak over 32 days keeps counting */
    pc_hb_t g = { 0 };
    for (int i = 0; i < 40; i++) CHECK(pc_hb_mark(&g, t + i));
    CHECK_EQ_INT(pc_hb_streak(&g, t + 39), 40);
    CHECK(pc_hb_on(&g, t + 39, 31) && !pc_hb_on(&g, t + 39, 32));
    CHECK(pc_hb_mark(&g, t + 100));
    CHECK(pc_hb_streak(&g, t + 100) == 1 && g.mask == 1u && !pc_hb_on(&g, t + 100, 1));
}

static void test_habit_unmark(void)
{
    pc_hb_t h = { 0 };
    int t = 30000;
    CHECK(!pc_hb_unmark(&h, t));
    pc_hb_mark(&h, t);
    CHECK(pc_hb_unmark(&h, t) && h.last == 0 && h.mask == 0 && h.streak == 0);   /* back to never */
    pc_hb_mark(&h, t); pc_hb_mark(&h, t + 1); pc_hb_mark(&h, t + 2);
    CHECK(!pc_hb_unmark(&h, t + 3));                                 /* today is not marked */
    CHECK(pc_hb_unmark(&h, t + 2));
    CHECK(h.last == t + 1 && pc_hb_streak(&h, t + 2) == 2 && pc_hb_on(&h, t + 2, 1) && !pc_hb_on(&h, t + 2, 0));
    CHECK(pc_hb_mark(&h, t + 2) && pc_hb_streak(&h, t + 2) == 3);    /* mark again: as before */
    pc_hb_t g = { 0 };                                               /* unmark after a gap: the earlier run comes back */
    pc_hb_mark(&g, t); pc_hb_mark(&g, t + 1); pc_hb_mark(&g, t + 5);
    CHECK_EQ_INT(g.streak, 1);
    CHECK(pc_hb_unmark(&g, t + 5) && g.last == t + 1 && g.streak == 2);
    pc_hb_t l = { 0 };
    for (int i = 0; i < 40; i++) pc_hb_mark(&l, t + i);
    CHECK(pc_hb_unmark(&l, t + 39) && l.streak == 39 && l.last == t + 38);
}

static void test_clock_back(void)
{
    pc_hb_t h = { 0 };
    pc_hb_mark(&h, 30000); pc_hb_mark(&h, 30001);
    CHECK(!pc_hb_mark(&h, 30000));                                  /* one day back: refused, the record stands */
    CHECK(h.last == 30001 && h.streak == 2);
    CHECK(pc_hb_mark(&h, 20000));                                   /* a wrong future date was corrected: a fresh record from today */
    CHECK(h.last == 20000 && h.streak == 1 && h.mask == 1u);
    CHECK(!pc_hb_mark(&h, 20000));
    pc_drink_t s = { .day = 100, .cups = 5, .next_due = 7 };
    CHECK(!pc_drink_sync_day(&s, 100) && s.cups == 5);
    CHECK(!pc_drink_sync_day(&s, 99) && s.cups == 5 && s.day == 100 && s.next_due == 7);   /* one day back: the cups stand */
    CHECK(pc_drink_sync_day(&s, 50) && s.day == 50 && s.cups == 0 && s.next_due == 0);      /* far back: restart cleanly */
    s.cups = 3;
    CHECK(pc_drink_sync_day(&s, 51) && s.cups == 0);                /* a later day */
}

int main(void)
{
    RUN(test_local_time);
    RUN(test_cfg);
    RUN(test_drink_reminder);
    RUN(test_habit_streak);
    RUN(test_habit_unmark);
    RUN(test_clock_back);
    return 0;
}
