// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "pc/pc_clockapp.h"
/* Clock, alarm, pomodoro (spec 2026-11-10-clock-design.md). LVGL context. */
void clock_svc_start(void);          /* boot: SNTP when Wi-Fi is up, the 1 s check timer */
bool clock_time_valid(void);         /* synced since boot */
int64_t clock_now_utc(void);
int32_t clock_tz_min(void);          /* minutes east of UTC now (daylight saving applied when the stored zone has rules) */
const char *clock_tz_str(void);      /* cfg "tz" (POSIX TZ string); absent or invalid: UTC+8, whatever the language; set only on the web page */
int32_t clock_tz_min_at(int64_t utc);/* the offset in force at that instant */
int32_t clock_tz_day_min(int y, int m, int d);   /* the offset in force at noon of that local day (sunrise / sunset of that day) */
bool clock_tz_valid(const char *posix);
bool clock_tz_set(const char *posix);/* false = not a valid string (nothing stored) */
void clock_zone_label(char *out, size_t n);  /* "UTC+8"; "UTC-4 DST" while daylight saving is in force */
bool clock_sun_has(void);            /* a sunrise/sunset location was set from the web page (cfg sun_lat / sun_lon, 0.01 deg) */
int32_t clock_sun_lat_e4(void);      /* degrees * 1e4 (sun.h) */
int32_t clock_sun_lon_e4(void);
void clock_sun_set(int lat_c, int lon_c);   /* 0.01 degrees, north / east positive; range-checked by the caller */
void clock_sun_clear(void);
bool clock_set_manual(int y, int mo, int d, int h, int mi);   /* set the clock to this LOCAL time (cfg tz), seconds 0; false: out of range (2025-01-01 .. 2099-12-31), nothing set. A later SNTP sync overrides it */
bool clock_rtc_keeps(void);                                   /* the battery clock (FakePod RTC) is there and holds a trustworthy time: the Set time page says the time survives power-off */
bool clock_rtc_write_now(void);                               /* console `rtc set`: write the system time to the chip now (false without the chip or when the read-back fails) */
void clock_sync_now(void);           /* Wi-Fi on and SNTP restart */
pc_alarm_t *clock_alarm(void);       /* edit, then clock_alarm_saved() */
void clock_alarm_saved(void);
const char *clock_alarm_sound(void); /* "" = built-in */
void clock_alarm_set_sound(const char *path);
void clock_alarm_test(bool on);      /* preview the ring sound */
bool clock_ringing(void);
bool clock_pomo_chiming(void);       /* the pomodoro chime sounded in the last 5 s (drink_svc stays quiet then) */
void clock_ring_stop(void);
void clock_ring_snooze(void);
pc_pomo_t *clock_pomo(void);
void clock_pomo_start(void);
void clock_pomo_stop(void);
void clock_pomo_pause(void);
void clock_pomo_resume(void);
