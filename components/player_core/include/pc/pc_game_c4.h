// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "pc/pc_game_ai.h"
/* 四子棋 / Connect four (games hub G0): the rules (7 columns x 6 rows, gravity) and a few-ply minimax computer player, no UI.
 * Cells: index = row * 7 + col, row 0 at the TOP; values 0 empty, 1 first player, 2 second. The AI is a stepper: one call searches for a wall-clock
 * budget (alpha-beta on an explicit stack, resumable at any node; pc_game_ai.h), so a UI timer runs one step per tick and never blocks. The result does not
 * depend on the slicing. Equal best moves: the one nearest the centre, then by `seed`. */
#define PC_C4_W 7
#define PC_C4_H 6
#define PC_C4_N (PC_C4_W * PC_C4_H)
/* The rule variants (G0b):
 *   CLASSIC  the usual game: a full board without four is a draw
 *   WRAP     環形: the columns wrap, column 6 is next to column 0, for the lines across and the diagonals (a line may run 5 6 0 1); vertical lines do not wrap
 *   POPOUT   彈出 (Pop Out): a move is a drop OR a pop: the side to move takes ITS OWN disc from the bottom of a column and the discs above fall one cell.
 *            A pop can make four for either side or for both at once: four for the side that popped (alone or with the other side's) = the popper
 *            wins; four for the other side only = the other side wins. A full board is not the end (pops remain); a game reaching PC_C4_MOVE_CAP
 *            moves is a draw. A move is a number: 0..6 a drop into that column, PC_C4_POP_MOVE(col) = 7..13 a pop of that column. */
enum { PC_C4_CLASSIC = 0, PC_C4_WRAP, PC_C4_POPOUT, PC_C4_RULES };
#define PC_C4_POP_MOVE(col) (PC_C4_W + (col))
#define PC_C4_MOVES (2 * PC_C4_W)
#define PC_C4_MOVE_CAP 200
typedef struct {
    uint8_t cell[PC_C4_N];
    uint8_t height[PC_C4_W];      /* stones in each column */
    uint8_t turn;                 /* 1 or 2: who drops next */
    uint8_t rule;                 /* PC_C4_* */
    uint8_t last;                 /* who made the last move (0 none): the popper when a pop made four for both */
    uint16_t moves;               /* moves played (CLASSIC, WRAP: the discs on the board) */
} pc_c4_t;

void pc_c4_init(pc_c4_t *g);                              /* empty, player 1 to drop, classic */
void pc_c4_init_rule(pc_c4_t *g, int rule);               /* the same with a rule (out of range: classic) */
int  pc_c4_drop(pc_c4_t *g, int col);                     /* the side to move drops into col: the cell index it lands on; -1: no such column, full, or the game is over */
bool pc_c4_can_pop(const pc_c4_t *g, int col);            /* POPOUT, not over, and the bottom disc of col is the side to move's */
int  pc_c4_pop(pc_c4_t *g, int col);                      /* that pop: the bottom cell of col; -1 refused */
bool pc_c4_legal(const pc_c4_t *g, int move);             /* a drop (0..6) or a pop (PC_C4_POP_MOVE) the side to move may make now */
int  pc_c4_play(pc_c4_t *g, int move);                    /* pc_c4_drop or pc_c4_pop by the move's number: the cell, -1 refused */
int  pc_c4_winner(const pc_c4_t *g, int line[4]);         /* 1 or 2 with four in a row (line = its four cells, in order along the line; may be NULL), else 0; both: the last mover */
bool pc_c4_over(const pc_c4_t *g);                        /* won, full (CLASSIC, WRAP), the move cap (POPOUT) */

#define PC_C4_DEPTH 6                                     /* plies the computer looks ahead (CLASSIC, WRAP) */
#define PC_C4_POP_DEPTH 5                                 /* POPOUT: up to 14 moves a node, one ply less */
#define PC_C4_MAXD  8
typedef struct {                  /* one level of the search (an explicit stack: the search can stop at any node and resume) */
    int32_t alpha, beta, best;
    int8_t depth, ply, k, col;    /* plies left, plies from the root, next order index, the move made here */
    uint8_t entered;              /* 0: the node is not entered yet */
} pc_c4_frame_t;
typedef struct {
    pc_c4_t pos;
    int depth;
    int8_t col[PC_C4_MOVES];      /* the root moves (drops centre first, then pops), PC_C4_POP_MOVE numbers for pops */
    int32_t score[PC_C4_MOVES];       /* the value of each for the side to move (a move that cannot beat the best so far has a bound only) */
    int nroot, m;                 /* root move m is being searched (or next) */
    bool in_root;                 /* root move m is dropped and frames 0..sp are its search */
    int sp;
    pc_c4_frame_t f[PC_C4_MAXD + 1];
    int32_t best;
    uint32_t rng;
    int nodes, nodes_max, nodes_total;   /* nodes of the last slice / of the largest slice / of the whole search */
    int slices, slice_us, max_slice_us;  /* slices run, the last one's microseconds and the largest (0 without a clock) */
    int move;                     /* the move (a column; PC_C4_POP_MOVE for a pop), -1 until done */
} pc_c4_ai_t;

void pc_c4_ai_begin(pc_c4_ai_t *a, const pc_c4_t *g, uint32_t seed, int depth);   /* depth 0: PC_C4_DEPTH (POPOUT: PC_C4_POP_DEPTH); 3..8. The search follows the position's rule */
/* One slice (see pc_game_ai.h): searches until done or the budget is spent; true when the choice is made (and on later calls). */
bool pc_c4_ai_step(pc_c4_ai_t *a, const pc_ai_budget_t *budget);
int  pc_c4_ai_move(const pc_c4_ai_t *a);                  /* the move (a column, or PC_C4_POP_MOVE(col)), -1 while searching or when the game is over */
int  pc_c4_eval(const pc_c4_t *g, int who);               /* the static value of a position for `who` (what the search uses at its leaves) */
