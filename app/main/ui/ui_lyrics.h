// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include "lvgl.h"
/* The lyrics view (visualiser kind 歌詞, 2026-11-17): the current line large in the middle, one or two lines before and after dimmer, every line wrapping
 * to the width of the glass (a line that needs more rows than its band has is cut at the band's edge). Three bands (before: bottom aligned, current:
 * centred, after: top aligned) keep the current line where it is whatever the neighbours' length is. No timers, no input: the page sets the text.
 * All calls from the LVGL task. */
lv_obj_t *ui_lyrics(lv_obj_t *parent, int side);                 /* side: lines before and after the current one, 1 or 2; not clickable; deleted with its parent */
void ui_lyrics_set(lv_obj_t *l, const char *const t[5]);        /* t[0..1] before (t[1] nearest), t[2] current, t[3..4] after; NULL or "" = empty; a view made with side 1 shows t[1..3] only */
void ui_lyrics_note(lv_obj_t *l, const char *text);              /* one dim note in the middle (no lyrics, not UTF-8) and every band empty */
int  ui_lyrics_side(const lv_obj_t *l);                          /* the side count it was made with (0 for NULL) */
