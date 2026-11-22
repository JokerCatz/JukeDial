// SPDX-License-Identifier: LGPL-2.1-or-later
/* esp_timer on the virtual clock. Callbacks run only from sim_timers_run (a step boundary of sim_advance_ms),
 * earliest due first, and see sim_now_us() of that boundary. */
#include <stdlib.h>
#include "esp_timer.h"
#include "sim.h"

struct esp_timer {
    esp_timer_cb_t cb;
    void *arg;
    int64_t due_us;
    int64_t period_us;   /* 0: one-shot */
    bool active;
    bool dead;           /* esp_timer_delete called from inside its own callback */
    struct esp_timer *next;
};

/* Timers due at the same instant fire newest-created first; a callback restarting itself with timeout 0 loops forever (same on the board). */
static struct esp_timer *g_head;
static struct esp_timer *g_running;

int64_t esp_timer_get_time(void) { return sim_now_us(); }

esp_err_t esp_timer_create(const esp_timer_create_args_t *args, esp_timer_handle_t *out)
{
    if (!args || !args->callback || !out) return ESP_ERR_INVALID_ARG;
    struct esp_timer *t = calloc(1, sizeof *t);
    if (!t) return ESP_ERR_NO_MEM;
    t->cb = args->callback;
    t->arg = args->arg;
    t->next = g_head;   /* newest first; ties are fired in list order */
    g_head = t;
    *out = t;
    return ESP_OK;
}

static esp_err_t start(esp_timer_handle_t t, uint64_t first_us, uint64_t period_us)
{
    if (!t) return ESP_ERR_INVALID_ARG;
    if (t->active) return ESP_ERR_INVALID_STATE;   /* as in ESP-IDF */
    t->due_us = sim_now_us() + (int64_t)first_us;
    t->period_us = (int64_t)period_us;
    t->active = true;
    return ESP_OK;
}

esp_err_t esp_timer_start_once(esp_timer_handle_t t, uint64_t timeout_us) { return start(t, timeout_us, 0); }
esp_err_t esp_timer_start_periodic(esp_timer_handle_t t, uint64_t period_us) { return period_us ? start(t, period_us, period_us) : ESP_ERR_INVALID_ARG; }

esp_err_t esp_timer_stop(esp_timer_handle_t t)
{
    if (!t) return ESP_ERR_INVALID_ARG;
    if (!t->active) return ESP_ERR_INVALID_STATE;
    t->active = false;
    return ESP_OK;
}

esp_err_t esp_timer_delete(esp_timer_handle_t t)
{
    if (!t) return ESP_ERR_INVALID_ARG;
    if (t->active) return ESP_ERR_INVALID_STATE;   /* as in ESP-IDF */
    if (t == g_running) { t->dead = true; return ESP_OK; }
    for (struct esp_timer **p = &g_head; *p; p = &(*p)->next)
        if (*p == t) { *p = t->next; free(t); return ESP_OK; }
    return ESP_ERR_INVALID_ARG;
}

/* Live timers: created and not deleted (a timer deleted from its own callback is counted until that callback returns). */
int sim_esp_timer_count(void)
{
    int n = 0;
    for (struct esp_timer *t = g_head; t; t = t->next) n++;
    return n;
}

void sim_timers_run(void)
{
    for (;;) {   /* re-scan after every callback: it may start, stop or delete any timer */
        struct esp_timer *best = NULL;
        for (struct esp_timer *t = g_head; t; t = t->next)
            if (t->active && t->due_us <= sim_now_us() && (!best || t->due_us < best->due_us)) best = t;
        if (!best) return;
        if (best->period_us) best->due_us += best->period_us;
        else best->active = false;
        g_running = best;
        best->cb(best->arg);
        g_running = NULL;
        if (best->dead) {
            best->active = false;
            esp_timer_delete(best);
        }
    }
}
