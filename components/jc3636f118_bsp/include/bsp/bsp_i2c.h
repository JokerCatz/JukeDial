// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdint.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { BSP_I2C_TOUCH = 0, BSP_I2C_IMU = 1, BSP_I2C_BUS_COUNT } bsp_i2c_bus_t;

/* Idempotent. Creates both buses; a failure on one bus does not stop the other. */
esp_err_t bsp_i2c_init(void);
/* NULL if that bus is not initialised. */
i2c_master_bus_handle_t bsp_i2c_bus(bsp_i2c_bus_t bus);
/* Probes 0x08..0x77. Returns number found (<= max), or -1 if the bus is unavailable. */
int bsp_i2c_scan(bsp_i2c_bus_t bus, uint8_t *found, int max);

#ifdef __cplusplus
}
#endif
