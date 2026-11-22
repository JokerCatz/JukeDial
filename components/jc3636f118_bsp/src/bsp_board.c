// SPDX-License-Identifier: LGPL-2.1-or-later
#include "bsp/bsp_board.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* Round-board image: is this the FakePod? Its AXP2101 answers at 0x34 on GPIO48 (SDA) / GPIO47 (SCL); on this board nothing is wired there (schematic: both end in unused net
 * ports; no pin of the BSP map). Only those two pins are touched, only by a throw-away bus on the IMU's port (deleted before bsp_i2c_init): nine SCL clocks to free a slave that
 * holds SDA after a reset mid-transfer, then up to 4 probes 20 ms apart (an address clocked, nothing written to any device). ANY acknowledge = the FakePod: this image refuses. */
#define PROBE_SDA 48
#define PROBE_SCL 47
bool bsp_board_ok(const char **what)
{
    i2c_master_bus_handle_t bus = NULL;
    const i2c_master_bus_config_t cfg = {
        .i2c_port = I2C_NUM_1, .sda_io_num = PROBE_SDA, .scl_io_num = PROBE_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7, .flags.enable_internal_pullup = 1,
    };
    bool other = false;
    if (i2c_new_master_bus(&cfg, &bus) == ESP_OK) {
        i2c_master_bus_reset(bus);
        for (int i = 0; i < 4 && !other; i++) {
            if (i) vTaskDelay(pdMS_TO_TICKS(20));
            other = i2c_master_probe(bus, 0x34, 50) == ESP_OK;
        }
        i2c_del_master_bus(bus);
    }
    if (what) *what = other ? "axp2101 present" : "no axp2101";
    return !other;
}
