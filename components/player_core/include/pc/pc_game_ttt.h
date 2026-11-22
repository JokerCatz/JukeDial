// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "pc/pc_game_ai.h"
/* 井字棋 / Tic-tac-toe (games hub G0): the rules and a perfect-play computer player, no UI. Cells 0..8, row by row (cell = row * 3 + col).
 * Cell values: 0 empty, 1 X (moves first), 2 O. The AI is a stepper: pc_ttt_ai_step() searches for a wall-clock budget (alpha-beta on an explicit stack,
 * resumable at any node; pc_game_ai.h), so a UI timer can run one step per tick and never block; the result does not depend on the slicing. The empty board has a
 * fixed opening (centre or a corner, by `seed`: all draw with best play), so the most expensive search never runs. Equal best moves are chosen by `seed`.
 * The search follows the position's rule: CLASSIC and BLIND to the end of the game (perfect play: the device sees hidden marks), VANISH to
 * PC_TTT_VANISH_DEPTH plies with a small value at the leaves (open lines of two). */
#define PC_TTT_N 9
/* The rule variants (G0b):
 *   CLASSIC  the usual game: a full grid without a line is a draw
 *   VANISH   消失版: each side keeps at most three marks. A side that has three and places another first loses its OLDEST mark, then the line is
 *            checked (the removed mark never counts; the new mark always lands on an empty cell, never on the one that goes). No draws: there are
 *            never more than six marks, the game goes on until a line (or PC_TTT_MOVE_CAP moves: a draw). pc_ttt_fading() names the mark that goes at that side's next placement.
 *   BLIND    盲棋: the classic rules; the page hides the marks (2 s after each one is placed), the rules do not change */
enum { PC_TTT_CLASSIC = 0, PC_TTT_VANISH, PC_TTT_BLIND, PC_TTT_RULES };
#define PC_TTT_KEEP 3                 /* marks a side keeps in VANISH */
typedef struct {
    uint8_t cell[PC_TTT_N];
    uint8_t turn;                 /* 1 or 2: who moves next */
    uint8_t rule;                 /* PC_TTT_* */
    uint16_t moves;               /* moves played (classic and blind: the stones on the board) */
    uint8_t hist[2][PC_TTT_KEEP]; /* VANISH: each side's marks, oldest first */
    uint8_t nhist[2];
    int8_t gone;                  /* VANISH: the cell the last move removed, -1 none */
} pc_ttt_t;

void pc_ttt_init(pc_ttt_t *g);                           /* empty board, X to move, classic */
void pc_ttt_init_rule(pc_ttt_t *g, int rule);            /* the same with a rule (out of range: classic) */
bool pc_ttt_play(pc_ttt_t *g, int idx);                  /* the side to move takes cell idx; false: out of range, taken, or the game is over */
int  pc_ttt_winner(const pc_ttt_t *g, int line[3]);      /* 1 or 2 when that side has three in a row (line = its cells; may be NULL), else 0 */
bool pc_ttt_over(const pc_ttt_t *g);                     /* won, or full (classic, blind) */
int  pc_ttt_fading(const pc_ttt_t *g, int side);         /* VANISH: the cell of `side`'s mark that goes when that side places next (it has three), else -1 */
#define PC_TTT_MOVE_CAP 1000                             /* VANISH: a game reaching this many moves is a draw (it has no other end) */
#define PC_TTT_VANISH_DEPTH 7                            /* VANISH: plies the computer looks ahead (the game has no end to search to) */
#define PC_TTT_VANISH_NODES 20000                        /* the most nodes one move's search takes (host test: every move of its playouts stays below; 9384 seen) */

typedef struct {
    int8_t alpha, beta, best;
    int8_t ply, k, cell, entered, gone;   /* gone: the mark the move here removed (VANISH), -1 none */
} pc_ttt_frame_t;
typedef struct {
    pc_ttt_t pos;
    int8_t order[PC_TTT_N];       /* the root moves, in the order they are searched */
    int8_t score[PC_TTT_N];       /* their values for the side to move (a move that cannot beat the best so far has a bound only) */
    int nroot, m;                 /* root move m is being searched (or next) */
    bool in_root;
    int8_t root_gone;             /* the mark root move m removed (VANISH) */
    int sp;
    pc_ttt_frame_t f[PC_TTT_N + 1];
    int best;
    uint32_t rng;
    int nodes, nodes_max, nodes_total;   /* nodes of the last slice / of the largest slice / of the whole search */
    int slices, slice_us, max_slice_us;  /* slices run, the last one's microseconds and the largest (0 without a clock) */
    int move;                     /* the choice, -1 until done */
} pc_ttt_ai_t;

void pc_ttt_ai_begin(pc_ttt_ai_t *a, const pc_ttt_t *g, uint32_t seed);
bool pc_ttt_ai_step(pc_ttt_ai_t *a, const pc_ai_budget_t *budget);   /* one slice; true when the choice is made (and on later calls) */
int  pc_ttt_ai_move(const pc_ttt_ai_t *a);               /* the cell, -1 while still searching or when the game is over */
