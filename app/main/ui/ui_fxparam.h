// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include "lvgl.h"
#include "pc/pc_fxapp.h"
/* The sound-effect rows (fx apps T7, spec 2026-11-13-fx-apps-design.md 6.2, 6.3): the list row of an app and the rows of a filter page generated from the app's
 * parameter table. The pages scr_fx and scr_fxapp only compose these. All calls from the LVGL task. */

/* One row of the filter list or the page's On/Off row: text and look from fx_look (OFF normal, RUN on, DIM dim). page false: "<name>  On|Off|On, paused"; page true: "On|Off|On, paused: <why>".
 * `cb(user)` on CLICKED. ui_fxparam_refresh renews it. */
lv_obj_t *ui_fxparam_state_row(lv_obj_t *list, int app, bool page, lv_event_cb_t cb, void *user);
/* The parameter rows of `app` (the Mix row first for MIX apps, then one "<name>: <value>" per parameter; equalizer: a header and four rows per band below the count).
 * Returns the rows made (headers not counted). A row of a value with three or more states borrows the knob (ui_axis_borrow), BOOL and two-value CHOICE rows are taps.
 * The page rebuilds itself (ui_replace) when a borrow that changed the equalizer's band count ends. One page at a time (the current app is kept here). */
int ui_fxparam_rows(lv_obj_t *list, int app);
/* Renews the text and look of every row made by this piece in `list` (state rows and parameter rows; the borrowed one in the selection look). */
void ui_fxparam_refresh(lv_obj_t *list);
/* The rows of the open filter page must match the equalizer's band count; if not (a borrow ended, the page is top again, reset, import), the page is rebuilt (ui_replace), later. No-op otherwise. */
void ui_fxparam_sync(void);
/* A parameter's row name: a band row's "Band 3 " prefix is dropped (the band has its header). */
const char *ui_fxparam_name(const pc_fxparam_t *p, bool zh);
/* console "fxui rows" / "fxui tap <n>": the headers and clickable rows of `list` in order (APP fxui header text= / APP fxui row=<i> look= text=, then APP fxui end);
 * a tap on the n-th clickable row, -1 = the last one, -2 the one before it. */
void ui_fxparam_dump(lv_obj_t *list);
void ui_fxparam_tap(lv_obj_t *list, int n);
lv_obj_t *ui_fxparam_list(void);              /* the list of the page that made rows last (the filter page on top: console), NULL once it is gone */
