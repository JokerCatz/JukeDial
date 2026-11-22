// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include "esp_err.h"

/* BOOT key (GPIO60, external 10k pull-up, pressed = low). Input only, no internal pulls:
 * it is a strapping pin. */
esp_err_t bsp_button_init(void);
bool      bsp_boot_button_pressed(void);
