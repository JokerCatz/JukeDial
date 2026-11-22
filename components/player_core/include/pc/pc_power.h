// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* USB vs battery from the BATT_ADC raw reading, with hysteresis (spec 4.8). */
typedef enum { PC_PWR_UNKNOWN = 0, PC_PWR_USB, PC_PWR_BATT } pc_power_t;

#define PC_PWR_USB_ABOVE  4000
#define PC_PWR_BATT_BELOW 3920

#define PC_PWR_SAMPLE_MS  250
#define PC_PWR_LOW_RUN    60   /* this many readings in a row below BATT_BELOW (15 s) before "battery" */

/* USB under load dips below the battery's high point, so one reading cannot tell them apart; only a long run of
 * low readings can (on USB the rail comes back above USB_ABOVE every few seconds, on battery never). */
typedef struct { pc_power_t state; uint16_t low_run; } pc_power_filt_t;

/* One reading (every PC_PWR_SAMPLE_MS). host = a computer on the debug port, which is USB power for sure.
 * Returns the new state. */
pc_power_t pc_power_step(pc_power_filt_t *f, int adc_raw, bool host);
uint32_t   pc_screen_timeout_ms(pc_power_t p);   /* USB 60 s, otherwise 20 s */
bool       pc_power_allows_heavy(pc_power_t p);  /* cover rotation, full effect chain */

#ifdef __cplusplus
}
#endif
