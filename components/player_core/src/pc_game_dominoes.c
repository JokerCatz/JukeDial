// SPDX-License-Identifier: LGPL-2.1-or-later
/* 骨牌 / Dominoes (ideas G4): see pc/pc_game_dominoes.h. Pure C, fixed-size state, no allocation. */
#include "pc/pc_game_dominoes.h"
#include <string.h>

static uint32_t rnd(pc_dom_t *g) { uint32_t x = g->rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return g->rng = x; }
static int round5(int v) { return (v + 2) / 5 * 5; }

int pc_dom_b(int id) { int b = 0; while ((b + 1) * (b + 2) / 2 <= id) b++; return b; }
int pc_dom_a(int id) { int b = pc_dom_b(id); return id - b * (b + 1) / 2; }
int pc_dom_id(int a, int b) { if (a > b) { int t = a; a = b; b = t; } return b * (b + 1) / 2 + a; }
bool pc_dom_face_ok(int face) { return face >= 0 && face < PC_DOM_FACES; }
int pc_dom_count(uint32_t m) { int n = 0; for (; m; m &= m - 1) n++; return n; }
int pc_dom_hand_pips(uint32_t m) { int s = 0; for (int i = 0; i < PC_DOM_TILES; i++) if (m & (1u << i)) s += pc_dom_a(i) + pc_dom_b(i); return s; }

void pc_dom_init(pc_dom_t *g, int rule, uint32_t seed)
{
    memset(g, 0, sizeof *g);
    g->rule = (uint8_t)(rule >= 0 && rule < PC_DOM_RULES ? rule : PC_DOM_CLASSIC);
    g->rng = seed * 2654435761u + 12345u;
    if (!g->rng) g->rng = 0x9E3779B9u;
    uint8_t deck[PC_DOM_TILES];
    for (int i = 0; i < PC_DOM_TILES; i++) deck[i] = (uint8_t)i;
    for (int i = PC_DOM_TILES - 1; i > 0; i--) { int j = (int)(rnd(g) % (uint32_t)(i + 1)); uint8_t t = deck[i]; deck[i] = deck[j]; deck[j] = t; }
    for (int i = 0; i < PC_DOM_HAND; i++) { g->hand[0] |= 1u << deck[i]; g->hand[1] |= 1u << deck[PC_DOM_HAND + i]; }
    g->yard_n = PC_DOM_TILES - 2 * PC_DOM_HAND;
    memcpy(g->yard, deck + 2 * PC_DOM_HAND, g->yard_n);
    g->gold_tile = -1;
    if (rnd(g) % PC_DOM_GOLD_N == 0) { int d = (int)(rnd(g) % 7); g->gold_tile = (int8_t)pc_dom_id(d, d); }
    g->last_who = -1; g->last_side = -1;
    /* the opening: the highest double, else the tile with the most pips (then the larger number) */
    int best = -1, bw = 0, bk = -1;
    for (int w = 0; w < 2; w++)
        for (int i = 0; i < PC_DOM_TILES; i++) {
            if (!(g->hand[w] & (1u << i))) continue;
            const int a = pc_dom_a(i), b = pc_dom_b(i), k = a == b ? 1000 + a : (a + b) * 10 + b;
            if (k > bk) { bk = k; best = i; bw = w; }
        }
    g->hand[bw] &= ~(1u << best);
    g->chain[0] = (uint8_t)best; g->cl[0] = (uint8_t)pc_dom_a(best); g->cr[0] = (uint8_t)pc_dom_b(best); g->chain_n = 1;
    g->start_tile = (int8_t)best; g->start_who = (uint8_t)bw;
    g->plays[bw] = 1;
    g->last_who = (int8_t)bw; g->last_tile = (uint8_t)best; g->last_side = PC_DOM_RIGHT;
    g->turn = (uint8_t)(1 - bw);
    g->winner = PC_DOM_DRAW;
}

int pc_dom_left(const pc_dom_t *g) { return g->chain_n ? g->cl[0] : -1; }
int pc_dom_right(const pc_dom_t *g) { return g->chain_n ? g->cr[g->chain_n - 1] : -1; }
int pc_dom_ends_sum(const pc_dom_t *g)
{
    const int n = g->chain_n;
    if (!n) return 0;
    if (n == 1) return g->cl[0] == g->cr[0] ? 2 * g->cl[0] : g->cl[0] + g->cr[0];
    const int l = g->cl[0] * (g->cl[0] == g->cr[0] ? 2 : 1), r = g->cr[n - 1] * (g->cl[n - 1] == g->cr[n - 1] ? 2 : 1);
    return l + r;
}
int pc_dom_legal(const pc_dom_t *g, int id)
{
    if (id < 0 || id >= PC_DOM_TILES) return 0;
    if (!g->chain_n) return 3;
    const int a = pc_dom_a(id), b = pc_dom_b(id), l = pc_dom_left(g), r = pc_dom_right(g);
    return ((a == l || b == l) ? 1 : 0) | ((a == r || b == r) ? 2 : 0);
}
uint32_t pc_dom_playable(const pc_dom_t *g, int who)
{
    uint32_t m = 0;
    for (int i = 0; i < PC_DOM_TILES; i++) if ((g->hand[who & 1] & (1u << i)) && pc_dom_legal(g, i)) m |= 1u << i;
    return m;
}
bool pc_dom_can_move(const pc_dom_t *g, int who) { return pc_dom_playable(g, who) != 0; }

