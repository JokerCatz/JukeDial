// SPDX-License-Identifier: LGPL-2.1-or-later
#include "sysmode.h"
#include <stdio.h>
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pc/pc_resume.h"
#include "audio_svc.h"
#include "bt_svc.h"
#include "cfg.h"
#include "crashnet.h"
#include "chip_ctl.h"
#include "library_svc.h"
#include "mp3_ctl.h"
#include "noise_engine.h"
#include "player_ctl.h"
#include "radio_svc.h"
#include "color_noise.h"
#include "fx_state.h"
#include "gamesave_svc.h"
#include "ui/font.h"
#include "ui/screens.h"
#include "ui/ui.h"

static pc_resume_t now_playing(void)
{
    pc_resume_t r = { .kind = PC_RES_NONE };
    const now_ops_t *o = player_ctl_now();
    if (!o || !o->playing || !o->playing()) return r;
    if (mp3_ctl_current() >= 0) {
        aud_status_t st;
        audio_svc_status(&st);                          /* where in the file: the song continues after the reboot */
        r = (pc_resume_t){ .kind = PC_RES_MP3, .a = mp3_ctl_folder(), .b = mp3_ctl_current(), .pos = st.pos_byte };
    }
    else if (color_noise_current() >= 0) r = (pc_resume_t){ .kind = PC_RES_COLOR, .a = color_noise_current() };
    else if (radio_current() >= 0) r = (pc_resume_t){ .kind = PC_RES_RADIO, .a = radio_current() };
    else if (noise_current() >= 0) r = (pc_resume_t){ .kind = PC_RES_NOISE, .a = noise_current() };
    else {
        int f, s;
        if (chip_now(r.name, sizeof r.name, &f, &s)) { r.kind = PC_RES_CHIP; r.a = f; r.b = s; }
    }
    return r;
}

static void restart_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(400));        /* let the message draw and the card settle */
    esp_restart();
}

void sysmode_reboot(void) { sysmode_reboot_msg(T(S_RESTARTING_TO_SWITCH_MODE) /* 重新開機切換模式… */); }

void sysmode_store_state(void)
{
    char b[PC_RESUME_MAX];
    pc_resume_t r = now_playing();
    crashnet_idle();                        /* a deliberate restart is no crash: nothing in flight (a crash inside the next boot mode must not mark this track) */
    if (pc_resume_format(&r, b, sizeof b) < 0) b[0] = 0;
    cfg_set_str("resume", b);
    fx_state_save_now();                    /* the sound as it is now, before the 3 s debounce would have written it */
    gamesave_flush();                       /* the games' bests and the tools' values: before their 10 s debounce (ideas G2) */
    int rc = cfg_save_now();
    printf("APP sysmode store resume='%s' save=%d\n", b, rc);
}

void sysmode_reboot_msg(const char *msg)
{
    sysmode_store_state();
    ui_push_message(msg);
    xTaskCreate(restart_task, "restart", 2048, NULL, 5, NULL);
}

void sysmode_resume(void)
{
    pc_resume_t r;
    const char *s = cfg_str("resume", "");
    if (!s[0]) return;
    bool ok = pc_resume_parse(s, &r);
    printf("APP sysmode resume '%s' ok=%d\n", s, ok);
    cfg_set_str("resume", "");             /* one shot: a crash while resuming does not replay it */
    cfg_save_now();
    if (!ok) return;
    if (cfg_int("bt_mode", 0) == BT_MODE_SPEAKER) { printf("APP sysmode resume dropped: speaker mode\n"); return; }   /* speaker mode is an exclusive source (ui/scr_spkask.c): what played before the reboot into it is not started */
    if (r.kind == PC_RES_MP3) {
        if (r.b >= g_lib.n || r.a >= g_lib.nfolders) return;
        mp3_ctl_play_folder_track_at(r.a, r.b, r.pos);
    } else if (r.kind == PC_RES_NOISE) {
        if (r.a >= noise_load()) return;
        noise_engine_start(r.a);
    } else if (r.kind == PC_RES_CHIP) {
        if (chip_open_system(r.name) <= r.a) return;
        chip_play(r.a, r.b);
    } else if (r.kind == PC_RES_COLOR) {
        if (color_noise_play(r.a)) return;
    } else if (r.kind == PC_RES_RADIO) {
        if (r.a >= radio_load() || radio_play(r.a)) return;
    }
    now_open();
}
