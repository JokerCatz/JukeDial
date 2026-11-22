// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include "bsp/bsp_geom.h"
/* The FakePod's charge-only boot (FakePod only). When the AXP2101's power-on source says the cable (and not the power key) switched the unit on, the player does not boot:
 * a minimal mode shows the battery percent and the charging state, the screen dims after a few seconds, and the power key starts the real boot. No audio, no Bluetooth, no Wi-Fi, no library.
 * The "full boot decided" mark lives in RTC memory (survives a software restart, not a power-off), so the restart that follows the power key does not come back here. The boot-loop guard
 * runs before this (3 crashes = safe mode as always). On the round board charge_mode_wanted() is false and nothing of it exists. */
typedef struct { int pct; bool charging, full, plugged, lowbat; } charge_status_t;
bool charge_mode_wanted(void);       /* app_main, after the recovery window: true = run the charge mode (never returns) */
void charge_mode_run(void) __attribute__((noreturn));
void charge_mode_status(charge_status_t *s);   /* the page's model (the PMU poll of the charge mode; the simulator's state keys batt= / charging= / lowbat=) */
void charge_mode_set_lowbat(int pct);          /* the low-battery boot refusal reuses the page with its own wording (pct = the reading) */
void charge_mode_start_player(void);           /* the page's first cell (and the power key): mark "full boot", restart */
void charge_mode_power_off(void);              /* the page's second cell: the PMU switches off */
