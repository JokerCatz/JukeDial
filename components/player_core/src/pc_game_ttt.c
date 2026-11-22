// SPDX-License-Identifier: LGPL-2.1-or-later
/* 井字棋 / Tic-tac-toe: rules (classic, 消失版, 盲棋) and the computer (pc_game_ttt.h). */
#include "pc/pc_game_ttt.h"
#include <string.h>

static const uint8_t LINES[8][3] = { {0,1,2}, {3,4,5}, {6,7,8}, {0,3,6}, {1,4,7}, {2,5,8}, {0,4,8}, {2,4,6} };

void pc_ttt_init_rule(pc_ttt_t *g, int rule)
{
    memset(g, 0, sizeof *g);
    g->turn = 1;
    g->rule = (uint8_t)(rule > 0 && rule < PC_TTT_RULES ? rule : PC_TTT_CLASSIC);
    g->gone = -1;
}
void pc_ttt_init(pc_ttt_t *g) { pc_ttt_init_rule(g, PC_TTT_CLASSIC); }

int pc_ttt_winner(const pc_ttt_t *g, int line[3])
{
    for (int l = 0; l < 8; l++) {
        uint8_t c = g->cell[LINES[l][0]];
        if (c && c == g->cell[LINES[l][1]] && c == g->cell[LINES[l][2]]) {
            if (line) for (int k = 0; k < 3; k++) line[k] = LINES[l][k];
            return c;
        }
    }
    return 0;
}

static bool full(const pc_ttt_t *g) { return g->rule != PC_TTT_VANISH ? g->moves >= PC_TTT_N : g->moves >= PC_TTT_MOVE_CAP; }   /* VANISH: the move cap is a draw */
bool pc_ttt_over(const pc_ttt_t *g) { return full(g) || pc_ttt_winner(g, NULL) != 0; }

int pc_ttt_fading(const pc_ttt_t *g, int side)
{
    if (g->rule != PC_TTT_VANISH || side < 1 || side > 2 || g->nhist[side - 1] < PC_TTT_KEEP) return -1;
    return g->hist[side - 1][0];
}

/* the move without checks: the removed mark (VANISH) first, then the new one; returns the removed cell, -1 none */
static int place(pc_ttt_t *g, int c)
{
    int rem = -1;
    if (g->rule == PC_TTT_VANISH) {
        int s = g->turn - 1;
        if (g->nhist[s] >= PC_TTT_KEEP) {
            rem = g->hist[s][0];
            g->cell[rem] = 0;
            memmove(g->hist[s], g->hist[s] + 1, PC_TTT_KEEP - 1);
            g->nhist[s]--;
        }
        g->hist[s][g->nhist[s]++] = (uint8_t)c;
    }
    g->cell[c] = g->turn;
    g->turn = (uint8_t)(3 - g->turn);
    g->moves++;
    return rem;
}

static void unplace(pc_ttt_t *g, int c, int rem)
{
    g->moves--;
    g->turn = (uint8_t)(3 - g->turn);
    g->cell[c] = 0;
    if (g->rule == PC_TTT_VANISH) {
        int s = g->turn - 1;
        g->nhist[s]--;
        if (rem >= 0) {
            memmove(g->hist[s] + 1, g->hist[s], PC_TTT_KEEP - 1);
            g->hist[s][0] = (uint8_t)rem;
            g->nhist[s]++;
            g->cell[rem] = g->turn;
        }
    }
}

bool pc_ttt_play(pc_ttt_t *g, int idx)
{
    if (idx < 0 || idx >= PC_TTT_N || g->cell[idx] || pc_ttt_over(g)) return false;
    g->gone = (int8_t)place(g, idx);
    return true;
}

static uint32_t rnd(pc_ttt_ai_t *a)
{
    uint32_t x = a->rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return a->rng = x;
}

void pc_ttt_ai_begin(pc_ttt_ai_t *a, const pc_ttt_t *g, uint32_t seed)
{
    memset(a, 0, sizeof *a);
    a->pos = *g;
    a->rng = seed * 2654435761u + 0x9E3779B9u;
    if (!a->rng) a->rng = 1;
    a->best = -100;
    a->move = -1;
    if (pc_ttt_over(g)) return;
    if (g->moves == 0) {                                      /* the opening: centre or a corner (all draw), no search; only on an empty cell (a hand-built position) */
        static const int8_t OPEN[5] = { 4, 0, 2, 6, 8 };
        int c = OPEN[rnd(a) % 5];
        if (!g->cell[c]) { a->move = c; return; }
    }
    for (int i = 0; i < PC_TTT_N; i++) if (!g->cell[i]) a->order[a->nroot++] = (int8_t)i;
}

