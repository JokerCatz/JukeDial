// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"
/* The instrument's 8 drum pads (UI unify T11, spec 2.7; moved from inst_ui.c): 8 annular sectors drawn by one object from A8 masks
 * (PSRAM, ~80 KB, made at the first create and kept for the mode's life), a label on each pad and the centre hub (ui_hub, 116 px).
 * A touch-down on a sector calls down(pad), its release up(pad); the flash is the instrument's own feedback (no pressed look elsewhere).
 * One instance (the pads page's). LVGL task only. */

/* The pads on `page` (the whole design circle); hub() runs on a tap of the centre hub. Returns the hub: its label (child 0, two lines)
 * is the page's to fill (ui_pill_set_text). When the pads object goes, up(pad) is called for every pad still held. */
lv_obj_t *inst_pads_create(lv_obj_t *page, void (*down)(int pad), void (*up)(int pad), void (*hub)(void));
void inst_pads_label(int pad, const char *text);   /* the pad's label (a kit sample name or a note name; "" none) */
void inst_pads_flash(int pad, uint32_t rgb);       /* the pad lights in rgb and fades in ~200 ms (inst_pads_tick) */
void inst_pads_tick(int ms);                       /* the fade: only the sectors whose step changed are redrawn; no-op without pads */
void inst_pads_inject(int pad, bool down);         /* test hook (sim "siminst paddown|padup"): a touch-down / release on the pads object as on `pad` */
