// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include "FreeRTOS.h"
typedef void *TaskHandle_t;
typedef void (*TaskFunction_t)(void *);
void vTaskDelay(TickType_t ticks);   /* shim_esp.c: sim_advance_ms */
/* Single-threaded: "creating" a task queues its body; sim_advance_ms runs it to completion at its next step boundary, from the
 * top level (never inside lv_timer_handler or an esp_timer callback). Only library_svc's scan task is created by code that is
 * compiled as it is; its body ends in vTaskDelete(NULL), a no-op here. vTaskDelay is a no-op while a task body runs. */
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn, const char *name, uint32_t stack, void *arg, UBaseType_t prio, TaskHandle_t *out, int core);
static inline void vTaskDelete(TaskHandle_t t) { (void)t; }
