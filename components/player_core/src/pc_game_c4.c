// SPDX-License-Identifier: LGPL-2.1-or-later
/* 四子棋 / Connect four: rules (classic, 環形, 彈出) and a few-ply minimax computer (pc_game_c4.h). */
#include "pc/pc_game_c4.h"
#include <string.h>
#include <limits.h>

#define WIN   100000
#define INF   1000000

void pc_c4_init_rule(pc_c4_t *g, int rule)
{
    memset(g, 0, sizeof *g);
    g->turn = 1;
    g->rule = (uint8_t)(rule > 0 && rule < PC_C4_RULES ? rule : PC_C4_CLASSIC);
}
void pc_c4_init(pc_c4_t *g) { pc_c4_init_rule(g, PC_C4_CLASSIC); }

static const int8_t D[4][2] = { {0, 1}, {1, 0}, {1, 1}, {1, -1} };

/* the column c, wrapped when the rule wraps (WRAP); -1 when it is off the board */
static inline int col_at(const pc_c4_t *g, int c)
{
    if (c >= 0 && c < PC_C4_W) return c;
    return g->rule == PC_C4_WRAP ? (c % PC_C4_W + PC_C4_W) % PC_C4_W : -1;
}

/* four in a row through cell `at` for `who`: fills line (4 cells, in order along the line) when asked. WRAP: across and the diagonals wrap */
static bool four_at(const pc_c4_t *g, int at, int who, int line[4])
{
    int r0 = at / PC_C4_W, c0 = at % PC_C4_W;
    for (int d = 0; d < 4; d++) {
        int n = 1, lo = 0;
        for (int k = 1; k < 4; k++) {
            int r = r0 + D[d][0] * k, c = col_at(g, c0 + D[d][1] * k);
            if (r < 0 || r >= PC_C4_H || c < 0 || g->cell[r * PC_C4_W + c] != who) break;
            n++;
        }
        for (int k = 1; k < 4; k++) {
            int r = r0 - D[d][0] * k, c = col_at(g, c0 - D[d][1] * k);
            if (r < 0 || r >= PC_C4_H || c < 0 || g->cell[r * PC_C4_W + c] != who) break;
            n++; lo = k;
        }
        if (n >= 4) {
            if (line) {
                int start = -lo;                          /* the first four of the run, from its low end */
                for (int k = 0; k < 4; k++) line[k] = (r0 + D[d][0] * (start + k)) * PC_C4_W + col_at(g, c0 + D[d][1] * (start + k));
            }
            return true;
        }
    }
    return false;
}

int pc_c4_winner(const pc_c4_t *g, int line[4])
{
    int l[2][4];
    bool w[2] = { false, false };
    for (int i = 0; i < PC_C4_N && !(w[0] && w[1]); i++) {
        int v = g->cell[i];
        if (v && !w[v - 1] && four_at(g, i, v, l[v - 1])) w[v - 1] = true;
    }
    int who = w[0] && w[1] ? (g->last ? g->last : 1) : w[0] ? 1 : w[1] ? 2 : 0;
    if (who && line) memcpy(line, l[who - 1], sizeof l[0]);
    return who;
}

static bool can_drop_raw(const pc_c4_t *g, int col) { return col >= 0 && col < PC_C4_W && g->height[col] < PC_C4_H; }
static bool can_pop_raw(const pc_c4_t *g, int col)
{
    return g->rule == PC_C4_POPOUT && col >= 0 && col < PC_C4_W && g->height[col] > 0 && g->cell[(PC_C4_H - 1) * PC_C4_W + col] == g->turn;
}
static bool any_move(const pc_c4_t *g)
{
    for (int c = 0; c < PC_C4_W; c++) if (can_drop_raw(g, c) || can_pop_raw(g, c)) return true;
    return false;
}
static bool drawn(const pc_c4_t *g)           /* the end without a winner */
{
    if (g->rule != PC_C4_POPOUT) return g->moves >= PC_C4_N;
    return g->moves >= PC_C4_MOVE_CAP || !any_move(g);
}

