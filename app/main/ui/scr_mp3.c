// SPDX-License-Identifier: LGPL-2.1-or-later
/* MP3: the music root as the song tree's first level (scr_tracks.c): Shuffle all, the songs lying directly in the root, then the folders (songs first), Rescan last (two taps: it stops playback).
 * Row sets, pages and Back are the tree page's; cfg last_screen is remembered here. */
#include "cfg.h"
#include "screens.h"

static void create(lv_obj_t *p, void *arg)
{
    (void)arg;
    cfg_set_str("last_screen", "mp3");
    scr_tracks_create(p, -1, true);
}
static bool on_knob(int d) { return scr_tracks_knob(true, d); }
static void on_resume(void) { scr_tracks_resume(true); }

const ui_screen_t scr_mp3 = { .name = "mp3", .create = create, .on_knob = on_knob, .on_resume = on_resume };
