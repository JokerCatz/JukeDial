// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Screen rotation: cfg "rotate" 0 = upright as the board is used today, 1 = turned 180 degrees (anything else reads as 0); applied at boot. ONE table turns it into the panel's
 * mirror flags, the touch's mirror flags and the key swap; the BSPs apply exactly these and nothing else (no separate mirror call). Each board's native mounting offset is a
 * row of kBOARD in pc_rotate.c, so 0 reproduces today's flags and 1 is 180 degrees from them. 90 / 270 do not exist. */
typedef enum { PC_ROT_ROUND = 0, PC_ROT_SQUARE } pc_rot_board_t;

typedef struct {
    bool p_mx, p_my;   /* esp_lcd_panel_mirror(x, y) */
    bool t_mx, t_my;   /* esp_lcd_touch flags mirror_x / mirror_y */
    bool key_swap;     /* FakePod: the two side keys trade places so the upper one stays "+" */
} pc_rot_t;

int pc_rotate_norm(int v);                                       /* 0 or 1; anything else is 0 */
pc_rot_t pc_rotate_map(pc_rot_board_t board, int rotate);
/* what the panel / the touch driver do with the flags on an n x n square (logical point -> panel memory; raw touch -> logical), for the simulator and the tests */
void pc_rotate_panel_apply(const pc_rot_t *r, int n, int x, int y, int *ox, int *oy);
void pc_rotate_touch_apply(const pc_rot_t *r, int n, int x, int y, int *ox, int *oy);
/* the screen as the user sees it: the logical point turned by `rotate` (a 180 degree turn), and the way back (the same map) */
void pc_rotate_screen(int rotate, int n, int x, int y, int *ox, int *oy);
#ifdef __cplusplus
}
#endif
