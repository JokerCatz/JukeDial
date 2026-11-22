// SPDX-License-Identifier: LGPL-2.1-or-later
/* What the FakePod does not have (yet): the round board's haptic driver, buzzer, knob and battery ADC (the DAC is bsp_dac.c + the round board's bsp_audio.c; the battery is bsp_pmu.c).
 * Every call answers ESP_ERR_NOT_FOUND / NOT_SUPPORTED so the shared code takes its "no such part" paths. */
#include "bsp/bsp_battery.h"
#include "bsp/bsp_buzzer.h"
#include "bsp/bsp_haptic.h"
#include "bsp/bsp_knob.h"

esp_err_t bsp_haptic_init(uint8_t *status_out) { if (status_out) *status_out = 0; return ESP_ERR_NOT_FOUND; }
esp_err_t bsp_haptic_play(uint8_t effect_id) { (void)effect_id; return ESP_ERR_NOT_FOUND; }
esp_err_t bsp_haptic_stop(void) { return ESP_ERR_NOT_FOUND; }
esp_err_t bsp_haptic_set_clamp(uint8_t od_clamp) { (void)od_clamp; return ESP_ERR_NOT_FOUND; }
esp_err_t bsp_haptic_read_reg(uint8_t reg, uint8_t *val) { (void)reg; (void)val; return ESP_ERR_NOT_FOUND; }

esp_err_t bsp_buzzer_init(void) { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t bsp_buzzer_level(bool on) { (void)on; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t bsp_buzzer_tone(uint32_t hz, uint32_t ms) { (void)hz; (void)ms; return ESP_ERR_NOT_SUPPORTED; }

esp_err_t bsp_battery_init(void) { return ESP_ERR_NOT_SUPPORTED; }   /* the AXP2101's reads are bsp_pmu.c (power_svc) */
esp_err_t bsp_battery_read(int *raw, int *pin_mv, int *rail_mv) { (void)raw; (void)pin_mv; (void)rail_mv; return ESP_ERR_NOT_SUPPORTED; }
int       bsp_battery_atten(void) { return -1; }

/* no knob: the keys arrive through ui_knob_inject (keys_svc) */
static bsp_knob_hook_t s_hook;
static bool s_swap;
esp_err_t bsp_knob_start(lv_disp_t *disp, bool swap_ab, lv_indev_t **out_indev) { (void)disp; s_swap = swap_ab; if (out_indev) *out_indev = NULL; return ESP_OK; }
int32_t   bsp_knob_get_count(void) { return 0; }
int32_t   bsp_knob_get_edges(void) { return 0; }
void      bsp_knob_set_swap(bool swap) { s_swap = swap; }
bool      bsp_knob_get_swap(void) { return s_swap; }
lv_group_t *bsp_knob_group(void) { return NULL; }
void      bsp_knob_set_hook(bsp_knob_hook_t hook) { s_hook = hook; }
uint32_t  bsp_knob_trace_read(uint32_t from, bsp_knob_trace_t *out, uint32_t max) { (void)out; (void)max; return from; }
