// SPDX-License-Identifier: LGPL-2.1-or-later
/* Fakes power_svc (battery ADC: USB or battery). */
/* Behaviour: USB or battery from SIM_STATE (batt=<0-100>, charging=<0|1>): on USB when charging=1 or when neither key is given (the board baselines were taken on USB), else on battery. The percentage is kept for tests; the firmware has no battery gauge to show it. */
/* Empty: start (no task, no ADC). */
#include "power_svc.h"
#include "sim.h"

void power_svc_start(void) {}
pc_power_t power_svc_state(void)
{
    int charging = sim_state_int("charging", -1), batt = sim_state_int("batt", -1);
    if (charging == 1 || (charging < 0 && batt < 0)) return PC_PWR_USB;
    return PC_PWR_BATT;
}
/* F2: the percentage only where the real board has a gauge (the square build = the FakePod's AXP2101, BSP_HAS_PMU); the round board answers none, as its firmware does. Sim state batt=<0-100>; none without it
 * (a FakePod without a battery). charging=1 also marks the charger. */
#include "bsp/bsp_geom.h"
#ifndef BSP_HAS_PMU
#define BSP_HAS_PMU 0
#endif
int power_svc_batt_pct(void) { int b = sim_state_int("batt", -1); return BSP_HAS_PMU && b >= 0 ? (b > 100 ? 100 : b) : -1; }
bool power_svc_charging(void) { return BSP_HAS_PMU && sim_state_int("charging", 0) == 1; }
