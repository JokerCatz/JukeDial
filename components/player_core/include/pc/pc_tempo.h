// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Tempo sources for the metronome (instrument spec 7, decision 13.5).
 *
 * Tap tempo: taps closer than 2 s form a run; from the 4th tap the tempo is the mean of the run's last
 * (up to) 4 intervals. A tap within 120 ms of the last is a bounce and ignored; an interval under half or over
 * twice the run's mean starts a new run from the previous tap. */
#define PC_BPM_MIN 30
#define PC_BPM_MAX 300
typedef struct { int64_t t[5]; int n; } pc_tap_t;
void pc_tap_reset(pc_tap_t *t);
int  pc_tap(pc_tap_t *t, int64_t ms);             /* BPM once 4 taps are in the run (clamped 30..300), else 0 */

/* MIDI clock follower (24 ticks a beat). Tick times are local microseconds, already mapped from the BLE
 * MIDI sender timestamps (pc_midits_map), so most connection-interval jitter is gone before this.
 * Smoothing: a least-squares line (time against tick number) through the last PC_CLK_WIN ticks; the slope is
 * the tick period, the line predicts beat times. 3 ticks in a row off the line by more than a quarter period
 * (all on the same side) is a tempo jump: the fit restarts from those three. A gap over 1 s starts over.
 * 0xFA: the next tick is beat 0; 0xFB: runs on from the tick count; 0xFC: stops (the fit keeps going). */
#define PC_CLK_WIN 48
typedef struct {
    int64_t t[PC_CLK_WIN];       /* ring of tick times; t[(n - 1) % WIN] is tick n - 1 */
    uint32_t n;                  /* ticks seen since the last reset */
    int count;                   /* ticks in the window */
    int64_t t0;                  /* fit: time(tick k) = t0 + a + b * (k - k0) */
    uint32_t k0;
    double a, b;
    int fit_n;                   /* ticks behind the fit, 0: none */
    int side, off_run;           /* tempo-jump detector */
    bool running;
    uint32_t start;              /* tick number of beat 0 */
} pc_clk_t;
void pc_clk_reset(pc_clk_t *c);
void pc_clk_tick(pc_clk_t *c, int64_t t_us);
void pc_clk_start(pc_clk_t *c);
void pc_clk_continue(pc_clk_t *c);
void pc_clk_stop(pc_clk_t *c);
/* a tempo is known (6+ ticks) and the last tick is under max(500 ms, 3 periods) old */
bool pc_clk_locked(const pc_clk_t *c, int64_t now_us);
double pc_clk_bpm(const pc_clk_t *c);              /* 0 when no fit */
double pc_clk_period_us(const pc_clk_t *c);        /* one tick, 0 when no fit */
/* predicted local time of beat k (tick start + 24 k); false when no fit */
bool pc_clk_beat_time(const pc_clk_t *c, uint32_t k, int64_t *t_us);
/* the first beat whose predicted time is at or after t_us (0 if beat 0 is still ahead) */
uint32_t pc_clk_beat_after(const pc_clk_t *c, int64_t t_us);
#ifdef __cplusplus
}
#endif
