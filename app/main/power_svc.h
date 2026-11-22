// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include "pc/pc_power.h"
void power_svc_start(void);          /* core 0 task, reads BATT_ADC every PC_PWR_SAMPLE_MS */
pc_power_t power_svc_state(void);
void power_svc_dump(void);           /* console "power": state, last raw, host, low_run, and the two raw histograms */
void power_svc_hist_reset(void);     /* console "power reset" */
/* F2: the FakePod's AXP2101 (BSP_HAS_PMU) answers a battery percentage and the charger state; the round board has only its rail ADC and answers -1 / false (quick settings then shows
 * the source only, as before). -1 also on a FakePod without a battery (the schematic shows no connector) or before the first read. */
int  power_svc_batt_pct(void);
bool power_svc_charging(void);
