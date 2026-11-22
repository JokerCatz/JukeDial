// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
/* Who needs Wi-Fi: pure bookkeeping, no I/O. The radio, a clock sync, a station probe and the console each hold the radio on;
 * when the last one lets go it stays on for PC_WH_GRACE_MS (station hopping must not reconnect) and then wanted-off.
 * "Off pending" is not stored: it is on && holders == 0, and off_at_ms is then the deadline. */
#include <stdbool.h>
#include <stdint.h>

typedef enum { PC_WH_RADIO = 1, PC_WH_CLOCK = 2, PC_WH_MANUAL = 4, PC_WH_PROBE = 8 } pc_wh_who_t;
#define PC_WH_GRACE_MS 60000

typedef struct { uint8_t holders; bool on; uint32_t off_at_ms; } pc_wifihold_t;

/* each returns the wanted radio state after the call: true = Wi-Fi should be on */
bool pc_wh_hold(pc_wifihold_t *h, pc_wh_who_t who);                       /* on at once; cancels a pending off */
bool pc_wh_release(pc_wifihold_t *h, pc_wh_who_t who, uint32_t now_ms);   /* last holder gone: off after PC_WH_GRACE_MS */
bool pc_wh_tick(pc_wifihold_t *h, uint32_t now_ms);                       /* turns off when the grace is over; wrap-safe */
void pc_wh_force_off(pc_wifihold_t *h);                                   /* drops every holder, off now */
bool pc_wh_pending(const pc_wifihold_t *h);                               /* on, nobody holds: the grace is running */
