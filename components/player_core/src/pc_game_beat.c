// SPDX-License-Identifier: LGPL-2.1-or-later
/* 節拍點擊: see include/pc/pc_game_beat.h */
#include <string.h>
#include "pc/pc_game_beat.h"

static uint32_t rnd(pc_gb_t *g) { g->seed ^= g->seed << 13; g->seed ^= g->seed >> 17; g->seed ^= g->seed << 5; return g->seed; }
int pc_gb_window(const pc_gb_t *g, int which)
{
    int base = which == 0 ? PC_GB_PERFECT_MS : which == 1 ? PC_GB_GOOD_MS : PC_GB_ACCEPT_MS;
    return g->window == 1 ? base * 3 / 2 : g->window == 2 ? base * 7 / 10 : base;
}

void pc_gb_init(pc_gb_t *g, int rule, int window, uint32_t seed)
{
    memset(g, 0, sizeof *g);
    g->rule = (uint8_t)(rule < 0 || rule >= PC_GB_RULES ? 0 : rule);
    g->window = (uint8_t)(window < 0 || window >= PC_GB_WINDOWS ? 0 : window);
    g->seed = seed * 2654435761u + 0x9E3779B9u;                              /* spread small seeds, then warm the generator up */
    if (!g->seed) g->seed = 1;
    for (int i = 0; i < 8; i++) rnd(g);
    g->bpm = PC_GB_BPM_DEF;
    g->state = PC_GB_READY;
}
void pc_gb_set_bpm(pc_gb_t *g, int bpm) { g->bpm = bpm < PC_GB_BPM_MIN ? PC_GB_BPM_MIN : bpm > PC_GB_BPM_MAX ? PC_GB_BPM_MAX : bpm; }

static void ev(pc_gb_t *g, int kind, bool gold, int value, uint32_t points)
{
    if (g->n_ev < PC_GB_EVENTS) g->ev[g->n_ev++] = (pc_gb_ev_t){ (uint8_t)kind, gold, value, points };
}
static int ring_count(const pc_gb_t *g) { int n = 0; for (int i = 0; i < PC_GB_RINGS; i++) n += g->ring[i].live; return n; }

static void spawn(pc_gb_t *g, uint32_t target)
{
    for (int i = 0; i < PC_GB_RINGS; i++)
        if (!g->ring[i].live) {
            g->ring[i] = (pc_gb_ring_t){ true, (rnd(g) % PC_GB_GOLD_N) == 0, target };
            g->spawned++;
            return;
        }
}
static void resolve(pc_gb_t *g)
{
    g->resolved++;
    if (g->resolved >= PC_GB_ROUND) { g->state = PC_GB_OVER; ev(g, PC_GB_EV_OVER, false, g->combo_best, g->points); }
}
static void miss(pc_gb_t *g, bool gold, int off)
{
    g->miss++;
    g->combo = 0;
    ev(g, PC_GB_EV_MISS, gold, off, 0);
}

