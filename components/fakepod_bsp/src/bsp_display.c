// SPDX-License-Identifier: LGPL-2.1-or-later
#include <stdio.h>
#include "esp_attr.h"
#include "bsp/bsp_display.h"
#include "pc/pc_rotate.h"
#include "pc/pc_lcdclk.h"
#include "pc/pc_lab.h"
#include "bsp/bsp_geom.h"
#include "bsp/bsp_pins.h"
#include "bsp_co5300.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_lv_adapter.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define BSP_LCD_VCI_OFF_MS 50    /* AMOLED supply held low at boot (a clean cycle after a chip reset) */
#define BSP_LCD_VCI_ON_MS  120   /* supply up -> reset pulse */

/* CO5300 AMOLED (QSPI clock 40 MHz: the maker's firmware found 80 MHz draws an odd 1 px black vertical line at the left edge of a partial redraw): QSPI bus -> power (VCI_EN) -> reset -> init -> LVGL adapter (PSRAM, double buffer: the round board's profile) -> start. No backlight: the brightness is
 * the panel's command 0x51, 0 % = brightness 0 and display off. */
static const char *TAG = "bsp_display";

static esp_lcd_panel_io_handle_t s_io;
static esp_lcd_panel_handle_t s_panel;
/* the SPI driver bounces PSRAM frames through an internal DMA buffer of max_transfer_sz: small and fixed, as on the round board (spec 4.2). night3 D: the chunk (rows), the queue depth
 * and the direct-from-PSRAM DMA are lab knobs (bsp_lab_lcd); 0 = today's 16 rows, depth 2, bounce */
#define BSP_LCD_ROWS_DEFAULT 16
EXT_RAM_BSS_ATTR static int s_lab_psram, s_lab_queue, s_lab_rows;   /* PSRAM: no new internal RAM */
void bsp_lab_lcd(int psram_dma, int queue, int rows) { s_lab_psram = psram_dma; s_lab_queue = queue; s_lab_rows = rows; }

static lv_disp_t *s_disp;
static uint32_t s_task_stack;
static bool s_stack_psram;
static bool s_on;

void bsp_display_set_task_stack(uint32_t bytes, bool psram) { s_task_stack = bytes; s_stack_psram = psram; }

/* Brightness, and what "off" means for an AMOLED. Everything here shares the SPI bus with the pixel transfers, so it is sent inside the LVGL lock (the lock is recursive: the
 * UI calls this with it held). 0 %: brightness 0, DISPOFF (0x28), SLEEP IN (0x10): the driver IC's real low-power state. Out of it (maker's firmware, measured on this board):
 * SLEEP OUT + 120 ms + the whole init table again (a SOFT wake: the hardware reset on GPIO36, the flash-voltage strap, is pulsed once, in bsp_display_start_ex, and never on a wake; the maker's firmware wakes the same way), the panel RAM is then black, so the whole screen is drawn before the brightness comes up. */
esp_err_t bsp_display_backlight_set(uint8_t percent)
{
    if (!s_panel) return ESP_ERR_INVALID_STATE;
    if (percent > 100) percent = 100;
    const bool locked = s_disp && bsp_display_lock(-1);
    esp_err_t e = ESP_OK;
    if (percent == 0) {
        if (s_on) {
            const uint8_t z = 0;
            e = bsp_co5300_cmd(s_panel, 0x51, &z, 1);
            if (e == ESP_OK) e = bsp_co5300_cmd(s_panel, 0x28, NULL, 0);
            if (e == ESP_OK) e = bsp_co5300_cmd(s_panel, 0x10, NULL, 0);
            vTaskDelay(pdMS_TO_TICKS(10));
            if (e == ESP_OK) s_on = false;
        }
    } else {
        if (!s_on) {
            e = esp_lcd_panel_init(s_panel);               /* 0x11 + 120 ms + the init table, brightness 0 */
            if (e == ESP_OK) {
                s_on = true;
                if (s_disp) { lv_obj_invalidate(lv_screen_active()); lv_obj_invalidate(lv_layer_top()); lv_refr_now(s_disp); }
            }
        }
        if (e == ESP_OK) {
            const uint8_t v = (uint8_t)((255u * percent + 50u) / 100u);
            e = bsp_co5300_cmd(s_panel, 0x51, &v, 1);
        }
    }
    if (locked) bsp_display_unlock();
    return e;
}

