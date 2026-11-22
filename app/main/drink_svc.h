// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "pc/pc_habit.h"
/* 喝水提醒 service (ideas G7, 時鐘 family): the settings and the day's cups (pc_habit.h has the rules), saved through gamesave_svc (keys drink.on, drink.int, drink.st,
 * drink.en, drink.goal, drink.day, drink.cups; games.txt), and the reminder itself. LVGL task only (clock_svc's 1 s tick calls drink_svc_tick).
 * Reminder: bsp_haptic_play(47) (the firm buzz the pomodoro chime uses) and the bank's "ok" (sfx_play: it follows the volume, plays over music, the 小工具音效 setting
 * silences it, the buzz stays). It never wakes the screen, pushes no page and never interrupts a ringing alarm: with the screen off the owner feels and hears it
 * and the sleep rules stay exactly as they are; the 喝水 page (when open) just shows the new "next" time.
 * Console `drink`: "" -> "APP drink on= int= st= en= goal= cups= day= next=<HH:MM|--> reminded=<n>"; `drink cup|undo|snooze|reset`, `drink tap <i>` (a touch on a sector), `drink ring`,
 * `drink set <on> <int> <st> <en> <goal> <cups>` (as the ring would; the day is today), `drink tick` (one service tick now: tests). Statics are PSRAM .bss (-msmall-data-limit=0). */
enum { DRINK_ON, DRINK_INTERVAL, DRINK_START, DRINK_END, DRINK_GOAL };
void drink_svc_start(void);                               /* after gamesave_start */
void drink_svc_tick(void);                                /* once a second, only while the clock is valid */
const pc_drink_cfg_t *drink_cfg(void);
const pc_drink_t *drink_state(void);
void drink_today(void);                                   /* the state is for today (midnight rollover); saves when the day changed */
bool drink_step(int axis, int d);                         /* an axis of the settings (DRINK_ON toggles, d ignored); false: not an axis */
void drink_cup(void);
void drink_undo(void);
void drink_snooze(void);
int drink_next_min(void);                                 /* minute of the local day of the next reminder, -1 none pending (off, goal reached, outside the window) */
void drink_cmd_state(const char *arg);                    /* the state half of the console (scr_drink.c adds the page half) */
