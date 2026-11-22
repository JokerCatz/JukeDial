// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include "lvgl.h"
/* The playing ring (spec 5.8, one for the cover page and the spectrum page): the dim layer, the segmented ring, the 4 s rule and the pause state.
 * Centre = pause / resume (the ring stays open); the cells are the source's own (favourite, previous, list, the host's cell, play order, next,
 * details, stop on the cover page); every tap or page turn restarts the 4 s; the bottom cell is Back. */
typedef enum { NOW_MENU_COVER, NOW_MENU_VIZ } now_menu_host_t;
/* Opens the playing ring on `parent` (a 360x360 page root). on_extra runs when the host's own cell is tapped (cover: open the spectrum page; viz: open the style ring);
 * on_back runs for the bottom Back cell. Both are called after the ring has closed itself. Returns false (nothing opened) when no source is playing.
 * When the ring cannot be allocated the dim layer still opens with a bare Back button (ui_ring's fallback), so the page is never a dead end. */
bool now_menu_open(lv_obj_t *parent, now_menu_host_t host, void (*on_extra)(void), void (*on_back)(void));
void now_menu_close(void);          /* no-op when closed; pages call it from destroy / on_suspend */
void now_menu_owner_check(void);    /* the ring closes when playback has another owner than the one its cells were built for (the cover page's change hook) */
bool now_menu_is_open(void);
void now_menu_raise(void);          /* an open ring moves above whatever the page added after it (the photos) */
bool now_menu_paused(void);         /* the pause state (one owner: the toggle and this query live here; the audio service holds the truth) */
void now_menu_console(int pick);    /* what scr_now_menu(pick) did: -2 open on the top page (cover or viz), -4 page, -5 state line, -3 pause-state line, -1 centre, else the cell index */
