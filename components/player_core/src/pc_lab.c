// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_lab.h"
#include <stdlib.h>
#include <string.h>

/* Same order as the enum. The defaults are the constants the knob replaces (ui_geom.h, pc_keys.h, pc_blind.h, bsp_touch.c ...). */
static const pc_lab_def_t T[PC_LAB_N] = {
    [PC_LAB_FLICK_EDGE]     = { "lab_flick_edge",      50,   56,   20,  120, 1, PC_LAB_LIVE,   false },
    [PC_LAB_FLICK_MS]       = { "lab_flick_ms",        300,  300,  150, 600, 1, PC_LAB_LIVE,   false },
    [PC_LAB_FLICK_START_MS] = { "lab_flick_start_ms",  150,  150,  80,  400, 1, PC_LAB_LIVE,   false },
    [PC_LAB_FLICK_PCT]      = { "lab_flick_pct",       40,   40,   20,  70,  1, PC_LAB_LIVE,   false },
    [PC_LAB_FLICK_H_PCT]    = { "lab_flick_h_pct",     30,   30,   15,  60,  1, PC_LAB_LIVE,   false },
    [PC_LAB_NOWB_HIDE_MS]   = { "lab_nowb_hide_ms",    4000, 4000, 1000, 15000, 1, PC_LAB_LIVE, true },
    [PC_LAB_KEY_DBL_MS]     = { "lab_key_dbl_ms",      300,  300,  150, 800, 1, PC_LAB_LIVE,   false },
    [PC_LAB_KEY_HOLD_MS]    = { "lab_key_hold_ms",     450,  450,  200, 1500, 1, PC_LAB_LIVE,  false },
    [PC_LAB_KEY_REP_MS]     = { "lab_key_rep_ms",      140,  140,  60,  500, 1, PC_LAB_LIVE,   false },
    [PC_LAB_TOUCH_X_NUM]    = { "lab_touch_x_num",     29,   29,   26,  32,  1, PC_LAB_LIVE,   false },
    [PC_LAB_CO5300_C4]      = { "lab_co5300_c4",       1,    1,    0,   1,   1, PC_LAB_REBOOT, false },
    [PC_LAB_CO5300_58]      = { "lab_co5300_58",       1,    1,    0,   1,   1, PC_LAB_REBOOT, false },
    [PC_LAB_DAC_PDN_PAIR]   = { "lab_dac_pdn_pair",    0,    0,    0,   1,   1, PC_LAB_NEXT,   false },
    [PC_LAB_DAC_HP_INV]     = { "lab_dac_hp_inv",      0,    0,    0,   1,   1, PC_LAB_NEXT,   false },
    [PC_LAB_DAC_XTAL_IBIAS] = { "lab_dac_xtal_ibias",  4,    4,    2,   6,   2, PC_LAB_NEXT,   false },
    [PC_LAB_IMU_SCALE_PM]   = { "lab_imu_scale_pm",    1000, 1000, 800, 1250, 1, PC_LAB_LIVE,  false },
    [PC_LAB_USB_SUSPEND_MS] = { "lab_usb_suspend_ms",  2000, 2000, 500, 30000, 1, PC_LAB_LIVE, true },
    [PC_LAB_LCD_PSRAM_DMA]  = { "lab_lcd_psram_dma",   0,    0,    0,   1,   1, PC_LAB_REBOOT, false },   /* FakePod: the panel's SPI DMA reads the PSRAM frame directly (no bounce copy, no internal buffers) */
    [PC_LAB_LCD_QUEUE]      = { "lab_lcd_queue",       2,    2,    2,   6,   1, PC_LAB_REBOOT, false },   /* FakePod: the panel's SPI transaction queue depth */
    [PC_LAB_LCD_CHUNK_ROWS] = { "lab_lcd_chunk_rows",  16,   16,   16,  128, 16, PC_LAB_REBOOT, false },  /* FakePod: rows of the panel per SPI transaction (fewer transactions per frame) */
};

const pc_lab_def_t *pc_lab_def(int i) { return i >= 0 && i < PC_LAB_N ? &T[i] : NULL; }

int pc_lab_find(const char *key)
{
    if (!key) return -1;
    if (!strncmp(key, "lab_", 4)) key += 4;
    for (int i = 0; i < PC_LAB_N; i++) if (!strcmp(T[i].key + 4, key)) return i;
    return -1;
}

int pc_lab_default(int i, bool round) { return i < 0 || i >= PC_LAB_N ? 0 : round ? T[i].def_round : T[i].def_square; }

bool pc_lab_ok(int i, int v)
{
    if (i < 0 || i >= PC_LAB_N) return false;
    const pc_lab_def_t *d = &T[i];
    if (v == 0 && d->zero_ok) return true;
    return v >= d->min && v <= d->max && (v - d->min) % d->step == 0;
}

const char *pc_lab_when_name(int when) { return when == PC_LAB_LIVE ? "live" : when == PC_LAB_NEXT ? "next track" : "reboot"; }

bool pc_lab_parse_int(const char *s, int *v)
{
    if (!s || !*s) return false;
    char *e;
    long n = strtol(s, &e, 10);
    if (*e || n < -1000000 || n > 1000000) return false;
    *v = (int)n;
    return true;
}

int pc_lab_lcd_internal_bytes(int psram_dma, int queue, int rows, int hres)
{
    return psram_dma ? 0 : (queue + 1) * rows * hres * 2;
}
bool pc_lab_lcd_ok(int psram_dma, int queue, int rows, int hres)
{
    return pc_lab_lcd_internal_bytes(psram_dma, queue, rows, hres) <= PC_LAB_LCD_INTERNAL_MAX;
}
