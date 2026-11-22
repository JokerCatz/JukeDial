// SPDX-License-Identifier: LGPL-2.1-or-later
#include "imu_svc.h"
#include <stdio.h>
#include "bsp/bsp_geom.h"
#include "lab.h"
#include <math.h>

#ifndef BSP_HAS_IMU
#define BSP_HAS_IMU 0
#endif

#if BSP_HAS_IMU
#include "bsp/bsp_i2c.h"
#include "bsp/bsp_imu.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "freertos/idf_additions.h"
#include "pc/pc_imu.h"
#include "pc/pc_imucal.h"

#define FAST_MS   20          /* 50 Hz while the vector is wanted */
#define SLOW_MS   250
#define IDLE_US   1000000     /* "wanted" for this long after the last imu_svc_gravity */

static pc_imu_t s_f;                      /* PSRAM (-msmall-data-limit=0 in main/CMakeLists.txt) */
static pc_imucal_t s_cal;                 /* the learned scale (RAM only, learned again every boot) */
static bool s_cal_init;
static volatile int s_zstate;             /* the neutral pose: 0 none (absolute tilt), 1 waiting for a still moment, 2 held, 3 failed (too much movement) */
static volatile float s_nx, s_ny;
static int s_zn;                          /* samples seen since the request */
#define ZERO_MIN_N   15                   /* 300 ms of 50 Hz: the smoothing (alpha 0.3) has settled whatever the slow look left behind */
#define ZERO_MAX_N   75                   /* 1.5 s to find a still moment, else the request fails */
static bsp_imu_sample_t s_raw;
static volatile bool s_present;
static volatile int64_t s_asked_us;
static volatile float s_vx, s_vy, s_rate;
static uint32_t s_reads, s_fail;
static volatile bool s_map_new;                    /* a mounting change waits for the task to apply it (the console must not write s_f while it is being fed) */
static volatile int s_map_swap, s_map_sx, s_map_sy;
static uint8_t s_addr, s_who;

static void task(void *arg)
{
    (void)arg;
    int64_t last_us = esp_timer_get_time();
    for (;;) {
        if (s_map_new) { pc_imu_map(&s_f, s_map_swap != 0, s_map_sx, s_map_sy); s_map_new = false; }
        const int64_t now = esp_timer_get_time();
        const bool fast = now - s_asked_us < IDLE_US || s_zstate == 1;
        if (bsp_imu_read(&s_raw) == ESP_OK) {
            if (!s_cal_init) { pc_imucal_init(&s_cal, (float)lab_get(PC_LAB_IMU_SCALE_PM) / 1000.0f); s_cal_init = true; }   /* the lab knob is only the start value until the first still moment */
            const uint32_t dt = (uint32_t)((now - last_us) / 1000);
            pc_imucal_feed(&s_cal, s_raw.ax, s_raw.ay, s_raw.az, s_raw.gx, s_raw.gy, s_raw.gz, dt > 1000 ? 1000 : dt);
            const float sc = s_cal.learned ? s_cal.scale : (float)lab_get(PC_LAB_IMU_SCALE_PM) / 1000.0f;   /* the three axes scaled; the gyro is left alone */
            pc_imu_feed(&s_f, s_raw.ax * sc, s_raw.ay * sc, s_raw.az * sc, s_raw.gx, s_raw.gy, s_raw.gz);
            s_vx = s_f.vx; s_vy = s_f.vy; s_rate = s_f.rate_dps;
            if (s_zstate == 1) {
                s_zn++;
                if (s_zn >= ZERO_MIN_N && s_f.seeded && pc_imucal_still(s_f.rate_dps)) { s_nx = s_f.vx; s_ny = s_f.vy; s_zstate = 2; }
                else if (s_zn >= ZERO_MAX_N) s_zstate = 3;
            }
            s_reads++;
        } else s_fail++;
        last_us = now;
        vTaskDelay(pdMS_TO_TICKS(fast ? FAST_MS : SLOW_MS));
    }
}

