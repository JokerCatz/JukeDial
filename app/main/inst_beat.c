// SPDX-License-Identifier: LGPL-2.1-or-later
#include "inst_beat.h"
#include <stdio.h>
#include <string.h>
#include "bsp/bsp_haptic.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "inst_ble.h"
#include "inst_engine.h"

#define QN 4
static TaskHandle_t s_task;
static esp_timer_handle_t s_wake;
static volatile bool s_haptic;
static volatile int s_lead = 5000;
static volatile uint32_t s_seq;
static volatile uint8_t s_seen_beat, s_seen_accent;
/* stats */
#define HB 41
static uint32_t s_hist[HB], s_ticks;
static int64_t s_known_min = INT64_MAX;
static volatile bool s_sreset = true;

void inst_beat_set_haptic(bool on) { s_haptic = on; }
void inst_beat_set_lead(int us) { s_lead = us < 0 ? 0 : us > 25000 ? 25000 : us; }
int  inst_beat_lead(void) { return s_lead; }

uint32_t inst_beat_seen(uint8_t *beat, uint8_t *accent)
{
    uint32_t q = s_seq;
    if (beat) *beat = s_seen_beat;
    if (accent) *accent = s_seen_accent;
    return q;
}

void inst_beat_stats(int *n, float *p50, float *p95, float *mx, float *known_min, uint32_t *ticks)
{
    uint32_t h[HB], tot = 0;
    memcpy(h, s_hist, sizeof h);
    for (int i = 0; i < HB; i++) tot += h[i];
    *n = (int)tot;
    *p50 = *p95 = *mx = 0;
    uint32_t c = 0;
    for (int i = 0; i < HB; i++) {
        c += h[i];
        if (!*p50 && tot && c >= (tot + 1) / 2) *p50 = (i + 1) * 0.25f;
        if (!*p95 && tot && c >= (tot * 95 + 99) / 100) *p95 = (i + 1) * 0.25f;
        if (h[i]) *mx = (i + 1) * 0.25f;
    }
    *known_min = s_known_min == INT64_MAX ? 0 : (float)s_known_min / 1000.0f;
    *ticks = s_ticks;
    s_sreset = true;
}

static void wake_cb(void *arg) { (void)arg; xTaskNotifyGive(s_task); }

static void send1(uint8_t b, int64_t t_us)
{
    if (inst_ble_send(&b, 1, (uint16_t)((t_us / 1000) & 0x1FFF)) && b == 0xF8) s_ticks++;
}

static void task(void *arg)
{
    (void)arg;
    inst_beat_t q[QN];
    bool fired[QN] = { 0 };
    int qn = 0;
    bool clock_on = false, grid = false;
    int64_t g0 = 0, hstop = 0;
    double gp = 0;
    int gi = 0;
    for (;;) {
        inst_beat_t m;
        while (inst_engine_beat_pop(&m)) {
            int64_t now = esp_timer_get_time();
            if (m.kind == INST_BEAT_START) {
                if (!m.ext) { clock_on = true; send1(0xFA, now); }
            } else if (m.kind == INST_BEAT_STOP) {
                if (clock_on) send1(0xFC, now);
                clock_on = grid = false;
                qn = 0;
            } else if (qn < QN) {
                if (s_sreset) { memset(s_hist, 0, sizeof s_hist); s_known_min = INT64_MAX; s_ticks = 0; s_sreset = false; }
                if (m.t_us - m.planned_us < s_known_min) s_known_min = m.t_us - m.planned_us;
                fired[qn] = false;
                q[qn++] = m;
            }
        }
        int64_t now = esp_timer_get_time(), next = now + 1000000;
        /* the oldest beat: haptic first (lead), then heard */
        while (qn) {
            inst_beat_t *b = &q[0];
            int64_t th = b->t_us - s_lead;
            if (!fired[0]) {
                if (now >= th) {
                    fired[0] = true;
                    if (s_haptic) {
                        bsp_haptic_play(b->accent ? 1 : 5);
                        int64_t done = esp_timer_get_time();
                        int64_t e = done - th;
                        s_hist[e < 0 ? 0 : e / 250 < HB - 1 ? e / 250 : HB - 1]++;
                        hstop = done + (b->accent ? 25000 : 12000);
                    }
                } else { if (th < next) next = th; break; }
            }
            if (now < b->t_us) { if (b->t_us < next) next = b->t_us; break; }
            s_seen_beat = b->beat;
            s_seen_accent = b->accent;
            s_seq++;
            if (clock_on && !b->ext) { grid = true; g0 = b->t_us; gp = b->period_us / 24.0; gi = 0; }
            memmove(q, q + 1, (size_t)(qn - 1) * sizeof q[0]);
            memmove(fired, fired + 1, (size_t)(qn - 1) * sizeof fired[0]);
            qn--;
        }
        if (hstop) {
            if (now >= hstop) { bsp_haptic_stop(); hstop = 0; }
            else if (hstop < next) next = hstop;
        }
        while (grid) {
            int64_t tt = g0 + (int64_t)(gi * gp);
            if (now < tt) { if (tt < next) next = tt; break; }
            send1(0xF8, tt);
            if (++gi >= 24) grid = false;                  /* the next beat anchors the next 24 */
        }
        int64_t dt = next - esp_timer_get_time();
        if (dt > 0) {
            esp_timer_stop(s_wake);
            esp_timer_start_once(s_wake, (uint64_t)dt);
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);       /* the timer, or a new beat from the engine */
        }
    }
}

void inst_beat_start(bool haptic, int haptic_mv)
{
    s_haptic = haptic && haptic_mv > 0;
    if (haptic_mv > 0) bsp_haptic_set_clamp((uint8_t)((haptic_mv * 100 + 1066) / 2133));   /* as ui.c */
    const esp_timer_create_args_t a = { .callback = wake_cb, .name = "inst_beat" };
    esp_timer_create(&a, &s_wake);
    xTaskCreatePinnedToCore(task, "inst_beat", 3584, NULL, 21, &s_task, 0);   /* above Bluedroid (BTC 19, BTU 20): it only wakes, writes and sleeps */
    inst_engine_set_beat_task(s_task);
}
