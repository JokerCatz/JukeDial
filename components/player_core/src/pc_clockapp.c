// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_clockapp.h"
#include "time/alarm.h"
#include "time/clock.h"

static int64_t fdiv(int64_t a, int64_t b) { int64_t q = a / b; return (a % b != 0 && (a < 0) != (b < 0)) ? q - 1 : q; }

bool pc_alarm_check_tz(pc_alarm_t *a, int64_t prev, int64_t now, const pc_tz_t *tz)
{
    if (a->snooze_until && prev < a->snooze_until && a->snooze_until <= now) {   /* snooze over: ring again */
        a->snooze_until = 0;
        return true;
    }
    if (!a->on || prev >= now) return false;
    int32_t op = clock_clamp_tz_min(pc_tz_offset_min(tz, prev)), on = clock_clamp_tz_min(pc_tz_offset_min(tz, now));
    int64_t lp = prev + (int64_t)op * 60 - (int64_t)alarm_wrap_min(a->min) * 60;     /* the wall clock of each tick, shifted so the alarm is a day boundary */
    int64_t ln = now + (int64_t)on * 60 - (int64_t)alarm_wrap_min(a->min) * 60;
    int64_t occ = fdiv(ln, 86400);
    if (occ <= fdiv(lp, 86400) || occ == a->last_occ) return false;                  /* not crossed (the wall clock may even go back), or this occurrence rang already */
    clock_civil_t c;
    clock_civil_from_utc(now, on, &c);
    if (a->days != PC_DAYS_ONCE && !(a->days & (1u << c.weekday))) return false;
    a->last_occ = occ;
    if (a->days == PC_DAYS_ONCE) a->on = false;
    return true;
}

bool pc_alarm_check(pc_alarm_t *a, int64_t prev, int64_t now, int32_t tz_min)
{
    pc_tz_t z = { .std_min = tz_min, .dst_min = tz_min };
    return pc_alarm_check_tz(a, prev, now, &z);
}

void pc_alarm_edited(pc_alarm_t *a) { a->snooze_until = 0; a->last_occ = 0; }
void pc_alarm_snooze(pc_alarm_t *a, int64_t now, int minutes) { a->snooze_until = now + (int64_t)minutes * 60; }

void pc_pomo_start(pc_pomo_t *p, int64_t now)
{
    *p = (pc_pomo_t){ .phase = PC_POMO_FOCUS, .focus_s = 25 * 60, .break_s = 5 * 60, .long_s = 15 * 60, .every = 4 };
    p->ends = now + p->focus_s;
}

bool pc_pomo_tick(pc_pomo_t *p, int64_t now)
{
    if (p->phase == PC_POMO_IDLE || p->paused || now < p->ends) return false;
    if (p->phase == PC_POMO_FOCUS) {
        p->done++;
        p->phase = p->done % p->every ? PC_POMO_BREAK : PC_POMO_LONG;
        p->ends = now + (p->phase == PC_POMO_LONG ? p->long_s : p->break_s);
    } else {
        p->phase = PC_POMO_FOCUS;
        p->ends = now + p->focus_s;                       /* from now: a long pause never makes a burst of steps */
    }
    return true;
}

int pc_pomo_left(const pc_pomo_t *p, int64_t now)
{
    if (p->phase == PC_POMO_IDLE) return 0;
    if (p->paused) return p->paused_left;
    return now >= p->ends ? 0 : (int)(p->ends - now);
}
void pc_pomo_stop(pc_pomo_t *p) { p->phase = PC_POMO_IDLE; p->paused = false; }
void pc_pomo_pause(pc_pomo_t *p, int64_t now)
{
    if (p->phase == PC_POMO_IDLE || p->paused) return;
    p->paused_left = pc_pomo_left(p, now);
    p->paused = true;
}
void pc_pomo_resume(pc_pomo_t *p, int64_t now)
{
    if (!p->paused) return;
    p->paused = false;
    p->ends = now + p->paused_left;
}
