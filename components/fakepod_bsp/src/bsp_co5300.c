// SPDX-License-Identifier: LGPL-2.1-or-later
#include <stdlib.h>
#include <sys/cdefs.h>
#include "bsp_co5300.h"
#include "bsp/bsp_lab.h"
#include "esp_attr.h"
#include "bsp/bsp_pins.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_lcd_panel_interface.h"
#include "esp_lcd_panel_commands.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define OPC_WRITE_CMD   0x02u
#define OPC_WRITE_COLOR 0x32u

static const char *TAG = "co5300";

typedef struct {
    esp_lcd_panel_t base;
    esp_lcd_panel_io_handle_t io;
    int reset_gpio, x_gap, y_gap;
    uint8_t madctl;
} co5300_t;

static esp_err_t tx_cmd(co5300_t *p, uint8_t cmd, const void *data, size_t n)
{
    return esp_lcd_panel_io_tx_param(p->io, (int)((OPC_WRITE_CMD << 24) | ((uint32_t)cmd << 8)), data, n);
}

static esp_err_t pnl_del(esp_lcd_panel_t *panel) { free(__containerof(panel, co5300_t, base)); return ESP_OK; }   /* the reset pin is left as it is: high (see bsp_pins.h) */

static esp_err_t pnl_reset(esp_lcd_panel_t *panel)
{
    co5300_t *p = __containerof(panel, co5300_t, base);
    if (p->reset_gpio >= 0) {
        gpio_set_level(p->reset_gpio, 0);
        vTaskDelay(pdMS_TO_TICKS(20));
        gpio_set_level(p->reset_gpio, 1);      /* ends high, always */
        vTaskDelay(pdMS_TO_TICKS(120));
    } else {
        ESP_RETURN_ON_ERROR(tx_cmd(p, LCD_CMD_SWRESET, NULL, 0), TAG, "swreset");
        vTaskDelay(pdMS_TO_TICKS(120));
    }
    return ESP_OK;
}

typedef struct { uint8_t cmd; uint8_t n; uint8_t d[4]; uint16_t delay_ms; } init_t;
/* Values marked NP below are facts taken from the working firmware of the same panel on the same S31 board: NightPlayer (c) 2026 ZYFDroid, MIT (see docs/licenses). */
static const init_t INIT[] = {
    { 0xFE, 1, { 0x00 }, 0 },                          /* user command set */
    { 0xC4, 1, { 0x80 }, 0 },                          /* NP: SPI mode control, sent right after FE 00 on every NP panel variant; the quad-pixel path (opcode 0x32 below) is only proven with it. Remove it first if the first image is garbled */
    { 0x35, 1, { 0x00 }, 0 },                          /* NP: tearing-effect output on (the TE line, GPIO35, is not read by us; harmless) */
    { 0x3A, 1, { 0x55 }, 0 },                          /* RGB565 */
    { 0x53, 1, { 0x20 }, 0 },
    { 0x51, 1, { 0x00 }, 0 },                          /* brightness 0 until the first frame is drawn */
    { 0x63, 1, { 0xFF }, 0 },
    { 0x2A, 4, { 0x00, 0x0A, 0x01, 0xD5 }, 0 },        /* columns 10..469 */
    { 0x2B, 4, { 0x00, 0x00, 0x01, 0xCB }, 0 },        /* rows 0..459 */
    { 0x11, 0, { 0 }, 120 },                           /* sleep out */
    { 0x29, 0, { 0 }, 20 },                            /* display on */
    { 0x58, 1, { 0x07 }, 10 },                         /* NP: written after display-on with a 10 ms wait, NP's comment calls it the normal-mode brightness value; the register is not in the vendor array and not documented for this IC, so it is taken over as NP has it (check together with 0x63 on the board day) */
};

EXT_RAM_BSS_ATTR static uint8_t s_lab_skip;   /* LAB: bit0 = skip the C4 80 line, bit1 = skip the 58 07 line (0 = the table as it is): PSRAM, no internal RAM */
void bsp_lab_co5300(int send_c4, int send_58) { s_lab_skip = (uint8_t)((send_c4 ? 0 : 1) | (send_58 ? 0 : 2)); }

