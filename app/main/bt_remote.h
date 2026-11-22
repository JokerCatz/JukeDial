// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
/* Bluetooth headphones remote (AVRCP, bt-remote task 1): the headset's keys (we are the target for PLAY/PAUSE/STOP/FORWARD/BACKWARD)
 * and one shared volume (we are the controller: SetAbsoluteVolume + VOLUME_CHANGE). Headphones mode only; decisions in pc_remote. */
void bt_remote_start(void);            /* headphones_on (mode task): callbacks, AVRC CT + TG init, before the A2DP init */
void bt_remote_stop(void);             /* headphones_off (mode task): remote volume ends, AVRC deinit, before the A2DP deinit */
/* console hooks for the board test (the stack part cannot be simulated) */
void bt_remote_key(uint8_t avrc_code, bool pressed);   /* what a received passthrough key runs */
void bt_remote_heard(uint8_t abs);                      /* what a received VOLUME_CHANGE notification runs */
void bt_remote_print(void);                             /* one line: APP bt hp rc avrc=.. absvol=.. abs=.. pending=.. fails=.. */
