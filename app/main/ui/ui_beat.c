// SPDX-License-Identifier: LGPL-2.1-or-later
/* The beat feed: see ui_beat.h. State in one PSRAM block; this file is built with -msmall-data-limit=0 (its statics are .bss in PSRAM). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "audio_svc.h"
#include "pc/pc_beat.h"
#include "psram_alloc.h"
#include "ui_beat.h"
#include "ui_kit.h"

#define PERIOD_MS 50                                   /* 20 Hz */
#define OWNERS    4
#define LAT_WIRED_MS 30                                /* the spectrum tap is before the I2S DMA buffers: the sound is heard about this much after the tap */
#define LAT_BT_MS    180                               /* A2DP: the headphones' buffer and the codec: 150..250 ms (a default to be measured: checklist 4.30) */
typedef struct {
    pc_beat_t b;
    uint32_t last_tick, onsets, ticks, steps, max_us;
    uint64_t sum_us;
    int sim_bpm;                                       /* 0: the real spectrum */
    uint32_t sim_t0;
    int32_t latency;
    bool lat_manual, sink, held;
    uint32_t rng;
} bf_t;
static bf_t *s_st;
static lv_timer_t *s_timer;
static lv_obj_t *s_owner[OWNERS];                    /* the pages that use the feed: it runs while one lives (a second one shares it) */

static uint64_t now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}

/* the synthetic feed: a click every 60000/bpm ms from the moment it was switched on: a floor with a little noise, a burst on the low bars and half of it on the mid ones */
static void synth(bf_t *s, float bars[PC_BEAT_BARS])
{
    float P = 60000.0f / (float)s->sim_bpm, since = fmodf((float)lv_tick_elaps(s->sim_t0), P), env = expf(-since / 60.0f);
    for (int i = 0; i < PC_BEAT_BARS; i++) {
        s->rng = s->rng * 1664525u + 1013904223u;
        float v = 0.05f + 0.02f * (float)(s->rng >> 8) / 16777216.0f;
        if (i < 16) v += 0.85f * env; else if (i < PC_BEAT_BANDS) v += 0.5f * env;
        bars[i] = v > 1 ? 1 : v;
    }
}

/* the default latency follows the sink (wired / Bluetooth) unless the console set one */
static void apply_latency(bf_t *s)
{
    if (s->lat_manual) return;
    aud_info_t ai;
    audio_svc_info(&ai);
    s->sink = ai.sink;
    s->latency = ai.sink ? LAT_BT_MS : LAT_WIRED_MS;
    pc_beat_latency(&s->b, s->latency);
}

static void tick(lv_timer_t *t)
{
    (void)t;
    bf_t *s = s_st;
    float bars[PC_BEAT_BARS];
    uint64_t t0 = now_us();
    if (s->sim_bpm > 0) synth(s, bars); else audio_svc_spectrum(bars);
    if (s->steps % 20 == 0) apply_latency(s);
    uint32_t dt = lv_tick_elaps(s->last_tick);
    s->last_tick = lv_tick_get();
    uint32_t ev = pc_beat_step(&s->b, bars, dt > 500 ? 500 : dt);
    uint32_t us = (uint32_t)(now_us() - t0);
    s->onsets += (ev & PC_BEAT_ONSET) != 0;
    s->ticks += (ev & PC_BEAT_TICK) != 0;
    s->steps++;
    s->sum_us += us;
    if (us > s->max_us) s->max_us = us;
}

static int owner_find(lv_obj_t *o) { for (int i = 0; i < OWNERS; i++) if (s_owner[i] == o) return i; return -1; }
static int owners(void) { int n = 0; for (int i = 0; i < OWNERS; i++) n += s_owner[i] != NULL; return n; }
static void stop_if_none(void) { if (!owners() && s_timer) { lv_timer_delete(s_timer); s_timer = NULL; } }
static void owner_gone(lv_event_t *e)
{
    lv_obj_t *o = lv_event_get_user_data(e);
    int k = owner_find(o);
    if (k >= 0) s_owner[k] = NULL;
    stop_if_none();
}

