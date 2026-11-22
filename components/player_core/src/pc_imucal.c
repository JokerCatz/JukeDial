// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_imucal.h"
#include <math.h>
#include "pc/pc_imu.h"

void pc_imucal_init(pc_imucal_t *c, float start_scale)
{
    *c = (pc_imucal_t){ .scale = start_scale };
}

static void restart(pc_imucal_t *c, float m)
{
    c->amin = c->amax = c->asum = m;
    c->n = 1;
    c->still_ms = 0;
}

bool pc_imucal_feed(pc_imucal_t *c, float ax, float ay, float az, float gx, float gy, float gz, uint32_t dt_ms)
{
    const float m = sqrtf(ax * ax + ay * ay + az * az);
    const float rate = sqrtf(gx * gx + gy * gy + gz * gz);
    if (!pc_imucal_still(rate)) { c->n = 0; c->still_ms = 0; return false; }
    if (c->n == 0) { restart(c, m); return false; }
    const float lo = m < c->amin ? m : c->amin, hi = m > c->amax ? m : c->amax;
    if (hi - lo > PC_IMUCAL_BAND_G) { restart(c, m); return false; }
    c->amin = lo; c->amax = hi; c->asum += m; c->n++;
    c->still_ms += dt_ms;
    if (c->still_ms < PC_IMUCAL_STILL_MS) return false;
    const float mean = c->asum / (float)c->n;
    c->n = 0; c->still_ms = 0;                                   /* the next window starts with the next sample */
    const float s = mean > 0.01f ? 1.0f / mean : 0.0f;
    if (!(s >= PC_IMUCAL_SCALE_MIN && s <= PC_IMUCAL_SCALE_MAX)) { c->rejected++; return false; }
    c->scale = s; c->learned = true; c->last_mag = mean;
    return true;
}

bool pc_imucal_still(float gyro_dps) { return gyro_dps < PC_IMUCAL_GYRO_DPS; }

void pc_imucal_rel(float vx, float vy, float nx, float ny, float *ox, float *oy)
{
    float x = vx - nx, y = vy - ny;
    const float len = sqrtf(x * x + y * y);
    if (len < PC_IMU_DEAD / PC_IMU_FULL_G) x = y = 0;           /* the same dead zone as the absolute vector, so a held pose does not jitter */
    else if (len > 1.0f) { x /= len; y /= len; }
    if (ox) *ox = x;
    if (oy) *oy = y;
}
