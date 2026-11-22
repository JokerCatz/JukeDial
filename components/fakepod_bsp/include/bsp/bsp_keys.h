// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif
/* The FakePod's two keys. The bottom key is GPIO61 (BOOTMODE strap, input only); the top key is the AXP2101's power key: it has no GPIO, its edges arrive as PMU
 * interrupts (nIRQ on GPIO49), read over I2C0. The ONLY writes to the PMU are its IRQ-enable register 0x41 (= 0x0F: power-key edges, short and long press only) and the write-1-to-clear of
 * the status bits that were read: no voltage, rail, charger or power-key timing register is ever written. The top key's long hold stays the PMU's hard power-off.
 * bsp_i2c_init first. bsp_keys_init also gives ESP_ERR_NOT_FOUND when no AXP2101 answers (the bottom key still works then). */
esp_err_t bsp_keys_init(void);
/* The levels now (true = pressed). Cheap when the PMU is quiet (one GPIO read each); an I2C read of 1 byte plus a clear only while nIRQ is low. Call from one task. */
void bsp_keys_read(bool *top, bool *bottom);
/* The AXP2101 answered at init (0x03 = the chip id). */
bool bsp_pmu_present(void);
#ifdef __cplusplus
}
#endif