void imu_svc_start(void)
{
    if (s_present) return;
    pc_imu_init(&s_f, BSP_IMU_SWAP_XY != 0, BSP_IMU_SIGN_X, BSP_IMU_SIGN_Y);
    esp_err_t e = bsp_imu_init(&s_who, &s_addr);
    printf("APP imu init=%s addr=0x%02x who=0x%02x\n", esp_err_to_name(e), s_addr, s_who);
    if (e != ESP_OK) return;
    s_present = true;
    xTaskCreatePinnedToCoreWithCaps(task, "imu", 3072, NULL, 2, NULL, 0, MALLOC_CAP_SPIRAM);   /* PSRAM stack (PSR): I2C read into a stack buffer, as the touch read does on the LVGL task (PSRAM stack) */
}
bool imu_svc_present(void) { return s_present; }
void imu_svc_gravity(float *x, float *y)
{
    s_asked_us = esp_timer_get_time();
    float vx = s_present ? s_vx : 0, vy = s_present ? s_vy : 0;
    if (s_present && s_zstate == 2) pc_imucal_rel(vx, vy, s_nx, s_ny, &vx, &vy);   /* relative to the pose held at entry: the marble rests there */
    if (x) *x = vx;
    if (y) *y = vy;
}
bool imu_svc_zero(void)
{
    if (!s_present) return false;
    s_asked_us = esp_timer_get_time();
    s_zn = 0;
    s_zstate = 1;
    return true;
}
void imu_svc_zero_clear(void) { s_zstate = 0; }
int imu_svc_zero_state(void) { return s_present ? s_zstate : 0; }
float imu_svc_rate(void) { return s_present ? s_rate : 0; }
void imu_svc_map(bool swap_xy, int sign_x, int sign_y) { s_map_swap = swap_xy; s_map_sx = sign_x; s_map_sy = sign_y; s_map_new = true; }
void imu_svc_dump(void)
{
    if (!s_present) { printf("APP imu absent\n"); return; }
    s_asked_us = esp_timer_get_time();   /* keep it sampling while someone looks */
    printf("APP imu a=%.3f,%.3f,%.3f g=%.1f,%.1f,%.1f vec=%.2f,%.2f rate=%.1f map=%d,%d,%d reads=%u fail=%u shaken=%u addr=0x%02x\n",
           s_raw.ax, s_raw.ay, s_raw.az, s_raw.gx, s_raw.gy, s_raw.gz, s_vx, s_vy, s_rate, s_f.swap_xy, s_f.sign_x, s_f.sign_y,
           (unsigned)s_reads, (unsigned)s_fail, s_f.shaken, s_addr);
    char z[24] = "none";
    if (s_zstate == 2) snprintf(z, sizeof z, "%.2f,%.2f", s_nx, s_ny);
    else if (s_zstate == 1) snprintf(z, sizeof z, "waiting");
    else if (s_zstate == 3) snprintf(z, sizeof z, "failed");
    printf("APP imu scale_pm=%d learned=%d zero=%s rejected=%u\n", (int)(s_cal_init && s_cal.learned ? s_cal.scale * 1000.0f + 0.5f : (float)lab_get(PC_LAB_IMU_SCALE_PM)),
           s_cal_init && s_cal.learned, z, s_cal.rejected);
}
void imu_svc_cal(void)
{
    if (!s_present) { printf("APP imu absent\n"); return; }
    s_asked_us = esp_timer_get_time();   /* keep it sampling while someone looks */
    const float sc = s_cal_init && s_cal.learned ? s_cal.scale : (float)lab_get(PC_LAB_IMU_SCALE_PM) / 1000.0f;
    const float m = sqrtf(s_f.fx * s_f.fx + s_f.fy * s_f.fy + s_f.fz * s_f.fz) / sc;   /* the smoothed reading before our scale */
    printf("APP imu cal the scale is learned automatically whenever the device is still (gyro < %.0f dps, |a| steady for %u ms, any orientation); now |a|=%.4f g (unscaled, smoothed) gyro=%.1f dps, in use %d pm (%s), learned |a|=%.4f; lab imu_scale_pm %d is only the start value\n",
           (double)PC_IMUCAL_GYRO_DPS, (unsigned)PC_IMUCAL_STILL_MS, m, s_rate, (int)(sc * 1000.0f + 0.5f), s_cal_init && s_cal.learned ? "learned" : "not learned yet: still on the lab start value", s_cal.last_mag, lab_get(PC_LAB_IMU_SCALE_PM));
}
#else
void imu_svc_start(void) {}
void imu_svc_cal(void) { printf("APP imu absent\n"); }
bool imu_svc_present(void) { return false; }
void imu_svc_gravity(float *x, float *y) { if (x) *x = 0; if (y) *y = 0; }
float imu_svc_rate(void) { return 0; }
void imu_svc_map(bool swap_xy, int sign_x, int sign_y) { (void)swap_xy; (void)sign_x; (void)sign_y; }
void imu_svc_dump(void) { printf("APP imu absent\n"); }
bool imu_svc_zero(void) { return false; }
void imu_svc_zero_clear(void) {}
int imu_svc_zero_state(void) { return 0; }
#endif
