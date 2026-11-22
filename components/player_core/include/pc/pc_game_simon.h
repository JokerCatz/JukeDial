// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
/* 四色記憶 / Simon (ideas wave 0a, G1): the rules, no UI. Four colours (0..3) sit on four sectors (place[colour]); the device plays a sequence of
 * colours that grows by one each round, the player repeats it by tapping the sectors; a wrong tap ends the game. The page plays the sequence on an
 * LVGL timer (pc_simon_timing gives the light and the pause of each step) and feeds every tap to pc_simon_tap. The random colours come from the
 * seed the page gives (games_seed of the game: its own stream, never the shared one).
 * The rule variants (game-feel.md: a variant changes a rule):
 *   CLASSIC  repeat the sequence in the order it was played
 *   REVERSE  倒序: repeat it backwards (the last colour first)
 *   SHUFFLE  洗牌: after every PC_SIMON_SHUFFLE_EVERY rounds the colours move to other sectors (every colour leaves its sector); the sequence is of
 *            colours, so the player must find where each colour went
 *   FAST     加速: each round is played faster than the one before (classic speeds up only at lengths 6 and 14) */
enum { PC_SIMON_CLASSIC = 0, PC_SIMON_REVERSE, PC_SIMON_SHUFFLE, PC_SIMON_FAST, PC_SIMON_RULES };
enum { PC_SIMON_OK = 0, PC_SIMON_ROUND, PC_SIMON_WRONG, PC_SIMON_IGNORED };   /* pc_simon_tap: right so far / the round is complete / wrong (over) / not now */
#define PC_SIMON_COLOURS       4
#define PC_SIMON_MAX           99     /* the longest sequence: repeating it ends the game as a win */
#define PC_SIMON_MILESTONE     5      /* every this many rounds: a milestone (the page's fanfare and one more segment of the ring) */
#define PC_SIMON_SHUFFLE_EVERY 5
typedef struct {
    uint8_t rule;                     /* PC_SIMON_* */
    uint8_t len;                      /* the sequence's length now (0 before the first round) */
    uint8_t pos;                      /* taps of this round so far */
    uint8_t done;                     /* rounds repeated in full */
    uint8_t over;                     /* a wrong tap ended the game (or PC_SIMON_MAX was repeated) */
    uint8_t moves;                    /* times the colours moved (SHUFFLE) */
    uint8_t place[PC_SIMON_COLOURS];  /* the sector of each colour */
    uint8_t seq[PC_SIMON_MAX];        /* colours */
    uint32_t rng;
} pc_simon_t;

void pc_simon_init(pc_simon_t *g, int rule, uint32_t seed);   /* empty sequence, colours on their own sectors; a rule out of range = CLASSIC */
/* The next round: one more colour at the end (pos back to 0). SHUFFLE: when PC_SIMON_SHUFFLE_EVERY more rounds were done since the last move, the colours
 * move first (every colour to another sector). Returns true when the colours moved now. Nothing (false) when over or at PC_SIMON_MAX. */
bool pc_simon_grow(pc_simon_t *g);
int  pc_simon_shown(const pc_simon_t *g, int k);              /* the sector the device lights as the k-th of the round (0..len-1), -1 out of range */
int  pc_simon_expect(const pc_simon_t *g);                    /* the sector the next tap must hit (REVERSE: from the end), -1 when there is none */
int  pc_simon_tap(pc_simon_t *g, int sector);                 /* PC_SIMON_OK / ROUND / WRONG / IGNORED (over, before the first round, or a round already complete) */
void pc_simon_retry(pc_simon_t *g);                           /* the same round again from its first colour (the page's practice mode after a wrong tap) */
bool pc_simon_milestone(int done);                            /* done > 0 and a multiple of PC_SIMON_MILESTONE */
void pc_simon_timing(int rule, int len, int *on_ms, int *gap_ms);   /* how long each colour is lit and the pause after it, for a round of `len` */
