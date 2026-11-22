// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
/* 15 拼圖 / 15 puzzle (ideas G4): the rules, pure C (no UI, host-tested: test_game_puzzle15).
 * A square of side n (3, 4 or 5) holds the tiles 1..n*n-1 and one blank; a tile next to the blank (up, down, left, right) slides into it. Solved: the tiles
 * in order along the rows, the blank last (bottom right). The shuffle is always solvable (inversion parity, pc_p15_solvable) and never already solved; it is
 * deterministic from the seed (the page gives games_seed). One move = one tile slid; there is no undo.
 * Rules (規則, classic first): 0 經典 4x4, 1 快 3x3, 2 大 5x5.
 * Surprise: a solve in at most pc_p15_par(rule) moves (close to what an optimal solver needs on average) is a golden solve. */
enum { PC_P15_R4 = 0, PC_P15_R3, PC_P15_R5, PC_P15_RULES };
#define PC_P15_MAX 5
typedef struct {
    uint8_t rule, n, blank;           /* blank: the index of the blank cell */
    uint16_t moves;
    uint8_t t[PC_P15_MAX * PC_P15_MAX];   /* cell -> tile 1..n*n-1, 0 = the blank */
} pc_p15_t;
int  pc_p15_side(int rule);                                   /* 4, 3, 5 (an unknown rule is the classic) */
int  pc_p15_par(int rule);                                    /* the golden solve: at most this many moves */
void pc_p15_init(pc_p15_t *g, int rule, uint32_t seed);       /* a solvable shuffle that is not solved */
void pc_p15_solved_state(pc_p15_t *g, int rule);              /* the solved board (tests, pictures) */
bool pc_p15_solvable(const uint8_t *t, int n);                /* the tiles of an n x n board can be brought to the solved order (the goal has the blank last) */
bool pc_p15_movable(const pc_p15_t *g, int idx);              /* the cell next to the blank, holding a tile */
bool pc_p15_move(pc_p15_t *g, int idx);                       /* slides that tile into the blank; false: not next to it */
bool pc_p15_solved(const pc_p15_t *g);
bool pc_p15_home(const pc_p15_t *g, int idx);                 /* the cell holds the tile that belongs there */
int  pc_p15_manhattan(const pc_p15_t *g);                     /* the sum of the tiles' distances from home (a lower bound of the moves left) */
int  pc_p15_neighbours(const pc_p15_t *g, int *out);          /* the cells next to the blank in reading order (up to 4): the key cursor's stops */
