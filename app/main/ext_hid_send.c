// SPDX-License-Identifier: LGPL-2.1-or-later
/* 外接模式 T2: the HID send queue's owner (ext_hid_send.h). The queue is one PSRAM static (this file is built with -msmall-data-limit=0); the stall / release rules are pc_extsend_pump. */
#include "ext_hid_send.h"
#include "esp_attr.h"

EXT_RAM_BSS_ATTR static pc_extsend_t s_q;

void ext_send_init(void) { pc_extsend_init(&s_q); }
bool ext_send_key(const pc_extkeys_key_t *k) { return pc_extsend_key(&s_q, k); }
bool ext_send_text(const char *ascii) { return pc_extsend_text(&s_q, ascii); }
int ext_send_knob(int role, int detents) { return pc_extsend_knob(&s_q, role, detents); }
int ext_send_pending(void) { return pc_extsend_count(&s_q); }
uint32_t ext_send_drops(void) { return s_q.drops; }
void ext_send_flush(void) { pc_extsend_flush_up(&s_q, NULL); }
void ext_send_flush_up(ext_send_sink_fn sink) { pc_extsend_flush_up(&s_q, sink); }
void ext_send_pump(uint32_t now_ms, ext_send_sink_fn sink) { pc_extsend_pump(&s_q, now_ms, sink); }