void ui_beat_on(lv_obj_t *owner)
{
    if (!owner) return;
    if (owner_find(owner) >= 0) return;
    if (!s_st) s_st = psram_calloc(1, sizeof *s_st);
    if (!s_st) return;
    int slot = owner_find(NULL);
    if (slot < 0) return;                              /* four users at once is all there is */
    if (!s_timer) {                                    /* the first user: a clean detector (a console `beat sim` and `latency` survive) */
        int sb = s_st->sim_bpm;
        uint32_t t0 = s_st->sim_t0;
        int32_t lat = s_st->latency;
        bool man = s_st->lat_manual;
        memset(s_st, 0, sizeof *s_st);
        s_st->sim_bpm = sb;
        s_st->sim_t0 = t0;
        s_st->latency = lat;
        s_st->lat_manual = man;
        s_st->rng = 12345;
        pc_beat_init(&s_st->b);
        s_st->last_tick = lv_tick_get();
        apply_latency(s_st);
        if (man) pc_beat_latency(&s_st->b, lat);
        s_timer = lv_timer_create(tick, PERIOD_MS, NULL);
    }
    s_owner[slot] = owner;
    lv_obj_add_event_cb(owner, owner_gone, LV_EVENT_DELETE, owner);
}
void ui_beat_hold(bool hold)
{
    if (!s_timer || !s_st || s_st->held == hold) return;                    /* only a change (a resume restarts the frame clock) */
    s_st->held = hold;
    if (hold) lv_timer_pause(s_timer);
    else { s_st->last_tick = lv_tick_get(); lv_timer_resume(s_timer); }      /* the detector keeps its state; the gap is not one long frame */
}

void ui_beat_info(ui_beat_info_t *o)
{
    memset(o, 0, sizeof *o);
    if (!s_st || !s_timer) return;
    const pc_beat_t *b = &s_st->b;
    o->bpm = b->bpm;
    o->confidence = b->confidence;
    if (!pc_beat_locked(b)) return;
    o->locked = true;
    o->period_ms = (uint32_t)(60000.0f / b->bpm + 0.5f);
    uint32_t next = pc_beat_next_ms(b), since = lv_tick_elaps(s_st->last_tick);         /* on the detector's clock; its last step was `since` ms ago */
    int32_t to = (int32_t)(next - b->now_ms) - (int32_t)since;
    while (to <= 0) to += (int32_t)o->period_ms;
    o->to_next_ms = (uint32_t)to;
}
uint32_t ui_beat_onsets(void) { return s_st ? s_st->onsets : 0; }

static lv_obj_t *s_test_owner;                       /* console `beat user 1`: a second user (an object under the active screen) for the sharing test */
void ui_beat_cmd(const char *arg)
{
    int n;
    if (!s_st) s_st = psram_calloc(1, sizeof *s_st);
    if (!s_st) { printf("APP beat no memory\n"); return; }
    if (sscanf(arg, "sim %d", &n) == 1) { s_st->sim_bpm = n < 0 ? 0 : n > 300 ? 300 : n; s_st->sim_t0 = lv_tick_get(); }
    else if (sscanf(arg, "latency %d", &n) == 1) { s_st->latency = n; s_st->lat_manual = true; pc_beat_latency(&s_st->b, n); }
    else if (sscanf(arg, "user %d", &n) == 1) {
        if (n && !s_test_owner) { s_test_owner = lv_obj_create(lv_screen_active()); ui_beat_on(s_test_owner); }
        else if (!n && s_test_owner) { lv_obj_delete(s_test_owner); s_test_owner = NULL; }
    }
    else if (!strcmp(arg, "latency auto")) { s_st->lat_manual = false; apply_latency(s_st); }
    else if (!strcmp(arg, "reset")) { int sb = s_st->sim_bpm; pc_beat_init(&s_st->b); pc_beat_latency(&s_st->b, s_st->latency); s_st->onsets = s_st->ticks = s_st->steps = s_st->max_us = 0; s_st->sum_us = 0; s_st->sim_bpm = sb; }
    else if (arg[0]) { printf("APP beat unknown %s\n", arg); return; }
    ui_beat_info_t i;
    ui_beat_info(&i);
    printf("APP beat on=%d src=%s bpm=%.1f conf=%.2f locked=%d onsets=%u ticks=%u steps=%u us=%u max=%u lat=%d users=%d\n", s_timer != NULL, s_st->sim_bpm ? "sim" : "spec", s_st->b.bpm, s_st->b.confidence,
           i.locked, (unsigned)s_st->onsets, (unsigned)s_st->ticks, (unsigned)s_st->steps, s_st->steps ? (unsigned)(s_st->sum_us / s_st->steps) : 0u, (unsigned)s_st->max_us, (int)s_st->latency, owners());
}
