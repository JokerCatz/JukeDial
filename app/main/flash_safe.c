// SPDX-License-Identifier: LGPL-2.1-or-later
#include "flash_safe.h"
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pc/pc_fsjob.h"

typedef struct { pc_fsjob_t job; TaskHandle_t parent; } fs_ctx_t;

static void fs_task(void *a)
{
    fs_ctx_t *c = a;
    TaskHandle_t parent = c->parent;
    if (pc_fsjob_run(&c->job)) free(c);        /* the caller gave up: the job is ours to free */
    else xTaskNotifyGive(parent);
    vTaskDelete(NULL);
}

bool flash_safe_call(void (*fn)(void *), void *arg, unsigned timeout_ms)
{
    fs_ctx_t *c = malloc(sizeof *c);           /* internal heap (small): the worker may outlive a timed-out caller */
    if (!c) return false;
    pc_fsjob_init(&c->job, fn, arg);
    c->parent = xTaskGetCurrentTaskHandle();
    ulTaskNotifyValueClear(NULL, 0xFFFFFFFFu);
    if (xTaskCreate(fs_task, "flash_safe", 3072, c, 5, NULL) != pdPASS) { free(c); return false; }
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(timeout_ms));
    if (pc_fsjob_collect(&c->job)) { free(c); return true; }
    return false;
}
