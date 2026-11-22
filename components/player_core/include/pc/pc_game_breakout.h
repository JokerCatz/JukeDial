// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
/* 環形打磚塊 / Ring breakout (ideas G1): the rules and the physics, pure C (no UI, host-tested: test_game_breakout).
 * The field is a circle of radius PC_BO_FIELD units, centred on (0, 0); x right, y DOWN (screen axes), so an angle (degrees) runs clockwise on the
 * screen from 3 o'clock, as LVGL's arcs do. The bricks are annular sectors on up to PC_BO_RINGS inner rings (PC_BO_PER_RING each); the paddle is an
 * arc on the outer edge (inner face PC_BO_PAD_IN) that moves round the circle; the ball bounces inside and is lost when it leaves the circle.
 * One pc_bo_step() is one fixed step of 1/30 s (ui_arena's tick); the ball moves in sub-steps of at most 1 unit, so it never tunnels a brick.
 * Physics:
 *   - paddle: a ball whose outer edge crosses the paddle's inner face inside its span bounces: mirrored about the radial normal (pc_bo_reflect),
 *     then turned by where it hit (english: the paddle's ends send it up to PC_BO_ENGLISH_DEG to that side), and kept within PC_BO_MAX_OUT_DEG of
 *     the inward normal so it never skims along the edge (pc_bo_paddle_dir). The speed is kept.
 *   - bricks: the ball (a circle) against an annular sector: the face of least overlap is the one touched (inner arc, outer arc, a side; off both
 *     ranges the nearest corner, by true distance); the ball is pushed out, and only when it moves INTO that face is it mirrored and the brick hit
 *     (one contact = one hit: a turning ring that sweeps into a leaving ball only pushes it); one brick a sub-step.
 * Rules (規則, classic first; a variant changes a rule):
 *   0 經典 Classic   one paddle, 3 lives
 *   1 雙拍 Twin      a second paddle opposite the first (they move together); narrower paddles
 *   2 黏球 Catch     the ball sticks where it lands on the paddle and moves with it; a tap (pc_bo_serve) lets it go, aimed by where it sits; it goes by itself after PC_BO_CATCH_TICKS;
 *                    only PC_BO_CATCHES catches a ball (a life, a level), then it bounces as in classic (fix round 1 ruling: 黏球 no easier than the others)
 * Bricks: normal (1 hit), tough (2 hits; the boss ring 3), boom (breaks its neighbours, a chain when they are boom too), power (drops a capsule outward:
 * caught by the paddle = wide paddle / a second ball / a slow ball, for a while), golden (rare: 1 level in PC_BO_GOLD_N by the seed; +1 life).
 * Levels: 1 has two rings, 2+ three; the rings rotate from level 3 (alternate directions, faster each level); every 5th level is a boss: the outer
 * ring is a fast-spinning wall of 3-hit bricks, the middle ring empty. Speed: the mode (slow / normal / fast) times 1 + 5 % a level (at most 1.5).
 * Score: a break = 10 x the combo (bricks broken since the ball last touched a paddle, from 1), a tough brick's first hit 5, golden +100,
 * a cleared level 50 x the level. All random choices come from the seed given at init (the game's own stream). */

#define PC_BO_FIELD        100.0f   /* the field's radius in units (the arena scales it to its px radius) */
#define PC_BO_BALL_R       3.5f
#define PC_BO_PAD_IN       86.0f    /* the paddle's inner face */
#define PC_BO_PAD_OUT      93.0f
#define PC_BO_RING_IN      30.0f    /* the inner edge of the innermost ring */
#define PC_BO_RING_T       9.0f     /* a ring's thickness */
#define PC_BO_RING_GAP     2.0f     /* between two rings */
#define PC_BO_RINGS        3
#define PC_BO_PER_RING     12
#define PC_BO_BRICKS       (PC_BO_RINGS * PC_BO_PER_RING)
#define PC_BO_BRICK_GAP    3.0f     /* degrees between two bricks of a ring */
#define PC_BO_BALLS        2
#define PC_BO_DROPS        4
#define PC_BO_EVENTS       24
#define PC_BO_LIVES        3
#define PC_BO_LIVES_MAX    5
#define PC_BO_HALF_DEG     22.0f    /* the paddle's half span (classic) */
#define PC_BO_HALF_TWIN    15.0f    /* 雙拍 */
#define PC_BO_WIDE         1.5f     /* the wide power-up's factor */
#define PC_BO_ENGLISH_DEG  25.0f
#define PC_BO_MAX_OUT_DEG  65.0f
#define PC_BO_DROP_SPEED   0.8f     /* units a step, outward */
#define PC_BO_CATCH_TICKS  90       /* 黏球: the ball goes by itself after 3 s */
#define PC_BO_CATCHES      3        /* 黏球: catches a ball (until it is lost or the level ends) */
#define PC_BO_CLEAR_TICKS  30       /* a cleared level shows for 1 s, then the next one waits for the serve */
#define PC_BO_POWER_TICKS  450      /* wide / slow last 15 s */
#define PC_BO_GOLD_N       6        /* one level in this many has a golden brick */
#define PC_BO_RULES        3
#define PC_BO_SPEEDS       3

