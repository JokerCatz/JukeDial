// SPDX-License-Identifier: LGPL-2.1-or-later
#include "time/alarm.h"

#include <stdio.h>

#include "time/clock.h"

int32_t alarm_wrap_min(int32_t alarm_min)
{
    alarm_min %= 1440;
    if (alarm_min < 0) {
        alarm_min += 1440;
    }
    return alarm_min;
}

void alarm_label(int32_t alarm_min, char *out, size_t outsz)
{
    alarm_min = alarm_wrap_min(alarm_min);
    snprintf(out, outsz, "%02ld:%02ld", (long)(alarm_min / 60),
             (long)(alarm_min % 60));
}

/* Floor division -- C's / truncates toward zero, wrong for the pre-1970
 * negatives a UTC-12 offset can produce, same hazard
 * clock_civil_from_utc handles. */
static int64_t floordiv(int64_t a, int64_t b)
{
    int64_t q = a / b;
    if (a % b != 0 && (a < 0) != (b < 0)) {
        q--;
    }
    return q;
}

bool alarm_crossed(int64_t prev_utc, int64_t now_utc, int32_t tz_min,
                   int32_t alarm_min)
{
    if (prev_utc >= now_utc) {
        return false;
    }
    alarm_min = alarm_wrap_min(alarm_min);
    tz_min = clock_clamp_tz_min(tz_min);
    /* Shift the local timeline so the alarm's daily occurrences land
     * exactly on day boundaries; the crossing test is then "did the day
     * number advance". Occurrence-counting by construction: however far
     * the interval reaches, the answer is one boolean. */
    int64_t shift = (int64_t)tz_min * 60 - (int64_t)alarm_min * 60;
    return floordiv(now_utc + shift, 86400) > floordiv(prev_utc + shift, 86400);
}
