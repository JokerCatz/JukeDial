// SPDX-License-Identifier: LGPL-2.1-or-later
#include "bsp/bsp_pmu.h"
#include <stdio.h>
#include "bsp/bsp_i2c.h"
#include "bsp/bsp_pins.h"
#include "bsp_pmu_priv.h"
#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"   /* ESP_LOGE in the switched-off write path */

static EXT_RAM_BSS_ATTR i2c_master_dev_handle_t s_dev;
static EXT_RAM_BSS_ATTR volatile bool s_creating;   /* one-time creation: a claim flag (atomic), the loser waits for the winner (no lock object to create racily) */

i2c_master_dev_handle_t bsp_pmu_dev(void)
{
    if (s_dev) return s_dev;
    bool expected = false;
    if (!__atomic_compare_exchange_n(&s_creating, &expected, true, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        while (s_creating) vTaskDelay(1);                 /* the other caller is adding the device */
        return s_dev;
    }
    i2c_master_bus_handle_t bus = bsp_i2c_bus(BSP_I2C_TOUCH);
    if (bus) {
        const i2c_device_config_t dc = { .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = BSP_I2C_ADDR_PMU, .scl_speed_hz = BSP_I2C_FREQ_HZ };
        i2c_master_dev_handle_t d = NULL;
        if (i2c_master_bus_add_device(bus, &dc, &d) == ESP_OK) s_dev = d;
    }
    __atomic_store_n(&s_creating, false, __ATOMIC_RELEASE);
    return s_dev;
}

static esp_err_t rd(uint8_t reg, uint8_t *v) { return i2c_master_transmit_receive(s_dev, &reg, 1, v, 1, 30); }

esp_err_t bsp_pmu_battery(pc_axp_t *out)
{
    if (!bsp_pmu_dev()) return ESP_ERR_NOT_FOUND;
    uint8_t s1, s2, vh, vl, g;
    if (rd(PC_AXP_REG_STATUS1, &s1) || rd(PC_AXP_REG_STATUS2, &s2) || rd(PC_AXP_REG_VBAT_H, &vh) || rd(PC_AXP_REG_VBAT_L, &vl) || rd(PC_AXP_REG_GAUGE, &g)) return ESP_FAIL;
    pc_axp_decode(out, s1, s2, vh, vl, g);
    return ESP_OK;
}

void bsp_pmu_dump(void)
{
    if (!bsp_pmu_dev()) { printf("APP pmu absent\n"); return; }
    uint8_t s1 = 0xFF, s2 = 0xFF, vh = 0xFF, vl = 0xFF, g = 0xFF, r18 = 0xFF, r30 = 0xFF;
    rd(PC_AXP_REG_STATUS1, &s1); rd(PC_AXP_REG_STATUS2, &s2); rd(PC_AXP_REG_VBAT_H, &vh); rd(PC_AXP_REG_VBAT_L, &vl); rd(PC_AXP_REG_GAUGE, &g); rd(0x18, &r18); rd(0x30, &r30);
    pc_axp_t a;
    pc_axp_decode(&a, s1, s2, vh, vl, g);
    uint8_t r20 = 0xFF, r21 = 0xFF;
    rd(0x20, &r20); rd(0x21, &r21);
    printf("APP pmu src r20=%02x r21=%02x\n", r20, r21);
    printf("APP pmu raw s1=%02x s2=%02x vbat=%02x%02x gauge=%02x r18=%02x r30=%02x | vbus=%d batt=%d dir=%d chg=%s vbat=%d mV pct=%d\n", s1, s2, vh, vl, g, r18, r30, a.vbus, a.batt, a.dir,
           pc_axp_chg_name(a.chg_state), a.vbat_mv, a.pct);
}

esp_err_t bsp_pmu_pwron_source(uint8_t *r20, uint8_t *r21)
{
    if (!bsp_pmu_dev()) return ESP_ERR_NOT_FOUND;
    uint8_t a = 0, b = 0;
    if (rd(0x20, &a) || rd(0x21, &b)) return ESP_FAIL;
    if (r20) *r20 = a;
    if (r21) *r21 = b;
    return ESP_OK;
}

esp_err_t bsp_pmu_power_off(void)
{
    if (!bsp_pmu_dev()) return ESP_ERR_NOT_FOUND;
    uint8_t v = 0;
    if (rd(0x10, &v)) return ESP_FAIL;
    const uint8_t b[2] = { 0x10, (uint8_t)(v | 0x01) };   /* SOFT_PWROFF; the chip cuts the rails at once */
    return i2c_master_transmit(s_dev, b, 2, 30);
}

#if BSP_AXP_GAUGE_ENABLE
static const char *TAG = "bsp_pmu";
/* The proposed writes, off by default (owner's decision): read, set the bit, write, read back. */
esp_err_t bsp_pmu_gauge_enable(void)
{
    if (!bsp_pmu_dev()) return ESP_ERR_NOT_FOUND;
    static const struct { uint8_t reg, bit; } W[] = { { 0x18, 0x08 }, { 0x30, 0x01 } };
    for (unsigned i = 0; i < sizeof W / sizeof W[0]; i++) {
        uint8_t v = 0, back = 0;
        if (rd(W[i].reg, &v)) return ESP_FAIL;
        if (v & W[i].bit) continue;
        const uint8_t b[2] = { W[i].reg, (uint8_t)(v | W[i].bit) };
        if (i2c_master_transmit(s_dev, b, 2, 30) != ESP_OK || rd(W[i].reg, &back) || !(back & W[i].bit)) { ESP_LOGE(TAG, "reg 0x%02x write not kept", W[i].reg); return ESP_FAIL; }
    }
    return ESP_OK;
}
#endif
