// SPDX-License-Identifier: LGPL-2.1-or-later
#ifndef TIME_ALARM_H
#define TIME_ALARM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The alarm's pure arithmetic (decision 0018's PLAYER tier: waking to a
 * chosen track). Only the math lives here -- the firing policy (what to
 * play, the already-playing rule) is app/main/clock_svc.c's; the rows are
 * screen_settings.c's. The alarm time is LOCAL minutes of day, because
 * that is what a person sets; the crossing test converts through the
 * same fixed offset the clock displays with. */

#define ALARM_MIN_DEFAULT (7 * 60) /* 07:00, a default that reads as an alarm */
#define ALARM_STEP_MIN 15          /* the Settings row's Left/Right step */

/* Wraps any persisted (possibly hand-edited) value into 0..1439 --
 * time-of-day is circular, so wrapping is the truthful normalization
 * (unlike the TZ clamp, whose ends are real jurisdictions). */
int32_t alarm_wrap_min(int32_t alarm_min);

/* "07:00" -- the Settings row's and the idle page's display label.
 * `out` should be at least 6 bytes; longer is fine. */
void alarm_label(int32_t alarm_min, char *out, size_t outsz);

/* True when the local wall clock crossed alarm_min between the two UTC
 * instants: some instant t in (prev_utc, now_utc] reads exactly
 * alarm_min (at second 0) in the offset tz_min. Counted by whole
 * occurrences, so a clock jump (an NTP step over several hours -- or
 * days) that swallows the alarm instant still fires exactly once, and a
 * backwards step fires nothing until the wall clock passes the time
 * again. prev_utc >= now_utc answers false. */
bool alarm_crossed(int64_t prev_utc, int64_t now_utc, int32_t tz_min,
                   int32_t alarm_min);

#ifdef __cplusplus
}
#endif

#endif /* TIME_ALARM_H */
