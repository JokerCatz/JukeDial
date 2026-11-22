// SPDX-License-Identifier: LGPL-2.1-or-later
#include "bsp/bsp_board.h"
#include "bsp/bsp_pins.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* FakePod image: its own pins, a throw-away bus on I2C0 (bsp_i2c_init makes the real one later). Nine SCL clocks first, then up to 5 tries 20 ms apart; presence means the chip
 * id register 0x03 READS back as an AXP2101 (0x4A under the 0xCF mask), not just an acknowledge. Reads only. */
bool bsp_board_ok(const char **what)
{
    i2c_master_bus_handle_t bus = NULL;
    const i2c_master_bus_config_t cfg = {
        .i2c_port = BSP_I2C_PORT_TOUCH, .sda_io_num = BSP_PIN_TP_SDA, .scl_io_num = BSP_PIN_TP_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7, .flags.enable_internal_pullup = 1,
    };
    bool found = false, acked = false;
    if (i2c_new_master_bus(&cfg, &bus) == ESP_OK) {
        i2c_master_bus_reset(bus);
        i2c_master_dev_handle_t dev = NULL;
        const i2c_device_config_t dc = { .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = BSP_I2C_ADDR_PMU, .scl_speed_hz = BSP_I2C_FREQ_HZ };
        if (i2c_master_bus_add_device(bus, &dc, &dev) == ESP_OK) {
            for (int i = 0; i < 5 && !found; i++) {
                if (i) vTaskDelay(pdMS_TO_TICKS(20));
                const uint8_t reg = 0x03;
                uint8_t id = 0;
                if (i2c_master_transmit_receive(dev, &reg, 1, &id, 1, 50) == ESP_OK) { acked = true; found = (id & 0xCF) == 0x4A; }
            }
            i2c_master_bus_rm_device(dev);
        }
        i2c_del_master_bus(bus);
    }
    if (what) *what = found ? "axp2101" : acked ? "chip id is not an axp2101" : "no axp2101";
    return found;
}
