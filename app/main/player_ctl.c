// SPDX-License-Identifier: LGPL-2.1-or-later
#include "player_ctl.h"
#include "lvgl.h"

static player_event_fn s_fn;
static const now_ops_t *s_ops;
static void (*s_forget)(void);
void (*player_ctl_on_change)(void);

static void poll_cb(lv_timer_t *t)
{
    (void)t;
    aud_event_t e;
    while (xQueueReceive(audio_svc_events(), &e, 0) == pdTRUE)
        if (s_fn) s_fn(&e);
}

void player_ctl_start(void) { lv_timer_create(poll_cb, 100, NULL); }

void player_ctl_take(const now_ops_t *ops, void (*forget)(void), player_event_fn fn)
{
    if (s_ops != ops && s_forget) s_forget();
    s_ops = ops;
    s_forget = forget;
    s_fn = fn;
}

void player_ctl_release(void)
{
    s_ops = NULL;
    s_forget = NULL;
    s_fn = NULL;
    player_ctl_changed();
}

const now_ops_t *player_ctl_now(void) { return s_ops; }
void player_ctl_changed(void) { if (player_ctl_on_change) player_ctl_on_change(); }