static esp_err_t pnl_init(esp_lcd_panel_t *panel)
{
    co5300_t *p = __containerof(panel, co5300_t, base);
    for (size_t i = 0; i < sizeof INIT / sizeof INIT[0]; i++) {
        if ((INIT[i].cmd == 0xC4 && (s_lab_skip & 1)) || (INIT[i].cmd == 0x58 && (s_lab_skip & 2))) continue;   /* LAB lab_co5300_c4 / lab_co5300_58 */
        ESP_RETURN_ON_ERROR(tx_cmd(p, INIT[i].cmd, INIT[i].n ? INIT[i].d : NULL, INIT[i].n), TAG, "init cmd %02x", INIT[i].cmd);
        if (INIT[i].delay_ms) vTaskDelay(pdMS_TO_TICKS(INIT[i].delay_ms));
    }
    if (p->madctl) ESP_RETURN_ON_ERROR(tx_cmd(p, LCD_CMD_MADCTL, &p->madctl, 1), TAG, "madctl");   /* a soft wake runs this table again: the rotation is put back (rotate 0 sends nothing, as before) */
    return ESP_OK;
}

/* night3 D: where a flush spends its time (LVGL task only; PSRAM statics). cmd = the two polled CASET / RASET transactions; color = tx_color, which queues the chunks of the window and
 * returns when the last one is queued (the chunk copies into the bounce buffers and the waits for a free queue slot are inside it). Read and cleared by bsp_display_flush_stat. */
EXT_RAM_BSS_ATTR static struct { uint32_t n, px; int64_t cmd_us, color_us, color_max; } s_fl;
void bsp_co5300_flush_stat(void)
{
    const uint32_t n = s_fl.n ? s_fl.n : 1;
    const int64_t tot = s_fl.cmd_us + s_fl.color_us;
    printf("APP lcdflush calls=%lu px=%lu cmd_us_avg=%lu color_us_avg=%lu color_us_max=%lu panel_kb_per_ms_x10=%lu\n", (unsigned long)s_fl.n, (unsigned long)s_fl.px,
           (unsigned long)(s_fl.cmd_us / n), (unsigned long)(s_fl.color_us / n), (unsigned long)s_fl.color_max,
           (unsigned long)(tot ? (int64_t)s_fl.px * 2 * 10 / 1024 * 1000 / tot : 0));   /* KB sent per ms of queueing time, x10 (1 KB/ms = 1 MB/s) */
    s_fl.n = s_fl.px = 0; s_fl.cmd_us = s_fl.color_us = s_fl.color_max = 0;
}

static esp_err_t pnl_draw(esp_lcd_panel_t *panel, int x0, int y0, int x1, int y1, const void *color)
{
    co5300_t *p = __containerof(panel, co5300_t, base);
    x0 += p->x_gap; x1 += p->x_gap; y0 += p->y_gap; y1 += p->y_gap;
    const uint8_t ca[4] = { (uint8_t)(x0 >> 8), (uint8_t)x0, (uint8_t)((x1 - 1) >> 8), (uint8_t)(x1 - 1) };
    const uint8_t ra[4] = { (uint8_t)(y0 >> 8), (uint8_t)y0, (uint8_t)((y1 - 1) >> 8), (uint8_t)(y1 - 1) };
    const int64_t t0 = esp_timer_get_time();
    ESP_RETURN_ON_ERROR(tx_cmd(p, LCD_CMD_CASET, ca, 4), TAG, "caset");
    ESP_RETURN_ON_ERROR(tx_cmd(p, LCD_CMD_RASET, ra, 4), TAG, "raset");
    const size_t len = (size_t)(x1 - x0) * (size_t)(y1 - y0) * 2u;
    const int64_t t1 = esp_timer_get_time();
    const esp_err_t e = esp_lcd_panel_io_tx_color(p->io, (int)((OPC_WRITE_COLOR << 24) | ((uint32_t)LCD_CMD_RAMWR << 8)), color, len);
    const int64_t t2 = esp_timer_get_time();
    s_fl.n++; s_fl.px += (uint32_t)(len / 2); s_fl.cmd_us += t1 - t0; s_fl.color_us += t2 - t1;
    if (t2 - t1 > s_fl.color_max) s_fl.color_max = t2 - t1;
    return e;
}

