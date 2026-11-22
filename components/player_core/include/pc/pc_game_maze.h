// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
/* 旋轉迷宮 / Marble maze (ideas G6b): the rules of the maze rules of the knob toys page, no UI. The maze never turns on screen: the physics is that of a
 * marble in a fixed maze under a force; "turning the maze" is turning the force. Units: the field is a disc of radius PC_MZ_R; y points down; angles in degrees, 0 = 3 o'clock,
 * clockwise (the arena's). One fixed step is 1/30 s (pc_mz_step).
 * The physics takes a GRAVITY VECTOR as its input (pc_mz_set_grav: a direction, length 0..1; no source baked in): the knob sets its direction on the round board (pc_mz_turn),
 * a tilt sensor (the FakePod's IMU, later) or the console sets it directly. The central rules (斥力 / 吸力) derive their own vector from the marble's place (radial, constant length).
 * Rules:
 *   重力 (GRAV)    PC_MZ_RINGS concentric ring walls, each with ONE gap; the marble starts in the centre, the knob turns gravity PC_MZ_STEP_DEG a detent; it rolls to the wall in the "down"
 *                  direction, rolls round the channel to the gap when gravity points at it, falls through; the goal is the OUTER EXIT (r >= PC_MZ_EXIT_R): gravity points outwards
 *                  at the gap anyway, so the puzzle is lining the rings' gaps up (a centre goal would need gravity pointing inwards: that is the 吸力 rule).
 *   斥力 (REPEL)   a constant push outward; the knob moves the marble round its ring by PC_MZ_STEP_DEG a detent; it pops out through a gap when aligned; start in the centre, goal the exit.
 *   吸力 (ATTRACT) the reverse: start in the outer strip, goal the centre (r <= PC_MZ_CENTRE_R).
 *   傾斜 (TILT)    a square grid maze (PC_MZ_N x PC_MZ_N cells, a perfect maze of the seed) with trap holes (a fall = back to the start, falls counted); gravity is the vector input only
 *                  (an IMU, the console); the knob does nothing. Start top-left, goal bottom-right.
 * The gaps may DRIFT (drift 1 slow, 2 fast) in the ring rules: a ring's gap moves one degree every few steps, each ring its own way. The golden marble (gold): wider gaps, less friction.
 * The clock starts with the first move (a turn, a tap = pc_mz_start, a tilt over PC_MZ_TILT_ON). A call clears g->ev first (PC_MZ_EV_*). */
enum { PC_MZ_GRAV = 0, PC_MZ_REPEL, PC_MZ_ATTRACT, PC_MZ_TILT, PC_MZ_RULES };
enum { PC_MZ_PLAY = 0, PC_MZ_DONE };
enum { PC_MZ_EV_BOUNCE = 1, PC_MZ_EV_GAP, PC_MZ_EV_FALL, PC_MZ_EV_DONE };   /* BOUNCE: value = impact speed x10; GAP: value = the new level; FALL: value = the hole */
#define PC_MZ_TURN        360          /* degrees in a turn */
#define PC_MZ_R           100          /* the field's radius, units */
#define PC_MZ_RINGS       4
#define PC_MZ_MARBLE_R    4.5f
#define PC_MZ_WALL_HALF   2.0f         /* a ring wall's half thickness */
#define PC_MZ_STEP_DEG    10           /* a detent: gravity's direction / the marble round its ring */
#define PC_MZ_GAP_HALF    8            /* a gap's effective half width, degrees (for the marble's centre) */
#define PC_MZ_EXIT_R      93.0f
#define PC_MZ_CENTRE_R    11.0f
#define PC_MZ_N           5            /* tilt grid: cells a side */
#define PC_MZ_CELL        26.0f
#define PC_MZ_WALL_T      3.0f
#define PC_MZ_WALLS_MAX   32           /* tilt: the maze's wall rects (outer four + inner) */
#define PC_MZ_HOLES_MAX   5
#define PC_MZ_HOLE_R      6.5f
#define PC_MZ_TILT_ON     0.15f
#define PC_MZ_VMAX        6.0f         /* units a step: the marble's top speed (a wall is 2 x (WALL_HALF + MARBLE_R) = 13 thick for it, two sub-steps a step: no tunnelling) */
typedef struct { float x0, y0, x1, y1; } pc_mz_rect_t;
typedef struct {
    uint8_t rule, state, drift;
    bool started, gold;
    float x, y, vx, vy;                 /* the marble, units */
    float gx, gy;                       /* the gravity vector input, length 0..1 */
    int theta;                          /* GRAV: gravity's direction, degrees */
    int gap[PC_MZ_RINGS];               /* ring k's gap centre, degrees 0..359 */
    int8_t gdir[PC_MZ_RINGS];           /* drift direction */
    uint8_t gper[PC_MZ_RINGS], gacc[PC_MZ_RINGS];   /* steps per degree, the counter */
    int level;                          /* walls between the marble and the centre: 0..RINGS */
    int n_walls, n_holes;
    pc_mz_rect_t wall[PC_MZ_WALLS_MAX];
    float hole[PC_MZ_HOLES_MAX][2];
    float start[2], goal[2];
    int turns, falls, steps;
    uint32_t ms;
    uint32_t rng;
    int ev[6], ev_v[6], n_ev;
} pc_mz_t;
void pc_mz_init(pc_mz_t *g, int rule, int drift, uint32_t seed, bool gold);
float pc_mz_ring_r(int k);                          /* the radius of ring wall k (0 innermost) */
int  pc_mz_gap_half(const pc_mz_t *g);              /* the effective half width of a gap, degrees (gold: wider) */
void pc_mz_start(pc_mz_t *g);                       /* the clock starts (a tap) */
void pc_mz_set_grav(pc_mz_t *g, float dx, float dy);   /* the tilt vector input (length clamped to 1): applied ONLY in 傾斜 (a no-op for the other rules); a length over PC_MZ_TILT_ON starts the clock */
void pc_mz_set_theta(pc_mz_t *g, int deg);          /* 重力 only (tests, console): gravity's direction directly, a no-op for the other rules */
void pc_mz_turn(pc_mz_t *g, int d);                 /* detents: GRAV turns gravity, REPEL / ATTRACT move the marble round its ring, TILT nothing; counts a turn */
void pc_mz_step(pc_mz_t *g);                        /* one fixed step 1/30 s */
bool pc_mz_running(const pc_mz_t *g);
float pc_mz_r(const pc_mz_t *g);                    /* the marble's distance from the centre */
int  pc_mz_angle(const pc_mz_t *g);                 /* the marble's angle, degrees 0..359 */
int  pc_mz_angdiff(int a, int b);                   /* a - b wrapped to -180..180 */
int  pc_mz_cell_of(float v);                        /* tilt: the grid index of a coordinate (clamped 0..N-1) */
