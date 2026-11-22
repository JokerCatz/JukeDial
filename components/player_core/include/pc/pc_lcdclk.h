// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Experimental display QSPI clock (cfg "lcd_mhz", read at boot, look-test only). The round panel: 50 (default) or 80 MHz. The FakePod's AMOLED: 40 (default), 60 or 80
 * (the maker's firmware found 80 draws a 1 px black line at the left edge of a partial redraw). Anything else reads as the board's default. */
int pc_lcdclk_default(bool round_panel);
int pc_lcdclk_norm(int mhz, bool round_panel);
/* the time one full RGB565 frame of w x h needs on a 4-bit (quad) bus at mhz, in microseconds (wire time only: command and gap overhead come on top) */
int pc_lcdclk_frame_us(int w, int h, int mhz);
#ifdef __cplusplus
}
#endif