enum { PC_BO_CLASSIC = 0, PC_BO_TWIN, PC_BO_CATCH };
enum { PC_BO_SERVE = 0, PC_BO_PLAY, PC_BO_CLEAR, PC_BO_OVER };                       /* state */
enum { PC_BK_NONE = 0, PC_BK_NORMAL, PC_BK_TOUGH, PC_BK_BOOM, PC_BK_POWER, PC_BK_GOLD, PC_BK_BOSS };   /* brick kind */
enum { PC_PW_WIDE = 0, PC_PW_MULTI, PC_PW_SLOW, PC_PW_N };                              /* power-up */
enum { PC_EV_PADDLE = 1, PC_EV_HIT, PC_EV_BREAK, PC_EV_BOOM, PC_EV_POWER, PC_EV_GOLD, PC_EV_LOST, PC_EV_LIFE, PC_EV_CLEAR, PC_EV_OVER, PC_EV_COMBO, PC_EV_RELEASE };

typedef struct { float x, y, vx, vy; bool live, stuck; float stuck_deg; int stuck_ticks; int touch; } pc_bo_ball_t;   /* touch: the brick last hit + 1 (0 none): no second hit until the ball is clear of it */
typedef struct { uint8_t kind, hp, hp0, power; } pc_bo_brick_t;
typedef struct { float r, deg; uint8_t power; bool live; } pc_bo_drop_t;
typedef struct { uint8_t kind; int16_t brick; int16_t value; float x, y; } pc_bo_ev_t;   /* value: the combo (BREAK, COMBO), the points, the power (POWER) */

typedef struct {
    int rule, speed, state, level, lives;
    uint32_t score, ticks, rng;
    int combo, combo_best, bricks_left, broken, catches;
    float paddle_deg;                            /* the paddle's centre, 0..360 */
    float rot[PC_BO_RINGS], spin[PC_BO_RINGS];   /* each ring's rotation (deg) and its spin (deg a step) */
    pc_bo_brick_t brick[PC_BO_BRICKS];           /* ring * PC_BO_PER_RING + j */
    pc_bo_ball_t ball[PC_BO_BALLS];
    pc_bo_drop_t drop[PC_BO_DROPS];
    int wide_ticks, slow_ticks, clear_ticks;
    pc_bo_ev_t ev[PC_BO_EVENTS];                 /* what the last call did (pc_bo_step, pc_bo_serve): sounds, effects */
    int n_ev;
} pc_bo_t;

void pc_bo_init(pc_bo_t *g, int rule, int speed, uint32_t seed);   /* level 1, PC_BO_LIVES, the ball on the paddle (SERVE); paddle at 90 (6 o'clock) */
void pc_bo_level(pc_bo_t *g, int level);         /* lay out a level (bricks, spins, golden brick), the ball back on the paddle (SERVE) */
bool pc_bo_serve(pc_bo_t *g);                    /* a tap: SERVE -> PLAY (the ball goes); 黏球 a stuck ball goes. false: nothing to do */
void pc_bo_move(pc_bo_t *g, float deg);          /* the paddle turns by deg (+ clockwise); a ball resting or stuck on it moves with it */
void pc_bo_step(pc_bo_t *g);                     /* one fixed step (1/30 s); its events in g->ev */
bool pc_bo_running(const pc_bo_t *g);            /* PLAY or CLEAR: steps change something */
float pc_bo_half(const pc_bo_t *g);              /* the paddle's half span now (rule, wide power-up) */
int pc_bo_paddles(const pc_bo_t *g);             /* 1, 2 (雙拍 (twin paddles)) */
float pc_bo_speed(const pc_bo_t *g);             /* units a step now (mode, level, slow power-up) */
/* a brick's sector now (its ring's rotation): radii in units, angles in degrees a0 < a1 (a0 in 0..360). false: no such brick or broken */
bool pc_bo_brick_sector(const pc_bo_t *g, int i, float *r_in, float *r_out, float *a0, float *a1);
/* the pure pieces (host-tested) */
void pc_bo_reflect(float vx, float vy, float nx, float ny, float *ox, float *oy);   /* v mirrored about the unit normal n: v - 2 (v.n) n */
/* the direction after a paddle hit at (x, y) on a paddle centred at pad_deg with half span half_deg, coming with (vx, vy): the reflection, the english,
 * the limit to the inward normal; the speed of v. The result in (ox, oy). */
void pc_bo_paddle_dir(float x, float y, float vx, float vy, float pad_deg, float half_deg, float *ox, float *oy);
float pc_bo_angdiff(float a, float b);           /* a - b wrapped into -180..180 */
