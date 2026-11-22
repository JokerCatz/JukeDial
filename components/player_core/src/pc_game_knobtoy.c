// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_game_knobtoy.h"

static uint32_t rnd(uint32_t *s)
{
    uint32_t x = *s ? *s : 0x9E3779B9u;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return *s = x;
}
static void ev(pc_kt_t *g, int e) { if (g->n_ev < (int)(sizeof g->ev / sizeof g->ev[0])) g->ev[g->n_ev++] = e; }
static int absi(int v) { return v < 0 ? -v : v; }
#define FISH_EDGE 5

void pc_kt_init(pc_kt_t *g, int rule, uint32_t seed, bool gold)
{
    *g = (pc_kt_t){ 0 };
    g->rule = (uint8_t)(rule >= 0 && rule < PC_KT_RULES ? rule : 0);
    g->gold = gold;
    g->rng = seed * 2654435761u + 12345u;
    for (int i = 0; i < 4; i++) rnd(&g->rng);
    if (g->rule == PC_KT_WIND) g->sweet = PC_KT_SWEET_MIN + (int)(rnd(&g->rng) >> 8) % (PC_KT_SWEET_MAX - PC_KT_SWEET_MIN + 1);
    else {
        g->fish = 30 + (int)(rnd(&g->rng) >> 8) % 41;
        g->dir = (rnd(&g->rng) >> 8) & 1 ? 1 : -1;
        g->v = 50;
    }
}
int pc_kt_wind_cm(int v, int sweet)
{
    if (v <= 0 || v >= PC_KT_WIND_MAX) return 0;
    if (v == sweet) return 120;
    if (v < sweet) { const int c = 100 - (sweet - v) * 5; return c < 5 ? 5 : c; }
    const int c = 100 - (v - sweet) * 8;
    return c < 0 ? 0 : c;
}
int pc_kt_reach(int rule) { return rule == PC_KT_FISH ? 10 : 8; }
int pc_kt_zone_lo(const pc_kt_t *g) { const int a = g->fish - PC_KT_FISH_ZONE; return a < 0 ? 0 : a; }
int pc_kt_zone_hi(const pc_kt_t *g) { const int b = g->fish + PC_KT_FISH_ZONE; return b > PC_KT_FISH_RANGE - 1 ? PC_KT_FISH_RANGE - 1 : b; }
bool pc_kt_in_zone(const pc_kt_t *g) { return absi(g->v - g->fish) <= PC_KT_FISH_ZONE; }
int pc_kt_catch_pct(const pc_kt_t *g) { return g->catch_ * 100 / PC_KT_CATCH_FULL; }
bool pc_kt_running(const pc_kt_t *g) { return g->started && g->state == PC_KT_PLAY; }

void pc_kt_move(pc_kt_t *g, int d)
{
    g->n_ev = 0;
    if (g->state != PC_KT_PLAY) return;
    if (d) { g->started = true; g->steps++; }
    if (g->rule == PC_KT_WIND) {
        g->v += d;
        if (g->v < 0) g->v = 0;
        if (g->v >= PC_KT_WIND_MAX) { g->v = 0; ev(g, PC_KT_EV_SNAP); }   /* wound too far: the spring snaps and is slack again */
    } else {
        g->v += d;
        if (g->v < 0) g->v = 0;
        if (g->v > PC_KT_FISH_RANGE - 1) g->v = PC_KT_FISH_RANGE - 1;
    }
}
void pc_kt_release(pc_kt_t *g)
{
    g->n_ev = 0;
    if (g->state != PC_KT_PLAY || g->rule != PC_KT_WIND) return;
    int cm = pc_kt_wind_cm(g->v, g->sweet);
    if (g->gold) cm *= 2;
    g->last_cm = cm;
    g->total += cm;
    g->launches++;
    ev(g, PC_KT_EV_LAUNCH);
    if (g->v == g->sweet) ev(g, PC_KT_EV_PERFECT);
    g->v = 0;
    g->prev_sweet = g->sweet;
    g->sweet = PC_KT_SWEET_MIN + (int)(rnd(&g->rng) >> 8) % (PC_KT_SWEET_MAX - PC_KT_SWEET_MIN + 1);   /* the next spring */
    if (g->launches >= PC_KT_LAUNCHES) { g->state = PC_KT_DONE; ev(g, PC_KT_EV_DONE); }
}
static void fish_step(pc_kt_t *g)
{
    const uint32_t r = rnd(&g->rng);
    if (g->dash > 0) { g->fish += g->dir * 2; g->dash--; }
    else {
        if ((r >> 4) % 100 < (g->gold ? 35u : 55u)) g->fish += g->dir;
        if ((r >> 12) % 100 < 12) g->dir = -g->dir;
        if ((r >> 20) % 100 < (g->gold ? 2u : 4u)) { g->dash = 6; ev(g, PC_KT_EV_DASH); }
    }
    if (g->fish < FISH_EDGE) { g->fish = FISH_EDGE; g->dir = 1; }
    if (g->fish > PC_KT_FISH_RANGE - 1 - FISH_EDGE) { g->fish = PC_KT_FISH_RANGE - 1 - FISH_EDGE; g->dir = -1; }
    if (pc_kt_in_zone(g)) {
        g->catch_ += g->gold ? 40 : PC_KT_CATCH_STEP;
        g->line -= 4;
        if (g->line < 0) g->line = 0;
        if (g->catch_ >= PC_KT_CATCH_FULL) { g->catch_ = PC_KT_CATCH_FULL; g->state = PC_KT_DONE; ev(g, PC_KT_EV_CAUGHT); }
    } else {
        g->line += absi(g->v - g->fish) > 12 ? 5 : 3;
        g->catch_ -= 5;
        if (g->catch_ < 0) g->catch_ = 0;
        if (g->line >= PC_KT_LINE_FULL) { g->line = PC_KT_LINE_FULL; g->catch_ = 0; g->state = PC_KT_LOST; ev(g, PC_KT_EV_LOST); }
    }
}
void pc_kt_tick(pc_kt_t *g, uint32_t ms)
{
    g->n_ev = 0;
    if (!g->started || g->state != PC_KT_PLAY) return;
    g->acc += ms;
    while (g->acc >= PC_KT_STEP_MS && g->state == PC_KT_PLAY) {
        g->acc -= PC_KT_STEP_MS;
        g->ms += PC_KT_STEP_MS;
        if (g->rule == PC_KT_WIND) {
            g->leak_ms += PC_KT_STEP_MS;
            if (g->leak_ms >= PC_KT_LEAK_MS) { g->leak_ms -= PC_KT_LEAK_MS; if (g->v > 0) g->v--; }
        } else fish_step(g);
    }
}
