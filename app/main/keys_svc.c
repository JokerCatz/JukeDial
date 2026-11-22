// SPDX-License-Identifier: LGPL-2.1-or-later
#include "esp_attr.h"
#include "keys_svc.h"
#include "lab.h"
#include "bsp/bsp_geom.h"

#ifndef BSP_HAS_KEYS
#define BSP_HAS_KEYS 0
#endif

#if BSP_HAS_KEYS
#include <stdio.h>
#include "bsp/bsp_i2c.h"
#include "bsp/bsp_keys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "freertos/idf_additions.h"
#include "pc/pc_keys.h"
#include "pc/pc_blind.h"
#include "audio_svc.h"
#include "player_ctl.h"
#include "pc/pc_rotate.h"
#include "bsp/bsp_display.h"
#include "lvgl.h"
#include "ui.h"

#define POLL_MS 5
#define DRAIN_MS 10
static pc_keys_t s_k;                /* PSRAM (-msmall-data-limit=0); only the key task touches it */
static volatile int s_detents;       /* what the key task has recognised, not yet applied (atomic add / exchange) */
static volatile int s_both;          /* both-keys events, not yet applied */
static volatile int s_events_seen;
static pc_blind_t s_blind;           /* PSRAM; the key task only */
static bool s_blind_on;              /* the player keys (keys_svc_start), not a host mode (keys_svc_start_host) */
static volatile int s_vol, s_next, s_pp;   /* the blind layer actions, applied by the drain timer */
EXT_RAM_BSS_ATTR static bool s_key_swap;              /* rotate 1 (180): the two keys trade places (set once at start, the display's rotation) */

/* The key task (3 KB PSRAM stack) only recognises keys and posts them. Everything a detent reaches (a page's on_knob, ring paging, FreeType text fitting, the volume
 * overlay, screen wake) is page code: it runs here, in the LVGL task, from this timer, exactly where the round board's knob indev would run it. */
static void drain_cb(lv_timer_t *t)
{
    (void)t;
    const int both = __atomic_exchange_n(&s_both, 0, __ATOMIC_ACQ_REL);
    const int d = __atomic_exchange_n(&s_detents, 0, __ATOMIC_ACQ_REL);
    if (both) { if (ui_screen_on()) ui_sleep(); else ui_wake(); printf("APP key both\n"); }
    if (d) { ui_knob_inject(d); printf("APP key detents=%d\n", d); }
    const int v = __atomic_exchange_n(&s_vol, 0, __ATOMIC_ACQ_REL), nx = __atomic_exchange_n(&s_next, 0, __ATOMIC_ACQ_REL), pp = __atomic_exchange_n(&s_pp, 0, __ATOMIC_ACQ_REL);
    if (v) ui_volume_step_dark(v);
    if (nx) { const now_ops_t *o = player_ctl_now(); if (o && o->next) o->next(); }
    if (pp && player_ctl_now()) audio_svc_pause(!audio_svc_paused());
    if (v || nx || pp) printf("APP key blind vol=%d next=%d playpause=%d\n", v, nx, pp);
}

static void task(void *arg)
{
    (void)arg;
    pc_keys_init(&s_k);
    for (;;) {
        bool top, bottom;
        bsp_keys_read(&top, &bottom);
        if (s_key_swap) { const bool t = top; top = bottom; bottom = t; }   /* rotate 1 (180 degrees): the physical upper key is the lower one on screen (pc_rotate.h) */
        const uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
        s_k.hold_ms = (uint16_t)lab_get(PC_LAB_KEY_HOLD_MS); s_k.rep_ms = (uint16_t)lab_get(PC_LAB_KEY_REP_MS); s_blind.dbl_ms = (uint16_t)lab_get(PC_LAB_KEY_DBL_MS);   /* LAB: the key timings, read per poll */
        const int e = pc_keys_step(&s_k, top, bottom, now);
        const bool dark = s_blind_on && !ui_screen_on() && player_ctl_now() != NULL;   /* a playing (or paused) source and a dark screen: the keys are its remote */
        if (e == PC_KEYS_BOTH) __atomic_add_fetch(&s_both, 1, __ATOMIC_ACQ_REL);
        else if (dark) {
            int a = (e == PC_KEYS_UP || e == PC_KEYS_DOWN) ? pc_blind_feed(&s_blind, e, pc_keys_was_repeat(&s_k), now) : PC_BLIND_NONE;
            if (a == PC_BLIND_NONE) a = pc_blind_poll(&s_blind, now);
            if (a == PC_BLIND_VOL_UP) __atomic_add_fetch(&s_vol, 1, __ATOMIC_ACQ_REL);
            else if (a == PC_BLIND_VOL_DOWN) __atomic_add_fetch(&s_vol, -1, __ATOMIC_ACQ_REL);
            else if (a == PC_BLIND_NEXT) __atomic_add_fetch(&s_next, 1, __ATOMIC_ACQ_REL);
            else if (a == PC_BLIND_PLAYPAUSE) __atomic_add_fetch(&s_pp, 1, __ATOMIC_ACQ_REL);
        } else {
            s_blind.pend = 0;                                                       /* lit again (or nothing to remote): a waiting click is dropped */
            if (e) __atomic_add_fetch(&s_detents, e, __ATOMIC_ACQ_REL);
        }
        if (e) s_events_seen++;
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    }
}

static void start(bool blind)
{
    s_blind_on = blind;
    pc_blind_init(&s_blind);
    s_key_swap = pc_rotate_map(PC_ROT_SQUARE, bsp_display_rotation()).key_swap;
    esp_err_t e = bsp_keys_init();
    printf("APP keys init=%s pmu=%d\n", esp_err_to_name(e), bsp_pmu_present());
    lv_timer_create(drain_cb, DRAIN_MS, NULL);
    xTaskCreatePinnedToCoreWithCaps(task, "keys", 3072, NULL, 4, NULL, 0, MALLOC_CAP_SPIRAM);   /* PSRAM stack (PSR, FakePod): 2 GPIO/I2C reads every POLL_MS, no flash write */
}

/* call with the LVGL lock held (app_main does): the drain timer belongs to the LVGL task */
void keys_svc_start(void) { start(true); }
void keys_svc_start_host(void) { start(false); }
#else
void keys_svc_start(void) {}
void keys_svc_start_host(void) {}
#endif
