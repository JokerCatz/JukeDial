// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdint.h>
#include "lvgl.h"
/* Clock faces besides the info page (spec 2026-11-10-clock-design.md section 5). cfg clockface=<pc_cf name>. LVGL task. */
int  clock_face_current(void);                  /* pc_cf index */
void clock_face_set(int idx);                   /* saves cfg */
const char *clock_face_label(int idx);
void clock_face_create(lv_obj_t *parent, int idx);   /* idx >= 1; the parent stays clickable (tap -> clock menu) */
void clock_face_destroy(void);
/* the moon by its age, waxing lit on the right: a px-square ARGB canvas in PSRAM, freed with the object */
lv_obj_t *clock_moon_create(lv_obj_t *parent, int px);
void clock_moon_draw(lv_obj_t *canvas, int64_t utc);
void clock_moon_report(int64_t utc);          /* console: prints the moon facts line (APP clockface moon phase=...) */
void clock_face_dump(void);                   /* console `clockface dump`: the HUD face's data as last set ("APP tface ..."), "APP tface none" for another face */
