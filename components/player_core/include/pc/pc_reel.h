// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "pc/pc_fortune.h"   /* pc_rng_t */
#ifdef __cplusplus
extern "C" {
#endif
/* Reels (小工具: slot machine now, list pickers later; spec an internal note).
 * Pure logic, no statics: weighted pick, reel strips, the spin's motion curve, slot paylines. */

/* index i with probability w[i] / sum(w); -1 when n <= 0 or every weight is 0 */
int pc_weighted_pick(pc_rng_t *r, const uint16_t *w, int n);

/* A reel strip of len stops in which symbol s appears counts[s] times (sum(counts) must equal len), spread out:
 * no symbol sits next to itself (cyclic) when that is possible. Returns len, or -1 on a bad count sum. */
int pc_reel_strip(pc_rng_t *r, const uint8_t *counts, int nsym, uint8_t *strip, int len);

/* ---- motion ----
 * Position is in cells, absolute (it only grows); the shown stop is floor(pos) mod len. A spin: speed up over
 * accel_ms, cruise, slow down over decel_ms (cubic ease-out, its start speed equals the cruise speed) to
 * target + overshoot, then settle back onto target over settle_ms (smoothstep). Reel i begins to slow down at
 * t0 + spin_ms + i * stagger_ms; its cruise speed is fitted (near v_nom) so it lands on the wanted stop exactly. */
typedef struct {
    float v_nom;          /* cells per second while cruising */
    int accel_ms, spin_ms, stagger_ms, decel_ms, settle_ms;
    float overshoot;      /* cells past the stop before settling back */
    int early_stagger_ms; /* stop on request: reel i starts slowing this much after reel i-1 */
} pc_reel_cfg_t;
extern const pc_reel_cfg_t pc_reel_default;

typedef struct {
    int32_t t0, t_acc, t_dec, t_set, t_end;   /* start, cruise from, slow from, settle from, still from (ms) */
    double p0, v, s, target, ov;              /* v in cells per ms; s = distance slowing down (to target + ov) */
} pc_reel_motion_t;

/* plan reel i (0-based) of a spin starting at t0 from position p0 to land on stop (0..len-1) */
void   pc_reel_plan(pc_reel_motion_t *m, double p0, int stop, int len, int32_t t0, int i, const pc_reel_cfg_t *c);
/* a stop request at now: a reel still cruising starts slowing at its next chance on or after
 * max(now, cruise start) + i * early_stagger_ms (within len / v); the stop it lands on does not change.
 * Returns true if the plan changed. */
bool   pc_reel_stop_early(pc_reel_motion_t *m, int len, int32_t now, int i, const pc_reel_cfg_t *c);
double pc_reel_pos(const pc_reel_motion_t *m, int32_t t);
static inline bool pc_reel_landed(const pc_reel_motion_t *m, int32_t t) { return t >= m->t_set; }   /* reached the overshoot */
static inline bool pc_reel_done(const pc_reel_motion_t *m, int32_t t) { return t >= m->t_end; }
int    pc_reel_stop_at(double pos, int len);   /* floor(pos) mod len */

/* ---- 3x3 slot machine ----
 * Symbols: 0 cherry 1 lemon 2 grape 3 bell 4 star 5 diamond 6 seven 7 clover (wild: stands for any symbol).
 * grid[row][reel], row 0 at the top. Lines: three rows, then the two diagonals. A line pays three of a kind
 * (wilds fill in; three clovers pay their own), else two cherries (or wilds) from the left. */
enum { PC_SYM_CHERRY, PC_SYM_LEMON, PC_SYM_GRAPE, PC_SYM_BELL, PC_SYM_STAR, PC_SYM_DIAMOND, PC_SYM_SEVEN, PC_SYM_CLOVER, PC_SYM_N };
#define PC_SLOT_REELS 3
#define PC_SLOT_ROWS 3
#define PC_SLOT_LINES 5
#define PC_SLOT_LEN 27
extern const uint8_t pc_slot_counts[PC_SYM_N];    /* stops of each symbol on every reel (sum PC_SLOT_LEN) */
extern const uint16_t pc_slot_pay3[PC_SYM_N];     /* three of a kind */
#define PC_SLOT_PAY_CHERRY2 2
extern const uint8_t pc_slot_line_rows[PC_SLOT_LINES][PC_SLOT_REELS];   /* row on each reel */
/* fills grid from the stops (top row = stop, then stop+1, stop+2 on each strip) */
void pc_slot_grid(const uint8_t *const strips[PC_SLOT_REELS], int len, const int stops[PC_SLOT_REELS], uint8_t grid[PC_SLOT_ROWS][PC_SLOT_REELS]);
/* points per line into line_pay (0 = no win; may be NULL); returns the total */
int pc_slot_eval(const uint8_t grid[PC_SLOT_ROWS][PC_SLOT_REELS], int line_pay[PC_SLOT_LINES]);
/* what paid on each line, for the page to say so (R22: the same rules and pays as pc_slot_eval).
 * line 0..4; sym = the symbol that paid (PC_SYM_CHERRY for the two-cherry rule); cells = 3, or 2 for the two-cherry rule
 * (the line's first two cells from the left); wild = number of clovers that stood in; pay = points. */
typedef struct { uint8_t line; uint8_t sym; uint8_t cells; uint8_t wild; uint16_t pay; } pc_slot_win_t;
/* returns the number of paying lines (in line order); the sum of pay is what pc_slot_eval returns */
int pc_slot_wins(const uint8_t grid[PC_SLOT_ROWS][PC_SLOT_REELS], pc_slot_win_t out[PC_SLOT_LINES]);
#ifdef __cplusplus
}
#endif
