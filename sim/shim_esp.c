// SPDX-License-Identifier: LGPL-2.1-or-later
/* ESP-IDF odds and ends for the host: esp_err_to_name, esp_random, FreeRTOS delay/task shells, SNTP (never reached: no Wi-Fi). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_err.h"
#include "esp_netif_sntp.h"
#include "esp_random.h"
#include "esp_sntp.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "sim.h"

const char *esp_err_to_name(esp_err_t e)
{
    switch (e) {
    case ESP_OK: return "ESP_OK";
    case ESP_FAIL: return "ESP_FAIL";
    case ESP_ERR_NO_MEM: return "ESP_ERR_NO_MEM";
    case ESP_ERR_INVALID_ARG: return "ESP_ERR_INVALID_ARG";
    case ESP_ERR_INVALID_STATE: return "ESP_ERR_INVALID_STATE";
    case ESP_ERR_INVALID_SIZE: return "ESP_ERR_INVALID_SIZE";
    case ESP_ERR_NOT_FOUND: return "ESP_ERR_NOT_FOUND";
    default: return "ESP_ERR_?";
    }
}

uint32_t esp_random(void)
{
    static uint32_t x;
    if (!x) x = sim_seed() ? sim_seed() : 1;   /* xorshift32 must not start at 0 */
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return x;
}

/* ---- tasks: bodies are queued and run to completion at a step boundary of sim_advance_ms ---- */
#define MAX_TASKS 8
static struct { TaskFunction_t fn; void *arg; } g_tasks[MAX_TASKS];
static int g_ntasks;
static bool g_in_task;

void vTaskDelay(TickType_t ticks) { if (ticks && !g_in_task) sim_advance_ms(ticks); }

BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn, const char *name, uint32_t stack, void *arg, UBaseType_t prio, TaskHandle_t *out, int core)
{
    (void)name; (void)stack; (void)prio; (void)core;
    if (out) *out = NULL;
    if (g_ntasks == MAX_TASKS) return pdFAIL;
    g_tasks[g_ntasks].fn = fn;
    g_tasks[g_ntasks].arg = arg;
    g_ntasks++;
    return pdPASS;
}

bool sim_tasks_hold;                   /* "simtaskhold 1": queued bodies wait (a scan that is still running while another page covers its page) */
void sim_tasks_run(void)
{
    if (g_in_task || sim_tasks_hold) return;
    g_in_task = true;
    while (g_ntasks) {                 /* a body may create another task: it runs in this same pass */
        TaskFunction_t fn = g_tasks[0].fn;
        void *arg = g_tasks[0].arg;
        memmove(&g_tasks[0], &g_tasks[1], (size_t)(--g_ntasks) * sizeof g_tasks[0]);
        fn(arg);
    }
    g_in_task = false;
}

/* ---- queues ---- */
struct sim_queue { uint32_t len, size, head, count; unsigned char *buf; };

QueueHandle_t xQueueCreate(uint32_t length, uint32_t item_size)
{
    struct sim_queue *q = calloc(1, sizeof *q);
    if (!q) return NULL;
    q->len = length;
    q->size = item_size;
    q->buf = calloc(length ? length : 1, item_size ? item_size : 1);
    if (!q->buf) { free(q); return NULL; }
    return q;
}

BaseType_t xQueueSend(QueueHandle_t h, const void *item, TickType_t wait)
{
    struct sim_queue *q = h;
    (void)wait;
    if (!q || q->count == q->len) return pdFALSE;
    memcpy(q->buf + (size_t)((q->head + q->count) % q->len) * q->size, item, q->size);
    q->count++;
    return pdTRUE;
}

BaseType_t xQueueReceive(QueueHandle_t h, void *item, TickType_t wait)
{
    struct sim_queue *q = h;
    (void)wait;
    if (!q || !q->count) return pdFALSE;
    memcpy(item, q->buf + (size_t)q->head * q->size, q->size);
    q->head = (q->head + 1) % q->len;
    q->count--;
    return pdTRUE;
}

void esp_sntp_restart(void) {}
static void (*g_sntp_cb)(struct timeval *);
esp_err_t esp_netif_sntp_init(const esp_sntp_config_t *c) { g_sntp_cb = c ? c->sync_cb : NULL; return ESP_OK; }
bool sim_sntp_fire(void) { struct timeval tv = { 0 }; if (!g_sntp_cb) return false; g_sntp_cb(&tv); return true; }   /* simsntp: the sync callback (the lwIP task on the board) */
