// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* 外接模式 T4: the one HID report map of the external mode, shared by USB (usb_mode.c) and BLE (ext_ble.c): consumer control (id 1, 16-bit usage),
 * keyboard (id 2, modifiers + reserved + six keys, LED output), mouse (id 3: five buttons, X, Y, wheel, horizontal wheel; only the wheel is used: the knob's
 * `scroll` role). Report payload sizes (without the id) are below. */
#define PC_EXTHID_RID_CONSUMER 1
#define PC_EXTHID_RID_KEYBOARD 2
#define PC_EXTHID_RID_MOUSE    3
#define PC_EXTHID_LEN_CONSUMER 2
#define PC_EXTHID_LEN_KEYBOARD 8
#define PC_EXTHID_LEN_MOUSE    5      /* buttons, x, y, wheel, pan */
#define PC_EXTHID_MAP_LEN 171    /* sizeof pc_exthid_map (a constant for static tables; pc_exthid.c asserts it) */
extern const uint8_t pc_exthid_map[];
extern const uint16_t pc_exthid_map_len;
#ifdef __cplusplus
}
#endif
