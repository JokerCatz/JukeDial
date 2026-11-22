// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "pc/pc_extdesc.h"
/* 外接模式 T4 / T5: the sound card's USB side: the UAC2 class requests (clock rate, feature-unit mute / volume: pc_extuac), the alternate settings, the feedback word and the
 * pump task (T5) that moves the audio endpoint's data into the ring of ext_audio.c (everything about audio_svc is there). Firmware only (TinyUSB callbacks); the simulator's
 * `ext usb` / `ext uac` console drives pc_extuac, ext_audio.c and the descriptor directly. */
void ext_uac_init(const pc_extdesc_info_t *info);   /* the advertised rates, once at the mode start (before tinyusb_driver_install) */
void ext_uac_bus_gone(void);                      /* the cable went: alt 0 (the status page and the knob role fall back) */
void ext_uac_start(void);                          /* after tinyusb_driver_install: the pump task (priority EXT_PRIO_UAC) */
