// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"
struct game_style;
/* The result card of a finished game (G0b game feel): a card IN the play field (a child of the ui_board, centred on it), shown a moment
 * (GAME_RESULT_DELAY_MS) after the end so the win line and the glow are seen first. It says who won, the moves and the time, the best of this
 * session (pc_game_score) or "new best", a streak badge (PC_GAME_STREAK wins in a row against the device) and the golden game. A tap on the
 * card = again (the page's `again`); a tap on the board beside it does what the page does after the end (also again). One card at a time (one
 * static pointer, the card's state is one PSRAM block freed with it). Colours from the board's style (another field, ui_arena: the style it gives).
 * A score game (G1 ring breakout, `GAME_SCORE_POINTS`): the points and the level, the most points of the session; no streak. All calls from the LVGL task.
 * Console (`games result ...`): "" prints "APP game_result up=<0|1> shown=<0|1> title=<t> moves=<n> ms=<n> best=<moves>/<ms> new_best=<0|1> streak=<n> gold=<0|1>"
 * (a score game: "... title=<t> points=<n> level=<n> best=<points> new_best=<0|1> gold=<0|1>"; up=0 alone when none); "tap" taps the card. */
#define GAME_RESULT_DELAY_MS 600
/* How a game scores (one field, `mode`, in both structs; zero = the default). */
typedef enum {
    GAME_SCORE_MOVES = 0,         /* moves and time, fewer is better (the board games; a streak against the device) */
    GAME_SCORE_HIGH,              /* G1: a score game: `moves` is the score, more is better; the card says "<unit> N" and "最佳 <unit> N" (the length in the four-colour memory game) */
    GAME_SCORE_POINTS             /* G1: points and the level (`level` <= 0: points only), the most points of the rule is the best; no streak */
} game_score_mode_t;
typedef struct {
    const char *title;            /* "You won!" (copied) */
    int moves;
    uint32_t ms;
    int best_moves;               /* 0: no best yet */
    uint32_t best_ms;
    bool new_best;
    int streak;                   /* wins in a row against the device (the badge from PC_GAME_STREAK) */
    bool gold;                    /* the golden game */
    game_score_mode_t mode;       /* see game_score_mode_t */
    const char *unit;             /* the score's word (T(): "length"); NULL: moves */
    uint32_t points, best_points;
    int level;
    const struct game_style *style;   /* the card's colours; NULL: the board's (the parent must then be a ui_board; ui_sectors and ui_arena give their own) */
} game_result_t;
lv_obj_t *game_result_show(lv_obj_t *board, const game_result_t *r, void (*again)(void));   /* replaces a card that is up; NULL: no memory (the status line still says it) */
/* The end of a game, the one call a page makes: records it in the session score of the game (pc_game_score, one per line of games.def, kept while the
 * device is on), plays the end sound (sfx: "win", "streak" for a win that makes or extends a streak, "lose", "draw"), shows the card (the score's best and
 * streak in it) and prints "APP game <id> end outcome=<0 loss|1 win|2 draw> streak=<n> best=<moves> new_best=<0|1> sound=<name>". */
typedef struct {
    const char *id;               /* the game (games.def) */
    int variant;                  /* its rule (a best per rule) */
    int outcome;                  /* PC_GAME_WIN / LOSS / DRAW from the player's side (two players: a win of either side is a WIN) */
    bool vs_device;               /* against the device: only these games make a streak */
    int moves;
    uint32_t ms;
    const char *title;            /* the card's first line */
    bool gold;
    game_score_mode_t mode;       /* HIGH: `moves` is the score, the best is the highest (pc_game_score_record_high), no streak; the end sound is "win" for a new best or an outcome
                                   * WIN (somebody won: two-player, all of it remembered), else "lose". POINTS: the best is the most points of the rule (pc_game_score_points), no streak; the end sound
                                   * is "win" for a new best, else "game_over"; the line ends "points=<n> level=<n> sound=<name>" */
    const char *unit;             /* the score's word on the card (NULL: moves) */
    uint32_t points;
    int level;
    const struct game_style *style;   /* the card's colours; NULL: the board's (the parent must then be a ui_board: the four-colour memory game's ui_sectors and the ring breakout's ui_arena give theirs) */
} game_end_t;
void game_result_end(lv_obj_t *board, const game_end_t *e, void (*again)(void));
void game_result_streak_set(const char *id, int n);   /* console `games streak <n>` (tests): the session streak of a game */
void game_result_hide(void);
bool game_result_up(void);
void game_result_cmd(const char *arg);
/* The golden game (a surprise of every game): true for about one game in PC_GAME_GOLD_N, from the game's own seed (games_seed(id, n), n = the page's
 * own count of games), so it never touches the shared random generator. Console `games gold 1|0|auto` forces it for the next games (tests, pictures). */
bool game_result_gold(const char *id, uint32_t n);
void game_result_gold_force(int v);                   /* 1 every game golden, 0 none, -1 by the seed (the default) */
