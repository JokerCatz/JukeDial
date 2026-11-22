// SPDX-License-Identifier: LGPL-2.1-or-later
#include "bsp/bsp_i2c.h"
#include "bsp/bsp_pins.h"
#include "esp_log.h"

/* One bus (I2C0) carries the touch, the PMU and the IMU: BSP_I2C_TOUCH and BSP_I2C_IMU are the same handle here. */
static const char *TAG = "bsp_i2c";
static i2c_master_bus_handle_t s_bus;

esp_err_t bsp_i2c_init(void)
{
    if (s_bus) return ESP_OK;
    i2c_master_bus_config_t cfg = {
        .i2c_port = BSP_I2C_PORT_TOUCH,
        .sda_io_num = BSP_PIN_TP_SDA,
        .scl_io_num = BSP_PIN_TP_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = 1, /* board already has 2.2k; harmless */
    };
    esp_err_t e = i2c_new_master_bus(&cfg, &s_bus);
    if (e != ESP_OK) ESP_LOGE(TAG, "bus0: %s", esp_err_to_name(e));
    return e;
}

i2c_master_bus_handle_t bsp_i2c_bus(bsp_i2c_bus_t bus) { return (bus >= 0 && bus < BSP_I2C_BUS_COUNT) ? s_bus : NULL; }

int bsp_i2c_scan(bsp_i2c_bus_t bus, uint8_t *found, int max)
{
    i2c_master_bus_handle_t h = bsp_i2c_bus(bus);
    if (!h) return -1;
    int n = 0;
    for (uint8_t a = 0x08; a <= 0x77; a++) {
        if (i2c_master_probe(h, a, 50) == ESP_OK) {
            if (n < max) found[n] = a;
            n++;
        }
    }
    return n < max ? n : max;
}
