// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "pc/pc_extsend.h"
/* 外接模式 T2: the one HID send queue of the external mode (pc_extsend in PSRAM). The LVGL task fills it (a key, a text, the knob); a sender drains it
 * with ext_send_pump: the firmware's `usb_keys` task (usb_mode.c, the TinyUSB sink) or the simulator's timer (fake_usb_mode.c, a printing sink).
 * Compiled by both. The USB descriptor is not part of it. */
void ext_send_init(void);                                  /* empty queue (once per mode start) */
bool ext_send_key(const pc_extkeys_key_t *k);              /* false: refused (no room, or not a sendable key) */
bool ext_send_text(const char *ascii);                     /* the settings address; false: refused */
int ext_send_knob(int role, int detents);                  /* pc_extkeys_knob_t role; detents queued */
/* The sender: peek the due event, hand it to `sink`; sink false = the USB side is not ready (try again at the next call); after EXT_SEND_STALL_MS the event is dropped. */
#define EXT_SEND_STALL_MS PC_EXTSEND_STALL_MS
typedef bool (*ext_send_sink_fn)(const pc_extsend_ev *e);
void ext_send_pump(uint32_t now_ms, ext_send_sink_fn sink);
void ext_send_flush_up(ext_send_sink_fn sink);             /* an all-up keyboard + consumer report through `sink` (the link is up), then flush: no key stays held */
void ext_send_flush(void);                                 /* the computer went away: everything waiting is dropped */
int ext_send_pending(void);
uint32_t ext_send_drops(void);
