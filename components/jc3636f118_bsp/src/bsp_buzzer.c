// SPDX-License-Identifier: LGPL-2.1-or-later
#include "bsp/bsp_buzzer.h"
#include "bsp/bsp_pins.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define BZ_TIMER LEDC_TIMER_2
#define BZ_CH    LEDC_CHANNEL_1
static const char *TAG = "bsp_buzzer";

static esp_err_t as_gpio_low(void)
{
    const gpio_config_t c = { .pin_bit_mask = 1ULL << BSP_PIN_BUZZER, .mode = GPIO_MODE_OUTPUT };
    ESP_RETURN_ON_ERROR(gpio_config(&c), TAG, "gpio");
    return gpio_set_level(BSP_PIN_BUZZER, 0);
}

esp_err_t bsp_buzzer_init(void) { return as_gpio_low(); }

esp_err_t bsp_buzzer_level(bool on)
{
    ESP_RETURN_ON_ERROR(as_gpio_low(), TAG, "gpio");
    return gpio_set_level(BSP_PIN_BUZZER, on ? 1 : 0);
}

esp_err_t bsp_buzzer_tone(uint32_t hz, uint32_t ms)
{
    ESP_RETURN_ON_FALSE(hz >= 100 && hz <= 20000, ESP_ERR_INVALID_ARG, TAG, "hz");
    const ledc_timer_config_t t = {
        .speed_mode = LEDC_LOW_SPEED_MODE, .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = BZ_TIMER, .freq_hz = hz, .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&t), TAG, "timer");
    const ledc_channel_config_t c = {
        .gpio_num = BSP_PIN_BUZZER, .speed_mode = LEDC_LOW_SPEED_MODE, .channel = BZ_CH,
        .timer_sel = BZ_TIMER, .duty = 512, .hpoint = 0,
    };
    ESP_RETURN_ON_ERROR(ledc_channel_config(&c), TAG, "channel");
    vTaskDelay(pdMS_TO_TICKS(ms));
    ledc_stop(LEDC_LOW_SPEED_MODE, BZ_CH, 0);
    return as_gpio_low();
}
