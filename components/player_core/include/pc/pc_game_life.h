// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* 生命遊戲 / Game of Life, the fine square grid (ideas G6, game id `life`), pure C (no UI, host-tested: test_game_life).
 * A w x h grid of cells (the page uses the field's box in cell units: 360 / cell px). Every cell holds a 4-bit state kept in FOUR BITPLANES (64 cells a word, one
 * row = nw words, padding bits always 0; the caller hands in one memory block, pc_life_mem bytes, which the page takes from PSRAM):
 *     0        empty
 *     1..7     alive, the age LEVEL: born = 1; once every PC_LIFE_AGE_GENS generations (a global tick: generation % 6 == 0) every surviving cell below 7 goes up one,
 *              so a long-lived cell reaches 7 (GOLD) after about 36-42 generations; a cell that dies and is born again starts at 1
 *     8..12    a TRAIL (afterglow): a cell that just died is 8, then 9, ... for `trail` generations (0, 1, 3 or 5 steps), then empty; a birth on it makes it alive
 * Bit 3 = trail, so alive = ~b3 & (b0 | b1 | b2). The rule step, the age tick and the trail fade are all bitwise over 64 cells at once (the 8 neighbours are summed
 * by carry-save adders, 3 rows x left/centre/right, then each rule is a pair of masks over the count 0..8).
 * Rules (規則): CLASSIC B3/S23, BEAT the same (the page adds a random cluster on a beat), HIGH (HighLife B36/S23), DAYNIGHT (Day and Night B3678/S34678).
 * Edges: WALL = the field's border (square grid: its sides; pc_life_shape round: the circle of diameter min(w, h), outside it is always dead, a glider that reaches it dies);
 * TORUS = the grid's box wraps (the page paints only cells inside the field).
 * Drawing: every change of a cell's visual class (pc_life_class: empty / young / gold / trail k) sets its bit in the CHANGED plane (pc_life_chg): the page paints those
 * cells and calls pc_life_chg_clear. Level steps that do not change the colour (1..6) are not changes. A step ORs into the plane, so seeding between steps is not lost. */
enum { PC_LIFE_CLASSIC = 0, PC_LIFE_BEAT, PC_LIFE_HIGH, PC_LIFE_DAYNIGHT, PC_LIFE_RULES };
#define PC_LIFE_AGE_GENS 6
#define PC_LIFE_GOLD     7
#define PC_LIFE_TRAIL0   8
#define PC_LIFE_TRAIL_MAX 5
enum { PC_LIFE_C_EMPTY = 0, PC_LIFE_C_YOUNG, PC_LIFE_C_GOLD, PC_LIFE_C_TRAIL0 };   /* visual class: trail k is PC_LIFE_C_TRAIL0 + k */
typedef struct {
    int w, h, nw;                    /* cells, words a row */
    int rule, torus, trail;          /* the rule, edge (1 = wrap), trail steps (0, 1, 3, 5) */
    uint32_t gen, pop, peak, rng;    /* generations stepped, alive cells now, the most alive at once, the seed stream */
    uint64_t *pl[4];                 /* the state's bitplanes b0..b3, h * nw words each */
    uint64_t *old;                   /* the alive cells before a step (scratch) */
    uint64_t *chg;                   /* cells whose visual class changed since pc_life_chg_clear */
    uint64_t *mask;                  /* the field: every cell (square) or the circle (pc_life_shape) */
    void *mem;
} pc_life_t;
size_t pc_life_mem(int w, int h);                                 /* bytes the block must hold (7 planes + a little) */
bool pc_life_init(pc_life_t *s, void *mem, size_t bytes, int w, int h, uint32_t seed);   /* false: block too small / bad size (w, h 8..512); the grid empty, classic, wall, no trail */
void pc_life_config(pc_life_t *s, int rule, int torus, int trail);   /* trail is rounded to 0, 1, 3 or 5; nothing else is touched */
void pc_life_clear(pc_life_t *s);                                 /* every cell empty (every set cell is marked changed); the counters restart */
int  pc_life_value(const pc_life_t *s, int x, int y);             /* the 4-bit state; 0 outside the grid */
int  pc_life_class(int value);                                    /* visual class of a state */
void pc_life_shape(pc_life_t *s, bool round);                     /* right after init: round = the circle of diameter min(w, h) is the field (default: the whole square) */
bool pc_life_in_circle(const pc_life_t *s, int x, int y);
void pc_life_set(pc_life_t *s, int x, int y, bool alive);        /* alive = level 1; dead = empty (a wall cell is refused in WALL mode) */
uint32_t pc_life_rand(pc_life_t *s);
void pc_life_fill_random(pc_life_t *s, int pct);                  /* a clear grid, then every cell alive with pct % */
int  pc_life_splash(pc_life_t *s, int cx, int cy, int r, int pct);   /* a random blob: cells within r of (cx, cy) alive with pct %; returns cells set */
int  pc_life_step(pc_life_t *s);                                  /* one generation; returns the population */
/* the pattern library: 滑翔機 .. 兔子, tiny RLE strings in flash */
#define PC_LIFE_PATTERNS 8
const char *pc_life_pattern_rle(int i);
void pc_life_pattern_size(int i, int *w, int *h);
/* place pattern i centred on (cx, cy), flipped / turned by orient 0..7 (bit 0: mirror x, bit 1: mirror y, bit 2: transpose); returns cells set (cells outside the grid / circle skipped) */
int  pc_life_place(pc_life_t *s, int i, int cx, int cy, int orient);
uint64_t *pc_life_chg(pc_life_t *s);
void pc_life_chg_clear(pc_life_t *s);
/* host benchmark helper: generations the table of caps allows a second for a cell size in px (the page's honest gen/s cap) */
int  pc_life_cap_gens(int cell_px);
