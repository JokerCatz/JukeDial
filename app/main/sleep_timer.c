// SPDX-License-Identifier: LGPL-2.1-or-later
#include "sleep_timer.h"
#include <stdio.h>
#include "audio_svc.h"
#include "gamesave_svc.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "player_ctl.h"
#include "sdkconfig.h"

static int s_min;
static int64_t s_due_us;

static void tick(lv_timer_t *t)
{
    (void)t;
    int64_t now = esp_timer_get_time();
    if (s_due_us && now >= s_due_us) {
        s_due_us = 0;
        const now_ops_t *o = player_ctl_now();
        if (o && o->playing()) {
            printf("APP sleep fade\n");
            gamesave_flush();                     /* the games' and tools' saved numbers reach the card before the device goes quiet (ideas G2) */
            audio_svc_stop(true);                 /* the owner stops on AUD_FADED */
        }
    }
}

void sleep_timer_start(void) { lv_timer_create(tick, 1000, NULL); }

void sleep_timer_set(int minutes)
{
    s_min = minutes;
#if CONFIG_APP_TEST_SLEEP_SHORT
    int64_t unit = 1000000LL;                     /* test build: "minutes" are seconds */
#else
    int64_t unit = 60000000LL;
#endif
    s_due_us = minutes > 0 ? esp_timer_get_time() + minutes * unit : 0;
    printf("APP sleep set=%d\n", minutes);
}

int sleep_timer_minutes(void) { return s_min; }
int sleep_timer_left_s(void) { return s_due_us ? (int)((s_due_us - esp_timer_get_time()) / 1000000) : -1; }
