// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include "lvgl.h"
/* The power-on picture (rules and constants: pc/pc_splash.h; feature note: the ui design notes, "開機畫面"). Used by the boot front (boot_front.c) only:
 * splash_prepare(mode) reads + decodes the file (false: none, one "APP splash none (<reason>)" line, nothing else happened), the display starts dark, splash_run(disp)
 * draws it, breathes the backlight once (blocks ~2 s: nothing else runs in the boot meanwhile) and removes it again at the user's brightness. */
bool splash_prepare(const char *mode);
void splash_run(lv_display_t *disp);   /* no-op when splash_prepare said false */
bool splash_shown(void);              /* the picture is on the screen now (the simulator's shot blends the system layer only then: the SDL driver keeps its own objects there) */
void splash_drop(void);                /* a prepared picture that will not be shown (the display did not start): freed */
