// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
/* 骨牌 / Dominoes (ideas G4): the rules, pure C (no UI, host-tested: test_game_dominoes). Double-six set, 28 tiles, 7 each, you (0) against the device (1).
 * A tile is an id 0..27: id = b * (b + 1) / 2 + a for the pips a <= b (so 0 = 0|0, 1 = 0|1, 2 = 1|1 ... 27 = 6|6). A hand is a bit mask over the ids.
 * The chain is a row of tiles in play order; tile i shows cl[i] on its left and cr[i] on its right (the chain's open ends are cl[0] and cr[n-1]; a tile matches an
 * end by one of its two numbers and is placed so that number touches the end). The game starts by itself: whoever holds the highest double plays it (without a
 * double: the tile with the most pips); the other side moves next.
 * Rules (規則, classic first):
 *   0 經典 Classic  a side that cannot play draws from the boneyard until it can (an empty boneyard: pass); the first empty hand wins; if both pass in a row (blocked)
 *                   the side with fewer pips wins (equal: a draw)
 *   1 阻塞賽 Block   no boneyard: a side that cannot play passes; the first empty hand wins, blocked as above
 *   2 五點 Five      as classic, and after a tile the sum of the two open ends is scored when it is a multiple of 5 (a double at an end counts both halves; a lone
 *                   double counts once); the round ends as classic and its winner also scores the other side's pips (the difference when blocked) rounded to 5; the
 *                   higher score wins
 * Surprise: with probability 1 in PC_DOM_GOLD_N a game has a golden double (gold_tile, from the seed): playing it is a gold event (+10 in 五點).
 * All random choices (the shuffle, the device's ties) come from the seed given at init. */
enum { PC_DOM_CLASSIC = 0, PC_DOM_BLOCK, PC_DOM_FIVE, PC_DOM_RULES };
enum { PC_DOM_PLAY = 0, PC_DOM_OVER };
enum { PC_DOM_YOU = 0, PC_DOM_DEV = 1, PC_DOM_DRAW = 2 };        /* a side; winner: also a draw */
enum { PC_DOM_LEFT = 0, PC_DOM_RIGHT = 1 };
#define PC_DOM_TILES  28
#define PC_DOM_HAND   7
#define PC_DOM_GOLD_N 10
typedef struct {
    uint8_t rule, state, turn, winner;
    uint8_t passes;                       /* passes in a row */
    uint8_t yard_n, chain_n;
    int8_t gold_tile;                     /* the golden double, -1 none */
    uint8_t yard[PC_DOM_TILES];           /* the boneyard: yard[yard_n - 1] is drawn next */
    uint8_t chain[PC_DOM_TILES];          /* tile ids in play order */
    uint8_t cl[PC_DOM_TILES], cr[PC_DOM_TILES];
    uint32_t hand[2];
    int score[2];                         /* 五點 (five points) */
    uint32_t rng;
    /* the last action, for the page's effects: who, tile, side (-1 draw / pass), gain (points), gold */
    int8_t last_who, last_side;
    uint8_t last_tile;
    int16_t last_gain;
    bool last_gold;
    int8_t start_tile;                    /* the tile the game began with, owner in start_who */
    uint8_t start_who;
    uint8_t plays[2];                     /* tiles each side played */
} pc_dom_t;

void pc_dom_init(pc_dom_t *g, int rule, uint32_t seed);       /* deal, auto-play the opening tile; an unknown rule is the classic */
int  pc_dom_a(int id);                    /* the pips of a tile (a <= b) */
int  pc_dom_b(int id);
int  pc_dom_id(int a, int b);             /* the tile with these pips (any order) */
#define PC_DOM_FACES 49                  /* the oriented faces a page shows: v1 * 7 + v2 */
bool pc_dom_face_ok(int face);            /* 0 <= face < PC_DOM_FACES (ui_cards' domino face set has exactly these) */
int  pc_dom_count(uint32_t mask);         /* tiles in a hand */
int  pc_dom_hand_pips(uint32_t mask);
int  pc_dom_left(const pc_dom_t *g);      /* the open ends' numbers (-1: an empty chain) */
int  pc_dom_right(const pc_dom_t *g);
int  pc_dom_ends_sum(const pc_dom_t *g);  /* what 五點 scores of the open ends now */
int  pc_dom_legal(const pc_dom_t *g, int id);                 /* bit 0: fits the left end, bit 1: the right end (an empty chain: both) */
bool pc_dom_can_move(const pc_dom_t *g, int who);
uint32_t pc_dom_playable(const pc_dom_t *g, int who);         /* the mask of the hand's tiles with a legal end */
int  pc_dom_place(pc_dom_t *g, int who, int id, int side);    /* >= 0: played, the points scored (五點); -1: illegal (not your turn, not in hand, no fit, over) */
int  pc_dom_draw(pc_dom_t *g, int who);                       /* the tile drawn, or -1 (not allowed: a tile can be played, no boneyard, the block rule, not the turn) */
bool pc_dom_pass(pc_dom_t *g, int who);                       /* only when no tile can be played and nothing can be drawn */
bool pc_dom_ai(pc_dom_t *g, int *id, int *side);              /* the device's choice among the legal moves of the side to move; false: none (it must draw or pass) */
