// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Favourites and play counts by id (spec 5.5): "mp3:<path>", "chip:<zip>|<entry>|<sub>", "scene:<id>".
 * File: one "<id>=<fav>,<plays>" per line, split at the last '='. */
typedef struct pc_stats pc_stats_t;

pc_stats_t *pc_stats_create(void);
void pc_stats_destroy(pc_stats_t *s);
int  pc_stats_load(pc_stats_t *s, const char *path);   /* missing file -> 0 */
int  pc_stats_save(pc_stats_t *s, const char *path);   /* tmp + rename; clears dirty */
bool pc_stats_is_fav(const pc_stats_t *s, const char *id);
void pc_stats_toggle_fav(pc_stats_t *s, const char *id);
void pc_stats_add_play(pc_stats_t *s, const char *id);
uint32_t pc_stats_plays(const pc_stats_t *s, const char *id);
bool pc_stats_dirty(const pc_stats_t *s);
uint32_t pc_stats_fav_gen(const pc_stats_t *s);   /* changes whenever a favourite is toggled or the file is loaded (a cache of favourites knows when it is stale) */
int  pc_stats_list_favs(const pc_stats_t *s, const char *prefix, const char **out, int max);  /* pointers stay valid until the next change */

#ifdef __cplusplus
}
#endif
