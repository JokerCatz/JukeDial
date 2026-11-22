// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_rotate.h"
#include "pc_test.h"

static const int N = 36;
static const bool MOUNT180[] = { [PC_ROT_ROUND] = true, [PC_ROT_SQUARE] = false };

static void test_zero_is_today(void)
{
    pc_rot_t r = pc_rotate_map(PC_ROT_ROUND, 0);   /* esp_lcd_panel_mirror(true, true); touch mirror_x = mirror_y = 1 */
    CHECK(r.p_mx && r.p_my && r.t_mx && r.t_my && !r.key_swap);
    r = pc_rotate_map(PC_ROT_SQUARE, 0);           /* the FakePod today: no mirror anywhere */
    CHECK(!r.p_mx && !r.p_my && !r.t_mx && !r.t_my && !r.key_swap);
}

static void test_180(void)
{
    pc_rot_t r = pc_rotate_map(PC_ROT_ROUND, 1);   /* the vendor's mirror is undone: the panel and the glass both go straight */
    CHECK(!r.p_mx && !r.p_my && !r.t_mx && !r.t_my && r.key_swap);
    r = pc_rotate_map(PC_ROT_SQUARE, 1);
    CHECK(r.p_mx && r.p_my && r.t_mx && r.t_my && r.key_swap);
}

static void test_geometry(void)
{
    for (int b = 0; b < 2; b++)
        for (int a = 0; a < 2; a++) {
            const pc_rot_t r = pc_rotate_map((pc_rot_board_t)b, a);
            for (int y = 0; y < N; y += 5)
                for (int x = 0; x < N; x += 7) {
                    int fx, fy, sx, sy, ex, ey, lx, ly;
                    pc_rotate_panel_apply(&r, N, x, y, &fx, &fy);                 /* where the panel memory holds the pixel */
                    sx = MOUNT180[b] ? N - 1 - fx : fx; sy = MOUNT180[b] ? N - 1 - fy : fy;   /* ... and where the user sees it (the mount undone) */
                    pc_rotate_screen(a, N, x, y, &ex, &ey);
                    CHECK_EQ_INT(sx, ex); CHECK_EQ_INT(sy, ey);                   /* the picture is the logical one, turned 180 when asked */
                    pc_rotate_touch_apply(&r, N, fx, fy, &lx, &ly);               /* a finger on that spot (glass frame = panel frame) reports the logical point back */
                    CHECK_EQ_INT(lx, x); CHECK_EQ_INT(ly, y);
                }
        }
}

static void test_norm(void)
{
    CHECK_EQ_INT(pc_rotate_norm(0), 0); CHECK_EQ_INT(pc_rotate_norm(1), 1);
    CHECK_EQ_INT(pc_rotate_norm(2), 0); CHECK_EQ_INT(pc_rotate_norm(3), 0); CHECK_EQ_INT(pc_rotate_norm(-1), 0); CHECK_EQ_INT(pc_rotate_norm(9), 0);
    const pc_rot_t a = pc_rotate_map(PC_ROT_ROUND, 3), b = pc_rotate_map(PC_ROT_ROUND, 0);   /* out of range = 0 */
    CHECK(a.p_mx == b.p_mx && a.t_my == b.t_my && a.key_swap == b.key_swap);
}

int main(void)
{
    RUN(test_zero_is_today); RUN(test_180); RUN(test_geometry); RUN(test_norm);
    return 0;
}
