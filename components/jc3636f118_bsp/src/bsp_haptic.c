// SPDX-License-Identifier: LGPL-2.1-or-later
#include "bsp/bsp_haptic.h"
#include "bsp/bsp_i2c.h"
#include "bsp/bsp_pins.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define REG_STATUS    0x00
#define REG_MODE      0x01
#define REG_LIBRARY   0x03
#define REG_WAVESEQ1  0x04
#define REG_WAVESEQ2  0x05
#define REG_GO        0x0C
#define REG_FEEDBACK  0x1A
#define REG_CONTROL3  0x1D
#define DEVICE_ID_DRV2605L 7

static const char *TAG = "bsp_haptic";
static i2c_master_dev_handle_t s_dev;
/* play = 3 writes: one task's stop / clamp must not land in the middle of another's play (UI taps, the
 * instrument's beat task, the esp_timer stop). Short wait; a busy driver just skips this one. */
static SemaphoreHandle_t s_mx;
#define LOCK() do { if (s_mx && xSemaphoreTake(s_mx, pdMS_TO_TICKS(5)) != pdTRUE) return ESP_ERR_TIMEOUT; } while (0)
#define UNLOCK() do { if (s_mx) xSemaphoreGive(s_mx); } while (0)

esp_err_t bsp_haptic_read_reg(uint8_t reg, uint8_t *val)
{
    ESP_RETURN_ON_FALSE(s_dev, ESP_ERR_INVALID_STATE, TAG, "not init");
    return i2c_master_transmit_receive(s_dev, &reg, 1, val, 1, 50);
}

static esp_err_t wr(uint8_t reg, uint8_t val)
{
    uint8_t b[2] = {reg, val};
    return i2c_master_transmit(s_dev, b, 2, 50);
}

static esp_err_t rmw(uint8_t reg, uint8_t set_mask)
{
    uint8_t v;
    ESP_RETURN_ON_ERROR(bsp_haptic_read_reg(reg, &v), TAG, "read 0x%02x", reg);
    return wr(reg, v | set_mask);
}

esp_err_t bsp_haptic_init(uint8_t *status_out)
{
    i2c_master_bus_handle_t bus = bsp_i2c_bus(BSP_I2C_TOUCH);
    ESP_RETURN_ON_FALSE(bus, ESP_ERR_INVALID_STATE, TAG, "i2c bus missing");
    if (i2c_master_probe(bus, BSP_I2C_ADDR_HAPTIC, 50) != ESP_OK) return ESP_ERR_NOT_FOUND;
    if (!s_mx) s_mx = xSemaphoreCreateMutex();
    if (!s_dev) {
        const i2c_device_config_t dc = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = BSP_I2C_ADDR_HAPTIC,
            .scl_speed_hz = BSP_I2C_FREQ_HZ,
        };
        ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &dc, &s_dev), TAG, "add device");
    }
    uint8_t st = 0;
    ESP_RETURN_ON_ERROR(bsp_haptic_read_reg(REG_STATUS, &st), TAG, "status");
    if (status_out) *status_out = st;
    if ((st >> 5) != DEVICE_ID_DRV2605L) return ESP_ERR_NOT_FOUND;
    ESP_RETURN_ON_ERROR(wr(REG_MODE, 0x00), TAG, "mode");          /* out of standby, internal trigger */
    ESP_RETURN_ON_ERROR(rmw(REG_FEEDBACK, 0x80), TAG, "lra");      /* N_ERM_LRA = LRA */
    ESP_RETURN_ON_ERROR(wr(REG_LIBRARY, 6), TAG, "library");       /* LRA library */
    ESP_RETURN_ON_ERROR(rmw(REG_CONTROL3, 0x01), TAG, "open loop"); /* LRA_OPEN_LOOP */
    return ESP_OK;
}

esp_err_t bsp_haptic_play(uint8_t effect_id)
{
    ESP_RETURN_ON_FALSE(s_dev, ESP_ERR_INVALID_STATE, TAG, "not init");
    ESP_RETURN_ON_FALSE(effect_id >= 1 && effect_id <= 123, ESP_ERR_INVALID_ARG, TAG, "effect");
    LOCK();
    esp_err_t e = wr(REG_WAVESEQ1, effect_id);
    if (e == ESP_OK) e = wr(REG_WAVESEQ2, 0);
    if (e == ESP_OK) e = wr(REG_GO, 1);
    UNLOCK();
    return e;
}

esp_err_t bsp_haptic_set_clamp(uint8_t od_clamp)   /* reg 0x17: open-loop drive limit, ~21.3 mV a step */
{
    ESP_RETURN_ON_FALSE(s_dev, ESP_ERR_INVALID_STATE, TAG, "not init");
    LOCK();
    esp_err_t e = wr(0x17, od_clamp);
    UNLOCK();
    return e;
}

esp_err_t bsp_haptic_stop(void)
{
    ESP_RETURN_ON_FALSE(s_dev, ESP_ERR_INVALID_STATE, TAG, "not init");
    LOCK();
    esp_err_t e = wr(REG_GO, 0);
    UNLOCK();
    return e;
}
