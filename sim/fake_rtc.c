// SPDX-License-Identifier: LGPL-2.1-or-later
/* Fakes the FakePod's PCF85063 (bsp/bsp_rtc.h) in virtual time. State: SIM_STATE "rtc=absent|invalid|YYYY-MM-DDTHH:MM[:SS]" (default absent) or the console `simrtc <same>`; `simrtc`
 * alone prints it. A written time (bsp_rtc_set) makes the chip valid and it then runs with the simulator's clock. "invalid" = the oscillator-stop flag set (a fresh or flat chip). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "bsp/bsp_rtc.h"
#include "sim.h"

enum { R_UNSET, R_ABSENT, R_OS, R_VALID };
static int s_mode = R_UNSET;
static int64_t s_base_s, s_base_us;   /* the chip's UTC at the virtual time s_base_us */

static int64_t chip_now_s(void) { return s_base_s + (sim_now_us() - s_base_us) / 1000000; }
static bool parse(const char *a)
{
    struct tm tm = { 0 };
    int n;
    double sec = 0;
    while (*a == ' ') a++;
    if (!strcmp(a, "absent")) { s_mode = R_ABSENT; return true; }
    if (!strcmp(a, "invalid")) { s_mode = R_OS; s_base_s = 946684800; s_base_us = sim_now_us(); return true; }   /* 2000-01-01: what a chip that lost its power shows */
    n = sscanf(a, "%d-%d-%d%*1[T ]%d:%d:%lf", &tm.tm_year, &tm.tm_mon, &tm.tm_mday, &tm.tm_hour, &tm.tm_min, &sec);
    if (n < 5 || tm.tm_mon < 1 || tm.tm_mon > 12) return false;
    tm.tm_year -= 1900; tm.tm_mon -= 1; tm.tm_sec = (int)sec;
    s_base_s = (int64_t)timegm(&tm); s_base_us = sim_now_us(); s_mode = R_VALID;
    return true;
}
static void init(void)
{
    if (s_mode != R_UNSET) return;
    s_mode = R_ABSENT;
    const char *v = sim_state_str("rtc", NULL);
    if (v) parse(v);
}
bool fake_rtc_cmd(const char *arg)
{
    init();
    if (*arg && !parse(arg)) { printf("SIM simrtc usage: simrtc absent|invalid|YYYY-MM-DDTHH:MM[:SS]\n"); return true; }
    time_t t = (time_t)chip_now_s();
    struct tm g;
    gmtime_r(&t, &g);
    printf("SIM simrtc %s %04d-%02d-%02dT%02d:%02d:%02d\n", s_mode == R_ABSENT ? "absent" : s_mode == R_OS ? "invalid" : "valid", g.tm_year + 1900, g.tm_mon + 1, g.tm_mday, g.tm_hour, g.tm_min, g.tm_sec);
    return true;
}

bool bsp_rtc_present(void) { init(); return s_mode != R_ABSENT; }
esp_err_t bsp_rtc_status(bool *os, struct tm *utc)
{
    if (!bsp_rtc_present()) return ESP_ERR_NOT_FOUND;
    if (os) *os = s_mode == R_OS;
    time_t t = (time_t)chip_now_s();
    if (utc) gmtime_r(&t, utc);
    return ESP_OK;
}
esp_err_t bsp_rtc_get(struct tm *utc, bool *valid)
{
    bool os = true;
    struct tm t;
    if (valid) *valid = false;
    esp_err_t e = bsp_rtc_status(&os, &t);
    if (e != ESP_OK) return e;
    if (!os) { if (utc) *utc = t; if (valid) *valid = true; }
    return ESP_OK;
}
esp_err_t bsp_rtc_set(const struct tm *utc)
{
    if (!bsp_rtc_present()) return ESP_ERR_NOT_FOUND;
    if (utc->tm_year < 100 || utc->tm_year > 199) return ESP_ERR_INVALID_ARG;
    struct tm t = *utc;
    s_base_s = (int64_t)timegm(&t); s_base_us = sim_now_us(); s_mode = R_VALID;
    printf("SIM rtc written %04d-%02d-%02dT%02d:%02d:%02dZ\n", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
    return ESP_OK;
}
void bsp_rtc_dump(void)
{
    if (!bsp_rtc_present()) { printf("APP rtc absent\n"); return; }
    struct tm t;
    bool os;
    bsp_rtc_status(&os, &t);
    printf("APP rtc raw (simulator) ctrl1=00 os=%d utc=%04d-%02d-%02dT%02d:%02d:%02dZ\n", os, t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
}
