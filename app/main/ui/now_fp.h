// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include "lvgl.h"
/* The static now-playing page of the square panel (redone 2026-11-22: no button on the screen, the nine-grid takes the taps, ui_ninegrid.h).
 * Style A (NOWFP_CARDS, 封面＋資訊): cover square top left, a row of passive status icons right of it (playing / paused, shuffle, repeat: nothing to press), title / artist / format lines
 * under it, elapsed - bar - total at the bottom. Style B (NOWFP_FULL, 全封面, the default): the cover is the whole panel and nothing else (the nine-grid's labels and bands show over it on a tap).
 * NOWFP_GRID is the older page (the cover and the playing ring behind a tap): the page keeps it, and the round board has only that one.
 * One instance (the cover page); every call from the LVGL task. The page owns the cover picture (its decode and cache), this piece only draws it.
 * The bar shows the position and cannot seek: nothing in the player seeks inside a track. */
enum { NOWFP_CARDS, NOWFP_FULL, NOWFP_GRID, NOWFP_N };
bool nowfp_available(void);               /* the square panel has the setting; the round board never does */
int  nowfp_style(void);                   /* NOWFP_* (cfg now_fp; any other value reads as the default); always NOWFP_GRID on the round board */
const char *nowfp_style_label(void);
void nowfp_style_step(int d);             /* signed, wraps */

/* Builds style A or B in `parent` (a panel-sized box); nothing on it takes a tap (the nine-grid does). Returns the box, NULL when style is NOWFP_GRID or no memory. */
lv_obj_t *nowfp_create(lv_obj_t *parent, int style);
void nowfp_set_title(const char *text);   /* "<title>  -  <artist>" (the owner's title text) or a plain title: split at the last "  -  " */
void nowfp_set_cover(const lv_image_dsc_t *dsc);   /* the picture (stretched to the cover box); NULL: none, the box colour shows */
void nowfp_set_note(const char *text);    /* the dim note in the cover box (the phone's cover refused for low memory), NULL: none */
void nowfp_refresh(void);                 /* the status icons and the lines follow the owner and the audio state now (also done by the piece's own 250 ms timer) */
int  nowfp_cover_size(void);              /* the pixels the cover box shows (A 296, B 460; 0: no static page): the page decodes the cover at this size (thumb_svc_request_big) */
bool nowfp_active(void);                  /* a style A / B page is up */
void nowfp_print(void);                   /* console "nowfp": APP nowfp style=<full|info> paused=<0|1> mode=<kind|-> title=<..> artist=<..> fmt=<..> el=<..> tot=<..> bar=<permille> */