static void finish(pc_ttt_ai_t *a)
{
    int n = 0, pick[PC_TTT_N];
    for (int i = 0; i < a->nroot; i++) if (a->score[i] == a->best) pick[n++] = a->order[i];
    a->move = n ? pick[rnd(a) % (uint32_t)n] : -1;
}

static void consider(pc_ttt_frame_t *f, int v)
{
    if (v > f->best) f->best = (int8_t)v;
    if (f->best > f->alpha) f->alpha = f->best;
    if (f->alpha >= f->beta) f->k = PC_TTT_N;
}

/* VANISH leaves: open lines of two (the third cell empty) for the side to move, minus the other side's; at most +-8, far below a win (50 - ply) */
static int leaf(const pc_ttt_t *g)
{
    int s = 0;
    for (int l = 0; l < 8; l++) {
        int me = 0, them = 0;
        for (int k = 0; k < 3; k++) { int c = g->cell[LINES[l][k]]; me += c == g->turn; them += c && c != g->turn; }
        if (me == 2 && !them) s++;
        else if (them == 2 && !me) s--;
    }
    return s;
}

/* negamax with alpha-beta on an explicit stack (see pc_c4_ai in pc_game_c4.c): the value for the side to move; a win n plies from the root is worth
 * 10 - n (CLASSIC, BLIND: searched to the end) or 50 - n (VANISH: to PC_TTT_VANISH_DEPTH plies, leaf() below that).
 * true: the root move's search is finished (*out, the opponent's view); false: the slice is over. */
static bool search(pc_ttt_ai_t *a, const pc_ai_budget_t *b, int64_t t0, int *out)
{
    pc_ttt_t *g = &a->pos;
    const bool van = g->rule == PC_TTT_VANISH;
    const int top = van ? 50 : 10;
    for (;;) {
        pc_ttt_frame_t *f = &a->f[a->sp];
        bool have = false;
        int ret = 0;
        if (!f->entered) {
            if (a->nodes > 0 && pc_ai_slice_over(b, t0, a->nodes)) return false;
            a->nodes++; a->nodes_total++;
            f->entered = 1; f->k = 0; f->best = -100;
            if (pc_ttt_winner(g, NULL)) { ret = -(top - f->ply); have = true; }   /* the side that just moved has won */
            else if (full(g)) { ret = 0; have = true; }
            else if (van && f->ply >= PC_TTT_VANISH_DEPTH) { ret = leaf(g); have = true; }
        }
        if (!have) {
            while (f->k < PC_TTT_N && g->cell[f->k]) f->k++;
            if (f->k >= PC_TTT_N) { ret = f->best; have = true; }
            else {
                int c = f->k++;
                f->cell = (int8_t)c;
                f->gone = (int8_t)place(g, c);
                pc_ttt_frame_t *n = &a->f[++a->sp];
                n->ply = (int8_t)(f->ply + 1); n->alpha = (int8_t)-f->beta; n->beta = (int8_t)-f->alpha; n->entered = 0;
            }
        }
        if (have) {
            if (a->sp == 0) { *out = ret; return true; }
            pc_ttt_frame_t *p = &a->f[--a->sp];
            unplace(g, p->cell, p->gone);
            consider(p, -ret);
        }
    }
}

bool pc_ttt_ai_step(pc_ttt_ai_t *a, const pc_ai_budget_t *b)
{
    if (a->move >= 0 || a->m >= a->nroot) { if (a->move < 0 && a->nroot) finish(a); return true; }
    int64_t t0 = b && b->now_us ? b->now_us(b->user) : 0;
    a->nodes = 0;
    bool done = false;
    while (!done) {
        if (!a->in_root) {
            int cell = a->order[a->m];
            a->root_gone = (int8_t)place(&a->pos, cell);
            pc_ttt_frame_t *f = &a->f[0];
            f->ply = 1; f->entered = 0; f->alpha = -100; f->beta = a->best <= -100 ? 100 : (int8_t)(1 - a->best);
            a->sp = 0; a->in_root = true;
        } else {
            int v;
            if (!search(a, b, t0, &v)) break;
            unplace(&a->pos, a->order[a->m], a->root_gone);
            a->score[a->m] = (int8_t)-v;
            a->in_root = false;
            if (a->score[a->m] > a->best) a->best = a->score[a->m];
            a->m++;
            if (a->m >= a->nroot) { finish(a); done = true; }
        }
    }
    a->slices++;
    a->slice_us = b && b->now_us ? (int)(b->now_us(b->user) - t0) : 0;
    if (a->slice_us > a->max_slice_us) a->max_slice_us = a->slice_us;
    if (a->nodes > a->nodes_max) a->nodes_max = a->nodes;
    return a->move >= 0;
}

int pc_ttt_ai_move(const pc_ttt_ai_t *a) { return a->move; }
