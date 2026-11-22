// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include "bsp/bsp_geom.h"
#include "cfg.h"
#include "pc/pc_pwrpol.h"
/* The FakePod's power policy: auto power-off, the low-battery rules. FakePod ONLY: the round board has no PMU and gets none of it (BSP_HAS_PMU = 0: no setting is
 * shown, no timer runs, nothing is warned, power_pol_boot_check() is false). The decisions are pc_pwrpol.c (host-tested); this is the glue: LVGL timer, the power sequence, the messages. */
static inline bool power_pol_available(void) { return BSP_HAS_PMU != 0; }
static inline int  power_pol_autooff_min(void) { return pc_apo_norm(cfg_int("autooff", PC_APO_DEFAULT_MIN)); }   /* 0 off, 10, 30, 60 */
static inline void power_pol_autooff_step(int d) { cfg_set_int("autooff", pc_apo_step(power_pol_autooff_min(), d)); }
void power_pol_start(void);        /* LVGL lock held, after ui_start: the once-a-second check (auto power-off, 15 % warning, 60 s below 5 %) */
bool power_pol_boot_check(void);   /* LVGL lock held, after ui_start, before any service: true = below 2 % on battery: the page is up and the unit powers off in 5 s; the caller must not go on */
void power_pol_off(const char *msg, bool warn_sound);   /* LVGL task: the message, the place saved, the DAC down the pop-free way, the card unmounted, the PMU off */
