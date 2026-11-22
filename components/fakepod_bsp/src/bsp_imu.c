// SPDX-License-Identifier: LGPL-2.1-or-later
#include "bsp/bsp_imu.h"
#include "bsp/bsp_i2c.h"
#include "bsp/bsp_pins.h"
#include "esp_check.h"

/* QMI8658C on the shared I2C0 (the round board's driver, this board's bus; both addresses probed) at 125 Hz output rate, +-2 g and +-512 dps. */
#define REG_WHO_AM_I 0x00
#define REG_CTRL1    0x02
#define REG_CTRL2    0x03
#define REG_CTRL3    0x04
#define REG_CTRL7    0x08
#define REG_TEMP_L   0x33
#define WHO_AM_I_VAL 0x05

static const char *TAG = "bsp_imu";
static i2c_master_dev_handle_t s_dev;

static esp_err_t rd(uint8_t reg, uint8_t *buf, size_t n) { return i2c_master_transmit_receive(s_dev, &reg, 1, buf, n, 50); }
static esp_err_t wr(uint8_t reg, uint8_t v) { uint8_t b[2] = {reg, v}; return i2c_master_transmit(s_dev, b, 2, 50); }

esp_err_t bsp_imu_init(uint8_t *who_am_i_out, uint8_t *addr_out)
{
    i2c_master_bus_handle_t bus = bsp_i2c_bus(BSP_I2C_IMU);
    ESP_RETURN_ON_FALSE(bus, ESP_ERR_INVALID_STATE, TAG, "i2c bus missing");
    uint8_t addr = 0;
    if (i2c_master_probe(bus, BSP_I2C_ADDR_IMU_PRIMARY, 50) == ESP_OK) addr = BSP_I2C_ADDR_IMU_PRIMARY;
    else if (i2c_master_probe(bus, BSP_I2C_ADDR_IMU_ALT, 50) == ESP_OK) addr = BSP_I2C_ADDR_IMU_ALT;
    if (addr_out) *addr_out = addr;
    if (!addr) return ESP_ERR_NOT_FOUND;
    if (!s_dev) {
        const i2c_device_config_t dc = { .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = addr, .scl_speed_hz = BSP_I2C_FREQ_HZ };
        ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &dc, &s_dev), TAG, "add device");
    }
    uint8_t who = 0;
    ESP_RETURN_ON_ERROR(rd(REG_WHO_AM_I, &who, 1), TAG, "who_am_i");
    if (who_am_i_out) *who_am_i_out = who;
    if (who != WHO_AM_I_VAL) return ESP_ERR_NOT_FOUND;
    ESP_RETURN_ON_ERROR(wr(REG_CTRL1, 0x40), TAG, "ctrl1");  /* register address auto-increment */
    ESP_RETURN_ON_ERROR(wr(REG_CTRL2, 0x06), TAG, "ctrl2");  /* accel +-2 g, 125 Hz */
    ESP_RETURN_ON_ERROR(wr(REG_CTRL3, 0x56), TAG, "ctrl3");  /* gyro +-512 dps, 125 Hz */
    return wr(REG_CTRL7, 0x03);                              /* enable accel + gyro */
}

esp_err_t bsp_imu_read(bsp_imu_sample_t *s)
{
    if (!s_dev || !s) return ESP_ERR_INVALID_STATE;
    uint8_t b[14];
    const esp_err_t e = rd(REG_TEMP_L, b, sizeof b);   /* silent: a sensor that drops off the bus must not flood the log at 50 Hz (imu_svc counts the failures) */
    if (e != ESP_OK) return e;
    int16_t v[7];
    for (int i = 0; i < 7; i++) v[i] = (int16_t)(b[2 * i] | (b[2 * i + 1] << 8));
    s->temp_c = (float)(int8_t)b[1] + (float)b[0] / 256.0f;
    s->ax = v[1] / 16384.0f; s->ay = v[2] / 16384.0f; s->az = v[3] / 16384.0f;
    s->gx = v[4] / 64.0f;    s->gy = v[5] / 64.0f;    s->gz = v[6] / 64.0f;
    return ESP_OK;
}
