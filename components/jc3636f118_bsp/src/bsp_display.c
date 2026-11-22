// SPDX-License-Identifier: LGPL-2.1-or-later
#include <stdio.h>
#include "esp_attr.h"
#include "bsp/bsp_display.h"
#include "bsp/bsp_pins.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_st77916.h"
#include "esp_log.h"
#include "esp_lv_adapter.h"
#include "pc/pc_rotate.h"
#include "pc/pc_lcdclk.h"
#include "bsp/bsp_geom.h"

static const char *TAG = "bsp_display";

#include "bsp_st77916_init_cmds.inc"

#define BL_LEDC_TIMER   LEDC_TIMER_1
#define BL_LEDC_CH      LEDC_CHANNEL_0

static esp_lcd_panel_io_handle_t s_io;
static esp_lcd_panel_handle_t s_panel;
/* The SPI driver bounces PSRAM frames through an internal DMA buffer of max_transfer_sz;
 * keep it small and fixed so internal RAM use does not depend on init order (spec 4.2). */
#define BSP_LCD_DMA_CHUNK_BYTES (BSP_LCD_H_RES * 20 * 2)

static lv_disp_t *s_disp;
static uint32_t s_task_stack;    /* 0 = adapter default (8 KB internal) */
static bool s_stack_psram;
EXT_RAM_BSS_ATTR static int s_rotate;   /* PSRAM: no new internal RAM */

void bsp_display_set_rotation(int rotate) { s_rotate = pc_rotate_norm(rotate); }
int bsp_display_rotation(void) { return s_rotate; }
/* Before start: the experimental QSPI clock in MHz (cfg lcd_mhz, pc_lcdclk.h); 0 / not allowed = the board's default */
EXT_RAM_BSS_ATTR static int s_lcd_mhz;   /* PSRAM: no new internal RAM */
void bsp_display_set_clock_mhz(int mhz) { s_lcd_mhz = pc_lcdclk_norm(mhz, BSP_LCD_ROUND); }

void bsp_display_set_task_stack(uint32_t bytes, bool psram) { s_task_stack = bytes; s_stack_psram = psram; }

static esp_err_t backlight_init(void)
{
    const ledc_timer_config_t t = {
        .speed_mode = LEDC_LOW_SPEED_MODE, .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = BL_LEDC_TIMER, .freq_hz = 20000, .clk_cfg = LEDC_AUTO_CLK,
    };
    const ledc_channel_config_t c = {
        .gpio_num = BSP_PIN_LCD_BLK, .speed_mode = LEDC_LOW_SPEED_MODE, .channel = BL_LEDC_CH,
        .intr_type = LEDC_INTR_DISABLE, .timer_sel = BL_LEDC_TIMER, .duty = 0, .hpoint = 0,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&t), TAG, "ledc timer");
    return ledc_channel_config(&c);
}

esp_err_t bsp_display_backlight_set(uint8_t percent)
{
    if (percent > 100) percent = 100;
    uint32_t duty = (1023u * percent) / 100u;
    ESP_RETURN_ON_ERROR(ledc_set_duty(LEDC_LOW_SPEED_MODE, BL_LEDC_CH, duty), TAG, "duty");
    return ledc_update_duty(LEDC_LOW_SPEED_MODE, BL_LEDC_CH);
}

esp_err_t bsp_display_start(lv_disp_t **out_disp) { return bsp_display_start_ex(out_disp, 80); }

