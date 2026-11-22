// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "pc/pc_timers.h"
/* Stopwatch and countdown timer (HUD task): the state lives here, not on a page, so both keep running while you leave the page (music plays on).
 * One 200 ms LVGL timer checks the countdown: the last seconds beep (sfx bank "timer_warn"), at zero a firm haptic (round board; the FakePod has no motor),
 * the sfx bank's "timer_end" and, when the countdown page is not open, a message. Sounds go through sfx (before the device gain), never a mix point of their own.
 * LVGL task only. */
void timers_start(void);                       /* boot: loads the countdown's set time (cfg cd_set), starts the timer */
int64_t timers_now_ms(void);                   /* the monotonic clock both use */
pc_sw_t *timers_sw(void);
pc_cd_t *timers_cd(void);
void timers_sw_toggle(void);                   /* start / pause / resume */
void timers_sw_lap(void);
void timers_sw_reset(void);
void timers_cd_toggle(void);                   /* start / pause / resume; from done: starts again */
void timers_cd_reset(void);
void timers_cd_set(int secs);                  /* idle / done only; stored */
void timers_cd_page(bool open);                /* the countdown page tells it is on screen (no message then) */
void timers_print(void);                       /* console `timers`: "APP timers sw=<state> el=<ms> laps=<n> cd=<state> left=<ms> set=<s> done=<n> msgs=<n> beeps=<n>" */
void timers_cd_jump(int secs_left);            /* console test: the running countdown has only secs_left left */
