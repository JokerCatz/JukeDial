// SPDX-License-Identifier: LGPL-2.1-or-later
#include "bsp/bsp_touch.h"
#include "bsp/bsp_lab.h"
#include "esp_attr.h"
#include "bsp/bsp_i2c.h"
#include "bsp/bsp_pins.h"
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch_cst816s.h"
#include "esp_log.h"
#include "esp_lv_adapter.h"
#include "bsp/bsp_display.h"
#include "pc/pc_rotate.h"

/* CST820 on the shared I2C0 (0x15). The maker's own esp_lcd_touch_cst820 reads the same registers as the CST816S driver we already use (0x02 count + 12-bit x/y,
 * 0xA7 chip id, no gesture register), so that driver is used as it is. Orientation of the glass against the screen is a bring-up item (the flags below). */
/* The real CST820 board needs the x coordinate scaled by 29/30 (NP's README table: "CST820, x x 29/30 calibration"; NightPlayer (c) 2026 ZYFDroid, MIT). Without it the touch is 3.4 % off
 * at the right edge. Named constants so the first board day can adjust them (a touch-test page shows the error); 30/30 = off. */
#define BSP_TOUCH_X_NUM 29
#define BSP_TOUCH_X_DEN 30
EXT_RAM_BSS_ATTR static uint8_t s_lab_x_num;     /* LAB lab_touch_x_num (0 = BSP_TOUCH_X_NUM): PSRAM, no internal RAM */
void bsp_lab_touch_x_num(int num) { s_lab_x_num = (uint8_t)num; }
static const char *TAG = "bsp_touch";
static lv_indev_t *s_indev;
static uint32_t s_presses;
static bool s_was_pressed;

/* The scale is applied to the raw reading before the mirror flag of the driver (process_coordinates runs after the flags in esp_lcd_touch: see the driver; the mirror then maps the scaled value). */
static void touch_scale_x(esp_lcd_touch_handle_t tp, uint16_t *x, uint16_t *y, uint16_t *strength, uint8_t *point_num, uint8_t max_point_num)
{
    (void)tp; (void)y; (void)strength; (void)max_point_num;
    for (uint8_t i = 0; i < *point_num; i++) x[i] = (uint16_t)((uint32_t)x[i] * (s_lab_x_num ? s_lab_x_num : BSP_TOUCH_X_NUM) / BSP_TOUCH_X_DEN);
}

esp_err_t bsp_touch_start(lv_disp_t *disp, lv_indev_t **out_indev)
{
    if (out_indev) *out_indev = NULL;
    i2c_master_bus_handle_t bus = bsp_i2c_bus(BSP_I2C_TOUCH);
    ESP_RETURN_ON_FALSE(bus && disp, ESP_ERR_INVALID_STATE, TAG, "i2c bus or display missing");

    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_CST816S_CONFIG();
    io_cfg.scl_speed_hz = BSP_I2C_FREQ_HZ;      /* IDF 6.1 refuses a device without a speed (the macro has none): a panic loop at boot otherwise */
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(bus, &io_cfg, &io), TAG, "touch io");

    const pc_rot_t rot = pc_rotate_map(PC_ROT_SQUARE, bsp_display_rotation());   /* the same table as the panel (rotate 0 = no mirror: a guess until the board) */
    esp_lcd_touch_config_t cfg = {
        .x_max = BSP_LCD_H_RES,
        .y_max = BSP_LCD_V_RES,
        .rst_gpio_num = BSP_PIN_TP_RST,
        .int_gpio_num = BSP_PIN_TP_INT,
        .levels = { .reset = 0, .interrupt = 0 },
        .flags = { .swap_xy = 0, .mirror_x = rot.t_mx, .mirror_y = rot.t_my },
        .process_coordinates = touch_scale_x,
    };
    esp_lcd_touch_handle_t tp = NULL;
    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_cst816s(io, &cfg, &tp), TAG, "cst820");

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
    bool pressed = lv_indev_get_state(s_indev) == LV_INDEV_STATE_PRESSED;
    if (pressed && !s_was_pressed) s_presses++;
    s_was_pressed = pressed;
    if (x) *x = (uint16_t)p.x;
    if (y) *y = (uint16_t)p.y;
    return pressed;
}

uint32_t bsp_touch_press_count(void) { return s_presses; }
