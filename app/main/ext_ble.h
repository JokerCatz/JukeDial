// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include "pc/pc_extsend.h"
/* 外接模式 T3: BLE HID (HOGP) keyboard + consumer control of the external mode, on IDF esp_hid (Bluedroid, BLE only: the classic half of the controller is
 * released). One report map, the same ids as the USB HID (consumer 1, keyboard 2). Just Works pairing, bonds in NVS. The stack is started once at the mode
 * start and kept (no deinit); it advertises only when ext_xport (pc_xport) says so. Firmware: ext_ble.c; simulator: sim/fake_ext_ble.c. Compiled in the external mode only. */
int ext_ble_init(void);                  /* once: controller + Bluedroid + the HID device; 0 ok (the keys then stay on USB), else the transport is unavailable */
bool ext_ble_ready(void);
void ext_ble_advertise(bool on);         /* connectable advertising on / off (the link stays) */
bool ext_ble_advertising(void);
bool ext_ble_connected(void);
const char *ext_ble_peer(void);          /* the connected computer's address tail "AA:BB:CC", "" when none */
int ext_ble_bonded(void);                /* bonded computers */
void ext_ble_unpair(void);               /* forget every bond and drop the link */
void ext_ble_disconnect(void);
bool ext_ble_sink(const pc_extsend_ev *e);   /* ext_send_pump's sink over BLE: false = the link is not ready (the pump retries, then drops the event) */
const char *ext_serial(void);         /* "JD-R-XXXXXX" (round) / "JD-S-XXXXXX" (FakePod): last 3 bytes of the base MAC (eFuse, read only), uppercase; USB serial and BLE HID serial */
