// SPDX-License-Identifier: LGPL-2.1-or-later
#include "power_svc.h"
#include <stdio.h>
#include <string.h>
#include "bsp/bsp_battery.h"
#include "bsp/bsp_geom.h"
#include "driver/usb_serial_jtag.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "freertos/idf_additions.h"

/* Raw-reading histograms for calibrating the battery threshold: unplug USB for a few minutes, plug back, "power".
 * Group 0 = no computer on the debug port, group 1 = a computer there. Bin 0: raw < HIST_LO, then HIST_STEP wide. */
#define HIST_LO   3584
#define HIST_STEP 16
#define HIST_BINS 33
typedef struct { uint16_t bin[HIST_BINS]; uint16_t min, max; uint32_t n; } hist_t;

static volatile pc_power_t s_state = PC_PWR_UNKNOWN;
static hist_t s_hist[2];                  /* zero-initialised: .bss; min is valid once n > 0 */
static volatile int s_last_raw = -1;
static volatile bool s_last_host;
static volatile uint16_t s_low_run;

pc_power_t power_svc_state(void) { return s_state; }

#ifndef BSP_HAS_PMU
#define BSP_HAS_PMU 0
#endif
#if BSP_HAS_PMU
#include "bsp/bsp_pmu.h"
#include "esp_attr.h"
/* FakePod: the AXP2101 is the only source. External power present = "usb" (the screen timeout and the heavy effects follow it as on the round board), else battery. The gauge is read
 * once a second; a failed read keeps the last answer. */
#define PMU_POLL_MS 1000
static EXT_RAM_BSS_ATTR volatile int s_pct1;   /* percent + 1 (0 = unknown): zero-initialised so it lives in PSRAM, not .sbss */
static EXT_RAM_BSS_ATTR volatile bool s_chg;
int  power_svc_batt_pct(void) { return s_pct1 - 1; }
bool power_svc_charging(void) { return s_chg; }
static void pmu_task(void *arg)
{
    (void)arg;
    bool failed = false;
    for (;;) {
        pc_axp_t a;
        if (bsp_pmu_battery(&a) == ESP_OK) {
            failed = false;
            const pc_power_t n = a.vbus || !a.batt ? PC_PWR_USB : PC_PWR_BATT;   /* no cell and running = it is the plug that powers it */
            s_pct1 = a.pct + 1;
            s_chg = a.dir == PC_AXP_CHARGING;
            if (n != s_state) { s_state = n; printf("APP power state=%s pct=%d vbat=%d chg=%s\n", n == PC_PWR_USB ? "usb" : "batt", a.pct, a.vbat_mv, pc_axp_chg_name(a.chg_state)); }
        } else if (!failed) { failed = true; printf("APP power pmu read failed\n"); }
        vTaskDelay(pdMS_TO_TICKS(PMU_POLL_MS));
    }
}
#else
int  power_svc_batt_pct(void) { return -1; }
bool power_svc_charging(void) { return false; }
#endif

static void hist_add(hist_t *h, int raw)
{
    int b = raw < HIST_LO ? 0 : 1 + (raw - HIST_LO) / HIST_STEP;
    if (b >= HIST_BINS) b = HIST_BINS - 1;
    if (h->bin[b] < UINT16_MAX) h->bin[b]++;
    uint16_t r = raw < 0 ? 0 : raw > UINT16_MAX ? UINT16_MAX : (uint16_t)raw;
    if (h->n == 0 || r < h->min) h->min = r;
    if (r > h->max) h->max = r;
    if (h->n < UINT32_MAX) h->n++;
}

void power_svc_hist_reset(void)
{
    for (int g = 0; g < 2; g++) {
        memset(&s_hist[g], 0, sizeof s_hist[g]);
    }
}

void power_svc_dump(void)
{
#if BSP_HAS_PMU
    bsp_pmu_dump();
#endif
    printf("APP power state=%s raw=%d host=%d low_run=%u\n", s_state == PC_PWR_USB ? "usb" : s_state == PC_PWR_BATT ? "batt" : "unknown",
           s_last_raw, s_last_host ? 1 : 0, (unsigned)s_low_run);
    for (int g = 0; g < 2; g++) {
        const hist_t *h = &s_hist[g];                    /* fixed arrays: a concurrent update only makes a value one sample stale */
        printf("APP power hist host=%d n=%lu min=%u max=%u bins=", g, (unsigned long)h->n, (unsigned)(h->n ? h->min : 0), (unsigned)h->max);
        bool first = true;
        for (int b = 0; b < HIST_BINS; b++) {
            if (!h->bin[b]) continue;
            printf("%s%d:%u", first ? "" : ",", b == 0 ? 0 : HIST_LO + (b - 1) * HIST_STEP, (unsigned)h->bin[b]);
            first = false;
        }
        printf("\n");
    }
}

static void task(void *arg)
{
    (void)arg;
    pc_power_filt_t f = { PC_PWR_UNKNOWN, 0 };
    bool read_failed = false;                          /* one log line per failure streak */
    for (;;) {
        int raw = 0, pmv, rmv;
        esp_err_t rc = bsp_battery_read(&raw, &pmv, &rmv);
        if (rc != ESP_OK) {
            if (!read_failed) printf("APP power adc read failed rc=%d\n", (int)rc);
            read_failed = true;
        } else {
            read_failed = false;
            bool host = usb_serial_jtag_is_connected();
            pc_power_t n = pc_power_step(&f, raw, host);
            hist_add(&s_hist[host ? 1 : 0], raw);
            s_last_raw = raw;
            s_last_host = host;
            s_low_run = f.low_run;
            if (n != s_state) {
                s_state = n;
                printf("APP power state=%s raw=%d host=%d\n", n == PC_PWR_USB ? "usb" : "batt", raw, host ? 1 : 0);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(PC_PWR_SAMPLE_MS));
    }
}

void power_svc_start(void)
{
#if BSP_HAS_PMU
    xTaskCreatePinnedToCoreWithCaps(pmu_task, "power", 3072, NULL, 2, NULL, 0, MALLOC_CAP_SPIRAM);   /* PSRAM stack like the ADC task: a few I2C reads and a printf */
    return;
#endif
    if (bsp_battery_init() != ESP_OK) { printf("APP power adc=fail\n"); return; }
    xTaskCreatePinnedToCoreWithCaps(task, "power", 3072, NULL, 2, NULL, 0, MALLOC_CAP_SPIRAM);   /* PSRAM stack (PSR): ADC one-shot read + printf, no ISR/DMA/flash write on this stack */
}
