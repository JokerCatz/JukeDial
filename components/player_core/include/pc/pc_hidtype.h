// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Text -> USB HID keyboard key presses (US layout), for typing a web address. Each press is {modifier, keycode};
 * the caller sends it and then a release. Returns the count, or -1 when a character cannot be typed. */
#define PC_HID_SHIFT 0x02
#define PC_HID_ENTER 0x28
int pc_hid_type(const char *text, uint8_t (*out)[2], int max);
#ifdef __cplusplus
}
#endif
