// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
/* The session score of a game page (G0b game feel, shared by every game): the result card's "best this session", the win streak (3 wins in a row
 * against the device: another jingle and a badge) and the rare golden game (1 in PC_GAME_GOLD_N). Nothing is stored: a page keeps one
 * pc_game_score_t as a static (PSRAM), so it lasts until the device restarts. No UI. */
#define PC_GAME_VARIANTS 6                /* rule variants a page may keep a best for */
#define PC_GAME_STREAK   3                /* wins in a row that make a streak */
#define PC_GAME_GOLD_N   30               /* one game in this many is golden */
enum { PC_GAME_LOSS = 0, PC_GAME_WIN = 1, PC_GAME_DRAW = 2 };
typedef struct {
    uint16_t played, wins, streak, best_streak;
    uint16_t best_moves[PC_GAME_VARIANTS];   /* the fewest moves of a win of each variant, 0 none yet */
    uint32_t best_ms[PC_GAME_VARIANTS];      /* that win's time (a win with as few moves and less time beats it); a score game: the best's time */
    uint32_t best_points[PC_GAME_VARIANTS];  /* a score game (G1 環形打磚塊): the most points of each variant, 0 none yet */
} pc_game_score_t;
typedef struct {
    bool new_best;                        /* this game is the variant's new best */
    bool streak_hit;                      /* this win makes the streak PC_GAME_STREAK or more */
    int streak;                           /* wins in a row against the device now */
    int best_moves;                       /* the variant's best now (0 none) */
    uint32_t best_ms;
    uint32_t best_points;                 /* a score game: the variant's most points now (0 none) */
} pc_game_rec_t;

void pc_game_score_reset(pc_game_score_t *s);
/* One finished game. outcome: PC_GAME_* from the player's side (two players: the winner's side counts as a WIN). vs_device: only those games change
 * the streak (a loss or a draw ends it). moves: the game's moves (both sides), ms: its time. A variant outside 0..PC_GAME_VARIANTS-1 is counted, never a best. */
pc_game_rec_t pc_game_score_record(pc_game_score_t *s, int variant, int outcome, bool vs_device, int moves, uint32_t ms);
/* One finished run of a SCORE game (G1 四色記憶: the length reached; more is better): the variant's best is the highest score (best_moves holds it, held
 * at 65535; best_ms is that run's time); a score equal to the best is not a new best; 0 is never a best. No win count and no streak (nobody to beat). */
pc_game_rec_t pc_game_score_record_high(pc_game_score_t *s, int variant, int score, uint32_t ms);
/* The seed of a game's own random stream (games_seed): the hash of its id, plus n (the game's own count), mixed with `boot`, one random value taken once per
 * boot (G1 fix round 1: without it the Nth game after every power-on was the same: the same colours in 四色記憶). boot 0 = the hash alone (the simulator
 * pins 0, so its tests and pictures are what they were). */
static inline uint32_t pc_game_seed(const char *id, uint32_t n, uint32_t boot)
{
    uint32_t h = 2166136261u;
    for (; *id; id++) h = (h ^ (uint8_t)*id) * 16777619u;
    return (h + n * 0x9E3779B9u) ^ (boot * 0x85EBCA6Bu);
}
/* One finished score game (G1: points, not moves; no side wins): the best is the most points (a tie is not a new best), the streak is left as it is. */
pc_game_rec_t pc_game_score_points(pc_game_score_t *s, int variant, uint32_t points, uint32_t ms);
bool pc_game_gold(uint32_t seed);         /* this game (its seed: games_seed of the page) is golden: about one in PC_GAME_GOLD_N */
