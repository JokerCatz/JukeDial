// SPDX-License-Identifier: LGPL-2.1-or-later
/* 四色記憶 / Simon: the rules (pc_game_simon.h). */
#include "pc/pc_game_simon.h"
#include <string.h>

static uint32_t next(pc_simon_t *g)                          /* xorshift32: the game's own stream */
{
    uint32_t x = g->rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    g->rng = x;
    return x;
}

void pc_simon_init(pc_simon_t *g, int rule, uint32_t seed)
{
    memset(g, 0, sizeof *g);
    g->rule = (uint8_t)(rule >= 0 && rule < PC_SIMON_RULES ? rule : PC_SIMON_CLASSIC);
    for (int c = 0; c < PC_SIMON_COLOURS; c++) g->place[c] = (uint8_t)c;
    g->rng = seed * 2654435761u ^ 0x9E3779B9u;
    if (!g->rng) g->rng = 0x6d2b79f5u;
    next(g);
}

/* every colour to another sector: a random derangement of the four (9 of the 24 orders; drawn until one is found, a rotation if the draws run out) */
static void move_colours(pc_simon_t *g)
{
    uint8_t p[PC_SIMON_COLOURS];
    for (int tries = 0; tries < 64; tries++) {
        for (int i = 0; i < PC_SIMON_COLOURS; i++) p[i] = (uint8_t)i;
        for (int i = PC_SIMON_COLOURS - 1; i > 0; i--) { int j = (int)(next(g) % (uint32_t)(i + 1)); uint8_t t = p[i]; p[i] = p[j]; p[j] = t; }
        bool ok = true;
        for (int c = 0; c < PC_SIMON_COLOURS; c++) ok &= p[c] != c;
        if (ok) goto found;
    }
    for (int i = 0; i < PC_SIMON_COLOURS; i++) p[i] = (uint8_t)((i + 1) % PC_SIMON_COLOURS);
found:;
    uint8_t old[PC_SIMON_COLOURS];
    memcpy(old, g->place, sizeof old);
    for (int c = 0; c < PC_SIMON_COLOURS; c++) g->place[c] = old[p[c]];   /* colour c takes the sector of colour p[c]: never its own */
    g->moves++;
}

bool pc_simon_grow(pc_simon_t *g)
{
    if (g->over || g->len >= PC_SIMON_MAX) return false;
    bool moved = false;
    if (g->rule == PC_SIMON_SHUFFLE && g->done > 0 && g->done % PC_SIMON_SHUFFLE_EVERY == 0 && g->moves < g->done / PC_SIMON_SHUFFLE_EVERY) {
        move_colours(g);
        moved = true;
    }
    g->seq[g->len++] = (uint8_t)(next(g) >> 8 & (PC_SIMON_COLOURS - 1));
    g->pos = 0;
    return moved;
}

int pc_simon_shown(const pc_simon_t *g, int k) { return k >= 0 && k < g->len ? g->place[g->seq[k]] : -1; }

int pc_simon_expect(const pc_simon_t *g)
{
    if (g->over || g->pos >= g->len) return -1;
    return pc_simon_shown(g, g->rule == PC_SIMON_REVERSE ? g->len - 1 - g->pos : g->pos);
}

int pc_simon_tap(pc_simon_t *g, int sector)
{
    int want = pc_simon_expect(g);
    if (want < 0) return PC_SIMON_IGNORED;
    if (sector != want) { g->over = 1; return PC_SIMON_WRONG; }
    if (++g->pos < g->len) return PC_SIMON_OK;
    g->done++;
    if (g->len >= PC_SIMON_MAX) g->over = 1;                 /* the longest sequence repeated: the end (a win) */
    return PC_SIMON_ROUND;
}

void pc_simon_retry(pc_simon_t *g) { g->over = 0; g->pos = 0; }

bool pc_simon_milestone(int done) { return done > 0 && done % PC_SIMON_MILESTONE == 0; }

void pc_simon_timing(int rule, int len, int *on_ms, int *gap_ms)
{
    int on;
    if (rule == PC_SIMON_FAST) on = len <= 1 ? 420 : 420 - 22 * (len - 1);   /* faster every round down to the floor (130 ms from round 15) */
    else on = len <= 5 ? 420 : len <= 13 ? 340 : 260;                       /* the classic steps */
    if (on < 130) on = 130;
    *on_ms = on;
    *gap_ms = on * 2 / 5 < 60 ? 60 : on * 2 / 5;
}
