// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include "lvgl.h"
#include "pc/pc_ambient.h"
/* The colours of the ambient scenes (ideas G5 氛圍頁): one table, scene x tone (the 風格 cell's 經典 / 霓虹 / 糖果: warm and natural, vivid, pastel) x PC_AM_TONES
 * colours + the field's fill, scaled by the brightness (a night-friendly step list, never full). The page never names a colour; pc_am_prim_t.tone indexes `tone`.
 * Tones by scene: lava 0 halo 1 blob 2 blob 3 gold 4 -; fish 0 food 1 fish 2 fish 3 bubble 4 gold 5 eye; fire 0 smoke 1 red 2 orange 3 yellow 4 hot 5 log;
 * night 0..3 a gradient (a primitive's `mix` blends with the next tone: the drifting colour) 4 star. Tables are const (flash); no RAM. */
#define AMBIENT_BRIGHT_N 5
typedef struct { lv_color_t bg, rim, tone[PC_AM_TONES]; } ambient_pal_t;
int ambient_bright_pct(int level);                                   /* level wrapped-free: clamped to 0..AMBIENT_BRIGHT_N-1; 20 35 50 65 80; default 65 */
int ambient_bright_default(void);                                    /* 3 */
void ambient_palette(int scene, int style, int level, ambient_pal_t *out);
