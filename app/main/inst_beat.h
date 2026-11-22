// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
/* Beat task (stage 3; core 0, priority 21: above LVGL and Bluedroid): the engine posts each beat about 30 ms before it is heard. This
 * task, never the audio task, does what the beat needs outside the audio stream, each at its own moment:
 *  - haptics (DRV2605L on I2C0 with the touch): lead_us before the click, accent = ROM effect 1 (strong click)
 *    cut at 25 ms, other beats = effect 5 (sharp click 60 %) cut at 12 ms;
 *  - the screen: the beat is marked when it is heard, the LVGL task reads it (inst_beat_seen);
 *  - internal clock out over BLE MIDI: 0xFA at start, 24 x 0xF8 a beat from the beat's time, 0xFC at stop,
 *    each stamped with its own time. Nothing is sent while following an external clock. */
void inst_beat_start(bool haptic, int haptic_mv);
void inst_beat_set_haptic(bool on);
void inst_beat_set_lead(int us);
int  inst_beat_lead(void);
/* the last beat heard: returns its sequence number (changes with every beat), beat in bar, accent */
uint32_t inst_beat_seen(uint8_t *beat, uint8_t *accent);
/* timing since the last call: haptic GO written vs its target, and how early beats were known (ms) */
void inst_beat_stats(int *n, float *hap_p50, float *hap_p95, float *hap_max, float *known_min, uint32_t *ticks_sent);
