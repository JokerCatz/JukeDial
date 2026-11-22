// SPDX-License-Identifier: LGPL-2.1-or-later
#ifndef TIME_CLOCK_H
#define TIME_CLOCK_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Pure calendar arithmetic for the idle clock (decision 0018): UTC
 * seconds in, civil date/time out. No platform calls, no libc time.h, no
 * state -- the timezone is a PARAMETER, applied at display only; every
 * stored timestamp in this project stays UTC.
 *
 * Timezone is a fixed offset in MINUTES east of UTC (0018: no daylight
 * saving, ever). Minutes rather than hours because real offsets include
 * :30 and :45 jurisdictions. The range below covers every offset in
 * current use (UTC-12:00 Baker Island to UTC+14:00 Kiribati). */

#define CLOCK_TZ_MIN_LOWEST (-720) /* UTC-12:00 */
#define CLOCK_TZ_MIN_HIGHEST 840   /* UTC+14:00 */
#define CLOCK_TZ_STEP_MIN 30       /* the Settings row's Left/Right step */

/* Clamps a persisted (free-form, possibly hand-edited) offset into the
 * legal range. */
int32_t clock_clamp_tz_min(int32_t tz_min);

/* "UTC", "UTC+8", "UTC-3:30" -- the Settings row's display label.
 * `out` should be at least 10 bytes; longer is fine. */
void clock_tz_label(int32_t tz_min, char *out, size_t outsz);

typedef struct {
    int year;    /* e.g. 2026 */
    int month;   /* 1..12 */
    int day;     /* 1..31 */
    int hour;    /* 0..23 */
    int minute;  /* 0..59 */
    int second;  /* 0..59 */
    int weekday; /* 0=Sunday .. 6=Saturday */
} clock_civil_t;

/* Splits UTC seconds-since-1970 into a civil date/time in the fixed
 * offset `tz_min`. Exact integer arithmetic (Gregorian, proleptic), no
 * leap seconds -- NTP time has none either. */
void clock_civil_from_utc(int64_t utc_s, int32_t tz_min, clock_civil_t *out);

/* Days since 1970-01-01 for a civil date -- the exact inverse of the day
 * split above (Hinnant's days_from_civil). What sun.c/terms.c use to
 * anchor a civil date back onto the UTC axis; the unit suite keeps its
 * own independent copy on purpose, so the two cannot agree by sharing a
 * bug. */
int64_t clock_days_from_civil(int y, int m, int d);

/* Days in month m (1..12) of year y, leap years included (Gregorian); an out-of-range month answers 31. */
int clock_days_in_month(int y, int m);

/* "SUN".."SAT". Out-of-range answers "???", never NULL. */
const char *clock_weekday_name(int weekday);

#ifdef __cplusplus
}
#endif

#endif /* TIME_CLOCK_H */
