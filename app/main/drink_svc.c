// SPDX-License-Identifier: LGPL-2.1-or-later
/* 喝水提醒 service: see drink_svc.h. */
#include "drink_svc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bsp/bsp_haptic.h"
#include "clock_svc.h"
#include "gamesave_svc.h"
#include "sfx.h"

static pc_drink_cfg_t s_cfg;
static pc_drink_t s_st;
static int s_reminded;

const pc_drink_cfg_t *drink_cfg(void) { return &s_cfg; }
const pc_drink_t *drink_state(void) { return &s_st; }

static void save_cfg(void)
{
    gamesave_set("drink.on", s_cfg.on);
    gamesave_set("drink.int", s_cfg.interval);
    gamesave_set("drink.st", s_cfg.start_h);
    gamesave_set("drink.en", s_cfg.end_h);
    gamesave_set("drink.goal", s_cfg.goal);
}
static void save_day(void) { gamesave_set("drink.day", (uint32_t)s_st.day); gamesave_set("drink.cups", (uint32_t)s_st.cups); }

void drink_svc_start(void)
{
    pc_drink_cfg_default(&s_cfg);
    s_cfg.on = (uint8_t)gamesave_get_or("drink.on", s_cfg.on);
    s_cfg.interval = (uint8_t)gamesave_get_or("drink.int", s_cfg.interval);
    s_cfg.start_h = (uint8_t)gamesave_get_or("drink.st", s_cfg.start_h);
    s_cfg.end_h = (uint8_t)gamesave_get_or("drink.en", s_cfg.end_h);
    s_cfg.goal = (uint8_t)gamesave_get_or("drink.goal", s_cfg.goal);
    pc_drink_cfg_fix(&s_cfg);
    s_st = (pc_drink_t){ .day = (int32_t)gamesave_get_or("drink.day", 0), .cups = (int)gamesave_get_or("drink.cups", 0) };
    if (s_st.cups > PC_DRINK_CUPS_MAX) s_st.cups = PC_DRINK_CUPS_MAX;
}

void drink_today(void)
{
    if (!clock_time_valid()) return;                           /* no date yet: the saved day stands */
    if (pc_drink_sync_day(&s_st, pc_hb_local_day(clock_now_utc(), clock_tz_min()))) { save_day(); printf("APP drink new day=%d\n", (int)s_st.day); }
}

static void remind(void)
{
    if (clock_ringing() || clock_pomo_chiming()) return;       /* an alarm or the pomodoro chime has the device */
    bsp_haptic_play(47);
    sfx_play("ok");
    s_reminded++;
    printf("APP drink remind cups=%d goal=%d reminded=%d\n", s_st.cups, s_cfg.goal, s_reminded);
}
void drink_svc_tick(void)
{
    drink_today();
    if (pc_drink_tick(&s_st, &s_cfg, clock_now_utc(), clock_tz_min())) remind();
}

bool drink_step(int axis, int d)
{
    switch (axis) {
    case DRINK_ON: s_cfg.on = !s_cfg.on; s_st.next_due = 0; break;
    case DRINK_INTERVAL: pc_drink_step_interval(&s_cfg, d); s_st.next_due = 0; break;
    case DRINK_START: pc_drink_step_start(&s_cfg, d); s_st.next_due = 0; break;
    case DRINK_END: pc_drink_step_end(&s_cfg, d); s_st.next_due = 0; break;
    case DRINK_GOAL: pc_drink_step_goal(&s_cfg, d); break;
    default: return false;
    }
    save_cfg();
    printf("APP drink setup on=%d int=%d st=%d en=%d goal=%d\n", s_cfg.on, pc_drink_interval_min(s_cfg.interval), s_cfg.start_h, s_cfg.end_h, s_cfg.goal);
    return true;
}
void drink_cup(void)
{
    drink_today();
    pc_drink_cup(&s_st, &s_cfg, clock_now_utc());
    save_day();
    printf("APP drink cup cups=%d goal=%d\n", s_st.cups, s_cfg.goal);
}
void drink_undo(void) { drink_today(); pc_drink_undo(&s_st); save_day(); printf("APP drink undo cups=%d\n", s_st.cups); }
void drink_snooze(void) { pc_drink_snooze(&s_st, clock_now_utc()); printf("APP drink snooze\n"); }

int drink_next_min(void)
{
    if (!s_cfg.on || s_st.next_due == 0 || !clock_time_valid()) return -1;
    return pc_hb_local_minute(s_st.next_due, clock_tz_min());
}

void drink_cmd_state(const char *a)
{
    int on, in, st, en, goal, cups;
    if (!strcmp(a, "cup")) drink_cup();
    else if (!strcmp(a, "undo")) drink_undo();
    else if (!strcmp(a, "snooze")) drink_snooze();
    else if (!strcmp(a, "tick")) drink_svc_tick();
    else if (!strcmp(a, "reset")) { s_st.cups = 0; s_st.next_due = 0; save_day(); }
    else if (sscanf(a, "set %d %d %d %d %d %d", &on, &in, &st, &en, &goal, &cups) == 6) {   /* locals first: a partial match must not touch the state */
        s_cfg = (pc_drink_cfg_t){ .on = (uint8_t)on, .interval = (uint8_t)in, .start_h = (uint8_t)st, .end_h = (uint8_t)en, .goal = (uint8_t)goal };
        pc_drink_cfg_fix(&s_cfg);
        drink_today();
        s_st.cups = cups < 0 ? 0 : cups > PC_DRINK_CUPS_MAX ? PC_DRINK_CUPS_MAX : cups;
        s_st.next_due = 0;
        save_cfg();
        save_day();
    }
    else if (a[0]) { printf("APP drink unknown %s\n", a); return; }
    int nx = drink_next_min();
    char b[8];
    if (nx < 0) snprintf(b, sizeof b, "--"); else snprintf(b, sizeof b, "%02d:%02d", nx / 60, nx % 60);
    printf("APP drink on=%d int=%d st=%d en=%d goal=%d cups=%d day=%d next=%s reminded=%d\n", s_cfg.on, pc_drink_interval_min(s_cfg.interval), s_cfg.start_h, s_cfg.end_h, s_cfg.goal,
           s_st.cups, (int)s_st.day, b, s_reminded);
}
