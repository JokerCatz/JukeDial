// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdint.h>
#include "lvgl.h"
#include "ui_kit.h"
/* The instrument's voice-level bars (UI unify T10, spec 2.7; moved from inst_ui.c): one object draws a track and a level for every voice,
 * so a frame of changes is one invalidated area, not one per bar. One instance (the play page's). LVGL task only. */
lv_obj_t *inst_bars_create(lv_obj_t *parent, ui_slot_t s);   /* the box in the slot, not clickable; no bars until inst_bars_build */
void inst_bars_build(int n);                  /* n voices (cut to 8): widths and gaps for that many, every bar drawn again */
void inst_bars_set(int i, uint8_t level);     /* voice i's level 0..255 (peak hold: falls 4 px a frame); redraws only when the bar moved */
uint32_t inst_voice_rgb(int v);               /* the colour of voice v (theme accents first), shared with the ring effect and the pads */
