// SPDX-License-Identifier: LGPL-2.1-or-later
/* Experimental display QSPI clock (lcd_clock.h). Flash code, PSRAM state: no internal RAM of our own. */
#include <stdio.h>
#include "esp_attr.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bsp/bsp_button.h"
#include "bsp/bsp_display.h"
#include "bsp/bsp_geom.h"
#include "pc/pc_lcdclk.h"
#include "cfg.h"
#include "guard.h"
#include "lcd_clock.h"

EXT_RAM_BSS_ATTR static bool s_escape;

void lcd_clock_prepare(bool persist)
{
    const int def = pc_lcdclk_default(BSP_LCD_ROUND);
    const int saved = cfg_int("lcd_mhz", def);
    int use = pc_lcdclk_norm(saved, BSP_LCD_ROUND);
    if (use != def && (s_escape || guard_last_reset_was_crash())) {
        printf("APP display qspi: %s, lcd_mhz %d goes back to %d\n", s_escape ? "boot key held" : "the last reset was a crash", saved, def);
        use = def;
        if (persist) cfg_set_int("lcd_mhz", def);
    }
    bsp_display_set_clock_mhz(use);
}

void lcd_clock_escape_poll(void)
{
    if (bsp_button_init() != ESP_OK) return;
    const int64_t t_end = esp_timer_get_time() + 3000 * 1000;        /* the end of app_main's 3 s recovery window */
    int64_t held_since = -1;
    for (;;) {
        const int64_t now = esp_timer_get_time();
        if (bsp_boot_button_pressed()) { if (held_since < 0) held_since = now; }
        else held_since = -1;
        if (held_since >= 0 && now - held_since >= 3000 * 1000) { s_escape = true; printf("APP display qspi: escape (boot key held 3 s)\n"); return; }
        if (now >= t_end && (held_since < 0 || now > 5000 * 1000)) return;   /* past the window: only while the key is still down, and never past 5 s of uptime */
        vTaskDelay(pdMS_TO_TICKS(25));
    }
}
