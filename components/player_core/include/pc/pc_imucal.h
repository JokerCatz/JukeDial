// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* IMU calibration, pure maths (no hardware): (1) the magnitude scale learned from the live reading whenever the device is still, (2) the neutral pose: a tilt vector
 * taken relative to the pose held when a feature was entered. Units: accel in g, gyro in dps.
 * Scale: the device is "still" when the gyro length stays under PC_IMUCAL_GYRO_DPS and |a| (raw, before our scale) stays inside a PC_IMUCAL_BAND_G band for
 * PC_IMUCAL_STILL_MS; the mean |a| of that window gives scale = 1 / |a| (any orientation: gravity is 1 g whichever way the device lies). A result outside
 * PC_IMUCAL_SCALE_MIN..MAX is not taken (the previous scale stays, `rejected` counts it). Relative tilt: the vector minus the neutral, clamped to length 1. */
#define PC_IMUCAL_GYRO_DPS    5.0f
#define PC_IMUCAL_STILL_MS    1000u
#define PC_IMUCAL_BAND_G      0.03f
#define PC_IMUCAL_SCALE_MIN   0.80f
#define PC_IMUCAL_SCALE_MAX   1.25f
typedef struct {
    float scale;                     /* in use: starts as the caller's default (the lab knob), then the learned one */
    bool learned;
    unsigned rejected;               /* still windows whose scale was out of range (diagnostic) */
    float last_mag;                  /* the last learned |a| (raw) */
    float amin, amax, asum;          /* the running still window */
    uint32_t n, still_ms;
} pc_imucal_t;
void pc_imucal_init(pc_imucal_t *c, float start_scale);
/* One raw sample (sensor units, unscaled) and the time since the previous one. True when this sample completed a window and the scale was updated. */
bool pc_imucal_feed(pc_imucal_t *c, float ax, float ay, float az, float gx, float gy, float gz, uint32_t dt_ms);
bool pc_imucal_still(float gyro_dps);                                         /* the gyro length is low enough to take a neutral pose */
void pc_imucal_rel(float vx, float vy, float nx, float ny, float *ox, float *oy);   /* (v - n), under the dead zone 0, length clamped to 1 */
#ifdef __cplusplus
}
#endif
