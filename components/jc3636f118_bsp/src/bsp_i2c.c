// SPDX-License-Identifier: LGPL-2.1-or-later
#include "bsp/bsp_i2c.h"
#include "bsp/bsp_pins.h"
#include "esp_log.h"

static const char *TAG = "bsp_i2c";
static i2c_master_bus_handle_t s_bus[BSP_I2C_BUS_COUNT];

static esp_err_t new_bus(i2c_port_num_t port, gpio_num_t sda, gpio_num_t scl, i2c_master_bus_handle_t *out)
{
    i2c_master_bus_config_t cfg = {
        .i2c_port = port,
        .sda_io_num = sda,
        .scl_io_num = scl,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = 1, /* board already has 2.2k; harmless */
    };
    return i2c_new_master_bus(&cfg, out);
}

esp_err_t bsp_i2c_init(void)
{
    esp_err_t first_err = ESP_OK;
    if (!s_bus[BSP_I2C_TOUCH]) {
        esp_err_t e = new_bus(BSP_I2C_PORT_TOUCH, BSP_PIN_TP_SDA, BSP_PIN_TP_SCL, &s_bus[BSP_I2C_TOUCH]);
        if (e != ESP_OK) { ESP_LOGE(TAG, "bus0: %s", esp_err_to_name(e)); first_err = e; }
    }
    if (!s_bus[BSP_I2C_IMU]) {
        esp_err_t e = new_bus(BSP_I2C_PORT_IMU, BSP_PIN_IMU_SDA, BSP_PIN_IMU_SCL, &s_bus[BSP_I2C_IMU]);
        if (e != ESP_OK) { ESP_LOGE(TAG, "bus1: %s", esp_err_to_name(e)); if (first_err == ESP_OK) first_err = e; }
    }
    return first_err;
}

i2c_master_bus_handle_t bsp_i2c_bus(bsp_i2c_bus_t bus)
{
    return (bus >= 0 && bus < BSP_I2C_BUS_COUNT) ? s_bus[bus] : NULL;
}

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
