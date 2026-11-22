// SPDX-License-Identifier: LGPL-2.1-or-later
/* Fakes inst_ble (the BLE MIDI peripheral "JukeDial MIDI"). */
/* Behaviour: a connected flag that "siminst ble 0|1" flips; no packets, no connection interval, nothing is ever sent. */
/* Empty: the stack (no controller, no GATT server, no advertising). */
#include "inst_ble.h"
#include "sim.h"

static bool s_conn;
int  inst_ble_start(void) { return 0; }
bool inst_ble_connected(void) { return s_conn; }
int  inst_ble_interval_x100(void) { return s_conn ? 1500 : 0; }
uint32_t inst_ble_packets(void) { return 0; }
bool inst_ble_send(const uint8_t *msg, int n, uint16_t ts13) { (void)msg; (void)n; (void)ts13; return false; }
bool inst_ble_subscribed(void) { return false; }
uint32_t inst_ble_sent(void) { return 0; }
void fake_inst_ble_set(bool on) { s_conn = on; }
