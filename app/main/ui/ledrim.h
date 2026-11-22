// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdint.h>
#include "lvgl.h"
#include "pc/pc_ledfx.h"
/* Rim "LED ring" for the player's screens (tools: dice, slot): 12 segments over the upper 270 degrees (the bottom
 * is left to the back button), the instrument app's look (A8 masks made once in PSRAM, blitted in colour, one
 * invalidated area per quarter). One instance at a time; LVGL task only. Create it first on a screen (lowest), so
 * it never takes a tap. Lit segments fade on a 33 ms timer that stops when all are dark. */
#define LEDRIM_SEGS 12
lv_obj_t *ledrim_create(lv_obj_t *parent);      /* NULL without PSRAM for the masks (then the calls do nothing) */
void ledrim_destroy(void);                      /* from the screen's destroy: timer and masks */
void ledrim_flash(uint32_t rgb, int fade_ms);   /* every segment, fading out over fade_ms */
void ledrim_chase(uint32_t rgb, int laps);      /* a comet going round laps times (big wins) */
void ledrim_seg(int seg, uint32_t rgb, int fade_ms);   /* one segment, 0 = lower left, clockwise to 11 */
/* The segment geometry, shared with the instrument app's ring (12 segments, 22.5 degrees apart; the caller loops). */
float ledrim_seg_deg(int seg);                  /* centre of segment seg, degrees clockwise from the top */
void ledrim_seg_area(int seg, lv_area_t *a);    /* its bounding box, 1 px margin */
bool ledrim_seg_mask(const lv_area_t *a, int seg, lv_image_dsc_t *out);   /* A8 mask in PSRAM; false = no memory */
static inline uint16_t ledrim_rgb565(uint32_t rgb) { return (uint16_t)((rgb >> 8 & 0xf800) | (rgb >> 5 & 0x07e0) | (rgb >> 3 & 0x001f)); }
/* The instrument's effect ring (UI unify T10, spec 2.7; moved from inst_ui.c): the same 12 segment masks, coloured from a pc_ledfx_t the
 * caller owns and ticks. Made once for the mode's life as the instrument host's underlay (below every page root); its own statics, so it
 * never meets the player's ledrim_create above. */
lv_obj_t *ledrim_fx_create(lv_obj_t *scr, pc_ledfx_t *fx);   /* NULL without PSRAM for the masks (then the calls below do nothing) */
void ledrim_fx_update(int group);              /* each 30 Hz tick of the caller: render fx, invalidate the changed segments, one area per `group` segments */
typedef struct { uint32_t n, avg_us, max_us; } ledrim_fx_stat_t;
void ledrim_fx_stat(ledrim_fx_stat_t *out);   /* draws of the ring since the last call (count, average and longest draw); resets */
