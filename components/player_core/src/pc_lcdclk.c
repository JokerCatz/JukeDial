// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_lcdclk.h"

int pc_lcdclk_default(bool round_panel) { return round_panel ? 50 : 40; }

int pc_lcdclk_norm(int mhz, bool round_panel)
{
    if (round_panel) return mhz == 50 || mhz == 80 ? mhz : 50;
    return mhz == 40 || mhz == 60 || mhz == 80 ? mhz : 40;
}

int pc_lcdclk_frame_us(int w, int h, int mhz)
{
    if (w <= 0 || h <= 0 || mhz <= 0) return 0;
    return (int)(((long long)w * h * 16 / 4 + mhz / 2) / mhz);   /* bits / (4 lanes * mhz) */
}
