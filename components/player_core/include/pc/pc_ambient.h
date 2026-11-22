// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
/* 氛圍頁 / Ambient scenes (ideas G5): the motion of four scenes, pure C (no UI, host-tested: test_ambient). Units: the field is 100 units from the centre
 * to the rim (the page scales to the arena's px radius); x right, y DOWN. One pc_am_step() is one fixed step of 1/30 s (ui_arena's tick), scaled by the speed.
 *   0 熔岩燈 Lava lamp  blobs warm up at the bottom, rise, cool at the top and sink; two that touch merge (area kept), a tap splits the nearest one;
 *                       rare: a golden blob (a tap on it bursts it into three small golden ones)
 *   1 魚缸 Aquarium     five fish wander, bubbles rise; a tap drops a food pellet (the nearest fish swims to it and eats it, grows a little), a tap on a fish
 *                       makes it dart; rare: a golden fish crosses the tank (a tap on it catches it)
 *   2 篝火 Campfire     flame particles from two logs (hot white-yellow at the root, red, then smoke) and ember sparks; a tap flares the fire;
 *                       rare: a burst of sparks
 *   3 夜燈 Night light  three nested glows breathing slowly (no clock: the phase is the step count), colour drifting, a few twinkling stars; a tap pulses it;
 *                       rare: a firefly drifts through (a tap near it makes it blink)
 * The energy (0..1, the playing music's level, the page reads it from the arena) makes everything livelier: bigger and faster, more flames, a stronger glow.
 * The scene is drawn from a list of primitives (pc_am_draw): balls and rounded rects in field units with a tone (0..PC_AM_TONES-1: the page's palette
 * index) and an opacity; `mix` blends the tone with the NEXT tone (the night light's drifting colour). All random choices come from the seed given at init. */
#define PC_AM_FIELD   100.0f
#define PC_AM_TONES   6
#define PC_AM_ITEMS   44
#define PC_AM_MAXPRIM 60
#define PC_AM_EVENTS  8
enum { PC_AM_LAVA = 0, PC_AM_FISH, PC_AM_FIRE, PC_AM_NIGHT, PC_AM_SCENES };
enum { PC_AM_K_NONE = 0, PC_AM_K_BLOB, PC_AM_K_FISH, PC_AM_K_GOLDFISH, PC_AM_K_BUBBLE, PC_AM_K_FOOD, PC_AM_K_FLAME, PC_AM_K_EMBER, PC_AM_K_FIREFLY, PC_AM_K_STAR };
enum { PC_AM_EV_SPLIT = 1, PC_AM_EV_FEED, PC_AM_EV_EAT, PC_AM_EV_DART, PC_AM_EV_GOLD_SEEN, PC_AM_EV_GOLD_CAUGHT, PC_AM_EV_FLARE, PC_AM_EV_SPARKS, PC_AM_EV_PULSE, PC_AM_EV_FIREFLY, PC_AM_EV_BLINK };
enum { PC_AM_BALL = 0, PC_AM_RECT };
typedef struct { uint8_t kind; float x, y; } pc_am_ev_t;       /* what happened (the page's effect, haptic, one sound) and where */
typedef struct { float x, y, vx, vy, r, age, life, aux, tx, ty; uint8_t kind, tone; } pc_am_item_t;   /* kind 0 = a free slot; aux: a blob's heat, a fish's retarget time, a star's phase; tx, ty: a fish's target; a fish's dart time is `life` */
typedef struct { uint8_t shape, tone, mix, opa; float x, y, a, b; } pc_am_prim_t;   /* BALL: radius a; RECT: width a, height b (fully rounded); centre x, y */
typedef struct {
    uint8_t scene;
    float speed;                         /* time scale, 1 = normal */
    float energy;                        /* 0..1 */
    float phase, pulse, flare, drift;    /* night: the breath (rad), the poke's pulse; fire: the flare; night: colour drift 0..1 */
    float acc;                           /* fire: the fractional spawn */
    uint32_t rng, tick, steps_since_rare;
    int rare_gap;                        /* steps before a rare event may come (a forced one resets it) */
    int n_ev;
    pc_am_ev_t ev[PC_AM_EVENTS];
    pc_am_item_t it[PC_AM_ITEMS];
} pc_am_t;

void pc_am_init(pc_am_t *g, int scene, uint32_t seed, float speed);   /* an unknown scene is the lava lamp */
void pc_am_step(pc_am_t *g, float energy);                            /* one 1/30 s step; clears the events first */
void pc_am_poke(pc_am_t *g, float x, float y);                        /* a tap at a field point; adds events */
int  pc_am_draw(const pc_am_t *g, pc_am_prim_t *out, int max);        /* the primitives, back to front; returns how many (at most PC_AM_MAXPRIM) */
int  pc_am_count(const pc_am_t *g, int kind);                         /* live items of a kind */
bool pc_am_force_rare(pc_am_t *g);                                    /* tests and pictures: the scene's rare event now; false when one is already showing */
