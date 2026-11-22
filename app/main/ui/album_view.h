// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include "lvgl.h"
/* Slideshow of the album cache (spec 2026-11-11-album-design.md): full screen, no text. The returned object
 * is not clickable (taps reach the parent) and frees everything when deleted. LVGL task. */
lv_obj_t *album_view_create(lv_obj_t *parent);
bool album_view_showing(lv_obj_t *view);      /* a photo is on screen */
void album_view_next(void);                   /* every view: the next photo now */
/* settings (cfg now_view=cover|album|viz, album_sec=10|30|60|300, album_order=shuffle|order) */
bool album_now_mode(void);
bool now_view_viz(void);                      /* now_view=viz: the playing page opens on the spectrum page */
void album_now_mode_step(int detents);       /* cover -> album -> viz -> cover, by detents (wraps) */
const char *album_now_label(void);            /* cover / album / spectrum */
int  album_interval_s(void);
bool album_shuffle(void);
void album_order_next(void);
const char *album_order_label(void);
void album_view_stat(void);                   /* console: frames shown, fades, last swap time */
