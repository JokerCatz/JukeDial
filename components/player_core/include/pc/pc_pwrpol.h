// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* FakePod power policy (FakePod only: the round board has no PMU and gets none of this), pure and host-tested. Three small decisions:
 *  - auto power-off: idle = nothing playing AND no input for N minutes (setting "autooff": 0 off, 10, 30, 60; default 30), never while external power is present;
 *  - low battery: boot refusal below 2 %, a shutdown after 60 s below 5 % (all while not on external power), one warning at 15 %;
 *  - the charge-only boot: the AXP2101 power-on source says "USB plug" and not "power key" (register 0x20: bit2 VBUS insert, bit0 key; NightPlayer (c) 2026 ZYFDroid, MIT, measured
 *    on the real board: 0x20 = 0x04 after a plug-in boot).
 * Nothing here touches hardware. */
#define PC_APO_DEFAULT_MIN 30
#define PC_LOWBAT_BOOT_PCT 2
#define PC_LOWBAT_RUN_PCT 5
#define PC_LOWBAT_RUN_MS 60000u
#define PC_LOWBAT_WARN_PCT 15
#define PC_LOWBAT_REARM_PCT 20

int  pc_apo_norm(int minutes);                 /* a stored value to one of 0 / 10 / 30 / 60 (anything else: the default) */
int  pc_apo_step(int minutes, int d);          /* next value in the list 0, 10, 30, 60 (wraps), d signed */
typedef struct { uint32_t quiet_ms; } pc_apo_t;
/* once per tick: dt_ms since the last call; playing: any source sounds; external: VBUS present; input_idle_ms: since the last touch / key. True = power off now. */
bool pc_apo_tick(pc_apo_t *s, int minutes, uint32_t dt_ms, bool playing, bool external, uint32_t input_idle_ms);

/* OLED rules (the FakePod's AMOLED must never stay lit: BSP_PANEL_OLED; the round board's LCD keeps its own behaviour):
 *  - no screen-timeout choice is ever "never": a host whose idle limit is 0 (the instrument app: the screen never timed out) gets PC_OLED_IDLE_MAX_MS, and no limit is longer;
 *  - a touch that is held down for PC_OLED_PRESS_MAX_MS (a stuck panel, an object lying on the glass) no longer counts as input: the screen goes off;
 *  - on external power the auto power-off's idle time (setting, or 10 min when that is off) turns the SCREEN off instead of the unit (a safety net: the ordinary 60 s screen timeout is shorter). */
#define PC_OLED_IDLE_MAX_MS  600000u    /* 10 min: an instrument is played by touch (every touch restarts it), a shorter cut would be an annoyance while a score is read between phrases */
#define PC_OLED_PRESS_MAX_MS 120000u
uint32_t pc_oled_idle_limit(uint32_t limit_ms);       /* 0 (never) or above the maximum: the maximum */
bool pc_oled_press_stuck(uint32_t pressed_ms);
bool pc_apo_ext_screen_off(int minutes, bool external, uint32_t input_idle_ms);   /* true = turn the screen off now (external power present, idle for `minutes`; 0 = 10 min) */

enum { PC_LB_NONE = 0, PC_LB_WARN, PC_LB_SHUTDOWN };
typedef struct { uint32_t low_ms; bool warned; } pc_lowbat_t;
bool pc_lowbat_boot_refuse(int pct, bool external);   /* pct < 2 on battery alone: show the message and power off (a failed read, pct -1, never refuses) */
/* once per tick with the PMU's answer (pct < 0 = no reading / no battery: nothing happens and the timer restarts). SHUTDOWN comes once after 60 s of < 5 %. */
int  pc_lowbat_tick(pc_lowbat_t *s, int pct, bool external, uint32_t dt_ms);

bool pc_pwron_is_plug_only(uint8_t reg20);     /* VBUS-insert set and the power-key bit clear: the unit was switched on by the cable alone */
#ifdef __cplusplus
}
#endif
