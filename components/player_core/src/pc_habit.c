// SPDX-License-Identifier: LGPL-2.1-or-later
/* pc_habit (ideas G7): see pc_habit.h. */
#include "pc/pc_habit.h"

static int64_t fdiv(int64_t a, int64_t b) { int64_t q = a / b; return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q; }
int32_t pc_hb_local_day(int64_t utc, int32_t tz_min) { return (int32_t)fdiv(utc + (int64_t)tz_min * 60, 86400); }
int pc_hb_local_minute(int64_t utc, int32_t tz_min)
{
    int64_t s = utc + (int64_t)tz_min * 60;
    return (int)((s - fdiv(s, 86400) * 86400) / 60);
}

static const int INTERVAL[PC_DRINK_INTERVALS] = { 30, 45, 60, 90 };
int pc_drink_interval_min(int idx) { return INTERVAL[idx < 0 ? 0 : idx >= PC_DRINK_INTERVALS ? PC_DRINK_INTERVALS - 1 : idx]; }
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
void pc_drink_cfg_default(pc_drink_cfg_t *c) { *c = (pc_drink_cfg_t){ .on = 0, .interval = 2, .start_h = 9, .end_h = 21, .goal = 8 }; }
void pc_drink_cfg_fix(pc_drink_cfg_t *c)
{
    c->on = c->on ? 1 : 0;
    c->interval = (uint8_t)clampi(c->interval, 0, PC_DRINK_INTERVALS - 1);
    c->start_h = (uint8_t)clampi(c->start_h, 0, 23);
    c->end_h = (uint8_t)clampi(c->end_h, 1, 24);
    if (c->start_h >= c->end_h) c->end_h = (uint8_t)(c->start_h + 1);
    c->goal = (uint8_t)clampi(c->goal, PC_DRINK_GOAL_MIN, PC_DRINK_GOAL_MAX);
}
void pc_drink_step_interval(pc_drink_cfg_t *c, int d) { c->interval = (uint8_t)((((int)c->interval + d) % PC_DRINK_INTERVALS + PC_DRINK_INTERVALS) % PC_DRINK_INTERVALS); }
void pc_drink_step_start(pc_drink_cfg_t *c, int d) { c->start_h = (uint8_t)clampi((int)c->start_h + d, 0, 23); if (c->end_h <= c->start_h) c->end_h = (uint8_t)(c->start_h + 1); }
void pc_drink_step_end(pc_drink_cfg_t *c, int d) { c->end_h = (uint8_t)clampi((int)c->end_h + d, 1, 24); if (c->start_h >= c->end_h) c->start_h = (uint8_t)(c->end_h - 1); }
void pc_drink_step_goal(pc_drink_cfg_t *c, int d) { c->goal = (uint8_t)clampi((int)c->goal + d, PC_DRINK_GOAL_MIN, PC_DRINK_GOAL_MAX); }
bool pc_drink_in_window(const pc_drink_cfg_t *c, int minute) { return minute >= c->start_h * 60 && minute < c->end_h * 60; }

bool pc_drink_sync_day(pc_drink_t *s, int32_t day)
{
    if (day <= s->day && (int64_t)s->day - day <= 1) return false;            /* the same day, or a clock one day back (a zone change): the cups stand */
    s->day = day;
    s->cups = 0;
    s->next_due = 0;
    return true;
}
bool pc_drink_tick(pc_drink_t *s, const pc_drink_cfg_t *c, int64_t now, int32_t tz_min)
{
    int64_t step = (int64_t)pc_drink_interval_min(c->interval) * 60;
    if (!c->on || !pc_drink_in_window(c, pc_hb_local_minute(now, tz_min)) || s->cups >= c->goal) { s->next_due = 0; return false; }
    if (s->next_due == 0 || s->next_due - now > step) { s->next_due = now + step; return false; }   /* first look at an open window, or the clock went back */
    if (now < s->next_due) return false;
    s->next_due = now + step;
    return true;
}
void pc_drink_cup(pc_drink_t *s, const pc_drink_cfg_t *c, int64_t now)
{
    if (s->cups < PC_DRINK_CUPS_MAX) s->cups++;
    s->next_due = now + (int64_t)pc_drink_interval_min(c->interval) * 60;
}
void pc_drink_undo(pc_drink_t *s) { if (s->cups > 0) s->cups--; }
void pc_drink_snooze(pc_drink_t *s, int64_t now) { s->next_due = now + PC_DRINK_SNOOZE_MIN * 60; }

bool pc_hb_done(const pc_hb_t *h, int32_t today) { return h->last != 0 && h->last == today; }
bool pc_hb_on(const pc_hb_t *h, int32_t today, int back)
{
    if (back < 0 || back > 31 || h->last == 0) return false;
    int64_t k = (int64_t)h->last - ((int64_t)today - back);     /* bit k = done on last - k */
    return k >= 0 && k < 32 && ((h->mask >> k) & 1u);
}
bool pc_hb_mark(pc_hb_t *h, int32_t today)
{
    if (h->last != 0 && (int64_t)h->last - today > 1) *h = (pc_hb_t){ 0 };   /* the record is from a wrong future date (corrected clock): start again from today */
    if (h->last != 0 && today <= h->last) return false;                       /* today, or the clock one day back (a zone change): nothing */
    int64_t gap = (int64_t)today - h->last;
    bool next = h->last != 0 && gap == 1;
    h->streak = next ? h->streak + 1 : 1;
    h->mask = h->last == 0 || gap >= 32 ? 0u : h->mask << gap;
    h->mask |= 1u;
    h->last = today;
    return true;
}
bool pc_hb_unmark(pc_hb_t *h, int32_t today)
{
    if (!pc_hb_done(h, today)) return false;
    h->mask &= ~1u;
    if (h->mask == 0) { h->last = 0; h->streak = 0; return true; }
    int k = 0;
    while (!((h->mask >> k) & 1u)) k++;
    h->last -= k;
    h->mask >>= k;
    if (h->streak > 32) h->streak--;                            /* a run past the mask: one less */
    else { uint32_t r = 0; while (r < 32 && ((h->mask >> r) & 1u)) r++; h->streak = r; }
    return true;
}
uint32_t pc_hb_streak(const pc_hb_t *h, int32_t today)
{
    if (h->last == 0 || today < h->last) return h->last == 0 ? 0 : h->streak;   /* the clock went back: what it was */
    return (int64_t)today - h->last <= 1 ? h->streak : 0;
}
int pc_hb_strip(const pc_hb_t *h, int32_t today, char *out, int cap)
{
    if (cap < PC_HB_STRIP + 1) return 0;
    for (int i = 0; i < PC_HB_STRIP; i++) out[i] = pc_hb_on(h, today, PC_HB_STRIP - 1 - i) ? '#' : '.';
    out[PC_HB_STRIP] = 0;
    return PC_HB_STRIP;
}
