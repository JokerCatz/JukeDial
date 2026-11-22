// SPDX-License-Identifier: LGPL-2.1-or-later
#include "stats_svc.h"
#include "pc/pc_paths.h"
#include <stdio.h>
#include <sys/stat.h>
#include "cfg.h"
#include "lvgl.h"
#include "lowmem.h"
#include "pc/pc_lowmem.h"

#define STATS_PATH PC_CFG_DIR "/stats.txt"
pc_stats_t *g_stats;

void stats_svc_flush(void)
{
    if (!g_stats || !pc_stats_dirty(g_stats) || !g_sd_ok) return;
    pc_int_mkdir(PC_CFG_DIR);
    printf("APP stats saved rc=%d\n", pc_stats_save(g_stats, STATS_PATH));
}

static bool s_test;
void stats_svc_test_mode(void)
{
    if (!s_test) printf("APP stats counting off until reboot (console test)\n");
    s_test = true;
}
void stats_svc_add_play(const char *id)
{
    if (s_test) { printf("APP stats play not counted (test) id=%s\n", id); return; }
    pc_stats_add_play(g_stats, id);
}

static void timer_cb(lv_timer_t *t)
{
    (void)t;
    if (lowmem_level() >= PC_LOWMEM_NO_BACKGND) { printf("APP stats save deferred reason=lowmem\n"); return; }   /* next tick saves */
    stats_svc_flush();
}

void stats_svc_start(void)
{
    g_stats = pc_stats_create();
    if (g_sd_ok) pc_stats_load(g_stats, STATS_PATH);
    lv_timer_create(timer_cb, 5 * 60 * 1000, NULL);
}
