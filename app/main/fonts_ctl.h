// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include "cJSON.h"
/* "font_put*" requests of the settings protocol (web font builder, spec 2026-11-15-web-tool.md W7): a font file goes to the card's _player/fonts/ as <name>.new,
 * which font_init() swaps in at the next boot (font.c swap_new: never replace a font FreeType has open). LVGL task (ctl_run).
 * r already holds "id" and "ok": true; returns r (with ok false and "error" on failure). */
cJSON *fonts_ctl(const char *op, cJSON *r, const cJSON *q);
