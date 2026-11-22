// SPDX-License-Identifier: LGPL-2.1-or-later
#include "bsp/bsp_battery.h"
#include "bsp/bsp_math.h"
#include "bsp/bsp_pins.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_check.h"

static const char *TAG = "bsp_batt";
static adc_oneshot_unit_handle_t s_unit;
static adc_channel_t s_chan;
static adc_cali_handle_t s_cali;
static int s_atten = -1;

esp_err_t bsp_battery_init(void)
{
    if (s_unit) return ESP_OK;
    adc_unit_t unit;
    ESP_RETURN_ON_ERROR(adc_oneshot_io_to_channel(BSP_PIN_BATT_ADC, &unit, &s_chan), TAG, "io->channel");
    const adc_oneshot_unit_init_cfg_t uc = { .unit_id = unit };
    ESP_RETURN_ON_ERROR(adc_oneshot_new_unit(&uc, &s_unit), TAG, "unit");
    /* esp32s31 has a single attenuation (SOC_ADC_ATTEN_NUM 1); try it first to avoid error logs. */
    static const adc_atten_t tries[] = { ADC_ATTEN_DB_0, ADC_ATTEN_DB_2_5, ADC_ATTEN_DB_6, ADC_ATTEN_DB_12 };
    esp_err_t e = ESP_FAIL;
    for (size_t i = 0; i < sizeof tries / sizeof tries[0] && e != ESP_OK; i++) {
        const adc_oneshot_chan_cfg_t cc = { .atten = tries[i], .bitwidth = ADC_BITWIDTH_DEFAULT };
        e = adc_oneshot_config_channel(s_unit, s_chan, &cc);
        if (e == ESP_OK) s_atten = tries[i];
    }
    ESP_RETURN_ON_ERROR(e, TAG, "config channel");
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    const adc_cali_curve_fitting_config_t cf = { .unit_id = unit, .chan = s_chan, .atten = s_atten, .bitwidth = ADC_BITWIDTH_DEFAULT };
    if (adc_cali_create_scheme_curve_fitting(&cf, &s_cali) != ESP_OK) s_cali = NULL;
#endif
    return ESP_OK;
}

int bsp_battery_atten(void) { return s_atten; }

esp_err_t bsp_battery_read(int *raw, int *pin_mv, int *rail_mv)
{
    ESP_RETURN_ON_FALSE(s_unit, ESP_ERR_INVALID_STATE, TAG, "not init");
    int r = 0;
    ESP_RETURN_ON_ERROR(adc_oneshot_read(s_unit, s_chan, &r), TAG, "read");
    int mv = -1;
    if (s_cali && adc_cali_raw_to_voltage(s_cali, r, &mv) != ESP_OK) mv = -1;
    if (raw) *raw = r;
    if (pin_mv) *pin_mv = mv;
    if (rail_mv) *rail_mv = mv < 0 ? -1 : bsp_math_rail_mv_from_pin_mv(mv);
    return ESP_OK;
}
