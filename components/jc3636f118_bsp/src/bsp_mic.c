// SPDX-License-Identifier: LGPL-2.1-or-later
#include "bsp/bsp_mic.h"
#include "bsp/bsp_pins.h"
#include "driver/i2s_pdm.h"
#include "esp_attr.h"
#include "esp_check.h"

static const char *TAG = "bsp_mic";
static i2s_chan_handle_t s_rx EXT_RAM_BSS_ATTR;   /* PSRAM: the tuner's mic handle costs no internal RAM (read from the LVGL task only) */

esp_err_t bsp_mic_start(uint32_t sample_rate)
{
    if (s_rx) return ESP_OK;
    i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(BSP_I2S_PORT_MIC, I2S_ROLE_MASTER);
    ESP_RETURN_ON_ERROR(i2s_new_channel(&cc, NULL, &s_rx), TAG, "new channel");
    i2s_pdm_rx_config_t pc = {
        .clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG(sample_rate),
        .slot_cfg = I2S_PDM_RX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = { .clk = BSP_PIN_MIC_CLK, .din = BSP_PIN_MIC_DATA, .invert_flags = { .clk_inv = false } },
    };
    esp_err_t e = i2s_channel_init_pdm_rx_mode(s_rx, &pc);
    if (e == ESP_OK) e = i2s_channel_enable(s_rx);
    if (e != ESP_OK) { i2s_del_channel(s_rx); s_rx = NULL; }
    return e;
}

esp_err_t bsp_mic_read(int16_t *buf, size_t samples, size_t *got, uint32_t timeout_ms)
{
    ESP_RETURN_ON_FALSE(s_rx, ESP_ERR_INVALID_STATE, TAG, "not started");
    size_t bytes = 0;
    esp_err_t e = i2s_channel_read(s_rx, buf, samples * 2, &bytes, timeout_ms);
    if (got) *got = bytes / 2;
    return e;
}

esp_err_t bsp_mic_stop(void)
{
    if (!s_rx) return ESP_OK;
    i2s_channel_disable(s_rx);
    esp_err_t e = i2s_del_channel(s_rx);
    s_rx = NULL;
    return e;
}
