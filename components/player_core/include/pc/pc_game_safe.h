// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
/* 保險箱開鎖 / Safe cracker (ideas G2): the rules, no UI. A combination of numbers 0..99 (PC_SAFE_RANGE, the dial wraps); the player turns the dial to a
 * number and confirms it; the right number locks the next stage, a wrong one resets the whole combination to the first number (wrong order resets).
 * The page feeds the dial's turns (pc_safe_turn: a detent event when the dial rests on the current number), the confirms (pc_safe_confirm) and the clock
 * (pc_safe_tick, only while the game runs). Rules (a rule changes a rule): 經典 three numbers; 專家 four numbers and a weaker hint (a smaller reach for the
 * graded haptic); 計時 three numbers against 60 s (the time out ends it). The numbers come from the seed the page gives (games_seed): each is at
 * least PC_SAFE_APART from the one before (and the first from the start at 0), so there is always a turn to make. */
enum { PC_SAFE_CLASSIC = 0, PC_SAFE_EXPERT, PC_SAFE_TIMED, PC_SAFE_RULES };
enum { PC_SAFE_PLAY = 0, PC_SAFE_OPEN, PC_SAFE_TIMEUP };            /* state */
enum { PC_SAFE_EV_DETENT = 1, PC_SAFE_EV_LOCK, PC_SAFE_EV_WRONG, PC_SAFE_EV_OPEN, PC_SAFE_EV_TIMEUP };
#define PC_SAFE_RANGE   100
#define PC_SAFE_MAX     4
#define PC_SAFE_APART   15
#define PC_SAFE_TIME_MS 60000u
typedef struct {
    uint8_t rule, n, stage, state;      /* the rule, the numbers, the one being looked for (0..n-1), PC_SAFE_* */
    bool started;                       /* the first turn or confirm: the clock runs from then */
    int pos;                            /* the dial 0..99 */
    int target[PC_SAFE_MAX];
    int attempts;                       /* confirms so far (the right ones count) */
    int turns;                          /* detents turned */
    uint32_t ms;                        /* the clock */
    int ev[4], n_ev;                    /* PC_SAFE_EV_* of the last call (cleared at the start of each) */
} pc_safe_t;
void pc_safe_init(pc_safe_t *g, int rule, uint32_t seed);
void pc_safe_turn(pc_safe_t *g, int d);                             /* the dial by d detents (signed, wraps); starts the clock; a DETENT event when it lands on the number looked for */
void pc_safe_confirm(pc_safe_t *g);                                 /* LOCK (more to go), OPEN (the last) or WRONG (back to the first number); counts an attempt */
void pc_safe_tick(pc_safe_t *g, uint32_t ms);                       /* the clock; the timed rule ends with TIMEUP at PC_SAFE_TIME_MS */
bool pc_safe_running(const pc_safe_t *g);                           /* the clock runs: started and still playing */
int  pc_safe_dist(const pc_safe_t *g);                              /* from the dial to the number looked for (the short way round) */
int  pc_safe_target(const pc_safe_t *g);                            /* the number looked for, -1 when the safe is open or over */
uint32_t pc_safe_left_ms(const pc_safe_t *g);                       /* timed: what is left; other rules 0 */
/* The hint's reach (a haptic grade starts this many numbers from the target): by rule (classic 12, expert 5, timed 12) and the page's hint setting
 * 0 strong (x3/2), 1 normal, 2 weak (x1/2); `gold` (the golden safe) doubles it */
int  pc_safe_reach(int rule, int hint, bool gold);
/* The visual cue's strength (ui_dial_cue, percent) from the hint setting (0 strong, 1 normal, 2 weak) and the rule: a weaker hint is a dimmer cue (100 / 70 / 40), the
 * expert rule dimmer again (3/4) but never below 30: it still gets some. */
int  pc_safe_cue_strength(int rule, int hint);
