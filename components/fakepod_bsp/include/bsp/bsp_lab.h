// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
/* LAB (app/main/lab.h): the run-time test-day knobs that live in this BSP. Each setter only stores a value (PSRAM statics: no internal RAM); the driver reads it
 * at its next use. 0 / default = today's behaviour. */
void bsp_lab_touch_x_num(int num);                     /* BSP_TOUCH_X_NUM (the x scale numerator, /30): live, read per touch sample */
void bsp_lab_dac(int pdn_pair, int hp_inv, int xtal_ibias);   /* the CS43131: the 0x080046 pair around the power-down, the headphone-detect polarity, XTAL_IBIAS (2 / 4 / 6); applied at the next chip boot (a changed value makes the config pending, as dac_fs) */
void bsp_lab_co5300(int send_c4, int send_58);         /* the panel init table: 0 = skip the C4 80 / the 58 07 line; read at the next panel init (boot, or a soft wake) */
void bsp_lab_lcd(int psram_dma, int queue, int rows);     /* the panel flush (night3 D): 1 = the SPI DMA reads the PSRAM frame directly, the transaction queue depth, rows per transaction; read once at the display start (a setting that would take more than PC_LAB_LCD_INTERNAL_MAX of bounce RAM is refused there, logged) */
