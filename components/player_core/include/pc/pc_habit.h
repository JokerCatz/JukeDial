// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* pc_habit (ideas G7): the pure rules of 喝水提醒 and 習慣打卡 (clock family). No I/O, no clock of its own: every call takes the time.
 * Local time = UTC + the time zone's offset in minutes at that moment (the clock service's clock_tz_min; daylight-saving rules of a web-set zone apply): a local DAY is floor((utc + tz*60) / 86400) days since
 * 1970-01-01, the local MINUTE is the minute of that day. A reminder and a mark belong to the local day they happen in; midnight starts a new one.
 *
 * Drink: interval (30 / 45 / 60 / 90 min), active hours [start_h, end_h) (start 0..23, end 1..24, always start < end: the stepping keeps it so, no window across
 * midnight), a goal of cups a day. Reminder = pc_drink_tick says "due": the first one is one interval after the tick first sees the window open (or after a cup / a
 * snooze), then every interval; none while off, none outside the window, none once the goal is reached, none while the day's state is for another day (call pc_drink_sync_day first).
 * A cup (or a snooze) moves the next reminder; a clock that jumps back cannot make the next reminder further away than one interval.
 *
 * Habits: per habit a record {last day marked, a 32-day mask (bit k = done on day last-k), the streak ending at last}. One mark a local day; the streak counts
 * consecutive days, a missed day breaks it (it shows 0 once a day passed with no mark; yesterday's streak still stands until today ends). */
#define PC_DRINK_INTERVALS 4
#define PC_DRINK_GOAL_MIN 1
#define PC_DRINK_GOAL_MAX 20
#define PC_DRINK_CUPS_MAX 99
#define PC_DRINK_SNOOZE_MIN 10
typedef struct { uint8_t on, interval, start_h, end_h, goal; } pc_drink_cfg_t;   /* on: reminders on (default off: a device that buzzes by itself is opt-in); interval: index into the four values */
typedef struct { int32_t day; int cups; int64_t next_due; } pc_drink_t;         /* next_due: UTC seconds, 0 = none yet */

int32_t pc_hb_local_day(int64_t utc, int32_t tz_min);
int pc_hb_local_minute(int64_t utc, int32_t tz_min);

int pc_drink_interval_min(int idx);                       /* 30, 45, 60, 90 (idx out of range is clamped) */
void pc_drink_cfg_default(pc_drink_cfg_t *c);             /* off, 60 min, 09:00 - 21:00, 8 cups */
void pc_drink_cfg_fix(pc_drink_cfg_t *c);                 /* clamps every field; start < end */
void pc_drink_step_interval(pc_drink_cfg_t *c, int d);    /* wraps round the four values */
void pc_drink_step_start(pc_drink_cfg_t *c, int d);       /* clamps 0..23; an end that would not be after it moves to start + 1 */
void pc_drink_step_end(pc_drink_cfg_t *c, int d);         /* clamps 1..24; a start that would not be before it moves to end - 1 */
void pc_drink_step_goal(pc_drink_cfg_t *c, int d);        /* clamps PC_DRINK_GOAL_MIN..MAX */
bool pc_drink_in_window(const pc_drink_cfg_t *c, int minute);

bool pc_drink_sync_day(pc_drink_t *s, int32_t day);       /* true: a later day, or a clock more than a day back (restart): cups 0, no reminder pending; the caller saves. The same day or one day back: false, nothing changes */
bool pc_drink_tick(pc_drink_t *s, const pc_drink_cfg_t *c, int64_t now_utc, int32_t tz_min);   /* true: remind now (and the next one is scheduled) */
void pc_drink_cup(pc_drink_t *s, const pc_drink_cfg_t *c, int64_t now_utc);    /* +1 (to PC_DRINK_CUPS_MAX); the next reminder is a full interval away */
void pc_drink_undo(pc_drink_t *s);                        /* -1 (not below 0) */
void pc_drink_snooze(pc_drink_t *s, int64_t now_utc);     /* the next reminder in PC_DRINK_SNOOZE_MIN minutes */

#define PC_HB_STRIP 7
typedef struct { int32_t last; uint32_t mask; uint32_t streak; } pc_hb_t;   /* last 0: never marked */
bool pc_hb_mark(pc_hb_t *h, int32_t today);               /* false: already marked today, or the clock is one day back. A last mark more than a day in the future (a wrong date since corrected) restarts the record from today */
bool pc_hb_unmark(pc_hb_t *h, int32_t today);             /* takes today's mark away; false when there is none */
bool pc_hb_done(const pc_hb_t *h, int32_t today);         /* marked today */
bool pc_hb_on(const pc_hb_t *h, int32_t today, int back); /* marked on the day `back` days ago (0 = today; 0..31) */
uint32_t pc_hb_streak(const pc_hb_t *h, int32_t today);   /* 0 when the last mark is older than yesterday */
int pc_hb_strip(const pc_hb_t *h, int32_t today, char *out, int cap);   /* PC_HB_STRIP chars, oldest first, today last: '#' done, '.' not; NUL-terminated; returns 7 (0: cap too small) */
#ifdef __cplusplus
}
#endif
