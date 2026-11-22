// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
/* The pure parts of the shared dial (ui_dial, ideas G2): no UI. A value in lo..hi that clamps or wraps (a lock's 0..99 wraps, a gauge's does not),
 * the distance to a target (the short way round on a wrapped dial), the haptic grade of that distance, the needle's angle, and the METER's smoothing
 * with a peak that holds and then falls (調音器 / 分貝計 / BPM later). Integers only. */
int pc_dial_wrap(int v, int lo, int hi, bool wrap);                 /* wrap: into lo..hi going round; else clamped */
int pc_dial_step(int v, int d, int lo, int hi, bool wrap);          /* v + d (any size, either sign) wrapped or clamped */
/* Plain modulo wrap, inline (per-cell loops use it): v into 0..n-1 for any v, either sign; n > 0 (no guard). pc_wrapi_step: v + d wrapped. */
static inline int pc_wrapi(int v, int n) { return ((v % n) + n) % n; }
static inline int pc_wrapi_step(int v, int d, int n) { return ((v + d) % n + n) % n; }
int pc_dial_dist(int a, int b, int lo, int hi, bool wrap);          /* |a - b|; wrapped: the shorter way round (at most half the range) */
/* Haptic grade of a distance d from the target: 3 = on it (d == 0: the distinct click of a detent), 2 = within a third of `reach` (at least 1),
 * 1 = within `reach`, 0 = beyond. reach <= 0: only the detent (3 or 0). */
int pc_dial_grade(int d, int reach);
/* The needle's angle in tenths of a degree (0 = 3 o'clock, clockwise): a0 at v == lo, a0 + span at hi on a gauge; on a wrapped dial hi + 1 is a full turn
 * (span 360: hi sits one step before lo again). */
int pc_dial_angle10(int v, int lo, int hi, bool wrap, int a0, int span);
/* The meter: shown (smoothed) and peak, in 1/16 units. feed sets the target; step is called every 1/30 s and returns true while anything moves. */
#define PC_METER_HOLD 30                                            /* steps the peak holds before it falls (1 s) */
typedef struct { int32_t shown, target, peak; int hold; uint8_t shift; bool nopeak; } pc_meter_t;   /* shift: the needle goes 1/2^shift of the way a step (0 = the default 2: a quarter); nopeak: no peak marker, shown is peak */
void pc_meter_init(pc_meter_t *m, int v);                           /* everything at v */
void pc_meter_config(pc_meter_t *m, int smooth_shift, bool peak);   /* smooth_shift 0 = default 2, else 1..6; peak false: no peak hold (peak follows shown); call right after init */
void pc_meter_feed(pc_meter_t *m, int v);                           /* the value the meter goes to */
bool pc_meter_step(pc_meter_t *m);
int  pc_meter_shown(const pc_meter_t *m);                           /* whole units, rounded */
int  pc_meter_peak(const pc_meter_t *m);
/* The proximity cue (ui_dial def->cue): how lit the cue is, 0..255, from the distance to the target zone: 0 at or beyond `reach` (or no reach), the full
 * `strength` (percent 0..100, clamped) on the target, linear between. A weaker strength is a dimmer cue, never a shorter one. */
int pc_dial_cue(int dist, int reach, int strength);
