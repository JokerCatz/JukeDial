// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_rotate.h"

/* Each board: whether the panel's memory frame sits 180 degrees from the upright picture (mount180), and whether the touch glass sits 180 degrees from the panel's frame.
 *   round (JC3636F118): mount180 = 1 - the vendor mounting, esp_lcd_panel_mirror(true, true) and touch mirror_x = mirror_y = 1; today's picture. Glass = panel frame.
 *   square (FakePod): 0 - no mirror anywhere today; a GUESS until the board (bsp_touch.c: the glass orientation is a bring-up item). */
static const struct { bool mount180, glass180; } kBOARD[] = {
    [PC_ROT_ROUND]  = { true, false },
    [PC_ROT_SQUARE] = { false, false },
};

int pc_rotate_norm(int v) { return v == 1 ? 1 : 0; }

pc_rot_t pc_rotate_map(pc_rot_board_t board, int rotate)
{
    const bool panel180 = kBOARD[board].mount180 != (pc_rotate_norm(rotate) == 1);   /* logical -> panel memory frame */
    const bool touch180 = kBOARD[board].glass180 != panel180;                         /* touch raw -> logical */
    pc_rot_t r = { .p_mx = panel180, .p_my = panel180, .t_mx = touch180, .t_my = touch180, .key_swap = pc_rotate_norm(rotate) == 1 };
    return r;
}

void pc_rotate_panel_apply(const pc_rot_t *r, int n, int x, int y, int *ox, int *oy)
{
    *ox = r->p_mx ? n - 1 - x : x;
    *oy = r->p_my ? n - 1 - y : y;
}

void pc_rotate_touch_apply(const pc_rot_t *r, int n, int x, int y, int *ox, int *oy)
{
    *ox = r->t_mx ? n - 1 - x : x;
    *oy = r->t_my ? n - 1 - y : y;
}

void pc_rotate_screen(int rotate, int n, int x, int y, int *ox, int *oy)
{
    const bool t = pc_rotate_norm(rotate) == 1;
    *ox = t ? n - 1 - x : x;
    *oy = t ? n - 1 - y : y;
}
