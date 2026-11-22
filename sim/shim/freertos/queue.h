// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include "FreeRTOS.h"
/* A small ring-buffer queue (shim_esp.c). Single-threaded: a full queue refuses at once, an empty one never blocks. */
typedef void *QueueHandle_t;
QueueHandle_t xQueueCreate(uint32_t length, uint32_t item_size);
BaseType_t xQueueSend(QueueHandle_t q, const void *item, TickType_t wait);
BaseType_t xQueueReceive(QueueHandle_t q, void *item, TickType_t wait);
