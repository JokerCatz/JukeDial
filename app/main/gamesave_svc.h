// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "pc/pc_game_score.h"
/* The games' and tools' saved numbers (ideas wave 0b G2): /sdcard/_player/config/games.txt, the file half of pc_gamesave (the pure text, caps and debounce are there).
 * Used by: the bests of every game (game_result_end), 多組計數器's values; later 2048, 推箱子, 棋鐘, 習慣打卡 ... : a key (a-z 0-9 _ ., at most 19 characters) -> a u32.
 * ONE writer: every call comes from the UI task (the LVGL lock); the file is written whole to games.txt.tmp and renamed over games.txt (atomic), at most once every
 * GAMESAVE_DEBOUNCE_MS (the first write at once), and at once on gamesave_flush (a counter page closing, a result card going, before a reboot, the sleep timer's fade).
 * A corrupt file (pc_gs_parse) starts the store empty and is kept as games.txt.bad (one copy, the latest); no card: the store works in memory only (status "no_card").
 * Status words: ok, no_card, corrupt (the .bad copy was made), write_failed (tried again at the next debounce), full (a key was refused), nomem.
 * The store is one 3 KB PSRAM block; no internal RAM. Console `gamesave`: "" -> "APP gamesave status=<s> keys=<n> dirty=<0|1> writes=<n> card=<0|1> bad=<0|1>",
 * "dump" lists "APP gamesave <key>=<value>", "get <k>", "set <k> <v>", "flush", "clear" (empties the store in memory, dirty: tests). */
#define GAMESAVE_DEBOUNCE_MS 10000
void gamesave_start(void);                                       /* after cfg_load: reads the file; the 1 s check timer */
bool gamesave_get(const char *key, uint32_t *v);
uint32_t gamesave_get_or(const char *key, uint32_t def);
bool gamesave_set(const char *key, uint32_t v);                  /* false: bad key or full */
void gamesave_load_score(const char *id, pc_game_score_t *s);    /* the stored bests of a game into a session score (pc_gs_load_score) */
void gamesave_store_score(const char *id, const pc_game_score_t *s);
void gamesave_flush(void);                                       /* writes now when something changed */
const char *gamesave_status(void);
void gamesave_cmd(const char *arg);
