// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include "esp_err.h"
/* The FakePod's PCF85063(A) real-time clock (I2C0 0x51, powered from the AXP2101 VRTC rail, INT unconnected; driver components/fakepod_bsp/src/bsp_rtc.c, the register <-> time
 * conversion is player_core's pc_rtc85063). The round board has none: every call answers "absent" (components/jc3636f118_bsp/src/bsp_rtc.c). The chip holds UTC.
 * Any task except an ISR (an I2C transfer with a mutex and a 30 ms timeout); never from the LVGL task when the call can be moved off it. */
bool      bsp_rtc_present(void);                              /* the chip answered at the first call (the probe also puts Control_1 in normal running, see the driver); cached */
esp_err_t bsp_rtc_get(struct tm *utc, bool *valid);           /* ESP_ERR_NOT_FOUND without the chip, ESP_FAIL on a failed read. *valid: the chip answered AND the oscillator-stop flag is clear AND every field is in range (utc is then filled) */
esp_err_t bsp_rtc_status(bool *os, struct tm *utc);           /* the same read, for the boot line: *os = the oscillator-stop flag; utc filled when the fields are in range (the return is ESP_OK) and ESP_ERR_INVALID_RESPONSE when they are not */
esp_err_t bsp_rtc_set(const struct tm *utc);                  /* writes the time (the seconds write clears the oscillator-stop flag), reads it back; ESP_ERR_INVALID_ARG outside 2000..2099, ESP_FAIL when the read-back differs */
void      bsp_rtc_dump(void);                                 /* console `rtc`: the registers 0x00..0x0A raw and the decoded time */
