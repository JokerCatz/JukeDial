// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_timers.h"
#include <stdio.h>

void pc_sw_reset(pc_sw_t *s) { *s = (pc_sw_t){ 0 }; }
void pc_sw_start(pc_sw_t *s, int64_t now) { if (!s->running) { s->running = true; s->t0_ms = now; } }
void pc_sw_pause(pc_sw_t *s, int64_t now) { if (s->running) { s->acc_ms += now - s->t0_ms; s->running = false; } }
int64_t pc_sw_elapsed(const pc_sw_t *s, int64_t now)
{
    int64_t e = s->acc_ms + (s->running ? now - s->t0_ms : 0);
    return e < 0 ? 0 : e;
}
bool pc_sw_lap(pc_sw_t *s, int64_t now)
{
    if (!s->running) return false;
    s->at[s->nlap % PC_SW_LAPS] = pc_sw_elapsed(s, now);
    s->nlap++;
    return true;
}
bool pc_sw_lap_get(const pc_sw_t *s, int k, int *no, int64_t *split, int64_t *total)
{
    int stored = s->nlap < PC_SW_LAPS ? s->nlap : PC_SW_LAPS;
    if (k < 0 || k >= stored) return false;
    int n = s->nlap - k;                                   /* 1 based number */
    int64_t t = s->at[(n - 1) % PC_SW_LAPS], prev = n > 1 && k + 1 < stored ? s->at[(n - 2) % PC_SW_LAPS] : 0;
    if (n > 1 && k + 1 >= stored) prev = t;                /* the one before it is gone: no honest split */
    if (no) *no = n;
    if (total) *total = t;
    if (split) *split = t - prev;
    return true;
}
int pc_sw_lap_best(const pc_sw_t *s)
{
    int stored = s->nlap < PC_SW_LAPS ? s->nlap : PC_SW_LAPS, best = -1;
    int64_t bs = 0;
    for (int k = 0; k < stored; k++) {
        int64_t sp;
        if (k + 1 >= stored && s->nlap > PC_SW_LAPS) break;     /* the oldest stored one has no known split */
        pc_sw_lap_get(s, k, NULL, &sp, NULL);
        if (best < 0 || sp < bs) { best = k; bs = sp; }
    }
    int usable = stored - (s->nlap > PC_SW_LAPS ? 1 : 0);
    return usable >= 2 ? best : -1;
}
void pc_sw_fmt(int64_t ms, char *b, size_t n)
{
    if (ms < 0) ms = 0;
    int64_t s = ms / 1000;
    if (s >= 3600) snprintf(b, n, "%d:%02d:%02d", (int)(s / 3600), (int)(s / 60 % 60), (int)(s % 60));
    else snprintf(b, n, "%02d:%02d.%02d", (int)(s / 60), (int)(s % 60), (int)(ms % 1000 / 10));
}

static int clampc(int s) { return s < 1 ? 1 : s > PC_CD_MAX_S ? PC_CD_MAX_S : s; }
void pc_cd_init(pc_cd_t *c, int set_s) { *c = (pc_cd_t){ .state = PC_CD_IDLE, .set_s = clampc(set_s) }; }
void pc_cd_set(pc_cd_t *c, int set_s) { if (c->state == PC_CD_IDLE || c->state == PC_CD_DONE) { c->set_s = clampc(set_s); c->state = PC_CD_IDLE; } }
void pc_cd_start(pc_cd_t *c, int64_t now)
{
    if (c->state == PC_CD_IDLE || c->state == PC_CD_DONE) { c->end_ms = now + (int64_t)c->set_s * 1000; c->state = PC_CD_RUN; }
    else if (c->state == PC_CD_PAUSE) { c->end_ms = now + c->held_ms; c->state = PC_CD_RUN; }
}
void pc_cd_pause(pc_cd_t *c, int64_t now)
{
    if (c->state != PC_CD_RUN) return;
    c->held_ms = c->end_ms - now < 0 ? 0 : c->end_ms - now;
    c->state = PC_CD_PAUSE;
}
void pc_cd_reset(pc_cd_t *c) { c->state = PC_CD_IDLE; }
int64_t pc_cd_left_ms(const pc_cd_t *c, int64_t now)
{
    switch (c->state) {
    case PC_CD_RUN: return c->end_ms > now ? c->end_ms - now : 0;
    case PC_CD_PAUSE: return c->held_ms;
    case PC_CD_DONE: return 0;
    default: return (int64_t)c->set_s * 1000;
    }
}
bool pc_cd_tick(pc_cd_t *c, int64_t now)
{
    if (c->state != PC_CD_RUN || now < c->end_ms) return false;
    c->state = PC_CD_DONE;
    return true;
}
bool pc_cd_warn(const pc_cd_t *c, int64_t now)
{
    if (c->state != PC_CD_RUN) return false;
    int64_t l = pc_cd_left_ms(c, now);
    return l > 0 && l <= PC_CD_WARN_MS;
}
bool pc_cd_blink(const pc_cd_t *c, int64_t now) { return pc_cd_warn(c, now) && pc_cd_left_ms(c, now) % 1000 >= 500; }   /* left counts down: the first half of the second is the high end of the remainder */
void pc_cd_fmt(int64_t ms, char *b, size_t n)
{
    if (ms < 0) ms = 0;
    int s = (int)((ms + 999) / 1000);
    if (s >= 3600) snprintf(b, n, "%d:%02d:%02d", s / 3600, s / 60 % 60, s % 60);
    else snprintf(b, n, "%02d:%02d", s / 60, s % 60);
}
