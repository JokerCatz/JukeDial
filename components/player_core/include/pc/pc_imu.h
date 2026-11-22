// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Accelerometer samples -> the tilt vector of a hand-held screen (F1b; the FakePod's QMI8658, the maze's tilt rule), no hardware here. Units: accel in g, gyro in dps.
 * The vector is in SCREEN axes (x right, y down, the UI's), the direction a marble would roll: "downhill", i.e. the opposite of the accelerometer's reading (an
 * accelerometer at rest reads the reaction to gravity). Length 0..1: PC_IMU_FULL_G (sin of the tilt) is a full tilt, under PC_IMU_DEAD the vector is 0.
 * Mounting: the sensor's axes against the screen's differ per board and are not in the schematic; swap_xy / sign_x / sign_y say how to map them (the console
 * `imu map` sets them for the bring-up; the board's constants are in bsp_geom.h). Shaking (the vector's length far from 1 g) is not tilt: the last output is kept. */
#define PC_IMU_FULL_G   0.50f
#define PC_IMU_DEAD     0.04f
#define PC_IMU_ALPHA    0.30f    /* the smoothing of a sample (at 50 Hz: a time constant of about 60 ms) */
#define PC_IMU_G_MIN    0.70f    /* the length (g) a sample may have to count as "at rest enough" */
#define PC_IMU_G_MAX    1.35f
typedef struct {
    bool swap_xy;
    int sign_x, sign_y;              /* +1 / -1 */
    float fx, fy, fz;                /* the smoothed accelerometer reading, sensor axes, g */
    float vx, vy;                    /* the output vector, screen axes */
    float rate_dps;                  /* the smoothed gyro length */
    bool seeded;
    unsigned shaken;                 /* samples dropped as shaking (diagnostic) */
} pc_imu_t;
void pc_imu_init(pc_imu_t *s, bool swap_xy, int sign_x, int sign_y);
void pc_imu_map(pc_imu_t *s, bool swap_xy, int sign_x, int sign_y);          /* the mounting, at run time (bring-up) */
void pc_imu_feed(pc_imu_t *s, float ax, float ay, float az, float gx, float gy, float gz);
void pc_imu_vector(const pc_imu_t *s, float *x, float *y);                   /* the tilt vector, length 0..1 */
#ifdef __cplusplus
}
#endif
