// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include "lvgl.h"
/* The safe-mode screen (UI unify T13, spec 2.8): three fixed English lines on black, built-in Montserrat 20 only (no flash pack, no card), no theme, cfg,
 * touch or knob; one 60 s one-shot turns the backlight off. LVGL task (display locked). `why` and `crashes` are shown in the middle line. */
void ui_safe_show(lv_display_t *d, const char *why, unsigned crashes);
/* the same, on a fresh empty screen loaded first (the console's `safeshow` over a running image: the pages under it neither show nor take taps) */
void ui_safe_show_fresh(lv_display_t *d, const char *why, unsigned crashes);
