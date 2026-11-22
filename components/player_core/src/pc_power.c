// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_power.h"

pc_power_t pc_power_step(pc_power_filt_t *f, int adc_raw, bool host)
{
    if (host || adc_raw > PC_PWR_USB_ABOVE) {
        f->low_run = 0;
        f->state = PC_PWR_USB;
    } else if (adc_raw >= PC_PWR_BATT_BELOW) {
        f->low_run = 0;                                   /* middle band: no change */
        if (f->state == PC_PWR_UNKNOWN) f->state = PC_PWR_BATT;
    } else {
        if (f->low_run < UINT16_MAX) f->low_run++;
        if (f->state == PC_PWR_UNKNOWN || f->low_run >= PC_PWR_LOW_RUN) f->state = PC_PWR_BATT;   /* unsure at boot: the frugal case, at once */
    }
    return f->state;
}

uint32_t pc_screen_timeout_ms(pc_power_t p) { return p == PC_PWR_USB ? 60000u : 20000u; }

bool pc_power_allows_heavy(pc_power_t p) { return p == PC_PWR_USB; }
