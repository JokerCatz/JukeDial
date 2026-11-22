// SPDX-License-Identifier: LGPL-2.1-or-later
#include "time/clock.h"

#include <stdio.h>

int32_t clock_clamp_tz_min(int32_t tz_min)
{
    if (tz_min < CLOCK_TZ_MIN_LOWEST) {
        return CLOCK_TZ_MIN_LOWEST;
    }
    if (tz_min > CLOCK_TZ_MIN_HIGHEST) {
        return CLOCK_TZ_MIN_HIGHEST;
    }
    return tz_min;
}

void clock_tz_label(int32_t tz_min, char *out, size_t outsz)
{
    tz_min = clock_clamp_tz_min(tz_min);
    if (tz_min == 0) {
        snprintf(out, outsz, "UTC");
        return;
    }
    int32_t a = tz_min < 0 ? -tz_min : tz_min;
    if (a % 60 == 0) {
        snprintf(out, outsz, "UTC%c%ld", tz_min < 0 ? '-' : '+', (long)(a / 60));
    } else {
        snprintf(out, outsz, "UTC%c%ld:%02ld", tz_min < 0 ? '-' : '+',
                 (long)(a / 60), (long)(a % 60));
    }
}

/* Days-since-1970 -> y/m/d, Howard Hinnant's civil_from_days: exact for
 * the whole proleptic Gregorian calendar, all-integer, branch-light. The
 * era/day-of-era decomposition is the standard published derivation, not
 * an invention of this file. */
static void civil_from_days(int64_t z, int *y, int *m, int *d)
{
    z += 719468; /* shift epoch from 1970-01-01 to 0000-03-01 */
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    int64_t doe = z - era * 146097;                                    /* [0, 146096] */
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365; /* [0, 399] */
    int64_t yy = yoe + era * 400;
    int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);             /* [0, 365] */
    int64_t mp = (5 * doy + 2) / 153;                                  /* [0, 11] */
    int64_t dd = doy - (153 * mp + 2) / 5 + 1;                         /* [1, 31] */
    int64_t mm = mp < 10 ? mp + 3 : mp - 9;                            /* [1, 12] */
    *y = (int)(yy + (mm <= 2 ? 1 : 0));
    *m = (int)mm;
    *d = (int)dd;
}

int64_t clock_days_from_civil(int y, int m, int d)
{
    /* Hinnant's days_from_civil -- the exact inverse of civil_from_days
     * above, same published derivation. */
    int64_t yy = y - (m <= 2 ? 1 : 0);
    int64_t era = (yy >= 0 ? yy : yy - 399) / 400;
    int64_t yoe = yy - era * 400;                                  /* [0, 399] */
    int64_t mp = m > 2 ? m - 3 : m + 9;                            /* [0, 11] */
    int64_t doy = (153 * mp + 2) / 5 + d - 1;                      /* [0, 365] */
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;           /* [0, 146096] */
    return era * 146097 + doe - 719468;
}

void clock_civil_from_utc(int64_t utc_s, int32_t tz_min, clock_civil_t *out)
{
    int64_t local = utc_s + (int64_t)clock_clamp_tz_min(tz_min) * 60;
    /* Floor division, so a pre-1970 instant (a negative local second,
     * reachable at UTC-12 in the epoch's first hours) still lands on the
     * right day rather than one off. */
    int64_t days = local / 86400;
    int64_t sod = local % 86400;
    if (sod < 0) {
        sod += 86400;
        days -= 1;
    }
    civil_from_days(days, &out->year, &out->month, &out->day);
    out->hour = (int)(sod / 3600);
    out->minute = (int)((sod / 60) % 60);
    out->second = (int)(sod % 60);
    /* 1970-01-01 was a Thursday (weekday 4, 0=Sunday). */
    int64_t wd = (days + 4) % 7;
    if (wd < 0) {
        wd += 7;
    }
    out->weekday = (int)wd;
}

const char *clock_weekday_name(int weekday)
{
    static const char *const k_names[7] = { "SUN", "MON", "TUE", "WED",
                                            "THU", "FRI", "SAT" };
    if (weekday < 0 || weekday > 6) {
        return "???";
    }
    return k_names[weekday];
}

int clock_days_in_month(int y, int m)
{
    static const int8_t k_len[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (m < 1 || m > 12) {
        return 31;
    }
    const int leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
    return k_len[m - 1] + (m == 2 && leap);
}