bool pc_c4_over(const pc_c4_t *g) { return drawn(g) || pc_c4_winner(g, NULL) != 0; }

static int do_drop(pc_c4_t *g, int col)                   /* no checks: the cell it lands on */
{
    int at = (PC_C4_H - 1 - g->height[col]) * PC_C4_W + col;
    g->cell[at] = g->turn;
    g->height[col]++;
    g->last = g->turn;
    g->turn = (uint8_t)(3 - g->turn);
    g->moves++;
    return at;
}

static void undo_drop(pc_c4_t *g, int col)
{
    g->moves--;
    g->turn = (uint8_t)(3 - g->turn);
    g->height[col]--;
    g->cell[(PC_C4_H - 1 - g->height[col]) * PC_C4_W + col] = 0;
}

static int do_pop(pc_c4_t *g, int col)                    /* no checks: the bottom disc goes, the column falls one cell */
{
    for (int r = PC_C4_H - 1; r > 0; r--) g->cell[r * PC_C4_W + col] = g->cell[(r - 1) * PC_C4_W + col];
    g->cell[col] = 0;
    g->height[col]--;
    g->last = g->turn;
    g->turn = (uint8_t)(3 - g->turn);
    g->moves++;
    return (PC_C4_H - 1) * PC_C4_W + col;
}

static void undo_pop(pc_c4_t *g, int col)
{
    g->moves--;
    g->turn = (uint8_t)(3 - g->turn);
    for (int r = 0; r < PC_C4_H - 1; r++) g->cell[r * PC_C4_W + col] = g->cell[(r + 1) * PC_C4_W + col];
    g->cell[(PC_C4_H - 1) * PC_C4_W + col] = g->turn;
    g->height[col]++;
}

/* after `who` popped col: +1 four for who (alone or with the other's), -1 four for the other side only, 0 none. Only column col changed, so a new line runs through it */
static int pop_result(const pc_c4_t *g, int col, int who)
{
    bool mine = false, theirs = false;
    for (int r = PC_C4_H - g->height[col]; r < PC_C4_H; r++) {
        int at = r * PC_C4_W + col;
        if (!mine && four_at(g, at, who, NULL)) mine = true;
        if (!theirs && four_at(g, at, 3 - who, NULL)) theirs = true;
    }
    return mine ? 1 : theirs ? -1 : 0;
}

int pc_c4_drop(pc_c4_t *g, int col)
{
    if (!can_drop_raw(g, col) || pc_c4_over(g)) return -1;
    return do_drop(g, col);
}
bool pc_c4_can_pop(const pc_c4_t *g, int col) { return can_pop_raw(g, col) && !pc_c4_over(g); }
int pc_c4_pop(pc_c4_t *g, int col) { return pc_c4_can_pop(g, col) ? do_pop(g, col) : -1; }
bool pc_c4_legal(const pc_c4_t *g, int m)
{
    if (m < 0 || m >= PC_C4_MOVES || pc_c4_over(g)) return false;
    return m < PC_C4_W ? can_drop_raw(g, m) : can_pop_raw(g, m - PC_C4_W);
}
int pc_c4_play(pc_c4_t *g, int m)
{
    if (!pc_c4_legal(g, m)) return -1;
    return m < PC_C4_W ? do_drop(g, m) : do_pop(g, m - PC_C4_W);
}

/* the value of the position for `who`: every window of four with stones of one side only counts (2, 3 in it), the centre columns a little
 * (not in WRAP: no column is the centre there; its windows across the edge count too) */
