// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "pc/pc_game_score.h"
/* pc_gamesave (ideas wave 0b G2): the pure half of /sdcard/_player/config/games.txt, a tiny key -> u32 store for the games' bests and the tools' values
 * (多組計數器 now; later 2048, 推箱子, 棋鐘, 習慣打卡 ...). No I/O here: the file glue (app/main/gamesave_svc.c) reads and writes the text, atomically.
 * Text: line 1 "pcgs1", then "<key>=<decimal>" lines, then "end=<n> <hex fnv1a of every byte before this line>". A file that is not exactly that
 * (a bad header, a bad line, a wrong count or checksum, too many keys, over PC_GS_FILE_MAX) is CORRUPT: pc_gs_parse leaves the store empty and says
 * so, and the glue keeps a .bad copy. Keys: 1..PC_GS_KEY_MAX-1 characters of [a-z0-9_.]. Size caps: PC_GS_ENTRIES keys, PC_GS_FILE_MAX bytes.
 * Writes are debounced by the glue through pc_gs_due (one write at most every N ms, the first one at once; a flush ignores N). One writer: the UI task.
 * The store is one plain struct (about 12 KB): the glue allocates it in PSRAM. */
#define PC_GS_KEY_MAX   20
#define PC_GS_ENTRIES   512
#define PC_GS_FILE_MAX  20480
typedef struct { char key[PC_GS_KEY_MAX]; uint32_t val; } pc_gs_entry_t;
typedef struct {
    pc_gs_entry_t e[PC_GS_ENTRIES];
    int n;
    bool dirty;                  /* a value changed since the last pc_gs_written */
    bool wrote;                  /* pc_gs_written ran once (the first write is never delayed) */
    uint32_t last_write;         /* ms of the last pc_gs_written */
} pc_gs_t;

void pc_gs_init(pc_gs_t *g);
bool pc_gs_valid_key(const char *key);
bool pc_gs_get(const pc_gs_t *g, const char *key, uint32_t *v);          /* false: not there */
uint32_t pc_gs_get_or(const pc_gs_t *g, const char *key, uint32_t def);
/* Sets key. false: a bad key or the store is full (nothing changed). The store is dirty only when the value is new or different. */
bool pc_gs_set(pc_gs_t *g, const char *key, uint32_t v);
/* All or nothing. >= 0: the keys read (the store was replaced, not dirty); < 0: corrupt (PC_GS_BAD_*), the store is left empty. Empty text (len 0) = 0 keys. */
enum { PC_GS_BAD_HEADER = -1, PC_GS_BAD_LINE = -2, PC_GS_BAD_END = -3, PC_GS_BAD_SUM = -4, PC_GS_BAD_SIZE = -5 };
int pc_gs_parse(pc_gs_t *g, const char *text, size_t len);
/* The text of the store, NUL-terminated, keys in the order they were first set. Returns its length, 0 when cap is too small (or the text would pass PC_GS_FILE_MAX). */
size_t pc_gs_format(const pc_gs_t *g, char *buf, size_t cap);
/* A write is due: dirty, and (never written, or debounce_ms since the last write). */
bool pc_gs_due(const pc_gs_t *g, uint32_t now_ms, uint32_t debounce_ms);
void pc_gs_written(pc_gs_t *g, uint32_t now_ms);                          /* the text reached the card: clean, the debounce starts */

/* The session score of a game (pc_game_score_t) on keys "<id>.m<v>" (best moves), ".t<v>" (its ms), ".p<v>" (best points) for the PC_GAME_VARIANTS variants;
 * only the bests are kept (the streak and the counts are of the session). Load fills the bests that are stored, leaves the rest; store writes the ones that are not 0. */
void pc_gs_load_score(const pc_gs_t *g, const char *id, pc_game_score_t *s);
bool pc_gs_store_score(pc_gs_t *g, const char *id, const pc_game_score_t *s);   /* false: a key was refused (full, or an id longer than PC_GS_ID_MAX): nothing silently lost */
#define PC_GS_ID_MAX 16                              /* "<id>.m0" must fit PC_GS_KEY_MAX - 1 */
