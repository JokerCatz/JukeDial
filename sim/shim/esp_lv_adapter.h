// SPDX-License-Identifier: LGPL-2.1-or-later
/* The three FreeType entry points of esp_lvgl_adapter that font.c uses (same names and types as the real header). */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "lvgl.h"
typedef void *esp_lv_adapter_ft_font_handle_t;
typedef enum {
    ESP_LV_ADAPTER_FT_FONT_STYLE_NORMAL = 0,
    ESP_LV_ADAPTER_FT_FONT_STYLE_ITALIC = 1,
    ESP_LV_ADAPTER_FT_FONT_STYLE_BOLD = 2,
    ESP_LV_ADAPTER_FT_FONT_STYLE_BOLD_ITALIC = 3,
} esp_lv_adapter_ft_font_style_t;
typedef struct {
    const char *name;
    uint16_t size;
    esp_lv_adapter_ft_font_style_t style;
    const void *mem;
    size_t mem_size;
} esp_lv_adapter_ft_font_config_t;
esp_err_t esp_lv_adapter_ft_font_init(const esp_lv_adapter_ft_font_config_t *config, esp_lv_adapter_ft_font_handle_t *handle);
const lv_font_t *esp_lv_adapter_ft_font_get(esp_lv_adapter_ft_font_handle_t handle);
esp_err_t esp_lv_adapter_ft_font_deinit(esp_lv_adapter_ft_font_handle_t handle);
