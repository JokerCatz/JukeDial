// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

/* GPIO44 drives Q2 (low-side): high = on. Two modes to tell an active from a passive buzzer:
 * level (DC) beeps only an active buzzer; tone (square wave) beeps both. */
esp_err_t bsp_buzzer_init(void);                         /* output, low */
esp_err_t bsp_buzzer_level(bool on);
esp_err_t bsp_buzzer_tone(uint32_t hz, uint32_t ms);     /* blocking; pin left low afterwards */
