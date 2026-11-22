// SPDX-License-Identifier: LGPL-2.1-or-later
#include "cpu_load.h"
#include <string.h>
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void cpu_snap(cpu_snap_t *s)
{
    static TaskStatus_t st[80];                 /* Bluetooth adds many tasks: IDLE1 fell off a 48-entry list */
    configRUN_TIME_COUNTER_TYPE total;
    UBaseType_t n = uxTaskGetSystemState(st, 80, &total);
    s->idle[0] = s->idle[1] = 0;
    for (UBaseType_t i = 0; i < n; i++) {
        if (!strcmp(st[i].pcTaskName, "IDLE0")) s->idle[0] = st[i].ulRunTimeCounter;
        if (!strcmp(st[i].pcTaskName, "IDLE1")) s->idle[1] = st[i].ulRunTimeCounter;
    }
    s->t_us = esp_timer_get_time();
}

void cpu_load(const cpu_snap_t *a, const cpu_snap_t *b, float out_pct[2])
{
    double dt = (double)(b->t_us - a->t_us);
    for (int c = 0; c < 2; c++) {
        double idle = (double)(b->idle[c] - a->idle[c]);
        double pct = dt > 0 ? 100.0 * (1.0 - idle / dt) : 0;
        out_pct[c] = pct < 0 ? 0 : (float)pct;
    }
}