static void finish(pc_dom_t *g, int emptier)
{
    g->state = PC_DOM_OVER;
    const int p0 = pc_dom_hand_pips(g->hand[0]), p1 = pc_dom_hand_pips(g->hand[1]);
    int w = PC_DOM_DRAW;
    if (emptier >= 0) {
        w = emptier;
        if (g->rule == PC_DOM_FIVE) g->score[emptier] += round5(emptier == 0 ? p1 : p0);
    } else {
        if (p0 < p1) w = PC_DOM_YOU; else if (p1 < p0) w = PC_DOM_DEV;
        if (g->rule == PC_DOM_FIVE && w != PC_DOM_DRAW) g->score[w] += round5(w == 0 ? p1 - p0 : p0 - p1);
    }
    if (g->rule == PC_DOM_FIVE) w = g->score[0] > g->score[1] ? PC_DOM_YOU : g->score[1] > g->score[0] ? PC_DOM_DEV : PC_DOM_DRAW;
    g->winner = (uint8_t)w;
}

int pc_dom_place(pc_dom_t *g, int who, int id, int side)
{
    if (g->state != PC_DOM_PLAY || who < 0 || who > 1 || g->turn != who || id < 0 || id >= PC_DOM_TILES || !(g->hand[who] & (1u << id))) return -1;
    if (side != PC_DOM_LEFT && side != PC_DOM_RIGHT) return -1;
    if (!(pc_dom_legal(g, id) & (1 << side))) return -1;
    const int a = pc_dom_a(id), b = pc_dom_b(id);
    const int e = side == PC_DOM_LEFT ? pc_dom_left(g) : pc_dom_right(g), other = (a == e) ? b : a;   /* the number that stays outside */
    const int n = g->chain_n;
    if (side == PC_DOM_LEFT) {
        memmove(g->chain + 1, g->chain, (size_t)n); memmove(g->cl + 1, g->cl, (size_t)n); memmove(g->cr + 1, g->cr, (size_t)n);
        g->chain[0] = (uint8_t)id; g->cl[0] = (uint8_t)other; g->cr[0] = (uint8_t)e;
    } else {
        g->chain[n] = (uint8_t)id; g->cl[n] = (uint8_t)e; g->cr[n] = (uint8_t)other;
    }
    g->chain_n++;
    g->hand[who] &= ~(1u << id);
    g->passes = 0;
    g->plays[who]++;
    int gain = 0;
    if (g->rule == PC_DOM_FIVE) { const int s = pc_dom_ends_sum(g); if (s > 0 && s % 5 == 0) gain = s; g->score[who] += gain; }
    g->last_gold = id == g->gold_tile;
    if (g->last_gold && g->rule == PC_DOM_FIVE) g->score[who] += 10;
    g->last_who = (int8_t)who; g->last_tile = (uint8_t)id; g->last_side = (int8_t)side; g->last_gain = (int16_t)gain;
    if (!g->hand[who]) finish(g, who); else g->turn = (uint8_t)(1 - who);
    return gain;
}

int pc_dom_draw(pc_dom_t *g, int who)
{
    if (g->state != PC_DOM_PLAY || g->turn != who || g->rule == PC_DOM_BLOCK || !g->yard_n || pc_dom_can_move(g, who)) return -1;
    const int id = g->yard[--g->yard_n];
    g->hand[who] |= 1u << id;
    g->last_who = (int8_t)who; g->last_tile = (uint8_t)id; g->last_side = -1; g->last_gain = 0; g->last_gold = false;
    return id;
}

bool pc_dom_pass(pc_dom_t *g, int who)
{
    if (g->state != PC_DOM_PLAY || g->turn != who || pc_dom_can_move(g, who) || (g->rule != PC_DOM_BLOCK && g->yard_n)) return false;
    g->last_who = (int8_t)who; g->last_side = -1; g->last_gain = 0; g->last_gold = false;
    if (++g->passes >= 2) finish(g, -1); else g->turn = (uint8_t)(1 - who);
    return true;
}

/* the 五點 sum of the open ends after a tile went on a side (a hypothetical move: nothing changes) */
static int sum_after(const pc_dom_t *g, int id, int side)
{
    const int a = pc_dom_a(id), b = pc_dom_b(id), n = g->chain_n;
    int l = pc_dom_left(g), r = pc_dom_right(g), ld = g->cl[0] == g->cr[0], rd = g->cl[n - 1] == g->cr[n - 1];
    const int e = side == PC_DOM_LEFT ? l : r, other = (a == e) ? b : a;
    if (side == PC_DOM_LEFT) { l = other; ld = a == b; } else { r = other; rd = a == b; }
    return l * (ld ? 2 : 1) + r * (rd ? 2 : 1);
}

bool pc_dom_ai(pc_dom_t *g, int *id, int *side)
{
    if (g->state != PC_DOM_PLAY) return false;
    const int who = g->turn;
    int best = -1, bs = 0, bv = -1;
    for (int i = 0; i < PC_DOM_TILES; i++) {
        if (!(g->hand[who] & (1u << i))) continue;
        const int lg = pc_dom_legal(g, i);
        for (int s = 0; s < 2; s++) {
            if (!(lg & (1 << s))) continue;
            int gain = 0;
            if (g->rule == PC_DOM_FIVE && g->chain_n) { const int sm = sum_after(g, i, s); if (sm > 0 && sm % 5 == 0) gain = sm; }
            int v = gain * 16 + pc_dom_a(i) + pc_dom_b(i) + (pc_dom_a(i) == pc_dom_b(i) ? 3 : 0) + (int)(rnd(g) % 3);
            if (pc_dom_count(g->hand[who]) == 1) v += 1000;
            if (v > bv) { bv = v; best = i; bs = s; }
        }
    }
    if (best < 0) return false;
    *id = best; *side = bs;
    return true;
}
