// SPDX-License-Identifier: LGPL-2.1-or-later
#include "bsp/bsp_knob.h"
#include "bsp/bsp_math.h"
#include "bsp/bsp_pins.h"
#include "bsp/bsp_knob_fsm.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

/* This board's "encoder" is two independent direction contacts, not a quadrature encoder.
 * Measured 2026-11-08 on the hwtest knob page: at rest A=1 B=1 (10k pull-ups); a clockwise
 * click pulls B low; a counter-clockwise click pulls A low, and B blips twice right after A
 * releases. Quadrature decoding netted 0 per click; counting falling edges miscounted CCW.
 * Here every edge on either pin raises a GPIO interrupt (nothing is polled) and the
 * first-contact state machine in bsp_knob_fsm.c decides each click. */

#define LOCKOUT_US 5000   /* both contacts must stay high this long before the next click */

static const char *TAG = "bsp_knob";
static bool s_started;
static bsp_knob_fsm_t s_fsm;
static volatile int32_t s_clicks;
static volatile uint32_t s_edges;
static volatile bool s_swap;
static lv_group_t *s_group;
static lv_indev_t *s_indev;
#if LVGL_VERSION_MAJOR < 9
static lv_indev_drv_t s_drv;
#endif
static int32_t s_last;
static bsp_knob_hook_t s_hook;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

/* Edge trace ring buffer (diagnostic): every interrupt records time and both levels. */
#define TRACE_N 256
static bsp_knob_trace_t s_trace[TRACE_N];
static volatile uint32_t s_trace_head;

uint32_t bsp_knob_trace_read(uint32_t from, bsp_knob_trace_t *out, uint32_t max)
{
    uint32_t head = s_trace_head, n = 0;
    if (head - from > TRACE_N) from = head - TRACE_N;
    while (from != head && n < max) out[n++] = s_trace[from++ % TRACE_N];
    return from;
}

static void knob_isr(void *arg)
{
    (void)arg;
    int a = gpio_get_level(BSP_PIN_KNOB_A);
    int b = gpio_get_level(BSP_PIN_KNOB_B);
    portENTER_CRITICAL_ISR(&s_mux);
    s_edges++;
    bsp_knob_trace_t *e = &s_trace[s_trace_head % TRACE_N];
    e->t_us = esp_timer_get_time(); e->a = (uint8_t)a; e->b = (uint8_t)b;
    s_trace_head++;
    s_clicks += bsp_knob_fsm_step(&s_fsm, a, b, esp_timer_get_time());
    portEXIT_CRITICAL_ISR(&s_mux);
}

int32_t bsp_knob_get_edges(void) { return (int32_t)s_edges; }

int32_t bsp_knob_get_count(void)
{
    int32_t c = s_clicks;
    return s_swap ? -c : c;
}

void bsp_knob_set_swap(bool swap)
{
    if (swap == s_swap) return;
    s_swap = swap;
    s_last = bsp_knob_get_count(); /* avoid a jump in the indev */
}

bool bsp_knob_get_swap(void) { return s_swap; }
lv_group_t *bsp_knob_group(void) { return s_group; }
void bsp_knob_set_hook(bsp_knob_hook_t hook) { s_hook = hook; }

#if LVGL_VERSION_MAJOR >= 9
static void read_cb(lv_indev_t *drv, lv_indev_data_t *data)
#else
static void read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
#endif
{
    (void)drv;
    int32_t now = bsp_knob_get_count();
    int32_t diff = now - s_last;
    s_last = now;
    data->state = LV_INDEV_STATE_RELEASED;
    data->enc_diff = 0;
    if (diff == 0) return;
    if (s_hook && s_hook(diff)) return;
    data->enc_diff = (int16_t)diff;
}

static esp_err_t contacts_start(void)
{
    bsp_knob_fsm_init(&s_fsm, LOCKOUT_US);
    const gpio_config_t c = {
        .pin_bit_mask = (1ULL << BSP_PIN_KNOB_A) | (1ULL << BSP_PIN_KNOB_B),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,   /* board has 10k pull-ups */
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&c), TAG, "gpio");
    esp_err_t e = gpio_install_isr_service(0);
    ESP_RETURN_ON_FALSE(e == ESP_OK || e == ESP_ERR_INVALID_STATE, e, TAG, "isr service"); /* touch may own it */
    ESP_RETURN_ON_ERROR(gpio_isr_handler_add(BSP_PIN_KNOB_A, knob_isr, NULL), TAG, "isr a");
    ESP_RETURN_ON_ERROR(gpio_isr_handler_add(BSP_PIN_KNOB_B, knob_isr, NULL), TAG, "isr b");
    ESP_LOGI(TAG, "direction-contact decoder on GPIO interrupts, lockout %d us", LOCKOUT_US);
    return ESP_OK;
}

esp_err_t bsp_knob_start(lv_disp_t *disp, bool swap_ab, lv_indev_t **out_indev)
{
    if (out_indev) *out_indev = NULL;
    if (!s_started) { ESP_RETURN_ON_ERROR(contacts_start(), TAG, "contacts"); s_started = true; }
    s_swap = swap_ab;
    s_last = bsp_knob_get_count();
    if (!s_group) {
        s_group = lv_group_create();
        lv_group_set_default(s_group);
    }
    if (!s_indev) {
#if LVGL_VERSION_MAJOR >= 9
        s_indev = lv_indev_create();
        ESP_RETURN_ON_FALSE(s_indev, ESP_FAIL, TAG, "indev create");
        lv_indev_set_type(s_indev, LV_INDEV_TYPE_ENCODER);
        lv_indev_set_read_cb(s_indev, read_cb);
        lv_indev_set_display(s_indev, disp);
#else
        lv_indev_drv_init(&s_drv);
        s_drv.type = LV_INDEV_TYPE_ENCODER;
        s_drv.read_cb = read_cb;
        s_drv.disp = disp;
        s_indev = lv_indev_drv_register(&s_drv);
        ESP_RETURN_ON_FALSE(s_indev, ESP_FAIL, TAG, "indev register");
#endif
        lv_indev_set_group(s_indev, s_group);
    }
    if (out_indev) *out_indev = s_indev;
    return ESP_OK;
}
