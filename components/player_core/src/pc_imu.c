// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_imu.h"
#include <math.h>
#include <string.h>

static int sgn(int v) { return v < 0 ? -1 : 1; }

void pc_imu_map(pc_imu_t *s, bool swap_xy, int sign_x, int sign_y) { s->swap_xy = swap_xy; s->sign_x = sgn(sign_x); s->sign_y = sgn(sign_y); }

void pc_imu_init(pc_imu_t *s, bool swap_xy, int sign_x, int sign_y)
{
    memset(s, 0, sizeof *s);
    pc_imu_map(s, swap_xy, sign_x, sign_y);
}

static void recompute(pc_imu_t *s)
{
    float x = s->swap_xy ? s->fy : s->fx, y = s->swap_xy ? s->fx : s->fy;
    x = -x * (float)s->sign_x / PC_IMU_FULL_G;
    y = -y * (float)s->sign_y / PC_IMU_FULL_G;
    const float len = sqrtf(x * x + y * y);
    if (len < PC_IMU_DEAD / PC_IMU_FULL_G) { s->vx = s->vy = 0; return; }
    if (len > 1.0f) { x /= len; y /= len; }
    s->vx = x; s->vy = y;
}

void pc_imu_feed(pc_imu_t *s, float ax, float ay, float az, float gx, float gy, float gz)
{
    const float g = sqrtf(ax * ax + ay * ay + az * az);
    const float rate = sqrtf(gx * gx + gy * gy + gz * gz);
    s->rate_dps += PC_IMU_ALPHA * (rate - s->rate_dps);
    if (g < PC_IMU_G_MIN || g > PC_IMU_G_MAX) { s->shaken++; return; }   /* free fall or a shake: not a tilt */
    if (!s->seeded) { s->fx = ax; s->fy = ay; s->fz = az; s->seeded = true; }
    else {
        s->fx += PC_IMU_ALPHA * (ax - s->fx);
        s->fy += PC_IMU_ALPHA * (ay - s->fy);
        s->fz += PC_IMU_ALPHA * (az - s->fz);
    }
    recompute(s);
}

void pc_imu_vector(const pc_imu_t *s, float *x, float *y) { if (x) *x = s->vx; if (y) *y = s->vy; }
