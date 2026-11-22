// SPDX-License-Identifier: LGPL-2.1-or-later
/* 15 拼圖: see include/pc/pc_game_puzzle15.h */
#include <string.h>
#include "pc/pc_game_puzzle15.h"

int pc_p15_side(int rule) { return rule == PC_P15_R3 ? 3 : rule == PC_P15_R5 ? 5 : 4; }
int pc_p15_par(int rule) { return rule == PC_P15_R3 ? 26 : rule == PC_P15_R5 ? 150 : 70; }

static void setup(pc_p15_t *g, int rule)
{
    memset(g, 0, sizeof *g);
    g->rule = (uint8_t)(rule < 0 || rule >= PC_P15_RULES ? PC_P15_R4 : rule);
    g->n = (uint8_t)pc_p15_side(g->rule);
}

void pc_p15_solved_state(pc_p15_t *g, int rule)
{
    setup(g, rule);
    int N = g->n * g->n;
    for (int i = 0; i < N - 1; i++) g->t[i] = (uint8_t)(i + 1);
    g->t[N - 1] = 0;
    g->blank = (uint8_t)(N - 1);
}

bool pc_p15_solvable(const uint8_t *t, int n)
{
    int N = n * n, inv = 0, brow = 0;
    for (int i = 0; i < N; i++) {
        if (!t[i]) { brow = i / n; continue; }
        for (int j = i + 1; j < N; j++) if (t[j] && t[j] < t[i]) inv++;
    }
    return n % 2 ? inv % 2 == 0 : (inv + brow) % 2 == 1;   /* the goal has the blank in the last row (n - 1, odd for an even n) */
}

bool pc_p15_solved(const pc_p15_t *g)
{
    int N = g->n * g->n;
    for (int i = 0; i < N - 1; i++) if (g->t[i] != i + 1) return false;
    return g->t[N - 1] == 0;
}

void pc_p15_init(pc_p15_t *g, int rule, uint32_t seed)
{
    setup(g, rule);
    const int n = g->n, N = n * n;
    uint32_t s = seed * 2654435761u + 0x9E3779B9u;
    if (!s) s = 1;
#define RND() (s ^= s << 13, s ^= s >> 17, s ^= s << 5, s)
    for (int i = 0; i < 8; i++) (void)RND();
    for (int i = 0; i < N; i++) g->t[i] = (uint8_t)((i + 1) % N);
    for (int i = N - 1; i > 0; i--) { int j = (int)(RND() % (uint32_t)(i + 1)); uint8_t x = g->t[i]; g->t[i] = g->t[j]; g->t[j] = x; }
    if (!pc_p15_solvable(g->t, n)) {                 /* swap the first two tiles (not the blank): the parity flips */
        int a = 0, b;
        while (!g->t[a]) a++;
        b = a + 1;
        while (!g->t[b]) b++;
        uint8_t x = g->t[a]; g->t[a] = g->t[b]; g->t[b] = x;
    }
    if (pc_p15_solved(g)) {                          /* the lucky shuffle: two tiles swapped twice over would be unsolvable, so slide the blank once */
        int b = N - 1;
        g->t[b] = g->t[b - 1]; g->t[b - 1] = 0;
    }
    for (int i = 0; i < N; i++) if (!g->t[i]) g->blank = (uint8_t)i;
#undef RND
}

static bool adjacent(const pc_p15_t *g, int a, int b)
{
    int n = g->n;
    if (a < 0 || b < 0 || a >= n * n || b >= n * n) return false;
    int dr = a / n - b / n, dc = a % n - b % n;
    return (dr == 0 && (dc == 1 || dc == -1)) || (dc == 0 && (dr == 1 || dr == -1));
}
bool pc_p15_movable(const pc_p15_t *g, int idx) { return adjacent(g, idx, g->blank); }
bool pc_p15_move(pc_p15_t *g, int idx)
{
    if (!pc_p15_movable(g, idx)) return false;
    g->t[g->blank] = g->t[idx];
    g->t[idx] = 0;
    g->blank = (uint8_t)idx;
    g->moves++;
    return true;
}
bool pc_p15_home(const pc_p15_t *g, int idx)
{
    int N = g->n * g->n;
    return idx >= 0 && idx < N && g->t[idx] == (idx == N - 1 ? 0 : idx + 1);
}
int pc_p15_manhattan(const pc_p15_t *g)
{
    int n = g->n, N = n * n, sum = 0;
    for (int i = 0; i < N; i++) {
        int v = g->t[i];
        if (!v) continue;
        int h = v - 1, dr = i / n - h / n, dc = i % n - h % n;
        sum += (dr < 0 ? -dr : dr) + (dc < 0 ? -dc : dc);
    }
    return sum;
}
int pc_p15_neighbours(const pc_p15_t *g, int *out)
{
    int k = 0, N = g->n * g->n;
    for (int i = 0; i < N; i++) if (adjacent(g, i, g->blank)) out[k++] = i;
    return k;
}
