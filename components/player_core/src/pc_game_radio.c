// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_game_radio.h"

static uint32_t rnd(uint32_t *s)
{
    uint32_t x = *s ? *s : 0x9E3779B9u;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return *s = x;
}
static void ev(pc_radio_t *g, int e) { if (g->n_ev < (int)(sizeof g->ev / sizeof g->ev[0])) g->ev[g->n_ev++] = e; }
static int absi(int v) { return v < 0 ? -v : v; }
#define POS_MIN PC_RADIO_APART
#define POS_MAX (PC_RADIO_RANGE - 6)

static bool far_enough(const pc_radio_t *g, int count, int p)
{
    for (int j = 0; j < count; j++) if (absi(g->st[j].pos - p) < PC_RADIO_APART) return false;
    return true;
}
/* a station at a new place: at least PC_RADIO_APART from the stations placed before it and from `from` (the start / the dial) */
static void place(pc_radio_t *g, int i, int count, int from)
{
    int p;
    do p = POS_MIN + (int)(rnd(&g->rng) >> 8) % (POS_MAX - POS_MIN + 1);
    while (!far_enough(g, count, p) || absi(p - from) < PC_RADIO_APART);
    g->st[i].pos = (uint8_t)p;
    g->st[i].locked = false;
    g->st[i].gold = rnd(&g->rng) % PC_RADIO_GOLD_N == 0;
    g->st[i].tune = (uint8_t)((rnd(&g->rng) >> 8) & 3);
}

void pc_radio_init(pc_radio_t *g, int rule, uint32_t seed)
{
    *g = (pc_radio_t){ 0 };
    g->rule = (uint8_t)(rule >= 0 && rule < PC_RADIO_RULES ? rule : 0);
    g->n = g->rule == PC_RADIO_TIMED ? 1 : PC_RADIO_MAX;
    g->rng = seed * 2654435761u + 12345u;
    for (int i = 0; i < 4; i++) rnd(&g->rng);
    for (int i = 0; i < g->n; i++) place(g, i, i, 0);
}
int pc_radio_reach(int rule, bool gold)
{
    const int r = rule == PC_RADIO_WEAK ? 6 : 10;
    if (!gold) return r;
    const int q = r * 2 / 3;
    return q < 5 ? 5 : q;
}
int pc_radio_signal_of(int dist, int reach)
{
    if (reach <= 0 || dist < 0 || dist >= reach) return 0;
    return 100 * (reach - dist) / reach;
}
int pc_radio_nearest(const pc_radio_t *g)
{
    int best = -1, bd = 1 << 20;
    if (g->state != PC_RADIO_PLAY) return -1;
    for (int i = 0; i < g->n; i++) {
        if (g->st[i].locked) continue;
        const int d = absi(g->st[i].pos - g->pos);
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}
int pc_radio_signal(const pc_radio_t *g)
{
    const int i = pc_radio_nearest(g);
    if (i < 0) return 0;
    return pc_radio_signal_of(absi(g->st[i].pos - g->pos), pc_radio_reach(g->rule, g->st[i].gold));
}
bool pc_radio_can_lock(const pc_radio_t *g) { return pc_radio_signal(g) >= PC_RADIO_LOCK_PCT; }

void pc_radio_tune(pc_radio_t *g, int d)
{
    g->n_ev = 0;
    if (g->state != PC_RADIO_PLAY || !d) return;
    int p = g->pos + d;
    g->pos = p < 0 ? 0 : p > PC_RADIO_RANGE - 1 ? PC_RADIO_RANGE - 1 : p;
    g->started = true;
}
void pc_radio_lock(pc_radio_t *g)
{
    g->n_ev = 0;
    if (g->state != PC_RADIO_PLAY) return;
    g->started = true;
    g->attempts++;
    const int i = pc_radio_nearest(g);
    if (i < 0 || !pc_radio_can_lock(g)) { g->misses++; ev(g, PC_RADIO_EV_MISS); return; }
    g->st[i].locked = true;
    g->found++;
    ev(g, PC_RADIO_EV_LOCK);
    if (g->st[i].gold) { g->bonus_ms += PC_RADIO_BONUS_MS; g->gold_found = true; ev(g, PC_RADIO_EV_GOLD); }
    if (g->rule == PC_RADIO_TIMED) { place(g, 0, 0, g->pos); return; }   /* a new station, one at a time */
    if (g->found >= g->n) { g->state = PC_RADIO_WON; ev(g, PC_RADIO_EV_WON); }
}
bool pc_radio_running(const pc_radio_t *g) { return g->started && g->state == PC_RADIO_PLAY; }
uint32_t pc_radio_left_ms(const pc_radio_t *g)
{
    if (g->rule != PC_RADIO_TIMED || g->state == PC_RADIO_TIMEUP) return 0;
    const uint32_t total = PC_RADIO_TIME_MS + g->bonus_ms;
    return g->ms >= total ? 0 : total - g->ms;
}
void pc_radio_tick(pc_radio_t *g, uint32_t ms)
{
    g->n_ev = 0;
    if (!pc_radio_running(g)) return;
    g->ms += ms;
    if (g->rule == PC_RADIO_TIMED && pc_radio_left_ms(g) == 0) { g->state = PC_RADIO_TIMEUP; ev(g, PC_RADIO_EV_TIMEUP); }
}
uint32_t pc_radio_time_ms(const pc_radio_t *g)
{
    if (g->rule == PC_RADIO_TIMED) return g->ms;
    return g->ms > g->bonus_ms ? g->ms - g->bonus_ms : 0;
}
int pc_radio_hub10(int pos) { return 800 + 2 * pos; }
int pc_radio_shake10(int signal)
{
    if (signal >= 40) return 0;
    return 30 * (40 - (signal < 0 ? 0 : signal)) / 40;
}
int pc_radio_sound(int rule, int signal, int tune, unsigned step)
{
    static const uint8_t FRAG[4][PC_RADIO_MELODY] = { { 0, 2, 1, 3 }, { 3, 1, 2, 0 }, { 0, 1, 2, 1 }, { 2, 0, 3, 1 } };
    if (signal < 40) {                                              /* static: a tick every P steps (denser for the weak rule and far from a station) */
        const unsigned p = rule == PC_RADIO_WEAK ? 2u + (unsigned)(signal < 0 ? 0 : signal) / 16u : 3u + (unsigned)(signal < 0 ? 0 : signal) / 8u;
        return step % p == 0 ? 0 : -1;
    }
    const int notes = signal < 55 ? 1 : signal < 70 ? 2 : signal < 80 ? 3 : PC_RADIO_MELODY;   /* the fragment: notes at steps 0, 3, 6, 9 of a 12 step cycle */
    const unsigned c = step % 12u;
    if (c % 3u == 0) { const int k = (int)(c / 3u); return k < notes ? 1 + FRAG[tune & 3][k] : -1; }
    return signal < 70 && step % 6u == 2u ? 0 : -1;                 /* some static between the notes until the signal is clear */
}
