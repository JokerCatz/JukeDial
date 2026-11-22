// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* "Stable" timing (instrument spec 9 / decision 13.6): play BLE MIDI events at their sender timestamp plus a
 * fixed delay instead of on arrival, so the connection-interval jitter goes away.
 * The sender clock (13-bit ms, wraps every 8.192 s) is mapped to local time by the smallest seen
 * (arrival - sender time): the fastest packet. That offset creeps up 200 ppm of elapsed time so clock drift
 * cannot pull it away; a jump of more than a second (new connection, sender restart) starts over. */
typedef struct { bool have; int64_t off_us, last_us, remote_ms; } pc_midits_t;

void pc_midits_reset(pc_midits_t *s);
/* the sender time as local time (us), unclamped: about the fastest packet's arrival (MIDI clock follower) */
int64_t pc_midits_map(pc_midits_t *s, uint16_t ts13, int64_t arrival_us);
/* local time (us) the event should sound: in [arrival, arrival + delay] */
int64_t pc_midits_due(pc_midits_t *s, uint16_t ts13, int64_t arrival_us, int64_t delay_us);
#ifdef __cplusplus
}
#endif
