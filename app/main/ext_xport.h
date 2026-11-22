// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "ext_hid_send.h"
/* 外接模式 T3: the transport of the external mode, USB or BLE (pc_xport state machine + ext_ble). LVGL task except ext_xport_sender / ext_xport_pump (the sender task). */
void ext_xport_start(void);                  /* mode start: BLE stack once (ext_ble_init); a failure keeps the keys on USB for good */
void ext_xport_poll(void);                   /* every screen tick: the cable (usb_mode_mounted), the question's timeout, advertising on / off, queue flush when the sender changes */
int ext_xport_ask(void);                     /* 0 none, 1 switch to Bluetooth?, 2 back to USB? */
void ext_xport_answer(bool yes);
bool ext_xport_request(void);                /* the 藍牙 cell: ask for the other side now; false when the cable state does not allow it */
int ext_xport_sender(void);                  /* pc_xport_sender_t */
bool ext_xport_ble_ok(void);                 /* the BLE stack started */
bool ext_xport_link_ok(void);                /* the active sender has a link: USB enumerated, or a BLE computer connected */
/* the sender's step: the active transport's sink (USB: usb_sink when `usb_up`; BLE: ext_ble_sink); a change of sender flushes the queue */
void ext_xport_pump(uint32_t now_ms, ext_send_sink_fn usb_sink, bool usb_up);
