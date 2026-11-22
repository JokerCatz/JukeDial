// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
/* BLE MIDI 1.0 peripheral "JukeDial MIDI" (Bluedroid GATT server, from an internal spike). Received packets go
 * to inst_engine_post_ble(). A twin service with private UUIDs echoes every write back as a notification: the
 * Linux test PC's PipeWire claims the standard one, so latency probes use the twin. */
int  inst_ble_start(void);          /* controller (BLE only) + Bluedroid + service + advertising; 0 ok */
bool inst_ble_connected(void);
int  inst_ble_interval_x100(void);  /* connection interval in 1/100 ms, 0 when not connected */
uint32_t inst_ble_packets(void);
/* MIDI out (clock): a notification with the given 13-bit timestamp; false when nobody listens */
bool inst_ble_send(const uint8_t *msg, int n, uint16_t ts13);
bool inst_ble_subscribed(void);
uint32_t inst_ble_sent(void);
