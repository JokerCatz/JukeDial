// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* CST816 on BSP_I2C_TOUCH (call bsp_i2c_init first), registered as an LVGL pointer
 * through the adapter. Mirror flags come from pc_rotate.h's table for the display's rotation. */
esp_err_t bsp_touch_start(lv_disp_t *disp, lv_indev_t **out_indev);
/* Last point seen by the LVGL pointer; true while pressed. Call from LVGL context. */
bool      bsp_touch_read_raw(uint16_t *x, uint16_t *y);
/* Number of press edges seen by LVGL since start. */
uint32_t  bsp_touch_press_count(void);

#ifdef __cplusplus
}
#endif
