// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
/* Sleep timer (spec goals 6, 4.3): after N minutes the sound fades out over 10 s and stops. LVGL context. */
void sleep_timer_start(void);
void sleep_timer_set(int minutes);   /* 0 = off */
int  sleep_timer_minutes(void);      /* the setting */
int  sleep_timer_left_s(void);       /* -1 when off */
