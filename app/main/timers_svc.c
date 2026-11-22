// SPDX-License-Identifier: LGPL-2.1-or-later
/* Stopwatch and countdown service (timers_svc.h). */
#include "timers_svc.h"
#include <stdio.h>
#include "esp_attr.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "bsp/bsp_haptic.h"
#include "cfg.h"
#include "sfx.h"
#include "ui/font.h"
#include "ui/ui.h"

static EXT_RAM_BSS_ATTR pc_sw_t s_sw;
static EXT_RAM_BSS_ATTR pc_cd_t s_cd;
static EXT_RAM_BSS_ATTR bool s_page;
static EXT_RAM_BSS_ATTR int s_done, s_msgs, s_beeps, s_last_beep_s;   /* s_last_beep_s: the second that beeped + 1, 0 none (PSRAM: no initialiser) */

int64_t timers_now_ms(void) { return esp_timer_get_time() / 1000; }
pc_sw_t *timers_sw(void) { return &s_sw; }
pc_cd_t *timers_cd(void) { return &s_cd; }
void timers_sw_toggle(void) { if (s_sw.running) pc_sw_pause(&s_sw, timers_now_ms()); else pc_sw_start(&s_sw, timers_now_ms()); }
void timers_sw_lap(void) { pc_sw_lap(&s_sw, timers_now_ms()); }
void timers_sw_reset(void) { pc_sw_reset(&s_sw); }
void timers_cd_toggle(void)
{
    int64_t now = timers_now_ms();
    if (s_cd.state == PC_CD_RUN) pc_cd_pause(&s_cd, now);
    else { pc_cd_start(&s_cd, now); s_last_beep_s = 0; }
}
void timers_cd_reset(void) { pc_cd_reset(&s_cd); }
void timers_cd_set(int secs)
{
    pc_cd_set(&s_cd, secs);
    if (s_cd.state == PC_CD_IDLE) cfg_set_int("cd_set", s_cd.set_s);
}
void timers_cd_page(bool open) { s_page = open; }
void timers_cd_jump(int secs_left) { if (s_cd.state == PC_CD_RUN) s_cd.end_ms = timers_now_ms() + (int64_t)secs_left * 1000; }

static void sound(const char *name)
{
    int id = pc_sfx_id(sfx_bank(), name);
    if (id >= 0) sfx_audition(id);                 /* an alarm: sounds whatever the toy-sound setting says */
}
static void tick(lv_timer_t *t)
{
    (void)t;
    int64_t now = timers_now_ms();
    if (s_cd.state == PC_CD_RUN) {
        int64_t left = pc_cd_left_ms(&s_cd, now);
        int sec = (int)((left + 999) / 1000);
        if (left > 0 && sec <= 3 && sec + 1 != s_last_beep_s) { s_last_beep_s = sec + 1; s_beeps++; sound("timer_warn"); }
    }
    if (pc_cd_tick(&s_cd, now)) {
        s_done++;
        bsp_haptic_play(47);                       /* a firm buzz (the FakePod has no motor: the shim does nothing) */
        sound("timer_end");
        if (!s_page) { s_msgs++; ui_push_message(T(S_CD_TIME_UP) /* Countdown finished */); }
        printf("APP timers countdown done page=%d\n", s_page);
    }
}
void timers_start(void)
{
    pc_cd_init(&s_cd, cfg_int("cd_set", 300));
    lv_timer_create(tick, 200, NULL);
}
void timers_print(void)
{
    int64_t now = timers_now_ms();
    printf("APP timers sw=%s el=%lld laps=%d cd=%s left=%lld set=%d done=%d msgs=%d beeps=%d\n", s_sw.running ? "run" : pc_sw_elapsed(&s_sw, now) ? "pause" : "idle",
           (long long)pc_sw_elapsed(&s_sw, now), s_sw.nlap, s_cd.state == PC_CD_RUN ? "run" : s_cd.state == PC_CD_PAUSE ? "pause" : s_cd.state == PC_CD_DONE ? "done" : "idle",
           (long long)pc_cd_left_ms(&s_cd, now), s_cd.set_s, s_done, s_msgs, s_beeps);
}
