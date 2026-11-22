// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
/* Boot-loop guard: consecutive crash resets counted in RTC memory; 3 in a row -> safe mode. */
bool guard_boot(void);              /* false: stay in safe mode */
bool guard_last_reset_was_crash(void);   /* the class guard_boot() found (panic / watchdog) */
uint32_t guard_crashes(void);           /* the consecutive-crash counter now (0 until healthy clears it, 0 if not valid); read only */
void guard_arm_healthy_timer(void); /* clears the counter after 60 s of uptime */
void guard_safe_mode(const char *why) __attribute__((noreturn));
/* The very first call of app_main: is this the board the image was built for (bsp_board_ok: the FakePod's AXP2101 answers or not)? On a wrong board it prints one line and
 * waits for ever without touching another pin (the two boards' pins collide: GPIO44 is the round board's buzzer and the FakePod's AMOLED supply, GPIO36 the FakePod's flash strap...). */
void guard_board_check(void);
/* Owner 2026-11-16: an abnormal restart or the safe mode puts the Bluetooth mode back to off. guard_boot() decides (reset reason + crash counter, kept in RTC memory); this applies it once
 * cfg is loaded, right before bt_svc_start(). Best effort: no card or any failure = one log line, the boot goes on. */
void guard_bt_reset_apply(bool card_ok);
/* crashtest safe only (crashlog.c, after the card-flag and counter==0 gates): set the counter to LIMIT-1 so the next crash reset enters safe mode. The healthy timer and the BT reset see three real crashes. */
void guard_arm_for_test(void);
