// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "pc/pc_library.h"
#include "pc/pc_stats.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Weighted shuffle (spec 4.4): newer ID3 dates weigh more; the last 20 played weigh 0; favourites x2.
 * "now" is the newest date in the library until the clock is set. */
int32_t  pc_days_between(uint32_t ymd_a, uint32_t ymd_b);           /* b - a */
uint32_t pc_shuffle_weight(uint32_t date, uint32_t now, bool fav);  /* <=30 d: 4, <=90 d: 2, else 1; x2 fav */

#define PC_RECENT_N 20
typedef struct { int idx[PC_RECENT_N]; int n, head; } pc_recent_t;   /* zero-initialise */
void pc_recent_push(pc_recent_t *r, int track_idx);
bool pc_recent_has(const pc_recent_t *r, int track_idx);
int  pc_recent_last(const pc_recent_t *r);                         /* -1 when empty */
void pc_recent_remap(pc_recent_t *r, int (*map)(int idx, void *u), void *u);   /* hires T8e: entries follow their songs (map; -1 drops one), order kept */

/* Picks from candidates (track indices); st may be NULL. All candidates recent -> any but the last played.
 * Returns a track index, -1 when nc == 0. */
int pc_shuffle_pick(const pc_library_t *lib, const int *candidates, int nc, const pc_stats_t *st, const pc_recent_t *recent, uint32_t rnd);

/* The same pick over a contiguous range of tracks (a folder with its sub-folders, or the whole library: pc_library_scope) with the weights made ONCE:
 * pc_wscope_build costs one pass over the range (a favourite lookup per track only when the stats hold an mp3 favourite), then a pick is a walk over
 * block sums (64 tracks a block) and one block, no string work. The result of pc_wscope_pick(rnd) equals pc_shuffle_pick over the candidates
 * first..first+n-1 with the same rnd (test_shuffle proves it). Stale when the library is replaced or pc_stats_fav_gen changed: pc_wscope_fresh. */
typedef struct { int first, n; uint8_t *w; uint32_t *bsum; uint64_t total; uint32_t favgen; const pc_library_t *lib; } pc_wscope_t;   /* zero-initialise */
int  pc_wscope_build(pc_wscope_t *ws, const pc_library_t *lib, int first, int n, const pc_stats_t *st);   /* 0, or -ENOMEM (ws left empty) */
bool pc_wscope_fresh(const pc_wscope_t *ws, const pc_library_t *lib, int first, int n, const pc_stats_t *st);
void pc_wscope_free(pc_wscope_t *ws);
int  pc_wscope_pick(const pc_wscope_t *ws, const pc_recent_t *recent, uint32_t rnd);   /* track index; -1 when the range is empty */

/* play order (spec 5.8): weighted shuffle (the caller's pick), folder order, or the same track again.
 * cand = the folder's track indices in library order; cur may be -1 or not in cand (starts at cand[0]). */
typedef enum { PC_ORDER_SHUFFLE, PC_ORDER_SEQUENTIAL, PC_ORDER_REPEAT_ONE, PC_ORDER_COUNT } pc_order_t;
int pc_order_next(pc_order_t o, const int *cand, int n, int cur, int shuffled);
/* the same over the range first..first+n-1 (what pc_order_next does with cand[i] = first + i) */
int pc_order_next_range(pc_order_t o, int first, int n, int cur, int shuffled);
#ifdef __cplusplus
}
#endif
