// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
/* The instrument app (spec an internal note): BLE MIDI -> GME chip
 * voices -> 3.5 mm. Another app in the same image, entered by reboot like USB mode: one shot, so any later reset
 * (a crash included) boots the player. */
bool instrument_take_request(void);   /* boot: true once after instrument_request() */
bool instrument_peek_request(void);   /* the same flag, not consumed (boot_front.c) */
void instrument_request(void);        /* LVGL: saves cfg + what is playing, restarts into the instrument */
void instrument_run(void);            /* never returns */
