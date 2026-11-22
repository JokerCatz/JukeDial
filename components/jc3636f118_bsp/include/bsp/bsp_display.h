// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_lcd_types.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* QSPI bus -> ST77916 (vendor init) -> LEDC backlight -> LVGL adapter (PSRAM buffers) -> start.
 * Backlight ends at 80%. On failure returns the first error and leaves *out_disp NULL. */
esp_err_t bsp_display_start(lv_disp_t **out_disp);
/* Same, ending at backlight_pct (0 keeps the panel dark). */
esp_err_t bsp_display_start_ex(lv_disp_t **out_disp, uint8_t backlight_pct);
/* Before start: the screen rotation, 0 upright / 1 turned 180 degrees (pc_rotate.h; 0 = as the board is used today). The touch follows (bsp_touch_start), the FakePod's keys too. */
void      bsp_display_set_rotation(int rotate);
int       bsp_display_rotation(void);
/* Before start: the experimental QSPI clock in MHz (cfg lcd_mhz; pc_lcdclk.h lists the values per board; 0 or a value the board does not allow = its default). Logged as "APP display qspi_mhz=". */
void      bsp_display_set_clock_mhz(int mhz);
/* Before start: LVGL task stack size and placement (default 8 KB internal). */
void      bsp_display_set_task_stack(uint32_t bytes, bool psram);
esp_err_t bsp_display_backlight_set(uint8_t percent);   /* clamps to 0..100 */
bool      bsp_display_lock(int32_t timeout_ms);         /* -1 = wait forever */
void      bsp_display_unlock(void);
esp_lcd_panel_handle_t bsp_display_panel(void);
/* Frees the SPI driver's internal DMA bounce buffers of the last frame (up to 2 x ~14-16 KB): esp_lcd only collects
 * finished colour transfers at the next one, so they stay allocated while nothing draws (screen off). LVGL task only. */
void bsp_display_release_dma(void);
/* Frames per second measured by the adapter; 0 if unavailable. */
uint32_t  bsp_display_fps(void);
/* FakePod: one "APP lcdflush" line with the panel flush counters since the last call (console uistat prints it after its own line); the round board and the simulator print nothing. */
void      bsp_display_flush_stat(void);

#ifdef __cplusplus
}
#endif
