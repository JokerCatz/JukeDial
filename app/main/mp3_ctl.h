// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "pc/pc_library.h"
/* MP3 playback: folder or whole-library weighted shuffle (spec 4.4). LVGL context only. */
void mp3_ctl_play_folder(int folder);   /* -1 = whole library */
void mp3_ctl_play_track(int idx);       /* keeps the current folder as the shuffle pool */
void mp3_ctl_play_folder_track(int folder, int idx);   /* that folder becomes the pool, starting with idx */
void mp3_ctl_play_folder_track_at(int folder, int idx, uint32_t byte);   /* ... from that byte (resume after a reboot) */
void mp3_ctl_next(void);              /* the "next" button */
void mp3_ctl_prev(void);              /* the "previous" button: the 20-song history, see mp3_ctl.c */
#include "pc/pc_shuffle.h"
pc_order_t mp3_ctl_order(void);       /* spec 5.8: cfg mp3_order */
void mp3_ctl_order_step(int detents);   /* by detents, wraps (the three orders) */
const char *mp3_ctl_order_label(void);
void mp3_ctl_stop(void);
enum { MP3_CLIP_NONE = 0, MP3_CLIP_PLAYING, MP3_CLIP_ENDED, MP3_CLIP_FAILED };   /* mp3_ctl_clip_state */
bool mp3_ctl_play_clip(int idx, int pct);          /* 猜歌: a clip of song idx from pct % of it (converted to what its backend takes); a restart: no history, no recent entry, no play counted; never goes on to another song */
bool mp3_ctl_play_back(int idx, uint32_t byte);    /* 猜歌 puts the user's song back at the byte the player reported (the same restart, a normal song again) */
int  mp3_ctl_clip_state(void);                     /* the last clip: MP3_CLIP_PLAYING, ENDED (the song ended inside it), FAILED (it would not open or decode), NONE */
int  mp3_ctl_current(void);             /* track index, -1 when not playing MP3 */
int  mp3_ctl_folder(void);
/* before g_lib is replaced by next (hires T8e: a rescan keeps playing): the playing track, the queued one, the history, the recent list and the
 * folder follow their paths / name into next; the playing file gone from it = stop with the reason (APP mp3 stop reason=gone, a message) */
void mp3_ctl_library_changed(const pc_library_t *next);
void mp3_ctl_print(void);              /* console mp3cur: APP mp3 cur=<idx> folder=<f> qidx=<i> hist=<n> back=<b> recent=<n> path=<path> */
void mp3_ctl_rg_changed(void);         /* cfg rg / rg_untag changed: the playing (and queued) track get the new gain */
