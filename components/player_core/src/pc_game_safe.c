// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_game_safe.h"
#include "pc/pc_dial.h"

static uint32_t rnd(uint32_t *s)
{
    uint32_t x = *s ? *s : 0x9E3779B9u;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return *s = x;
}
static int wrap(int v) { return pc_dial_wrap(v, 0, PC_SAFE_RANGE - 1, true); }
static void ev(pc_safe_t *g, int e) { if (g->n_ev < (int)(sizeof g->ev / sizeof g->ev[0])) g->ev[g->n_ev++] = e; }

void pc_safe_init(pc_safe_t *g, int rule, uint32_t seed)
{
    *g = (pc_safe_t){ 0 };
    g->rule = (uint8_t)(rule >= 0 && rule < PC_SAFE_RULES ? rule : 0);
    g->n = g->rule == PC_SAFE_EXPERT ? 4 : 3;
    uint32_t s = seed * 2654435761u + 12345u;
    for (int i = 0; i < 4; i++) rnd(&s);
    int prev = 0;
    for (int k = 0; k < g->n; k++) {
        int t;
        do t = (int)(rnd(&s) >> 8) % PC_SAFE_RANGE;
        while (pc_dial_dist(t, prev, 0, PC_SAFE_RANGE - 1, true) < PC_SAFE_APART);
        g->target[k] = prev = t;
    }
}
void pc_safe_turn(pc_safe_t *g, int d)
{
    g->n_ev = 0;
    if (g->state != PC_SAFE_PLAY || d == 0) return;
    g->started = true;
    const int sign = d > 0 ? 1 : -1, n = d * sign;
    const int t = g->target[g->stage];
    bool hit = false;
    for (int i = 1; i <= n && i <= PC_SAFE_RANGE; i++) if (wrap(g->pos + sign * i) == t) hit = true;   /* turning past it counts */
    g->pos = wrap(g->pos + d % PC_SAFE_RANGE);
    g->turns += n;
    if (hit) ev(g, PC_SAFE_EV_DETENT);
}
void pc_safe_confirm(pc_safe_t *g)
{
    g->n_ev = 0;
    if (g->state != PC_SAFE_PLAY) return;
    g->started = true;
    g->attempts++;
    if (g->pos == g->target[g->stage]) {
        if (++g->stage >= g->n) { g->state = PC_SAFE_OPEN; ev(g, PC_SAFE_EV_OPEN); }
        else ev(g, PC_SAFE_EV_LOCK);
    } else {
        g->stage = 0;
        ev(g, PC_SAFE_EV_WRONG);
    }
}
void pc_safe_tick(pc_safe_t *g, uint32_t ms)
{
    g->n_ev = 0;
    if (!pc_safe_running(g)) return;
    g->ms += ms;
    if (g->rule == PC_SAFE_TIMED && g->ms >= PC_SAFE_TIME_MS) {
        g->ms = PC_SAFE_TIME_MS;
        g->state = PC_SAFE_TIMEUP;
        ev(g, PC_SAFE_EV_TIMEUP);
    }
}
bool pc_safe_running(const pc_safe_t *g) { return g->started && g->state == PC_SAFE_PLAY; }
int pc_safe_target(const pc_safe_t *g) { return g->state == PC_SAFE_PLAY ? g->target[g->stage] : -1; }
int pc_safe_dist(const pc_safe_t *g) { return g->state == PC_SAFE_PLAY ? pc_dial_dist(g->pos, g->target[g->stage], 0, PC_SAFE_RANGE - 1, true) : PC_SAFE_RANGE; }
uint32_t pc_safe_left_ms(const pc_safe_t *g) { return g->rule == PC_SAFE_TIMED ? PC_SAFE_TIME_MS - g->ms : 0; }
int pc_safe_reach(int rule, int hint, bool gold)
{
    int r = rule == PC_SAFE_EXPERT ? 5 : 12;
    r = hint == 0 ? r * 3 / 2 : hint == 2 ? r / 2 : r;
    if (r < 1) r = 1;
    return gold ? r * 2 : r;
}
int pc_safe_cue_strength(int rule, int hint)
{
    int s = hint == 0 ? 100 : hint == 2 ? 40 : 70;
    if (rule == PC_SAFE_EXPERT) s = s * 3 / 4;
    return s < 30 ? 30 : s;
}
