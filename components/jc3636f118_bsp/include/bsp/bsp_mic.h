// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

/* PDM RX (PDM->PCM filter) on BSP_I2S_PORT_MIC, mono 16-bit. */
esp_err_t bsp_mic_start(uint32_t sample_rate);
esp_err_t bsp_mic_read(int16_t *buf, size_t samples, size_t *got, uint32_t timeout_ms);
esp_err_t bsp_mic_stop(void);
