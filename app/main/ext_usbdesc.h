// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "pc/pc_extdesc.h"
/* 外接模式 T4: the USB configuration descriptor of the external mode for one function mix (pc_extcombo_t), built from the settings: the 有線最高取樣率 cap
 * (cfg dac_max_rate, normalised; read once at the mode start, a change needs a new entry = a new enumeration), 16/24/32 bit, the real HID report map length.
 * Firmware (usb_mode.c) and simulator (`ext usb dump`) call the same function. Strings: 4 drive, 5 HID, 6 sound card. */
int ext_usbdesc_build(int combo, uint8_t *buf, size_t cap, pc_extdesc_info_t *info, pc_extdesc_fifo_t *fifo);   /* length, or the pc_extdesc_build error */
int ext_usbdesc_combo_norm(int stored);      /* a stored value out of range = the three-in-one default */
