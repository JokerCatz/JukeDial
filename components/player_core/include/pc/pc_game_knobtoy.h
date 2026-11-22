// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
/* 旋鈕小玩意 / Knob toys (ideas G6): two Playdate-style toys on one page, the rule picks the toy; the rules only, no UI. The input is ONE number on a dial,
 * moved by whole steps of 1 (a knob detent, a tap on the dial's half, a FakePod key press), so a toy is playable with stepped input and never needs an analog turn.
 * 上發條 (wind-up): the value is the spring's tension 0..PC_KT_WIND_MAX. Turn up to wind, down to ease off; the spring slackens by 1 every PC_KT_LEAK_MS once wound.
 * The sweet spot is a hidden tension (shown only by the dial's glow and haptics); a tap on the centre RELEASES the toy: it runs 100 cm at the sweet spot (+20 for the exact
 * one), less for a spring wound too little (5 cm a step) and much less for one wound too far (8 cm a step); at PC_KT_WIND_MAX the spring SNAPS (0 cm).
 * Three releases, the score is the total in cm (high). The golden spring doubles the distance.
 * 釣魚 (fishing): the value is the rod's place 0..PC_KT_FISH_RANGE-1; the fish swims about and now and then dashes; keep the rod within PC_KT_FISH_ZONE of it:
 * inside, the catch fills (PC_KT_CATCH_STEP per 100 ms) and the line relaxes; outside, the line tension rises (faster when far) and the catch slips;
 * the catch full = CAUGHT, the line full = SNAPPED. Score: steps and time (fewer is better). The golden fish is slower and tires faster.
 * Both clocks (the leak, the fish) start with the first move and run in whole 100 ms steps (pc_kt_tick). A call clears g->ev first (PC_KT_EV_*). */
enum { PC_KT_WIND = 0, PC_KT_FISH, PC_KT_RULES };
enum { PC_KT_PLAY = 0, PC_KT_DONE, PC_KT_LOST };                       /* state: DONE wind all released / fish caught; LOST fish snapped */
enum { PC_KT_EV_LAUNCH = 1, PC_KT_EV_PERFECT, PC_KT_EV_SNAP, PC_KT_EV_DONE, PC_KT_EV_CAUGHT, PC_KT_EV_LOST, PC_KT_EV_DASH };
#define PC_KT_WIND_MAX     40
#define PC_KT_LAUNCHES     3
#define PC_KT_LEAK_MS      1500u
#define PC_KT_SWEET_MIN    14
#define PC_KT_SWEET_MAX    33
#define PC_KT_FISH_RANGE   100
#define PC_KT_FISH_ZONE    4
#define PC_KT_CATCH_FULL   1000
#define PC_KT_CATCH_STEP   25
#define PC_KT_LINE_FULL    100
#define PC_KT_STEP_MS      100u
typedef struct {
    uint8_t rule, state;
    bool started, gold;
    int v;                              /* the dial's value */
    int sweet, prev_sweet, launches, last_cm, total;   /* wind: the hidden tension, the one of the release before (shown on the rim), releases done, the last distance, the total (cm) */
    uint32_t leak_ms;
    int fish, dir, dash;                /* fish: its place, its direction (+1 / -1), dash steps left */
    int catch_, line;                   /* 0..PC_KT_CATCH_FULL, 0..PC_KT_LINE_FULL */
    int steps;                          /* moves of the player (the card's "moves") */
    uint32_t ms, acc;                   /* the clock, whole steps; the carry of a partial step */
    uint32_t rng;
    int ev[4], n_ev;
} pc_kt_t;
void pc_kt_init(pc_kt_t *g, int rule, uint32_t seed, bool gold);
void pc_kt_move(pc_kt_t *g, int d);                 /* the dial by d (signed, clamped; wind: reaching MAX snaps the spring and it starts again at 0); starts the clock */
void pc_kt_release(pc_kt_t *g);                     /* wind: the centre tap; fish: nothing */
void pc_kt_tick(pc_kt_t *g, uint32_t ms);           /* the clock; only after the first move; ms is a multiple of PC_KT_STEP_MS in practice (the remainder is carried) */
bool pc_kt_running(const pc_kt_t *g);               /* started and still playing */
int  pc_kt_wind_cm(int v, int sweet);               /* the distance of a release at tension v (0 at v = 0 and at MAX); the exact sweet spot +20 */
int  pc_kt_reach(int rule);                         /* the dial's haptic / glow reach: wind 8 tension units, fish 10 places */
int  pc_kt_zone_lo(const pc_kt_t *g);               /* fish: the zone round the fish, clamped to the range */
int  pc_kt_zone_hi(const pc_kt_t *g);
bool pc_kt_in_zone(const pc_kt_t *g);
int  pc_kt_catch_pct(const pc_kt_t *g);             /* 0..100 */
