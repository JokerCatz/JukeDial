// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stddef.h>
/* Track B: speed playback (pitch kept) and the crossfade between tracks. The values the owner chose live in cfg (play_speed percent, xfade_s seconds);
 * what the audio task is asked for is those values, or off when the mode is too heavy (pc_speed_ok: aptX / LDAC / broadcast / above 48 kHz). The
 * UI shows the reason (honest degrade). LVGL task; speed_svc_refresh may also run from fx_mode_set's callers (it only reads atomics and posts two words). */
void speed_svc_init(void);                 /* after cfg_load: pushes the stored values to the audio service */
void speed_svc_refresh(void);              /* the mode bits changed (fx_mode_set): ask the audio service again */
int  speed_svc_pct(void);                  /* the chosen speed, percent (75 / 100 / 125 / 150 / 200) */
void speed_svc_step(int d);                /* the chosen speed by detents (signed, wraps) */
int  speed_svc_xfade_s(void);              /* the chosen crossfade, seconds (0 / 2 / 4 / 6) */
void speed_svc_xfade_step(int d);
const char *speed_svc_why(void);           /* NULL: running as chosen; else the reason it is off now (the language of the screen) */
void speed_svc_label(char *out, size_t cap);   /* "倍速 1.25×" (and the short reason when it is off) */
