// SPDX-License-Identifier: LGPL-2.1-or-later
#include "bsp/bsp_rtc.h"
#include <stdio.h>
#include "bsp/bsp_i2c.h"
#include "bsp/bsp_pins.h"
#include "driver/i2c_master.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "pc/pc_rtc85063.h"
#include "time/clock.h"

/* PCF85063(A) at 0x51 on I2C0 (the bus of the touch / PMU / IMU). Registers written, all of them (nothing else is ever written):
 *   0x00 Control_1, only at the probe and only when STOP or 12_24 is set (read-modify-write: those two bits cleared, CAP_SEL / CIE kept; the reset value 0x00 needs no write)
 *   0x04..0x0A seconds .. years, one burst, by bsp_rtc_set (seconds bit 7 = 0 clears the oscillator-stop flag)
 * CAP_SEL (load capacitance 7 / 12.5 pF) is left at the chip's value: the module's crystal is unknown. No clock-out, alarm or timer register is touched. */
static const char *TAG = "bsp_rtc";
static EXT_RAM_BSS_ATTR i2c_master_dev_handle_t s_dev;
static EXT_RAM_BSS_ATTR SemaphoreHandle_t s_lock;
static EXT_RAM_BSS_ATTR volatile int s_state;   /* 0 not probed, 1 probing, 2 present, 3 absent */

static esp_err_t rd(uint8_t reg, uint8_t *v, size_t n) { return i2c_master_transmit_receive(s_dev, &reg, 1, v, n, 30); }
static esp_err_t wr(uint8_t reg, const uint8_t *v, size_t n)
{
    uint8_t b[PC_RTC_BLOCK + 1];
    if (n > PC_RTC_BLOCK) return ESP_ERR_INVALID_ARG;
    b[0] = reg;
    for (size_t i = 0; i < n; i++) b[1 + i] = v[i];
    return i2c_master_transmit(s_dev, b, n + 1, 30);
}

static void probe(void)
{
    i2c_master_bus_handle_t bus = bsp_i2c_bus(BSP_I2C_TOUCH);
    uint8_t c1 = 0;
    s_lock = xSemaphoreCreateMutex();
    const i2c_device_config_t dc = { .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = PC_RTC_ADDR, .scl_speed_hz = BSP_I2C_FREQ_HZ };
    if (bus && s_lock && i2c_master_bus_add_device(bus, &dc, &s_dev) == ESP_OK && rd(PC_RTC_REG_CTRL1, &c1, 1) == ESP_OK) {
        const uint8_t run = pc_rtc85063_ctrl1_run(c1);
        if (run != c1) {                                          /* stopped or in 12 h mode: normal running, the rest of the register as it was */
            esp_err_t e = wr(PC_RTC_REG_CTRL1, &run, 1);
            uint8_t back = 0xFF;
            ESP_LOGW(TAG, "ctrl1 %02x -> %02x (%s)", c1, run, e == ESP_OK && rd(PC_RTC_REG_CTRL1, &back, 1) == ESP_OK && back == run ? "ok" : "failed");
        }
        s_state = 2;
    } else s_state = 3;
}

bool bsp_rtc_present(void)
{
    int expected = 0;
    if (__atomic_compare_exchange_n(&s_state, &expected, 1, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) probe();
    else while (s_state == 1) vTaskDelay(1);
    return s_state == 2;
}

static bool lock(void) { return xSemaphoreTake(s_lock, pdMS_TO_TICKS(100)) == pdTRUE; }

esp_err_t bsp_rtc_status(bool *os, struct tm *utc)
{
    if (!bsp_rtc_present()) return ESP_ERR_NOT_FOUND;
    uint8_t r[PC_RTC_BLOCK];
    if (!lock()) return ESP_FAIL;
    esp_err_t e = rd(PC_RTC_REG_SEC, r, sizeof r);
    xSemaphoreGive(s_lock);
    if (e != ESP_OK) return ESP_FAIL;
    struct tm t;
    const bool ok = pc_rtc85063_decode(r, &t, os);
    if (ok && utc) *utc = t;
    return ok ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

esp_err_t bsp_rtc_get(struct tm *utc, bool *valid)
{
    bool os = true;
    struct tm t;
    if (valid) *valid = false;
    esp_err_t e = bsp_rtc_status(&os, &t);
    if (e == ESP_ERR_NOT_FOUND) return e;
    if (e == ESP_FAIL) return e;
    if (e == ESP_OK && !os) { if (utc) *utc = t; if (valid) *valid = true; }
    return ESP_OK;                                                /* answered; *valid says whether the time can be trusted */
}

static int64_t epoch(const struct tm *t) { return clock_days_from_civil(t->tm_year + 1900, t->tm_mon + 1, t->tm_mday) * 86400 + t->tm_hour * 3600 + t->tm_min * 60 + t->tm_sec; }

esp_err_t bsp_rtc_set(const struct tm *utc)
{
    if (!bsp_rtc_present()) return ESP_ERR_NOT_FOUND;
    uint8_t r[PC_RTC_BLOCK];
    if (!pc_rtc85063_encode(utc, r)) return ESP_ERR_INVALID_ARG;
    if (!lock()) return ESP_FAIL;
    uint8_t back[PC_RTC_BLOCK];
    esp_err_t e = wr(PC_RTC_REG_SEC, r, sizeof r);
    if (e == ESP_OK) e = rd(PC_RTC_REG_SEC, back, sizeof back);
    xSemaphoreGive(s_lock);
    if (e != ESP_OK) return ESP_FAIL;
    struct tm t;
    bool os = true;
    int64_t d = pc_rtc85063_decode(back, &t, &os) ? epoch(&t) - epoch(utc) : 1000;
    if (os || d < 0 || d > 3) { ESP_LOGW(TAG, "set read-back differs (os=%d d=%lld)", os, (long long)d); return ESP_FAIL; }   /* the chip may have ticked a second or two */
    return ESP_OK;
}

void bsp_rtc_dump(void)
{
    if (!bsp_rtc_present()) { printf("APP rtc absent\n"); return; }
    uint8_t r[11] = { 0 };
    esp_err_t e = ESP_FAIL;
    if (lock()) { e = rd(0x00, r, sizeof r); xSemaphoreGive(s_lock); }
    if (e != ESP_OK) { printf("APP rtc read failed\n"); return; }
    printf("APP rtc raw %02x %02x %02x %02x | %02x %02x %02x %02x %02x %02x %02x\n", r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7], r[8], r[9], r[10]);
    struct tm t;
    bool os = false;
    const bool ok = pc_rtc85063_decode(r + 4, &t, &os);
    if (ok) printf("APP rtc ctrl1=%02x ctrl2=%02x os=%d wday=%d utc=%04d-%02d-%02dT%02d:%02d:%02dZ\n", r[0], r[1], os, r[8], t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
    else printf("APP rtc ctrl1=%02x ctrl2=%02x os=%d fields out of range\n", r[0], r[1], os);
}
