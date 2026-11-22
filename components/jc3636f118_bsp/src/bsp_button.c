// SPDX-License-Identifier: LGPL-2.1-or-later
#include "bsp/bsp_button.h"
#include "bsp/bsp_pins.h"
#include "driver/gpio.h"

esp_err_t bsp_button_init(void)
{
    const gpio_config_t c = {
        .pin_bit_mask = 1ULL << BSP_PIN_BOOT,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    return gpio_config(&c);
}

bool bsp_boot_button_pressed(void) { return gpio_get_level(BSP_PIN_BOOT) == 0; }
