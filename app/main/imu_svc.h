// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
/* The tilt source (F1b): the FakePod's QMI8658 (BSP_HAS_IMU), sampled in its own small task, filtered by pc_imu (smoothing, shake rejection, the mounting map), the vector kept in
 * PSRAM statics. The round board has no IMU: everything here answers "absent". The simulator's fake (sim/fake_imu_svc.c) answers the console's `simimu x y`.
 * Sampling is on demand: 50 Hz while someone asked for the vector in the last second (the maze's fixed step does), a slow look otherwise, so nothing runs for nothing. */
void imu_svc_start(void);                      /* probe + the task; no-op without an IMU (or when it did not answer) */
bool imu_svc_present(void);                    /* a tilt source answers: the maze offers its 傾斜 rule */
void imu_svc_gravity(float *x, float *y);      /* the tilt vector in screen axes (x right, y down), length 0..1, relative to the neutral pose when one is held, the way a marble would roll; 0, 0 when absent */
float imu_svc_rate(void);                      /* the gyro's smoothed length, dps (a gesture would use it) */
void imu_svc_map(bool swap_xy, int sign_x, int sign_y);   /* the sensor's mounting against the screen (bring-up; the board's default is bsp_geom.h's) */
void imu_svc_cal(void);                        /* console `imu cal`: prints the magnitude state; the scale is learned automatically (pc_imucal: whenever the device is still, any orientation, RAM only; lab imu_scale_pm is the start value) */
/* The neutral pose: imu_svc_gravity is relative to the pose held when imu_svc_zero ran (the marble rests at it). zero() asks (false: no IMU): the task waits for a still moment (gyro under
 * the threshold, 0.3 s of fast sampling, at most 1.5 s) and takes the vector then. zero_state: 0 none (absolute), 1 waiting, 2 held, 3 failed (too much movement: still absolute). */
bool imu_svc_zero(void);
void imu_svc_zero_clear(void);                 /* back to absolute (the page closes, or leaves the tilt rule) */
int imu_svc_zero_state(void);
void imu_svc_dump(void);                       /* console `imu`: the raw and the filtered values */
