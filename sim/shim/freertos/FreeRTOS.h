// SPDX-License-Identifier: LGPL-2.1-or-later
/* FreeRTOS on the host is an empty shell: one thread, locks always succeed, delays advance the virtual clock.
 * Code that really creates tasks is a fake instead (no scheduler is simulated). */
#pragma once
#include <stdint.h>
typedef uint32_t TickType_t;
typedef int32_t BaseType_t;
typedef uint32_t UBaseType_t;
#define pdTRUE  1
#define pdFALSE 0
#define pdPASS  1
#define pdFAIL  0
#define portMAX_DELAY 0xFFFFFFFFu
#define configTICK_RATE_HZ 1000
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
