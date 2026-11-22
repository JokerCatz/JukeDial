// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "ui.h"
#include "ui_help.h"
#include "pc/pc_game_score.h"
/* The games hub (games hub G0, spec 2026-11-11-tools-design.md): a ring on the tools ring's last cell listing the games of ui/games.def, 說明 last (how a game page works; each game has its own 說明 button on its page).
 * The registry is derived from that file (scr_games.c): no count, switch or table to edit when a game is added. */
typedef struct { const char *zh, *en; } ui_game_help_t;   /* a game's rules: one paragraph, defined next to the game's page (game_help_<id>), shown by the 說明 button on the game's page (games_help_entry) */

extern const ui_screen_t scr_games;                      /* the hub; name "tools_games" */
int games_count(void);                                   /* the lines of games.def */
const char *games_id(int i);                             /* "ttt" */
const char *games_name(int i);                           /* the zh or en name (T()) */
const char *games_name_zh(int i);                        /* both names, for the 說明 body (it is built in a given language) */
const char *games_name_en(int i);
const ui_screen_t *games_screen(int i);                  /* the game's page; name "tools_game_<id>" */
const ui_game_help_t *games_help(int i);
const ui_help_t *games_help_entry(int i);                /* the game's own 說明 (title = its name, body = its rules) for ui_help_open; one block, refilled by each call (ui_help_texts.c) */
/* The seed of a game's own random stream (the computer's choices between equal moves, the four-colour memory game's colours, the golden game): the hash of its id plus a count the
 * game keeps itself, so what one game does never changes what another shows, mixed with games_boot() (pc_game_seed) so the games differ from boot to boot */
uint32_t games_boot(void);                               /* one random value taken at the first call after boot (esp_random); the simulator pins it to 0; console `games boot [v]` */
void games_boot_set(uint32_t v);
static inline uint32_t games_seed(const char *id, uint32_t n) { return pc_game_seed(id, n, games_boot()); }
int games_find(const char *id);                          /* the index of an id, -1 */
void knobtoy_maze_cmd(const char *args);                 /* console `maze grav <dx> <dy>` (scr_game_knobtoy.c): the tilt vector of the knob toys' maze, percent of a full tilt; the first call stands in for the IMU (makes the tilt rule available) */
void games_cmd(const char *args);                        /* console: "" opens the hub (through the 小工具 ring); "open <id>"; "list" (APP games n=<n> ids=<id,id>); "help" opens 說明;
                                                          * the open game (G0b): "fx ..." (game_fx.h), "result [tap]" (game_result.h), "ring", "sel", "rule|style|mode <n>" (game_ring.h), "gold 1|0|auto", "streak <n>" (game_result.h) */