void pc_gb_step(pc_gb_t *g, uint32_t dt_ms, bool locked, uint32_t to_next_ms, uint32_t period_ms)
{
    g->n_ev = 0;
    g->now += dt_ms;
    if (g->state != PC_GB_PLAY) return;
    const int acc = pc_gb_window(g, 2);
    for (int i = 0; i < PC_GB_RINGS; i++) {                                  /* a ring nobody hit */
        pc_gb_ring_t *r = &g->ring[i];
        if (r->live && g->now > r->target + (uint32_t)acc) {
            r->live = false;
            miss(g, r->gold, acc);
            resolve(g);
            if (g->state == PC_GB_OVER) return;
        }
    }
    if (g->spawned >= PC_GB_ROUND) return;
    if (g->rule == PC_GB_METRO) {
        period_ms = 60000u / (uint32_t)g->bpm;
        if (!g->tnext_ok) { g->tnext = g->now + 1500; g->tnext_ok = true; }
    } else {
        g->unlocked = !locked || to_next_ms == 0 || period_ms == 0;
        if (g->unlocked) { if (!g->unlocked_since) g->unlocked_since = g->now ? g->now : 1; g->tnext_ok = false; return; }   /* lost: the next ring is placed afresh when the beat is found again (a stale tnext would put rings in the past) */
        g->unlocked_since = 0;
        uint32_t pred = g->now + to_next_ms + (g->rule == PC_GB_OFFBEAT ? period_ms / 2 : 0);   /* the next target on the grid */
        if (!g->tnext_ok) {                                                 /* the first ring: far enough ahead to be seen come in */
            g->tnext = pred;
            while (g->tnext < g->now + 700) g->tnext += period_ms;
            g->tnext_ok = true;
        } else {                                                            /* follow the grid: a slow pull toward the prediction, never a jump */
            int32_t d = (int32_t)(g->tnext - pred) % (int32_t)period_ms;
            if (d > (int32_t)period_ms / 2) d -= (int32_t)period_ms; else if (d < -(int32_t)period_ms / 2) d += (int32_t)period_ms;
            if (d > 15 || d < -15) g->tnext -= (uint32_t)(int32_t)(d / 4);
        }
    }
    while (g->spawned < PC_GB_ROUND && g->tnext <= g->now + PC_GB_LEAD_MS && ring_count(g) < PC_GB_RINGS) {
        spawn(g, g->tnext);
        g->tnext += period_ms;
    }
}

void pc_gb_tap(pc_gb_t *g, uint32_t since_ms)
{
    const uint32_t now = g->now + since_ms;                                  /* the time of the tap: the last step's clock plus what has passed since */
    if (g->state == PC_GB_READY) { g->state = PC_GB_PLAY; ev(g, PC_GB_EV_START, false, 0, 0); return; }
    if (g->state != PC_GB_PLAY) return;
    int best = -1;
    int32_t bd = 0;
    for (int i = 0; i < PC_GB_RINGS; i++) {                                  /* the nearest ring in reach */
        if (!g->ring[i].live) continue;
        int32_t d = (int32_t)(now - g->ring[i].target);
        if (d < 0 ? -d > pc_gb_window(g, 2) : d > pc_gb_window(g, 2)) continue;
        if (best < 0 || (d < 0 ? -d : d) < (bd < 0 ? -bd : bd)) { best = i; bd = d; }
    }
    if (best < 0) { g->stray++; g->combo = 0; ev(g, PC_GB_EV_STRAY, false, 0, 0); return; }
    pc_gb_ring_t *r = &g->ring[best];
    r->live = false;
    int ad = bd < 0 ? -bd : bd;
    if (ad > pc_gb_window(g, 1)) { miss(g, r->gold, bd); resolve(g); return; }
    bool perfect = ad <= pc_gb_window(g, 0);
    uint32_t base = perfect ? 100 : 50, mult = 10 + (uint32_t)(g->combo < PC_GB_COMBO_CAP ? g->combo : PC_GB_COMBO_CAP);
    uint32_t pts = base * mult / 10 * (r->gold ? 2u : 1u);
    g->points += pts;
    g->combo++;
    if (g->combo > g->combo_best) g->combo_best = g->combo;
    if (perfect) g->perfect++; else g->good++;
    if (r->gold) g->golds++;
    ev(g, perfect ? PC_GB_EV_PERFECT : PC_GB_EV_GOOD, r->gold, g->combo, pts);
    if (g->combo % 5 == 0) ev(g, PC_GB_EV_COMBO, false, g->combo, 0);
    resolve(g);
}

float pc_gb_progress(const pc_gb_t *g, int i)
{
    if (i < 0 || i >= PC_GB_RINGS || !g->ring[i].live) return -1.0f;
    return 1.0f - (float)((int32_t)(g->ring[i].target - g->now)) / (float)PC_GB_LEAD_MS;
}
bool pc_gb_running(const pc_gb_t *g) { return g->state == PC_GB_PLAY; }
bool pc_gb_no_beat(const pc_gb_t *g)
{
    return g->state == PC_GB_PLAY && g->rule != PC_GB_METRO && g->unlocked && g->unlocked_since && g->now - g->unlocked_since >= PC_GB_NOBEAT_MS && ring_count(g) == 0;
}
