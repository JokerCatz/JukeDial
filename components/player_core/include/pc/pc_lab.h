// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
/* LAB: the run-time knobs of the test day. Every "if it looks / sounds wrong, try X" of the checklist that was a compile-time constant is a knob here:
 * set from the console (`lab <key> <value>`), stored in the card's config under "lab_<name>", defaults = the constants it replaces. This file is the ONE place of the
 * ranges (pure, host-tested); app/main/lab.c holds the values and the console. A value outside its range is refused, never clamped.
 * when: LIVE = used at the next event; NEXT = the next track start (the DAC chip is booted per track after a config change); REBOOT = read once at boot. */
enum { PC_LAB_LIVE = 0, PC_LAB_NEXT = 1, PC_LAB_REBOOT = 2 };
enum {
    PC_LAB_FLICK_EDGE, PC_LAB_FLICK_MS, PC_LAB_FLICK_START_MS, PC_LAB_FLICK_PCT, PC_LAB_FLICK_H_PCT,
    PC_LAB_NOWB_HIDE_MS,
    PC_LAB_KEY_DBL_MS, PC_LAB_KEY_HOLD_MS, PC_LAB_KEY_REP_MS,
    PC_LAB_TOUCH_X_NUM,
    PC_LAB_CO5300_C4, PC_LAB_CO5300_58,
    PC_LAB_DAC_PDN_PAIR, PC_LAB_DAC_HP_INV, PC_LAB_DAC_XTAL_IBIAS,
    PC_LAB_IMU_SCALE_PM,
    PC_LAB_USB_SUSPEND_MS,
    PC_LAB_LCD_PSRAM_DMA, PC_LAB_LCD_QUEUE, PC_LAB_LCD_CHUNK_ROWS,
    PC_LAB_N
};
typedef struct {
    const char *key;            /* the config key, "lab_..." */
    short def_round, def_square;   /* today's value on the round board / the square one */
    short min, max, step;       /* allowed: min..max, (v - min) % step == 0 */
    unsigned char when;         /* PC_LAB_LIVE / NEXT / REBOOT */
    bool zero_ok;               /* 0 is allowed besides min..max (0 = "never") */
} pc_lab_def_t;
const pc_lab_def_t *pc_lab_def(int i);                 /* NULL outside 0..PC_LAB_N-1 */
int  pc_lab_find(const char *key);                     /* with or without the "lab_" prefix; -1 unknown */
int  pc_lab_default(int i, bool round);
bool pc_lab_ok(int i, int v);                          /* v is allowed */
/* night3 D (FakePod panel flush): internal RAM the QSPI bounce buffers take while a frame is sent. The SPI driver copies each chunk of a PSRAM frame into an internal DMA buffer
 * (queue depth + 1 of them can exist at once); with psram_dma (SPI_TRANS_DMA_USE_PSRAM) it reads PSRAM directly: no bounce, no copy. rows: chunk = rows x hres x 2 bytes. */
int  pc_lab_lcd_internal_bytes(int psram_dma, int queue, int rows, int hres);
#define PC_LAB_LCD_INTERNAL_MAX (64 * 1024)                 /* the most bounce RAM a knob setting may ask for (today's 16 rows x queue 2 = 44 KB at 460 px) */
bool pc_lab_lcd_ok(int psram_dma, int queue, int rows, int hres);
const char *pc_lab_when_name(int when);                /* "live" / "next track" / "reboot" */
bool pc_lab_parse_int(const char *s, int *v);          /* the whole string is a decimal integer (optional sign) */
#ifdef __cplusplus
}
#endif
