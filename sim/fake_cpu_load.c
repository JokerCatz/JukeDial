// SPDX-License-Identifier: LGPL-2.1-or-later
/* Fakes cpu_load (per-core CPU load from FreeRTOS idle-task run-time stats). */
/* Behaviour: a fixed 0 % load. */
/* Empty: nothing else. */
#include "cpu_load.h"

void cpu_snap(cpu_snap_t *s) { s->idle[0] = s->idle[1] = 0; s->t_us = 0; }
void cpu_load(const cpu_snap_t *a, const cpu_snap_t *b, float out_pct[2]) { (void)a; (void)b; out_pct[0] = out_pct[1] = 0; }
