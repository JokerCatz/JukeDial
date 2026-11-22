// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* The rate test (hires T11, H18): the owner finds by ear what THEIR DAC plays. Pure: no RTOS, no BSP; the caller feeds the clock in milliseconds.
 * One tier at a time (48k, 96k, 192k, 384k, those <= the ceiling): tone 1 (880 Hz) at the 44.1-family rate of the tier, a gap, tone 2 (1320 Hz) at the
 * 48-family rate; a tap from tone 2's start on says "I heard both" and the next tier starts at once. The test ends at the top tier (TOP), after
 * PC_RT_TIMEOUT_MS without a tap in a tier (TIMEOUT: the one before is the answer), at a fault of the output in a tier (FAULT), or when it is left (LEFT).
 * The tiers' result is `confirmed` (0: nothing confirmed). */
#define PC_RT_TONE_MS 1200
#define PC_RT_GAP_MS 300          /* tone 2 starts at 1500 ms */
#define PC_RT_TIMEOUT_MS 8000     /* from the tier's start */
typedef enum { PC_RT_RUN, PC_RT_DONE } pc_rt_state_t;
typedef enum { PC_RT_END_TOP, PC_RT_END_TIMEOUT, PC_RT_END_FAULT, PC_RT_END_LEFT } pc_rt_end_t;
#define PC_RT_BASELINE_MS 300     /* the tier's underrun baseline is taken this long after its start */
typedef struct { uint32_t tiers[4]; int ntiers, tier; uint32_t confirmed; int32_t t0_ms; uint8_t state, end, base_done; } pc_ratetest_t;

/* tiers 48000 / 96000 / 192000 / 384000 that are <= max_rate; none: DONE at once (end TOP, confirmed 0). t0 = now_ms. */
void pc_ratetest_init(pc_ratetest_t *t, uint32_t max_rate, int32_t now_ms);
/* the rate to play at: the 44.1 family of the tier in tone 1 (tier * 147 / 160), the 48 family in tone 2, 0 otherwise (gap, after the tones, done) */
uint32_t pc_ratetest_rate_now(const pc_ratetest_t *t, int32_t now_ms);
uint32_t pc_ratetest_tone_hz(const pc_ratetest_t *t, int32_t now_ms);          /* 880 in tone 1, 1320 in tone 2, 0 otherwise */
void pc_ratetest_tick(pc_ratetest_t *t, int32_t now_ms);                       /* the tier's timeout -> DONE(TIMEOUT) */
bool pc_ratetest_tap(pc_ratetest_t *t, int32_t now_ms);                        /* accepted from tone 2's start (while running): confirms the tier, then the next tier (t0 = now) or DONE(TOP) */
/* True once per tier, from PC_RT_BASELINE_MS after the tier's start (while running): the caller takes its underrun / write-timeout counters now and counts faults only
 * from deltas after it. The tier's own reconfigure (1-2 underruns at a change into 96k) is over by then; tone 1 lasts 1200 ms and the tap window opens at 1500 ms. */
bool pc_ratetest_baseline_at(pc_ratetest_t *t, int32_t now_ms);
void pc_ratetest_fault(pc_ratetest_t *t);                                      /* underrun / write timeout in this tier -> DONE(FAULT); the tier is not confirmed */
void pc_ratetest_leave(pc_ratetest_t *t);                                      /* DONE(LEFT) while running; the caller ignores the result */
/* One tone, rendered in blocks: `frames` stereo frames into lr (L = R) at -30 dBFS (0.0316), a 20 ms raised-cosine in and out over `len` frames in all;
 * *pos counts the frames done (past len: silence), *phase is the sine's phase in turns. Float maths per sample (the audio task has no double FPU). */
void pc_ratetest_render(float *lr, int frames, uint32_t rate, double *phase, uint32_t *pos, uint32_t len, double hz);

#ifdef __cplusplus
}
#endif