int pc_c4_eval(const pc_c4_t *g, int who)
{
    static const int W[5] = { 0, 1, 4, 24, 0 };
    const bool wrap = g->rule == PC_C4_WRAP;
    int s = 0;
    for (int r = 0; r < PC_C4_H; r++)
        for (int c = 0; c < PC_C4_W; c++) {
            int v = g->cell[r * PC_C4_W + c];
            if (!wrap) { int cb = 3 - (c > 3 ? c - 3 : 3 - c); if (v == who) s += cb; else if (v) s -= cb; }
            for (int d = 0; d < 4; d++) {
                int r2 = r + D[d][0] * 3, c2 = c + D[d][1] * 3;
                if (r2 >= PC_C4_H || (!wrap && (c2 < 0 || c2 >= PC_C4_W))) continue;
                int mine = 0, theirs = 0;
                for (int k = 0; k < 4; k++) {
                    int x = g->cell[(r + D[d][0] * k) * PC_C4_W + col_at(g, c + D[d][1] * k)];
                    if (x == who) mine++; else if (x) theirs++;
                }
                if (mine && !theirs) s += W[mine]; else if (theirs && !mine) s -= W[theirs];
            }
        }
    return s;
}

static const int8_t ORDER[PC_C4_W] = { 3, 2, 4, 1, 5, 0, 6 };
static inline int order_move(int k) { return k < PC_C4_W ? ORDER[k] : PC_C4_W + ORDER[k - PC_C4_W]; }   /* drops centre first, then pops */
static inline bool move_ok(const pc_c4_t *g, int m) { return m < PC_C4_W ? can_drop_raw(g, m) : can_pop_raw(g, m - PC_C4_W); }
static inline int n_moves(const pc_c4_t *g) { return g->rule == PC_C4_POPOUT ? PC_C4_MOVES : PC_C4_W; }

static uint32_t rnd(pc_c4_ai_t *a)
{
    uint32_t x = a->rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return a->rng = x;
}

void pc_c4_ai_begin(pc_c4_ai_t *a, const pc_c4_t *g, uint32_t seed, int depth)
{
    memset(a, 0, sizeof *a);
    a->pos = *g;
    a->depth = depth ? depth : g->rule == PC_C4_POPOUT ? PC_C4_POP_DEPTH : PC_C4_DEPTH;
    if (a->depth < 3) a->depth = 3;
    if (a->depth > PC_C4_MAXD) a->depth = PC_C4_MAXD;
    a->rng = seed * 2654435761u + 0x9E3779B9u;
    if (!a->rng) a->rng = 1;
    a->best = -INF;
    a->move = -1;
    if (pc_c4_over(g)) return;
    for (int k = 0; k < n_moves(g); k++) { int m = order_move(k); if (move_ok(g, m)) a->col[a->nroot++] = (int8_t)m; }
}

static int centre_dist(int m) { int c = m % PC_C4_W; return c > 3 ? c - 3 : 3 - c; }

static void finish(pc_c4_ai_t *a)
{
    int n = 0, pick[PC_C4_MOVES];
    for (int i = 0; i < a->nroot; i++) if (a->score[i] == a->best) pick[n++] = a->col[i];
    /* the order is centre first: the first of the equal best is the most central, the seed only decides between the equally central two */
    if (n > 1) {
        int c0 = centre_dist(pick[0]), m = 1;
        while (m < n && centre_dist(pick[m]) == c0) m++;
        a->move = pick[rnd(a) % (uint32_t)m];
    } else a->move = n ? pick[0] : -1;
}

static void consider(pc_c4_frame_t *f, int32_t v)
{
    if (v > f->best) f->best = v;
    if (f->best > f->alpha) f->alpha = f->best;
    if (f->alpha >= f->beta) f->k = PC_C4_MOVES;             /* cut: no more moves here */
}

static void undo_move(pc_c4_t *g, int m) { if (m < PC_C4_W) undo_drop(g, m); else undo_pop(g, m - PC_C4_W); }
/* makes move m for the side to move: +1 it won at once, -1 it lost at once (a pop that gives the other side four), 0 the game goes on */
static int make_move(pc_c4_t *g, int m)
{
    int who = g->turn;
    if (m < PC_C4_W) return four_at(g, do_drop(g, m), who, NULL) ? 1 : 0;
    do_pop(g, m - PC_C4_W);
    return pop_result(g, m - PC_C4_W, who);
}

/* The search of one root move (frames 0..sp) as negamax with alpha-beta; a win n plies from the root is worth WIN - n. The loop is the recursion
 * of the textbook version unrolled onto a->f[]: every node is entered at most once and the state between two nodes is all in `a`, so it can stop
 * after any node. Returns true when the root move's search is finished (its value in *out, the opponent's view), false when the slice is over. */
