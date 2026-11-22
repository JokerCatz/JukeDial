// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
/* Coloured noise (white, pink, brown, blue, violet, grey, green, velvet) from pc_noisegen. LVGL context. */
int  color_noise_play(int color);    /* 0 ok */
int  color_noise_current(void);      /* -1 when not playing */
