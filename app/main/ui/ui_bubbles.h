// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include "lvgl.h"
/* The bubble field (bubble wrap, shared piece): a sheet of round bubbles in hex packing inside the display's design circle (UI_CONTENT_D), filling the
 * whole circle (no Back button: the page's exit is the shared 返回 pill, ui_exit.h), drawn by ONE LVGL object (custom draw event, no object per bubble). A touch pops the nearest bubble (hit test in the piece, the cell
 * around a bubble is the whole pitch); popped bubbles stay flat. One random bubble per sheet is the special one (the page gives it a stronger haptic).
 * The layout is computed at create from the geometry (ui_geom.h), so the square panel gets the same circle. State: one PSRAM block (n * 5 bytes + a
 * header), freed with the object. All calls from the LVGL task. Console (ui_bubbles_cmd, `bubbles ...`): dump, tap <i>, at <i>. */

/* idx: the bubble that was just popped, special: it is the sheet's special one, left: the bubbles still raised (0: the sheet is done) */
typedef void (*ui_bubble_pop_cb)(int idx, bool special, int left);

/* A full sheet in `parent`; `cb` (may be NULL) runs after every pop. NULL when the block could not be allocated. */
lv_obj_t *ui_bubble_field_create(lv_obj_t *parent, ui_bubble_pop_cb cb);
/* Every bubble raised again, a new special one (never the previous one), the whole field redrawn. */
void ui_bubble_field_reset(lv_obj_t *field);
/* console `bubbles`: "" dumps the open field (APP bubbles n=<total> left=<left> special=<i>, then one "APP bubbles row <r> <1 raised / 0 popped ...>" per row),
 * "tap <i>" pops bubble i as a touch does (and dumps), "pop <i>" the same without the dump ("pop all": every bubble), "at <i>" prints its screen centre (APP bubbles at <i> x=<x>,y=<y>). */
void ui_bubbles_cmd(const char *arg);
