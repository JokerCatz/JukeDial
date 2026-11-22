// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdint.h>
#include "esp_err.h"

/* QMI8658C on BSP_I2C_IMU. Note: the factory firmware and hwtest both found no device
 * on this bus, so the IMU may not be populated on this board. */
typedef struct { float ax, ay, az, gx, gy, gz, temp_c; } bsp_imu_sample_t;

esp_err_t bsp_imu_init(uint8_t *who_am_i_out, uint8_t *addr_out); /* ESP_ERR_NOT_FOUND if absent */
esp_err_t bsp_imu_read(bsp_imu_sample_t *s);
