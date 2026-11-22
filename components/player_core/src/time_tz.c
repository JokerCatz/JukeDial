// SPDX-License-Identifier: LGPL-2.1-or-later
#include "time/tz.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "time/clock.h"

/* offset [+-]h[:mm[:ss]] -> seconds (sign kept); false when no digit */
static bool num_time(const char **p, int32_t *out)
{
    const char *s = *p;
    int sign = 1;
    if (*s == '+' || *s == '-') { sign = *s == '-' ? -1 : 1; s++; }
    if (!isdigit((unsigned char)*s)) return false;
    int32_t v[3] = { 0, 0, 0 };
    for (int i = 0; i < 3; i++) {
        if (!isdigit((unsigned char)*s)) return false;
        int32_t n = 0, dg = 0;
        while (isdigit((unsigned char)*s)) { n = n * 10 + (*s++ - '0'); if (++dg > 3) return false; }
        v[i] = n;
        if (*s != ':') break;
        s++;
    }
    if (v[0] > 167 || v[1] > 59 || v[2] > 59) return false;
    *out = sign * (v[0] * 3600 + v[1] * 60 + v[2]);
    *p = s;
    return true;
}
static bool name(const char **p)
{
    const char *s = *p;
    if (*s == '<') {
        s++;
        int n = 0;
        while (*s && *s != '>') { if (!isalnum((unsigned char)*s) && *s != '+' && *s != '-') return false; s++; n++; }
        if (*s != '>' || n < 1) return false;
        *p = s + 1;
        return true;
    }
    int n = 0;
    while (isalpha((unsigned char)*s)) { s++; n++; }
    if (n < 3) return false;
    *p = s;
    return true;
}
static bool rule(const char **p, pc_tz_rule_t *r)
{
    const char *s = *p;
    if (*s++ != 'M') return false;
    int v[3];
    for (int i = 0; i < 3; i++) {
        if (!isdigit((unsigned char)*s)) return false;
        v[i] = 0;
        while (isdigit((unsigned char)*s)) v[i] = v[i] * 10 + (*s++ - '0');
        if (i < 2) { if (*s++ != '.') return false; }
    }
    if (v[0] < 1 || v[0] > 12 || v[1] < 1 || v[1] > 5 || v[2] > 6) return false;
    r->m = (uint8_t)v[0]; r->w = (uint8_t)v[1]; r->d = (uint8_t)v[2];
    r->time_s = 7200;
    if (*s == '/') { s++; if (!num_time(&s, &r->time_s)) return false; }
    *p = s;
    return true;
}

bool pc_tz_parse(const char *s, pc_tz_t *out)
{
    if (!s) return false;
    pc_tz_t t;
    memset(&t, 0, sizeof t);
    int32_t o;
    if (!name(&s) || !num_time(&s, &o)) return false;
    t.std_min = -o / 60;
    if (*s) {
        if (!name(&s)) return false;
        t.has_dst = true;
        t.dst_min = t.std_min + 60;
        if (*s && *s != ',') { if (!num_time(&s, &o)) return false; t.dst_min = -o / 60; }
        if (*s++ != ',' || !rule(&s, &t.start) || *s++ != ',' || !rule(&s, &t.end) || *s) return false;
    }
    *out = t;
    return true;
}

/* UTC seconds of the rule's moment in `year`, the wall clock being `off_min` east of UTC at that moment */
static int64_t rule_utc(const pc_tz_rule_t *r, int year, int32_t off_min)
{
    int64_t first = clock_days_from_civil(year, r->m, 1);
    int64_t next = r->m == 12 ? clock_days_from_civil(year + 1, 1, 1) : clock_days_from_civil(year, r->m + 1, 1);
    int wd1 = (int)(((first + 4) % 7 + 7) % 7);                 /* 1970-01-01 was a Thursday */
    int day = 1 + ((r->d - wd1) % 7 + 7) % 7 + (r->w - 1) * 7;
    while (first + day - 1 >= next) day -= 7;                    /* week 5 = the last one */
    return (first + day - 1) * 86400 + r->time_s - (int64_t)off_min * 60;
}

bool pc_tz_is_dst(const pc_tz_t *tz, int64_t utc_s)
{
    if (!tz->has_dst) return false;
    clock_civil_t c;
    clock_civil_from_utc(utc_s, tz->std_min, &c);
    int64_t a = rule_utc(&tz->start, c.year, tz->std_min), b = rule_utc(&tz->end, c.year, tz->dst_min);
    return a < b ? (utc_s >= a && utc_s < b) : (utc_s >= a || utc_s < b);
}

int32_t pc_tz_offset_min(const pc_tz_t *tz, int64_t utc_s)
{
    return pc_tz_is_dst(tz, utc_s) ? tz->dst_min : tz->std_min;
}

int64_t pc_tz_local_to_utc(const pc_tz_t *tz, int64_t local_s)
{
    const int64_t a = local_s - (int64_t)tz->dst_min * 60, b = local_s - (int64_t)tz->std_min * 60;   /* the instant if daylight / standard time is in force */
    const int a_ok = pc_tz_offset_min(tz, a) == tz->dst_min, b_ok = pc_tz_offset_min(tz, b) == tz->std_min;
    if (a_ok && b_ok) return a < b ? a : b;                       /* repeated hour: the first occurrence */
    if (a_ok) return a;
    return b;                                                       /* b valid, or a skipped hour: standard offset (the clock then shows one hour later) */
}

const char *pc_tz_default(void) { return "UTC-8"; }
