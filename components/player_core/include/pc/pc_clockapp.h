// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Alarm rules and the pomodoro timer (spec 2026-11-10-clock-design.md). */
#define PC_DAYS_ONCE 0x00
#define PC_DAYS_EVERY 0x7F            /* bit 0 = Sunday ... bit 6 = Saturday */
#define PC_DAYS_WEEKDAYS 0x3E
#define PC_DAYS_WEEKEND 0x41
typedef struct { bool on; int min; uint8_t days; int64_t snooze_until; int64_t last_occ; /* the local occurrence (day number) that rang last; 0 = none */ } pc_alarm_t;
#include "time/tz.h"
/* true when it should start ringing in (prev, now] (UTC seconds); a once-alarm turns itself off when it rings */
bool pc_alarm_check(pc_alarm_t *a, int64_t prev, int64_t now, int32_t tz_min);
/* The same with a zone that may have daylight-saving rules: each tick is read with its own offset, so an alarm inside the skipped spring hour rings at the
 * first moment after it, and one inside the repeated autumn hour rings once (the occurrence that rang last is remembered). */
bool pc_alarm_check_tz(pc_alarm_t *a, int64_t prev, int64_t now, const pc_tz_t *tz);
void pc_alarm_snooze(pc_alarm_t *a, int64_t now, int minutes);
void pc_alarm_edited(pc_alarm_t *a);   /* the user switched it off or changed time/days: a pending snooze is cancelled */

typedef enum { PC_POMO_IDLE, PC_POMO_FOCUS, PC_POMO_BREAK, PC_POMO_LONG } pc_pomo_phase_t;
typedef struct { pc_pomo_phase_t phase; int64_t ends; int done; int focus_s, break_s, long_s, every; bool paused; int paused_left; } pc_pomo_t;
void pc_pomo_start(pc_pomo_t *p, int64_t now);   /* 25 / 5 min, a 15 min break after every 4th focus (seconds: any clock) */
bool pc_pomo_tick(pc_pomo_t *p, int64_t now);    /* true when the phase changed: sound the chime */
int  pc_pomo_left(const pc_pomo_t *p, int64_t now);
void pc_pomo_stop(pc_pomo_t *p);
void pc_pomo_pause(pc_pomo_t *p, int64_t now);   /* a running phase stops counting; the time left is kept (idle or already paused: nothing) */
void pc_pomo_resume(pc_pomo_t *p, int64_t now);  /* counts on from the time left (not paused: nothing) */
#ifdef __cplusplus
}
#endif