static esp_err_t pnl_invert(esp_lcd_panel_t *panel, bool inv)
{
    co5300_t *p = __containerof(panel, co5300_t, base);
    return tx_cmd(p, inv ? LCD_CMD_INVON : LCD_CMD_INVOFF, NULL, 0);
}
static esp_err_t pnl_mirror(esp_lcd_panel_t *panel, bool mx, bool my)
{
    co5300_t *p = __containerof(panel, co5300_t, base);
    p->madctl = (uint8_t)((p->madctl & ~0xC0) | (mx ? 0x40 : 0) | (my ? 0x80 : 0));
    return tx_cmd(p, LCD_CMD_MADCTL, &p->madctl, 1);
}
static esp_err_t pnl_swap(esp_lcd_panel_t *panel, bool swap)
{
    co5300_t *p = __containerof(panel, co5300_t, base);
    p->madctl = (uint8_t)((p->madctl & ~0x20) | (swap ? 0x20 : 0));
    return tx_cmd(p, LCD_CMD_MADCTL, &p->madctl, 1);
}
static esp_err_t pnl_gap(esp_lcd_panel_t *panel, int xg, int yg)
{
    co5300_t *p = __containerof(panel, co5300_t, base);
    p->x_gap = xg; p->y_gap = yg;
    return ESP_OK;
}
static esp_err_t pnl_onoff(esp_lcd_panel_t *panel, bool on)
{
    co5300_t *p = __containerof(panel, co5300_t, base);
    return tx_cmd(p, on ? LCD_CMD_DISPON : LCD_CMD_DISPOFF, NULL, 0);
}

esp_err_t bsp_co5300_new(esp_lcd_panel_io_handle_t io, int reset_gpio, int x_gap, esp_lcd_panel_handle_t *out)
{
    ESP_RETURN_ON_FALSE(io && out, ESP_ERR_INVALID_ARG, TAG, "args");
    co5300_t *p = calloc(1, sizeof *p);
    ESP_RETURN_ON_FALSE(p, ESP_ERR_NO_MEM, TAG, "no mem");
    if (reset_gpio >= 0) {
        gpio_set_level(reset_gpio, 1);              /* the output register high BEFORE the pin becomes an output: GPIO36 is the flash-voltage strap, it must never dip */
        const gpio_config_t c = { .pin_bit_mask = 1ULL << reset_gpio, .mode = GPIO_MODE_OUTPUT };
        esp_err_t e = gpio_config(&c);
        if (e != ESP_OK) { free(p); return e; }
        gpio_set_level(reset_gpio, 1);
    }
    p->io = io; p->reset_gpio = reset_gpio; p->x_gap = x_gap;
    p->base.del = pnl_del; p->base.reset = pnl_reset; p->base.init = pnl_init; p->base.draw_bitmap = pnl_draw;
    p->base.invert_color = pnl_invert; p->base.mirror = pnl_mirror; p->base.swap_xy = pnl_swap; p->base.set_gap = pnl_gap; p->base.disp_on_off = pnl_onoff;
    *out = &p->base;
    return ESP_OK;
}

esp_err_t bsp_co5300_cmd(esp_lcd_panel_handle_t panel, uint8_t cmd, const uint8_t *data, size_t n)
{
    ESP_RETURN_ON_FALSE(panel, ESP_ERR_INVALID_STATE, TAG, "no panel");
    return tx_cmd(__containerof(panel, co5300_t, base), cmd, data, n);
}
