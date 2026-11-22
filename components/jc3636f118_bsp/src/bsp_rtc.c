// SPDX-License-Identifier: LGPL-2.1-or-later
#include "bsp/bsp_rtc.h"
#include <stdio.h>
/* The round board has no RTC (the time is lost at power-off): every call answers "absent". */
bool bsp_rtc_present(void) { return false; }
esp_err_t bsp_rtc_get(struct tm *utc, bool *valid) { (void)utc; if (valid) *valid = false; return ESP_ERR_NOT_FOUND; }
esp_err_t bsp_rtc_status(bool *os, struct tm *utc) { (void)utc; if (os) *os = false; return ESP_ERR_NOT_FOUND; }
esp_err_t bsp_rtc_set(const struct tm *utc) { (void)utc; return ESP_ERR_NOT_FOUND; }
void bsp_rtc_dump(void) { printf("APP rtc absent\n"); }
