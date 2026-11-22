// SPDX-License-Identifier: LGPL-2.1-or-later
/* 記憶卡格式化: the board's lower half (sd_format.h). The format runs on a worker task (PSRAM stack); f_mkfs's work area is in PSRAM; the result
 * goes back to the LVGL task through ui_async. The simulator's lower half is sim/fake_sd.c. */
#include <stdio.h>
#include <stdlib.h>
#include "bsp/bsp_sdcard.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sd_format.h"
#include "ui/ui.h"

#define WORK_LEN (64 * 1024)             /* f_mkfs's work area: 128 sectors at a time (PSRAM) */

typedef struct { pc_fmt_plan_t plan; uint32_t serial, sig; uint8_t *work; int err, stage; } job_t;

const char *sd_hw_reason(void) { return bsp_sdcard_fail_reason(); }

bool sd_hw_card(uint64_t *bytes, uint32_t *serial)
{
    uint64_t n = 0;
    uint32_t ss = 0;
    if (bsp_sdcard_raw_info(&n, &ss, serial) != ESP_OK) return false;
    *bytes = n * ss;
    return true;
}

static void done(void *arg)
{
    job_t *j = arg;
    sd_format_finish(j->err, j->stage);
    heap_caps_free(j);
}

static void task(void *arg)
{
    job_t *j = arg;
    j->err = bsp_sdcard_format(&j->plan, j->serial, j->sig, j->work, WORK_LEN, &j->stage);
    heap_caps_free(j->work);
    j->work = NULL;
    ui_async(done, j);                       /* the job goes to the LVGL task with the result and is freed there */
    vTaskDeleteWithCaps(NULL);               /* a task made WithCaps gives its PSRAM stack back this way (review m2) */
}

bool sd_hw_format(const pc_fmt_plan_t *p, uint32_t serial)
{
    job_t *j = heap_caps_calloc(1, sizeof *j, MALLOC_CAP_SPIRAM);
    uint8_t *w = heap_caps_aligned_alloc(64, WORK_LEN, MALLOC_CAP_SPIRAM);
    if (!j || !w) { heap_caps_free(j); heap_caps_free(w); return false; }
    j->plan = *p;
    j->serial = serial;
    j->sig = esp_random();
    j->work = w;
    if (xTaskCreatePinnedToCoreWithCaps(task, "sdfmt", 8192, j, 3, NULL, 0, MALLOC_CAP_SPIRAM) != pdPASS) { heap_caps_free(w); heap_caps_free(j); return false; }
    return true;
}
