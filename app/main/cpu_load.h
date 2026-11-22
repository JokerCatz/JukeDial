// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
/* Per-core CPU load from FreeRTOS run-time stats of the idle tasks. */
typedef struct { unsigned long idle[2]; long long t_us; } cpu_snap_t;
void cpu_snap(cpu_snap_t *s);
/* Load in percent per core between two snapshots. */
void cpu_load(const cpu_snap_t *a, const cpu_snap_t *b, float out_pct[2]);