static bool search(pc_c4_ai_t *a, const pc_ai_budget_t *b, int64_t t0, int32_t *out)
{
    pc_c4_t *g = &a->pos;
    const int nm = n_moves(g);
    for (;;) {
        pc_c4_frame_t *f = &a->f[a->sp];
        bool have = false;
        int32_t ret = 0;
        if (!f->entered) {
            if (a->nodes > 0 && pc_ai_slice_over(b, t0, a->nodes)) return false;
            a->nodes++; a->nodes_total++;
            f->entered = 1; f->k = 0; f->best = -INF;
            if (g->rule != PC_C4_POPOUT ? g->moves >= PC_C4_N : g->moves >= PC_C4_MOVE_CAP) { ret = 0; have = true; }
            else if (f->depth <= 0) { ret = pc_c4_eval(g, g->turn); have = true; }
        }
        if (!have) {
            while (f->k < nm && !move_ok(g, order_move(f->k))) f->k++;
            if (f->k >= nm) { ret = f->best <= -INF ? 0 : f->best; have = true; }   /* no move at all: a draw */
            else {
                int m = order_move(f->k++);
                f->col = (int8_t)m;
                int r = make_move(g, m);
                if (r) { undo_move(g, m); consider(f, r > 0 ? WIN - f->ply : -(WIN - f->ply)); }
                else {
                    pc_c4_frame_t *n = &a->f[++a->sp];
                    n->depth = (int8_t)(f->depth - 1); n->ply = (int8_t)(f->ply + 1);
                    n->alpha = -f->beta; n->beta = -f->alpha; n->entered = 0;
                }
            }
        }
        if (have) {
            if (a->sp == 0) { *out = ret; return true; }
            pc_c4_frame_t *p = &a->f[--a->sp];
            undo_move(g, p->col);
            consider(p, -ret);
        }
    }
}

bool pc_c4_ai_step(pc_c4_ai_t *a, const pc_ai_budget_t *b)
{
    if (a->move >= 0 || a->m >= a->nroot) { if (a->move < 0 && a->nroot) finish(a); return true; }
    int64_t t0 = b && b->now_us ? b->now_us(b->user) : 0;
    a->nodes = 0;
    bool done = false;
    while (!done) {
        if (!a->in_root) {                                  /* start root move m */
            int m = a->col[a->m];
            int r = make_move(&a->pos, m);
            if (r) { undo_move(&a->pos, m); a->score[a->m] = r > 0 ? WIN : -WIN; }
            else if (a->pos.rule != PC_C4_POPOUT ? a->pos.moves >= PC_C4_N : a->pos.moves >= PC_C4_MOVE_CAP) { undo_move(&a->pos, m); a->score[a->m] = 0; }
            else {
                /* only a value of at least the best so far matters (ties are chosen between): the window below it is closed */
                pc_c4_frame_t *f = &a->f[0];
                f->depth = (int8_t)(a->depth - 1); f->ply = 1; f->entered = 0;
                f->alpha = -INF; f->beta = a->best <= -INF ? INF : 1 - a->best;
                a->sp = 0; a->in_root = true;
                continue;
            }
        } else {
            int32_t v;
            if (!search(a, b, t0, &v)) break;                /* the slice is over: resume here next call */
            undo_move(&a->pos, a->col[a->m]);
            a->score[a->m] = -v;
            a->in_root = false;
        }
        if (a->score[a->m] > a->best) a->best = a->score[a->m];
        a->m++;
        if (a->m >= a->nroot) { finish(a); done = true; }
    }
    a->slices++;
    a->slice_us = b && b->now_us ? (int)(b->now_us(b->user) - t0) : 0;
    if (a->slice_us > a->max_slice_us) a->max_slice_us = a->slice_us;
    if (a->nodes > a->nodes_max) a->nodes_max = a->nodes;
    return a->move >= 0;
}

int pc_c4_ai_move(const pc_c4_ai_t *a) { return a->move; }
