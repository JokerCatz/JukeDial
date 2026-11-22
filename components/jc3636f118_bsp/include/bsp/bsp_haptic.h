// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdint.h>
#include "esp_err.h"

/* DRV2605L on BSP_I2C_TOUCH at BSP_I2C_ADDR_HAPTIC. EN is tied high and IN/TRIG to GND,
 * so only internal (I2C GO bit) triggering is possible. Motor is an LRA. */
esp_err_t bsp_haptic_init(uint8_t *status_out);   /* ESP_ERR_NOT_FOUND if no ack or wrong device id */
esp_err_t bsp_haptic_play(uint8_t effect_id);     /* ROM library 6 (LRA), effects 1..123 */
esp_err_t bsp_haptic_stop(void);
esp_err_t bsp_haptic_set_clamp(uint8_t od_clamp);   /* OD_CLAMP (0x17): overall strength, ~21.3 mV per step (open-loop LRA) */
esp_err_t bsp_haptic_read_reg(uint8_t reg, uint8_t *val);