esp_err_t bsp_display_start_ex(lv_disp_t **out_disp, uint8_t backlight_pct)
{
    if (out_disp) *out_disp = NULL;
    if (s_disp) { if (out_disp) *out_disp = s_disp; return ESP_OK; }

    const spi_bus_config_t bus = ST77916_PANEL_BUS_QSPI_CONFIG(BSP_PIN_LCD_SCL, BSP_PIN_LCD_D0, BSP_PIN_LCD_D1,
                                                               BSP_PIN_LCD_D2, BSP_PIN_LCD_D3,
                                                               BSP_LCD_DMA_CHUNK_BYTES);
    ESP_RETURN_ON_ERROR(spi_bus_initialize(BSP_LCD_SPI_HOST, &bus, SPI_DMA_CH_AUTO), TAG, "spi bus");

    esp_lcd_panel_io_spi_config_t io_cfg = ST77916_PANEL_IO_QSPI_CONFIG(BSP_PIN_LCD_CS, NULL, NULL);
    const int qspi_mhz = pc_lcdclk_norm(s_lcd_mhz ? s_lcd_mhz : pc_lcdclk_default(BSP_LCD_ROUND), BSP_LCD_ROUND);
    printf("APP display qspi_mhz=%d\n", qspi_mhz);
    io_cfg.pclk_hz = qspi_mhz * 1000 * 1000;
    io_cfg.trans_queue_depth = 2;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)BSP_LCD_SPI_HOST, &io_cfg, &s_io), TAG, "panel io");

    st77916_vendor_config_t vendor = {
        .init_cmds = lcd_init_cmds,
        .init_cmds_size = sizeof(lcd_init_cmds) / sizeof(lcd_init_cmds[0]),
        .flags = { .use_qspi_interface = 1 },
    };
    const esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = BSP_PIN_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = &vendor,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st77916(s_io, &panel_cfg, &s_panel), TAG, "st77916");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "init");
    {   /* the ONE place the panel's orientation is set: pc_rotate.h's table (rotate 0 = the vendor mounting, mirror x + y, as it always was) */
        const pc_rot_t r = pc_rotate_map(PC_ROT_ROUND, s_rotate);
        ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(s_panel, r.p_mx, r.p_my), TAG, "mirror");
    }
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, true), TAG, "on");

    ESP_RETURN_ON_ERROR(backlight_init(), TAG, "backlight");

    esp_lv_adapter_config_t acfg = ESP_LV_ADAPTER_DEFAULT_CONFIG();
    acfg.task_core_id = 0;   /* core 1 belongs to audio (spec 4.2) */
    if (s_task_stack) { acfg.task_stack_size = s_task_stack; acfg.stack_in_psram = s_stack_psram; }
    ESP_RETURN_ON_ERROR(esp_lv_adapter_init(&acfg), TAG, "adapter init");
    esp_lv_adapter_display_config_t dcfg = ESP_LV_ADAPTER_DISPLAY_SPI_WITH_PSRAM_DEFAULT_CONFIG(
        s_panel, s_io, BSP_LCD_H_RES, BSP_LCD_V_RES, ESP_LV_ADAPTER_ROTATE_0);
    s_disp = esp_lv_adapter_register_display(&dcfg);
    ESP_RETURN_ON_FALSE(s_disp, ESP_FAIL, TAG, "register display");
    esp_lv_adapter_fps_stats_enable(s_disp, true);
    ESP_RETURN_ON_ERROR(esp_lv_adapter_start(), TAG, "adapter start");

    bsp_display_backlight_set(backlight_pct);
    if (out_disp) *out_disp = s_disp;
    return ESP_OK;
}

bool bsp_display_lock(int32_t timeout_ms) { return esp_lv_adapter_lock(timeout_ms) == ESP_OK; }
void bsp_display_unlock(void) { esp_lv_adapter_unlock(); }
esp_lcd_panel_handle_t bsp_display_panel(void) { return s_panel; }

/* any command makes esp_lcd wait for the queued colour transfers and free their bounce buffers; DISPON is
 * harmless while the panel is on (leak cycles 2026-11-11: the "screen off reads ~30 KB lower" was these) */
void bsp_display_release_dma(void) { if (s_panel) esp_lcd_panel_disp_on_off(s_panel, true); }

void bsp_display_flush_stat(void) {}

uint32_t bsp_display_fps(void)
{
    uint32_t fps = 0;
    if (!s_disp || esp_lv_adapter_get_fps(s_disp, &fps) != ESP_OK) return 0;
    return fps;
}
