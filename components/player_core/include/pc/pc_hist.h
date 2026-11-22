// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#ifdef __cplusplus
extern "C" {
#endif

/* Play history for "previous track": the last 20 songs that were started, oldest first, and a cursor that walks back and forth through them. */
#define PC_HIST_N 20
typedef struct { int idx[PC_HIST_N]; int n, back; } pc_hist_t;   /* zero-initialise; back = steps behind the newest entry (0 = at the newest) */
void pc_hist_add(pc_hist_t *h, int track_idx);   /* a song that was not reached by walking the history: entries newer than the cursor are dropped, this one is appended (oldest falls out when full), cursor at the newest */
void pc_hist_add_oldest(pc_hist_t *h, int track_idx);   /* inserts the song as the OLDEST entry and puts the cursor on it; when the history is full the NEWEST entry is dropped */
int  pc_hist_prev(pc_hist_t *h);                 /* moves the cursor one older and returns that song; -1 (cursor unchanged) at the oldest or when empty */
int  pc_hist_next(pc_hist_t *h);                 /* moves the cursor one newer and returns that song; -1 (cursor unchanged) at the newest */
int  pc_hist_peek_next(const pc_hist_t *h);      /* what pc_hist_next would return, cursor unchanged */
void pc_hist_clear(pc_hist_t *h);
/* hires T8e: the library changed - every entry becomes map(entry) (its index in the new library), entries mapped to -1 drop out; the cursor
 * stays on its song, or (gone) on the next older entry still there, else the oldest */
void pc_hist_remap(pc_hist_t *h, int (*map)(int idx, void *u), void *u);
#ifdef __cplusplus
}
#endif
