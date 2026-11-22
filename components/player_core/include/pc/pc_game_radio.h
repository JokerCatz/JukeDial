// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
/* 電台調頻尋寶 / Radio hunt (ideas G3): the rules, no UI. A frequency dial of PC_RADIO_RANGE positions (a gauge, no wrap; the page shows position p as
 * 80.0 + 0.2 p MHz); hidden stations sit at random positions. Between them there is static; near one the static thins out and a melody fragment gets
 * clearer (pc_radio_signal: 0..100 by the distance to the nearest station still to find, against that station's reach). A tap on the centre locks a station
 * when the signal is strong enough (PC_RADIO_LOCK_PCT); a try without a station is a miss (counted). Rules (a rule changes a rule): 經典 find three stations;
 * 限時 find as many as you can in 60 s (a new station appears after each lock, one at a time; more is better); 微弱 three stations, a narrower lock window
 * and louder (denser) static. The rare golden station (1 in PC_RADIO_GOLD_N, decided from the seed; hidden: a narrower signal) is a bonus: locking it takes
 * PC_RADIO_BONUS_MS off the time (timed: adds to the time left). The page feeds the dial's tunes (pc_radio_tune), the locks (pc_radio_lock) and the clock
 * (pc_radio_tick, only while the game runs). pc_radio_sound maps (signal, step) to the toy sound to post: the existing sfx path cannot express a continuous
 * noise level, so static is a sparse train of short ticks (denser when far) and the melody a few notes of the bank, more of them the clearer the signal. */
enum { PC_RADIO_CLASSIC = 0, PC_RADIO_TIMED, PC_RADIO_WEAK, PC_RADIO_RULES };
enum { PC_RADIO_PLAY = 0, PC_RADIO_WON, PC_RADIO_TIMEUP };           /* state */
enum { PC_RADIO_EV_LOCK = 1, PC_RADIO_EV_MISS, PC_RADIO_EV_GOLD, PC_RADIO_EV_WON, PC_RADIO_EV_TIMEUP };
#define PC_RADIO_RANGE    100
#define PC_RADIO_MAX      3
#define PC_RADIO_APART    14
#define PC_RADIO_TIME_MS  60000u
#define PC_RADIO_BONUS_MS 10000u
#define PC_RADIO_GOLD_N   10
#define PC_RADIO_LOCK_PCT 80
#define PC_RADIO_MELODY   4                                          /* notes of a fragment; the sound codes 1..4 are the notes */
typedef struct { uint8_t pos; bool locked, gold; uint8_t tune; } pc_radio_st_t;
typedef struct {
    uint8_t rule, n, found, state;      /* the rule, the stations to find at once (timed: 1), the stations locked, PC_RADIO_* */
    bool started;                       /* the first tune or lock: the clock runs from then */
    int pos;                            /* the dial 0..PC_RADIO_RANGE-1 */
    pc_radio_st_t st[PC_RADIO_MAX];
    int attempts, misses;               /* locks tried (the right ones count), tries without a station */
    uint32_t ms, bonus_ms;              /* the clock; the golden stations' bonus */
    uint32_t rng;
    bool gold_found;
    int ev[4], n_ev;                    /* PC_RADIO_EV_* of the last call (cleared at the start of each) */
} pc_radio_t;
void pc_radio_init(pc_radio_t *g, int rule, uint32_t seed);
void pc_radio_tune(pc_radio_t *g, int d);                           /* the dial by d (signed, clamped); starts the clock */
void pc_radio_lock(pc_radio_t *g);                                  /* LOCK (+ GOLD, + WON when the last), or MISS; counts an attempt */
void pc_radio_tick(pc_radio_t *g, uint32_t ms);                     /* the clock; timed ends with TIMEUP when the time is out */
bool pc_radio_running(const pc_radio_t *g);
int  pc_radio_reach(int rule, bool gold);                           /* a station's reach in positions: the signal starts this far from it (golden: narrower) */
int  pc_radio_signal_of(int dist, int reach);                       /* 0..100: 100 on the station, linear to 0 at the reach */
int  pc_radio_nearest(const pc_radio_t *g);                         /* the station still to find nearest to the dial, -1 none */
int  pc_radio_signal(const pc_radio_t *g);                          /* 0..100 of the nearest station still to find (0 when none) */
bool pc_radio_can_lock(const pc_radio_t *g);                        /* the signal is strong enough now (>= PC_RADIO_LOCK_PCT) */
uint32_t pc_radio_left_ms(const pc_radio_t *g);                     /* timed: what is left; other rules 0 */
uint32_t pc_radio_time_ms(const pc_radio_t *g);                     /* the time to show: the clock less the golden bonus (not below 0); timed: the clock */
int  pc_radio_hub10(int pos);                                       /* the frequency in tenths of MHz: 800 + 2 pos */
int  pc_radio_shake10(int signal);                                  /* the needle's jitter in tenths of a degree: only in static (signal < 40), up to 30 */
/* The sound for a 100 ms step: -1 none, 0 a static tick, 1..4 note n of the station's fragment. `tune` picks the fragment (0..3). */
int  pc_radio_sound(int rule, int signal, int tune, unsigned step);
