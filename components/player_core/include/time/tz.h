// SPDX-License-Identifier: LGPL-2.1-or-later
#ifndef TIME_TZ_H
#define TIME_TZ_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The time zone as a POSIX TZ string (cfg "tz"), e.g. "UTC-8", "<+0530>-5:30", "<-05>5<-04>,M3.2.0,M11.1.0".
 * Pure functions, no libc time/TZ. Supported: std name (3+ letters or <...>), offset [+-]h[:mm[:ss]] (POSIX sign: positive = WEST),
 * optional dst name [offset] and ,Mm.w.d[/time],Mm.w.d[/time] (time may be signed, default 02:00). Julian-day rules are refused.
 * Offsets in this API are minutes EAST of UTC (what clock.h's tz_min is). The zone is set only on the web page (a fixed offset
 * or the browser zone's daylight-saving rules); the device never changes it. */

typedef struct {
    uint8_t m, w, d;       /* month 1..12, week 1..5 (5 = last), weekday 0 = Sunday */
    int32_t time_s;        /* local wall-clock seconds after midnight of that day (may be negative or past 24 h) */
} pc_tz_rule_t;

typedef struct {
    int32_t std_min, dst_min;   /* minutes east of UTC */
    bool has_dst;
    pc_tz_rule_t start, end;    /* start: std -> dst (in standard local time); end: dst -> std (in daylight local time) */
} pc_tz_t;

bool pc_tz_parse(const char *s, pc_tz_t *out);               /* false: not valid (out untouched) */
int32_t pc_tz_offset_min(const pc_tz_t *tz, int64_t utc_s);  /* minutes east of UTC at that instant (daylight saving applied) */
/* The UTC instant of a local wall-clock time given as "local seconds" (the civil time read as if it were UTC). In a repeated autumn hour the first
 * occurrence (daylight time) wins; a time inside the skipped spring hour lands on the same wall time one hour later. */
int64_t pc_tz_local_to_utc(const pc_tz_t *tz, int64_t local_s);
bool pc_tz_is_dst(const pc_tz_t *tz, int64_t utc_s);

const char *pc_tz_default(void);                                /* no cfg "tz": UTC+8 (the same for every language) */

#ifdef __cplusplus
}
#endif

#endif /* TIME_TZ_H */
