// SPDX-License-Identifier: LGPL-2.1-or-later
#include "bsp/bsp_touch.h"
#include "bsp/bsp_i2c.h"
#include "bsp/bsp_pins.h"
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch_cst816s.h"
#include "esp_log.h"
#include "esp_lv_adapter.h"
#include "bsp/bsp_display.h"
#include "pc/pc_rotate.h"

static const char *TAG = "bsp_touch";
static lv_indev_t *s_indev;
static uint32_t s_presses;
static bool s_was_pressed;

esp_err_t bsp_touch_start(lv_disp_t *disp, lv_indev_t **out_indev)
{
    if (out_indev) *out_indev = NULL;
    i2c_master_bus_handle_t bus = bsp_i2c_bus(BSP_I2C_TOUCH);
    ESP_RETURN_ON_FALSE(bus && disp, ESP_ERR_INVALID_STATE, TAG, "i2c bus or display missing");

    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_CST816S_CONFIG();
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(bus, &io_cfg, &io), TAG, "touch io");

    const pc_rot_t rot = pc_rotate_map(PC_ROT_ROUND, bsp_display_rotation());   /* the same table as the panel: rotate 0 = mirror x + y, as the vendor sets it */
    esp_lcd_touch_config_t cfg = {
        .x_max = BSP_LCD_H_RES,
        .y_max = BSP_LCD_V_RES,
        .rst_gpio_num = BSP_PIN_TP_RST,
        .int_gpio_num = BSP_PIN_TP_INT,
        .levels = { .reset = 0, .interrupt = 0 },
        .flags = { .swap_xy = 0, .mirror_x = rot.t_mx, .mirror_y = rot.t_my },
    };
    esp_lcd_touch_handle_t tp = NULL;
    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_cst816s(io, &cfg, &tp), TAG, "cst816s");

    esp_lv_adapter_touch_config_t tcfg = ESP_LV_ADAPTER_TOUCH_DEFAULT_CONFIG(disp, tp);
    s_indev = esp_lv_adapter_register_touch(&tcfg);
    ESP_RETURN_ON_FALSE(s_indev, ESP_FAIL, TAG, "register touch");
    if (out_indev) *out_indev = s_indev;
    return ESP_OK;
}

bool bsp_touch_read_raw(uint16_t *x, uint16_t *y)
{
    if (!s_indev) return false;
    lv_point_t p;
    lv_indev_get_point(s_indev, &p);
#if LVGL_VERSION_MAJOR >= 9
    bool pressed = lv_indev_get_state(s_indev) == LV_INDEV_STATE_PRESSED;
#else
    bool pressed = s_indev->proc.state == LV_INDEV_STATE_PRESSED;
#endif
    if (pressed && !s_was_pressed) s_presses++;
    s_was_pressed = pressed;
    if (x) *x = (uint16_t)p.x;
    if (y) *y = (uint16_t)p.y;
    return pressed;
}

uint32_t bsp_touch_press_count(void) { return s_presses; }