/* The CO5300 takes a window only from an even column / row to an odd one: LVGL's dirty areas are widened before they are queued. */
static void round_area(lv_event_t *e)
{
    lv_area_t *a = lv_event_get_param(e);
    a->x1 &= ~1; a->y1 &= ~1;
    a->x2 |= 1;  a->y2 |= 1;
}

EXT_RAM_BSS_ATTR static int s_rotate;   /* PSRAM: no new internal RAM */
void bsp_display_set_rotation(int rotate) { s_rotate = pc_rotate_norm(rotate); }
int bsp_display_rotation(void) { return s_rotate; }
/* Before start: the experimental QSPI clock in MHz (cfg lcd_mhz, pc_lcdclk.h); 0 / not allowed = the board's default */
EXT_RAM_BSS_ATTR static int s_lcd_mhz;   /* PSRAM: no new internal RAM */
void bsp_display_set_clock_mhz(int mhz) { s_lcd_mhz = pc_lcdclk_norm(mhz, BSP_LCD_ROUND); }

esp_err_t bsp_display_start(lv_disp_t **out_disp) { return bsp_display_start_ex(out_disp, 80); }

esp_err_t bsp_display_start_ex(lv_disp_t **out_disp, uint8_t backlight_pct)
{
    if (out_disp) *out_disp = NULL;
    if (s_disp) { if (out_disp) *out_disp = s_disp; return ESP_OK; }

    /* the AMOLED supply: on, with the panel in reset-less idle for a moment before the reset pulse */
    { const gpio_config_t c = { .pin_bit_mask = 1ULL << BSP_PIN_LCD_VCI_EN, .mode = GPIO_MODE_OUTPUT };
      /* A clean supply cycle, then time for the rails before the reset pulse. Board 2026-11-22: with 20 ms here 3 of 5 soft reboots left the panel showing its
       * power-on RAM (first row white, dark pattern below) while the firmware drew normally; the init table sent again by a screen wake cured it, so the table had
       * reached a panel that was not ready. The battery keeps the panel alive across a chip reset, so the supply is taken low first: every boot starts the same. */
      gpio_set_level(BSP_PIN_LCD_VCI_EN, 0);
      ESP_RETURN_ON_ERROR(gpio_config(&c), TAG, "vci_en");
      gpio_set_level(BSP_PIN_LCD_VCI_EN, 0);
      vTaskDelay(pdMS_TO_TICKS(BSP_LCD_VCI_OFF_MS));
      gpio_set_level(BSP_PIN_LCD_VCI_EN, 1);
      vTaskDelay(pdMS_TO_TICKS(BSP_LCD_VCI_ON_MS)); }

    int lcd_psram = s_lab_psram, lcd_q = s_lab_queue ? s_lab_queue : 2, lcd_rows = s_lab_rows ? s_lab_rows : BSP_LCD_ROWS_DEFAULT;
    if (!pc_lab_lcd_ok(lcd_psram, lcd_q, lcd_rows, BSP_LCD_H_RES)) {
        printf("APP display lcd refused psram_dma=%d queue=%d rows=%d would take %d B of internal RAM (max %d): today's setting\n", lcd_psram, lcd_q, lcd_rows,
               pc_lab_lcd_internal_bytes(lcd_psram, lcd_q, lcd_rows, BSP_LCD_H_RES), PC_LAB_LCD_INTERNAL_MAX);
        lcd_psram = 0; lcd_q = 2; lcd_rows = BSP_LCD_ROWS_DEFAULT;
    }
    const int chunk_bytes = BSP_LCD_H_RES * lcd_rows * 2;
    printf("APP display lcd chunk_rows=%d chunk_bytes=%d queue=%d psram_dma=%d bounce_internal_max=%d\n", lcd_rows, chunk_bytes, lcd_q, lcd_psram,
           pc_lab_lcd_internal_bytes(lcd_psram, lcd_q, lcd_rows, BSP_LCD_H_RES));
    const spi_bus_config_t bus = {
        .sclk_io_num = BSP_PIN_LCD_SCL, .data0_io_num = BSP_PIN_LCD_D0, .data1_io_num = BSP_PIN_LCD_D1,
        .data2_io_num = BSP_PIN_LCD_D2, .data3_io_num = BSP_PIN_LCD_D3, .max_transfer_sz = chunk_bytes,
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(BSP_LCD_SPI_HOST, &bus, SPI_DMA_CH_AUTO), TAG, "spi bus");

    const int qspi_mhz = pc_lcdclk_norm(s_lcd_mhz ? s_lcd_mhz : pc_lcdclk_default(BSP_LCD_ROUND), BSP_LCD_ROUND);
    printf("APP display qspi_mhz=%d\n", qspi_mhz);
    const esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num = BSP_PIN_LCD_CS, .dc_gpio_num = -1, .spi_mode = 0, .pclk_hz = qspi_mhz * 1000 * 1000, .trans_queue_depth = (size_t)lcd_q,
        .lcd_cmd_bits = 32, .lcd_param_bits = 8, .flags = { .quad_mode = true, .psram_dma_direct = lcd_psram != 0 },
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)BSP_LCD_SPI_HOST, &io_cfg, &s_io), TAG, "panel io");
    ESP_RETURN_ON_ERROR(bsp_co5300_new(s_io, BSP_PIN_LCD_RST, BSP_LCD_X_GAP, &s_panel), TAG, "co5300");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "init");
    s_on = true;                                   /* the init sequence ends with display on, brightness 0 */
    {   /* the ONE place the panel's orientation is set: pc_rotate.h's table (rotate 0 = today's: no mirror) */
        const pc_rot_t r = pc_rotate_map(PC_ROT_SQUARE, s_rotate);
        ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(s_panel, r.p_mx, r.p_my), TAG, "mirror");
    }

    esp_lv_adapter_config_t acfg = ESP_LV_ADAPTER_DEFAULT_CONFIG();
    acfg.task_core_id = 0;   /* core 1 belongs to audio (spec 4.2) */
    if (s_task_stack) { acfg.task_stack_size = s_task_stack; acfg.stack_in_psram = s_stack_psram; }
    ESP_RETURN_ON_ERROR(esp_lv_adapter_init(&acfg), TAG, "adapter init");
    esp_lv_adapter_display_config_t dcfg = ESP_LV_ADAPTER_DISPLAY_SPI_WITH_PSRAM_DEFAULT_CONFIG(
        s_panel, s_io, BSP_LCD_H_RES, BSP_LCD_V_RES, ESP_LV_ADAPTER_ROTATE_0);
    s_disp = esp_lv_adapter_register_display(&dcfg);
    ESP_RETURN_ON_FALSE(s_disp, ESP_FAIL, TAG, "register display");
    lv_display_add_event_cb(s_disp, round_area, LV_EVENT_INVALIDATE_AREA, NULL);
    esp_lv_adapter_fps_stats_enable(s_disp, true);
    ESP_RETURN_ON_ERROR(esp_lv_adapter_start(), TAG, "adapter start");

    bsp_display_backlight_set(backlight_pct);
    if (out_disp) *out_disp = s_disp;
    return ESP_OK;
}

bool bsp_display_lock(int32_t timeout_ms) { return esp_lv_adapter_lock(timeout_ms) == ESP_OK; }
void bsp_display_unlock(void) { esp_lv_adapter_unlock(); }
esp_lcd_panel_handle_t bsp_display_panel(void) { return s_panel; }

/* any command makes esp_lcd wait for the queued colour transfers and free their bounce buffers (a NOP: nothing else changes, the panel may be dark) */
void bsp_display_release_dma(void) { if (s_panel) bsp_co5300_cmd(s_panel, 0x00, NULL, 0); }

void bsp_display_flush_stat(void) { bsp_co5300_flush_stat(); }

uint32_t bsp_display_fps(void)
{
    uint32_t fps = 0;
    if (!s_disp || esp_lv_adapter_get_fps(s_disp, &fps) != ESP_OK) return 0;
    return fps;
}
