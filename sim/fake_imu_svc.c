// SPDX-License-Identifier: LGPL-2.1-or-later
/* The simulator's imu_svc (F1b): no sensor; `simimu x y` (percent of a full tilt) is the vector, and the first one makes the source present, as the board's probe would. Without it
 * the source is absent, so the round board's pictures and tests are unchanged. */
#include <math.h>
#include <stdio.h>
#include "imu_svc.h"
#include "pc/pc_imucal.h"
#include "sim.h"

static bool s_present;
static float s_x, s_y;
static bool s_swap;
static int s_sx = 1, s_sy = 1;
static int s_zstate;                        /* the neutral pose, as the board's imu_svc: 0 none, 2 held, 3 failed (the simulated device is "moving") */
static float s_nx, s_ny;
static bool s_moving;

void fake_imu_set(int px, int py)
{
    float x = (float)px / 100.0f, y = (float)py / 100.0f;
    const float l = sqrtf(x * x + y * y);
    if (l > 1.0f) { x /= l; y /= l; }
    s_x = x; s_y = y;
    s_present = true;
}
void fake_imu_off(void) { s_present = false; s_x = s_y = 0; s_zstate = 0; s_moving = false; }
void fake_imu_moving(bool on) { s_moving = on; }

void imu_svc_start(void) {}
bool imu_svc_present(void) { return s_present; }
void imu_svc_gravity(float *x, float *y)
{
    float vx = s_present ? s_x : 0, vy = s_present ? s_y : 0;
    if (s_present && s_zstate == 2) pc_imucal_rel(vx, vy, s_nx, s_ny, &vx, &vy);
    if (x) *x = vx;
    if (y) *y = vy;
}
bool imu_svc_zero(void)
{
    if (!s_present) return false;
    if (s_moving) { s_zstate = 3; return true; }              /* the board waits up to 1.5 s for a still moment; the simulator answers at once */
    s_nx = s_x; s_ny = s_y; s_zstate = 2;
    return true;
}
void imu_svc_zero_clear(void) { s_zstate = 0; }
int imu_svc_zero_state(void) { return s_present ? s_zstate : 0; }
float imu_svc_rate(void) { return 0; }
void imu_svc_map(bool swap_xy, int sign_x, int sign_y) { s_swap = swap_xy; s_sx = sign_x < 0 ? -1 : 1; s_sy = sign_y < 0 ? -1 : 1; }
void imu_svc_cal(void) { printf(s_present ? "APP imu cal simulated: no sensor magnitude\n" : "APP imu absent\n"); }
void imu_svc_dump(void)
{
    if (!s_present) { printf("APP imu absent\n"); return; }
    printf("APP imu vec=%.2f,%.2f map=%d,%d,%d (simulated)\n", s_x, s_y, s_swap, s_sx, s_sy);
    if (s_zstate == 2) printf("APP imu scale_pm=1000 learned=0 zero=%.2f,%.2f rejected=0\n", s_nx, s_ny);
    else printf("APP imu scale_pm=1000 learned=0 zero=%s rejected=0\n", s_zstate == 3 ? "failed" : "none");
}
