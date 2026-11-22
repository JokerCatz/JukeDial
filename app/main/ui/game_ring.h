// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include "lvgl.h"
#include "ui_help.h"
/* A game page's controls (G0b game feel; ruling: a game page has its own small ring, 規則 / 風格 / 重來 / 說明 last). Shared by every game:
 * - the HEAD row: two cells. Left = who plays (the page's modes, 3 values: a tap borrows the knob and steps once, then the knob steps it, R21);
 *   right = the current rule's name; a tap opens the game's ring.
 * - the ring (ui_popring): 規則 (the rule variants, classic first: 3 or more values take the knob the borrowed way, 2 values cycle by tap),
 *   風格 (game_style: GAME_STYLES values, the knob), 重來 (a new game now), and 說明 last (the game's own rules page, games_help_entry).
 * The values live in the page's game_sel_t (a static of the page, so they last while the device is on); every change calls the page's
 * `changed` (a new rule: the page starts again; a style: the page restyles its board). One page at a time: the state is a few PSRAM statics.
 * Console (games.c): `games ring` opens the ring, `games rule|style|mode <n>` sets a value as the ring would (the page's changed runs). */
enum { GR_MODE = 0, GR_RULE, GR_STYLE, GR_AGAIN };
typedef struct { int mode, rule, style; } game_sel_t;
typedef struct {
    const char *id;                                   /* the game's id (games.def): its 說明 */
    int modes;                                        /* who plays: the HEAD's left cell, mode_text(v) for each */
    const char *(*mode_text)(int v);
    int rules;                                        /* 2..8 rule variants, rule_name(v) for each (T() names), classic first */
    const char *(*rule_name)(int v);
    void (*changed)(int what, int v);                 /* GR_MODE / GR_RULE / GR_STYLE now has value v; GR_AGAIN: 重來 */
    bool (*cmd)(const char *arg);                     /* optional (G1): a `games <arg>` the shared console does not know goes to the open game; true: taken */
    /* optional, all NULL / 0 for the board games (one unified set: spirograph axes, ambient-page titles / help / brightness, Game of Life 重來 text / clear):
     * rule_zh/en, style_zh/en: other titles for the rule and style cells ("場景" / "Scene", "色調" / "Tone"); help: the page's own 說明 when it is not a line of games.def;
     * again_text: the 重來 cell's label (NULL: 重來 / Restart);
     * axes (0..GR_AXES_MAX) more axis cells of the game's own between 風格 and 重來, each with 3 or more values, so each takes the knob the borrowed way (a tap steps it,
     *   then the knob steps it both ways, 4 s): the cell reads "<axis_name>\n<axis_value>"; axis_step applies d (any size, signed; a tap is +1; false: inert) and
     *   starts what the page restarts; the ring relabels itself after it;
     * action_text: one more cell after 重來 that is an ARMED TWO-TAP ACTION (the first tap shows action_armed() in the centre, the second runs action_run() and closes
     *   the ring); NULL: none. Ring order: 規則, 風格, axes, 重來, action, 說明. */
    const char *rule_zh, *rule_en, *style_zh, *style_en;
    const ui_help_t *help;
    const char *(*again_text)(void);
    int axes;
    const char *(*axis_name)(int k);
    const char *(*axis_value)(int k);
    bool (*axis_step)(int k, int d);
    const char *(*action_text)(void);
    const char *(*action_armed)(void);
    void (*action_run)(void);
    bool action_direct;                               /* the action cell is NOT armed: one tap runs it (a harmless action: the maze's 重新歸零) */
    int style_fix;                                    /* CF2: 0 = every style (the 風格 axis); n > 0: the game has ONLY style n-1 (tic-tac-toe: neon): the cell reads it, takes no step, and an old value (a console `games style`) is clamped to it */
} game_ring_t;
#define GR_AXES_MAX 4

void game_ring_head(lv_obj_t *page, const game_ring_t *def, game_sel_t *sel);   /* the HEAD row on `page`; def and sel must outlive the page */
void game_ring_open(void);                            /* the ring of the page that made the HEAD row */
void game_ring_set(int what, int v);                  /* GR_MODE / GR_RULE / GR_STYLE (wrapped into range): the value, the cells, changed() */
void game_ring_refresh(void);                         /* the HEAD cells' texts again */
const char *game_ring_id(void);                       /* the open game page's id, "" none */
bool game_ring_cmd(const char *arg);                  /* the open game's own console words (def->cmd); false: none took it */
void game_ring_print(void);                           /* "APP game_ring id=<id> mode=<n> rule=<n> style=<n> open=<0|1>" */
