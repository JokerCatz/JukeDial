// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include "esp_err.h"
#include "esp_lcd_types.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"

/* CO5300 (460x460 AMOLED, QSPI) as an esp_lcd panel: our own code, from the maker's facts (init array in .local/fakepod/hw-notes.md section 10: FE 00; 3A 55; 53 20; 51 00;
 * 63 FF; 2A/2B windows; 11 + 120 ms; 29) and the QSPI command framing every esp_lcd QSPI panel here uses (opcode 0x02 + command in bits 8..15 for a command, 0x32 for
 * colour). The panel needs every window to start on an even column and row and end on an odd one: bsp_display makes LVGL's dirty areas so. */
esp_err_t bsp_co5300_new(esp_lcd_panel_io_handle_t io, int reset_gpio, int x_gap, esp_lcd_panel_handle_t *out);
/* A command with parameters through the same framing (brightness 0x51, display on / off 0x29 / 0x28, NOP 0x00). */
esp_err_t bsp_co5300_cmd(esp_lcd_panel_handle_t panel, uint8_t cmd, const uint8_t *data, size_t n);
void bsp_co5300_flush_stat(void);   /* night3 D: one APP lcdflush line (the flush counters since the last call), then cleared */
