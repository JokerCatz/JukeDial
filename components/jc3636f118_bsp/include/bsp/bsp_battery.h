// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include "esp_err.h"

/* ADC on GPIO55, which sees the system rail through R6 15k / R63 10k (x2.5).
 * ESP-IDF v6.1 has no ADC calibration scheme for esp32s31 yet, so pin_mv/rail_mv are -1
 * unless calibration becomes available; raw is always valid. */
esp_err_t bsp_battery_init(void);
esp_err_t bsp_battery_read(int *raw, int *pin_mv, int *rail_mv);
int       bsp_battery_atten(void); /* attenuation actually accepted by the driver */
