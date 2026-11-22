// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include "pc/pc_stats.h"
/* Favourites / play counts in /sdcard/_player/config/stats.txt, saved every 5 min or on stop (spec 5.5). LVGL context only. */
extern pc_stats_t *g_stats;
void stats_svc_start(void);
void stats_svc_flush(void);
/* plays count through here; a plain console command (a test, not the settings page) stops counting until the
 * next reboot, so test playback never touches JC's play counts */
void stats_svc_add_play(const char *id);
void stats_svc_test_mode(void);
