// SPDX-License-Identifier: LGPL-2.1-or-later
#include "soak_log.h"
#include "pc/pc_paths.h"
#include <stdio.h>
#include <sys/stat.h>
#include "audio_svc.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "player_ctl.h"
#include "power_svc.h"

#define FLAG PC_FLAG_SOAK
#define DIR_ PC_LOGS_DIR
#define PATH DIR_ "/soak.txt"

static void line(const char *fmt_tail)
{
    FILE *f = fopen(PATH, "a");
    if (!f) return;
    fputs(fmt_tail, f);
    fclose(f);
}

static void tick(lv_timer_t *t)
{
    (void)t;
    aud_acc_t a;
    audio_svc_take_acc(&a);
    aud_status_t st;
    audio_svc_status(&st);
    const now_ops_t *o = player_ctl_now();
    char title[64] = "";
    if (o && o->playing()) o->title(title, sizeof title);
    char b[256];
    snprintf(b, sizeof b, "t=%lld state=%d underruns=%lu blk_max_us=%lu cpu1=%.0f int_free=%u int_min=%u psram_free=%u pwr=%d now=%s\n",
             esp_timer_get_time() / 1000000, st.state, (unsigned long)a.underruns, (unsigned long)a.blk_max_us, a.cpu1,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL), (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM), power_svc_state(), title);
    line(b);
}

void soak_log_start(int reset_reason)
{
    struct stat s;
    if (stat(FLAG, &s) != 0) return;
    pc_int_mkdir(DIR_);
    char b[96];
    snprintf(b, sizeof b, "boot reset_reason=%d\n", reset_reason);
    line(b);
    lv_timer_create(tick, 60000, NULL);
    printf("APP soak log on -> %s\n", PATH);
}
