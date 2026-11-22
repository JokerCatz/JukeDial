// SPDX-License-Identifier: LGPL-2.1-or-later
#include "bsp/bsp_keys.h"
#include "bsp/bsp_i2c.h"
#include "bsp/bsp_pins.h"
#include "bsp_pmu_priv.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "bsp_keys";

/* AXP2101 registers used (XPowersLib names): chip id 0x03, IRQ enable 2 = 0x41, IRQ status 2 = 0x49. Status 2: bit0 power key positive edge (released), bit1 negative edge
 * (pressed), bit2 long press, bit3 short press. Nothing else of the chip is touched. */
#define REG_CHIP_ID   0x03
#define REG_INTEN2    0x41
#define REG_INTSTS1   0x48
#define REG_INTSTS2   0x49
#define REG_INTSTS3   0x4A
#define ST2_POS       0x01
#define ST2_NEG       0x02
#define STALE_US      3500000
#define EN2_EDGES     (ST2_POS | ST2_NEG)
#define EN2_VALUE     0x0F                  /* power-key edges + short + long press, and nothing else: another board's firmware found any other enabled IRQ (the gauge's events) floods nIRQ */

static i2c_master_dev_handle_t s_pmu;
static bool s_pmu_ok, s_top;
static int64_t s_release_t;   /* a press and its release in one read: the key counts as down until then (longer than the recogniser's debounce) */
static int64_t s_top_t;   /* when the top key went down (us): a release edge that never came must not hold the key down for ever */

static esp_err_t rd(uint8_t reg, uint8_t *v) { return i2c_master_transmit_receive(s_pmu, &reg, 1, v, 1, 30); }
static esp_err_t wr(uint8_t reg, uint8_t v) { const uint8_t b[2] = { reg, v }; return i2c_master_transmit(s_pmu, b, 2, 30); }

bool bsp_pmu_present(void) { return s_pmu_ok; }

esp_err_t bsp_keys_init(void)
{
    const gpio_config_t k = { .pin_bit_mask = 1ULL << BSP_PIN_KEY_BOTTOM, .mode = GPIO_MODE_INPUT,
                              .pull_up_en = GPIO_PULLUP_DISABLE, .pull_down_en = GPIO_PULLDOWN_DISABLE };   /* a strapping pin: the board's 10k pull-up only */
    ESP_RETURN_ON_ERROR(gpio_config(&k), TAG, "key gpio");
    const gpio_config_t q = { .pin_bit_mask = 1ULL << BSP_PIN_PMU_IRQ, .mode = GPIO_MODE_INPUT };   /* the shifter's pull-up is on the board */
    ESP_RETURN_ON_ERROR(gpio_config(&q), TAG, "irq gpio");
    if (!s_pmu) s_pmu = bsp_pmu_dev();                    /* the one shared handle (bsp_pmu.c) */
    ESP_RETURN_ON_FALSE(s_pmu, ESP_ERR_INVALID_STATE, TAG, "i2c bus missing");
    uint8_t id = 0, en = 0;
    if (rd(REG_CHIP_ID, &id) != ESP_OK || (id & 0xCF) != 0x4A) { ESP_LOGW(TAG, "no AXP2101 (id 0x%02x)", id); return ESP_ERR_NOT_FOUND; }
    s_pmu_ok = true;
    ESP_RETURN_ON_ERROR(rd(REG_INTEN2, &en), TAG, "inten2");
    if (en != EN2_VALUE) ESP_RETURN_ON_ERROR(wr(REG_INTEN2, EN2_VALUE), TAG, "inten2 write");
    ESP_RETURN_ON_ERROR(wr(REG_INTSTS2, 0xFF), TAG, "clear");   /* the start-up's old events (the press that powered the device) */
    return ESP_OK;
}

void bsp_keys_read(bool *top, bool *bottom)
{
    if (s_pmu_ok) {
        if (s_release_t && esp_timer_get_time() >= s_release_t) { s_top = false; s_release_t = 0; }
        else if (!s_release_t && gpio_get_level(BSP_PIN_PMU_IRQ) == 0) {
            uint8_t st = 0;
            if (rd(REG_INTSTS2, &st) == ESP_OK && st) {
                wr(REG_INTSTS2, st);                                     /* write-1-to-clear only what was read (nIRQ goes high) */
                if ((st & EN2_EDGES) == EN2_EDGES) { s_top = true; s_top_t = esp_timer_get_time(); s_release_t = s_top_t + 25000; }
                else if (st & EN2_EDGES) { s_top = (st & ST2_NEG) != 0; s_top_t = esp_timer_get_time(); }
            }
            for (uint8_t reg = REG_INTSTS1; reg <= REG_INTSTS3; reg += 2) {   /* any other PMU event (plug, charger) holding nIRQ low is read and cleared too: the line must go quiet */
                uint8_t o = 0;
                if (rd(reg, &o) == ESP_OK && o) wr(reg, o);
            }
        }
    }
    if (s_top && esp_timer_get_time() - s_top_t > STALE_US) s_top = false;   /* the PMU powers off at a 4 s hold (0x27 = 0x12): by 3.5 s either the release edge was missed or the machine is going off */
    if (top) *top = s_top;
    if (bottom) *bottom = gpio_get_level(BSP_PIN_KEY_BOTTOM) == 0;
}
