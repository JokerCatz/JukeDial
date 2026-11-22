// SPDX-License-Identifier: LGPL-2.1-or-later
/* Fakes inst_beat (the beat task: haptic tick, screen mark, MIDI clock out). */
/* Behaviour: the beat last "heard" comes from the engine fake's metronome on the virtual clock; settings are only remembered. */
/* Empty: haptics, clock out over BLE, timing statistics (all zero). */
#include "inst_beat.h"
#include "sim.h"

static int s_lead = 30000;
void inst_beat_start(bool haptic, int haptic_mv) { (void)haptic; (void)haptic_mv; }
void inst_beat_set_haptic(bool on) { (void)on; }
void inst_beat_set_lead(int us) { s_lead = us; }
int  inst_beat_lead(void) { return s_lead; }
uint32_t inst_beat_seen(uint8_t *beat, uint8_t *accent) { return fake_inst_engine_beat(beat, accent); }
void inst_beat_stats(int *n, float *p50, float *p95, float *mx, float *known, uint32_t *ticks)
{
    *n = 0; *p50 = *p95 = *mx = *known = 0; *ticks = 0;
}
