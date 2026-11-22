// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
/* 旋轉擋板 / Rotating deflector (ideas G6): the rules and the geometry, pure C (no UI, host-tested: test_game_deflect).
 * The field is a circle of radius PC_DF_FIELD units, centred on (0, 0); x right, y DOWN (screen axes), angles in degrees clockwise from 3 o'clock.
 * A core sits at the centre (PC_DF_CORE_R). Round the core runs the GUARD LINE, and a shield (an angular span, centre `deg`, half span pc_df_half) sweeps along
 * it. Shots are fired from the field's rim at random angles, aimed at the core; a shot that reaches the guard line inside a shield is bounced back out
 * (scores), one that reaches it outside every shield goes on and hits the core (a life). The guard line follows the field's SHAPE (one function, both
 * shapes host-tested): PC_DF_ROUND a circle of radius PC_DF_GUARD_R; PC_DF_SQUARE the four edges of a square of half side PC_DF_GUARD_Q (the shield sweeps
 * the perimeter and turns the corners; its ends are the guard line's points on the rays deg - half and deg + half; pc_df_shield_segs gives the up to
 * PC_DF_SEGS axis-aligned pieces to draw). The shape is a parameter of pc_df_init; the page picks it from the display (BSP_LCD_ROUND).
 * One pc_df_step() is one fixed step of 1/30 s (ui_arena's tick); shots move in sub-steps of at most 1 unit, so none tunnels the guard line.
 * Rules (規則, classic first):
 *   0 經典 Classic  one shield
 *   1 雙擋 Twin     two shields 180 degrees apart (they move together), each narrower
 *   2 反彈 Rebound  a bounced shot that meets another shot breaks both (a bonus; a chain of bounces is a combo)
 * Shots: speed = the mode (slow / normal / fast) x (1 + 4 % per level, at most 2x); the next shot comes sooner as the level grows; level = 1 + bounces / 10.
 * Score: a bounce 10 x (1 + combo / 5), combo = bounces since the last hit of the core; a golden shot (rare, by the seed) bounced: +1 life (at most
 * PC_DF_LIVES_MAX) and +50, and a missed golden shot costs nothing. The combo widens the shield (+1.5 degrees a combo up to +PC_DF_GROW_MAX). All random
 * choices come from the seed given at init (the game's own stream). */

#define PC_DF_FIELD      100.0f   /* the field's radius in units (the arena scales it to its px radius) */
#define PC_DF_SPAWN_R    97.0f    /* shots appear on this circle */
#define PC_DF_CORE_R     11.0f    /* the core: a shot whose centre gets this close hits it */
#define PC_DF_GUARD_R    64.0f    /* the guard line, round field */
#define PC_DF_GUARD_Q    64.0f    /* the guard line, square field: half the side (corners at 90.5 units: inside the field) */
#define PC_DF_SHOT_R     3.5f
#define PC_DF_SHOTS      8
#define PC_DF_SEGS       4
#define PC_DF_EVENTS     16
#define PC_DF_LIVES      3
#define PC_DF_LIVES_MAX  5
#define PC_DF_HALF_DEG   26.0f    /* a shield's half span (classic) */
#define PC_DF_HALF_TWIN  17.0f    /* 雙擋 */
#define PC_DF_GROW_DEG   1.5f     /* a combo's widening, each */
#define PC_DF_GROW_MAX   12.0f
#define PC_DF_SLACK_DEG  2.0f     /* a shot's own size: a hit just past the shield's end still bounces */
#define PC_DF_STEP_DEG   15.0f    /* what a tap or a detent (knob or key) turns the shield */
#define PC_DF_GOLD_N     16       /* one shot in this many is golden */
#define PC_DF_BONUS      30       /* two shots broken against each other (反彈) */
#define PC_DF_RULES      3
#define PC_DF_SPEEDS     3

enum { PC_DF_CLASSIC = 0, PC_DF_TWIN, PC_DF_REBOUND };
enum { PC_DF_ROUND = 0, PC_DF_SQUARE };                                           /* shape */
enum { PC_DF_SERVE = 0, PC_DF_PLAY, PC_DF_OVER };                                 /* state */
enum { PC_DF_EV_BOUNCE = 1, PC_DF_EV_MISS, PC_DF_EV_GOLD, PC_DF_EV_LIFE, PC_DF_EV_POP, PC_DF_EV_COMBO, PC_DF_EV_LEVEL, PC_DF_EV_OVER, PC_DF_EV_START };

typedef struct { float x, y, vx, vy; bool live, back, gold, past; } pc_df_shot_t;   /* back: bounced; past: went through the guard line (heading for the core) */
typedef struct { uint8_t kind; int16_t value; float x, y; } pc_df_ev_t;            /* value: the combo (BOUNCE, COMBO), the points (POP), the level */

typedef struct {
    int rule, speed, shape, state, level, lives;
    uint32_t score, ticks, rng;
    int combo, combo_best, bounces, misses, golds, spawn_in;
    float deg;                                   /* the first shield's centre, 0..360 */
    pc_df_shot_t shot[PC_DF_SHOTS];
    pc_df_ev_t ev[PC_DF_EVENTS];                 /* what the last call did (pc_df_step, pc_df_start): sounds, effects */
    int n_ev;
} pc_df_t;

void pc_df_init(pc_df_t *g, int rule, int speed, int shape, uint32_t seed);   /* level 1, PC_DF_LIVES, SERVE, the shield at 270 (12 o'clock) */
bool pc_df_start(pc_df_t *g);                    /* a tap: SERVE -> PLAY. false: nothing to do */
void pc_df_move(pc_df_t *g, float deg);          /* the shields turn by deg (+ clockwise) */
void pc_df_step(pc_df_t *g);                     /* one fixed step (1/30 s); its events in g->ev */
bool pc_df_running(const pc_df_t *g);            /* PLAY */
float pc_df_half(const pc_df_t *g);              /* a shield's half span now (rule, combo) */
int pc_df_shields(const pc_df_t *g);             /* 1, 2 (雙擋 (twin shields)) */
float pc_df_speed(const pc_df_t *g);             /* units a step now (mode, level) */
bool pc_df_covers(const pc_df_t *g, float deg);  /* some shield covers a shot arriving at the angle deg (slack included) */
/* the geometry of the guard line, by shape (host-tested for both) */
float pc_df_guard_dist(int shape, float deg);                                 /* the guard line's distance from the centre along the ray at deg */
void pc_df_guard_pt(int shape, float deg, float *x, float *y);                /* the guard line's point on that ray */
void pc_df_guard_normal(int shape, float x, float y, float *nx, float *ny);   /* the outward unit normal of the guard line at the point (x, y) (its ray) */
/* the pieces of a shield centred at c_deg with half span `half` along the guard line, as (x0, y0, x1, y1) in units: round 0 pieces (the page draws an arc);
 * square 1..PC_DF_SEGS axis-aligned pieces (one per edge the span touches, split at the corners). Returns the count. */
int pc_df_shield_segs(int shape, float c_deg, float half, float seg[PC_DF_SEGS][4]);
float pc_df_angdiff(float a, float b);           /* a - b wrapped into -180..180 */
