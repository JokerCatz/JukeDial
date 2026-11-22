// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "pc/pc_axp2101.h"
/* The AXP2101's battery side, READ ONLY (F2). One transaction per register over I2C0 (bsp_i2c_init first); no write, no IRQ enable: the VBUS / battery IRQs stay masked (bsp_keys.c's
 * 0x41 = 0x0F) and the power service polls instead. The gauge and the battery ADC are whatever the chip's reset / the maker's boot left: see BSP_AXP_GAUGE_ENABLE below.
 * Any task (each read is one I2C transaction on the shared bus). */
esp_err_t bsp_pmu_battery(pc_axp_t *out);       /* ESP_ERR_NOT_FOUND without an AXP2101, ESP_FAIL on a failed read */
void      bsp_pmu_dump(void);                   /* console `power` on the FakePod: the registers raw and decoded (r18, r30 = the gauge / ADC enables, r20 / r21 = the power-on source and the last power-off reason) */
/* Power-on source and soft power-off (FakePod only). 0x20: the power-on source (bit0 power key, bit2 VBUS insert; 0x04 after a plug-in boot on the real board, NightPlayer (c) 2026 ZYFDroid, MIT);
 * 0x21: why it last went off. 0x10 bit0 = soft power-off, written by read-modify-write only (the other bits of the register are configuration). */
esp_err_t bsp_pmu_pwron_source(uint8_t *r20, uint8_t *r21);
esp_err_t bsp_pmu_power_off(void);                   /* console `power` on the FakePod: the five registers raw and decoded */
/* PROPOSED PMU WRITES (not made; the owner decides, see F2-report.md): if the percent reads 0 / the voltage 0 on a board with a cell, the gauge or the ADC is off. The two writes would be
 *   reg 0x18 |= 0x08  (fuel gauge enable)   and   reg 0x30 |= 0x01  (battery voltage ADC enable)
 * as read-modify-write with the old value read back and compared afterwards. They exist in bsp_pmu.c behind this switch, default 0. */
#ifndef BSP_AXP_GAUGE_ENABLE
#define BSP_AXP_GAUGE_ENABLE 0
#endif
#if BSP_AXP_GAUGE_ENABLE
esp_err_t bsp_pmu_gauge_enable(void);   /* the two proposed writes, read back; nobody calls it unless the owner rules for it */
#endif
