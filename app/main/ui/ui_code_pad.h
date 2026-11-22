// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include "lvgl.h"
/* The code pad (shared piece, cardfmt 2026-11-14): a game pad to enter a code by taps. Four direction keys (a cross on the left half of the design
 * circle) and two buttons B and A (right half, B lower left of A, as on a game pad), every key a tap target of UI_TAP_MIN + 4 with UI_TAP_GAP or
 * more between them, clear of the Back button's row; `len` progress dots in UI_SLOT_HEAD. A tap reports the key to the page; the page says how
 * many dots are lit (ui_code_pad_progress). No pressed look (the touch's haptic only), no animation. The piece keeps the keys tapped since the
 * dots were last cleared (the console dump). State: one PSRAM block, freed with the object. All calls from the LVGL task.
 * Console (ui_code_pad_cmd, `codepad ...`): "" dump (APP codepad len=<n> lit=<k> keys=<U D L R B A ...>), "tap <u|d|l|r|b|a>" (a tap as a touch
 * gives), "at <u|d|l|r|b|a>" (the key's screen centre: APP codepad at <k> x=<x>,y=<y>). */
enum { UI_CODE_UP, UI_CODE_DOWN, UI_CODE_LEFT, UI_CODE_RIGHT, UI_CODE_B, UI_CODE_A, UI_CODE_KEYS };
typedef void (*ui_code_pad_cb)(int key);

lv_obj_t *ui_code_pad_create(lv_obj_t *parent, int len, ui_code_pad_cb cb);   /* NULL when the block could not be allocated */
void ui_code_pad_progress(lv_obj_t *pad, int lit);     /* the first `lit` dots in the accent colour, the rest dim; 0 also clears the key record */
void ui_code_pad_cmd(const char *arg);
