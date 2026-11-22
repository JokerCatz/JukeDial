// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
/* 呼吸教練 / Breathing coach (ideas G7, inside the white-noise app): the pure timing, no UI (host-tested: test_breath). One pc_br_step() is one fixed step of
 * 1/30 s (ui_arena's tick). A cycle is four phases (inhale, hold, exhale, hold; a phase of 0 s is skipped):
 *   4-7-8  4 / 7 / 8 / 0     box 4-4-4-4  4 / 4 / 4 / 4     coherent 5-5  5 / 0 / 5 / 0
 * A session is 3, 5 or 10 minutes: it ends exactly when its time is up (done), and the circle stays where it is.
 * level (permille 0..1000): the guide circle's size, a smooth ease (inhale 0 -> 1000, hold 1000, exhale 1000 -> 0, hold 0).
 * Noise follow: pc_br_noise_pm(level, depth) is the noise gain in permille, 1000 - depth * (1000 - level) / 1000: never above 1000 (the user's own level),
 * never below 1000 - depth (depth is clamped to PC_BR_DEPTH_MAX = 500: a half). */
enum { PC_BR_478 = 0, PC_BR_BOX, PC_BR_55, PC_BR_PATTERNS };
enum { PC_BR_IN = 0, PC_BR_HOLD, PC_BR_OUT, PC_BR_REST, PC_BR_PHASES };
enum { PC_BR_EV_PHASE = 1, PC_BR_EV_DONE = 2 };
#define PC_BR_FPS 30
#define PC_BR_DEPTH_MAX 500
#define PC_BR_SESSIONS 3
typedef struct {
    uint8_t pattern, phase;
    bool done;
    int phase_tick;                  /* steps into the phase */
    int phase_ticks;                 /* the phase's length in steps */
    int elapsed, total;              /* steps */
    int cycles;                      /* completed cycles */
} pc_br_t;
int pc_br_session_s(int session);                 /* 0 -> 180, 1 -> 300, 2 -> 600 (out of range: 180) */
int pc_br_phase_s(int pattern, int phase);        /* seconds of a phase (0 when skipped; out of range pattern: 4-7-8) */
const char *pc_br_pattern_id(int pattern);        /* "478", "box", "55" */
void pc_br_init(pc_br_t *b, int pattern, int session);   /* at the start of the inhale */
int  pc_br_step(pc_br_t *b);                      /* one step; PC_BR_EV_* mask (PHASE: b->phase is the new phase; DONE: the session ended); nothing once done */
int  pc_br_level(const pc_br_t *b);               /* 0..1000 */
int  pc_br_phase_left_s(const pc_br_t *b);        /* seconds left in the phase, rounded up (the countdown) */
int  pc_br_left_s(const pc_br_t *b);              /* seconds left in the session, rounded up */
int  pc_br_noise_pm(int level, int depth);
/* The noise follow's hold (the audio task's side): the UI bumps a request counter on each call; the task keeps a 64-bit microsecond deadline and renews it
 * whenever it sees a new count, so the follow lasts `hold_us` after the last call and no 32-bit clock wraps. Zero-initialised: off until the first count. */
typedef struct { uint32_t seen; int64_t until; } pc_br_hold_t;
bool pc_br_hold_on(pc_br_hold_t *h, uint32_t req, int64_t now_us, int64_t hold_us);
