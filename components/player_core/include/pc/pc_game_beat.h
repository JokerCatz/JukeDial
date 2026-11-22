// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
/* 節拍點擊 / Beat tap (ideas G3): the rules, pure C (no UI, host-tested: test_game_beat). Rings run in toward the centre and arrive on the beat; a tap
 * when a ring arrives is a hit: PERFECT within PC_GB_PERFECT_MS, GOOD within PC_GB_GOOD_MS, else a miss (a ring that is hit more than PC_GB_GOOD_MS off
 * but within PC_GB_ACCEPT_MS is used up as a miss; a tap with no ring near is a stray: a miss that uses nothing). Combo = hits in a row; a miss resets it.
 * Points: PERFECT 100, GOOD 50, times (10 + min(combo before the hit, 30)) / 10 (1x .. 4x), a golden ring (1 in PC_GB_GOLD_N) doubles. A round is
 * PC_GB_ROUND rings; when the last one is hit or missed the round ends.
 * Rules (規則): 0 經典 Classic: the rings arrive on the playing file's beats (pc_beat: the page feeds the time to the next predicted beat and the
 * period); not locked = no new rings (the page says so and offers the metronome); 1 節拍器 Metronome: a fixed BPM 60..120, no music needed;
 * 2 反拍 Off-beat: as classic but the rings arrive half a beat after the beat (tap between the beats). The window mode scales the three windows:
 * 0 standard, 1 wide (x1.5), 2 tight (x0.7).
 * Time is the page's own clock (pc_gb_step(dt_ms)); every time here is on it. All random choices (the golden ring) come from the seed. */
#define PC_GB_RINGS       5
#define PC_GB_ROUND       32
#define PC_GB_LEAD_MS     1400        /* a ring runs from the rim to the centre in this long */
#define PC_GB_PERFECT_MS  50
#define PC_GB_GOOD_MS     110
#define PC_GB_ACCEPT_MS   220
#define PC_GB_GOLD_N      16
#define PC_GB_COMBO_CAP   30
#define PC_GB_BPM_MIN     60
#define PC_GB_BPM_MAX     120
#define PC_GB_BPM_DEF     90
#define PC_GB_RULES       3
#define PC_GB_WINDOWS     3
#define PC_GB_EVENTS      8
#define PC_GB_NOBEAT_MS   5000        /* classic unlocked for this long with nothing in flight: the page offers the metronome */
enum { PC_GB_CLASSIC = 0, PC_GB_METRO, PC_GB_OFFBEAT };
enum { PC_GB_READY = 0, PC_GB_PLAY, PC_GB_OVER };                         /* state */
enum { PC_GB_EV_NONE = 0, PC_GB_EV_START, PC_GB_EV_PERFECT, PC_GB_EV_GOOD, PC_GB_EV_MISS, PC_GB_EV_STRAY, PC_GB_EV_COMBO, PC_GB_EV_OVER };
typedef struct { uint8_t kind; bool gold; int value; uint32_t points; } pc_gb_ev_t;   /* value: the combo (after), or for EV_MISS the ms off (signed: + late) */
typedef struct { bool live, gold; uint32_t target; } pc_gb_ring_t;
typedef struct {
    uint8_t rule, window, state;
    int bpm;                                    /* metronome */
    uint32_t now, tnext, seed, points;
    bool tnext_ok;
    int spawned, resolved, combo, combo_best, perfect, good, miss, stray, golds;
    uint32_t unlocked_since;                    /* classic: now when the beat was lost (0: locked or not asked yet) */
    bool unlocked;
    pc_gb_ring_t ring[PC_GB_RINGS];
    pc_gb_ev_t ev[PC_GB_EVENTS];
    int n_ev;
} pc_gb_t;
void pc_gb_init(pc_gb_t *g, int rule, int window, uint32_t seed);   /* READY: the first tap starts */
void pc_gb_set_bpm(pc_gb_t *g, int bpm);                            /* clamped to 60..120 */
/* One step of dt_ms. beat: for rules 0 and 2 whether the detector is locked, the ms to its next beat (> 0) and its period; ignored by the metronome.
 * Fills g->ev (cleared first). */
void pc_gb_step(pc_gb_t *g, uint32_t dt_ms, bool locked, uint32_t to_next_ms, uint32_t period_ms);
void pc_gb_tap(pc_gb_t *g, uint32_t since_ms);                      /* in READY: start the round; in PLAY: judge at the clock + since_ms (ms since the last step; the page caps it at one step) (events added to this step's) */
int  pc_gb_window(const pc_gb_t *g, int which);                      /* the window of PC_GB_PERFECT_MS / GOOD / ACCEPT scaled by the mode, ms */
/* the ring's place: 0 at the rim .. 1 on the centre's target circle (more than 1 after it); -1 when not live */
float pc_gb_progress(const pc_gb_t *g, int i);
bool pc_gb_running(const pc_gb_t *g);                               /* steps change something now (PLAY, or rings still in flight) */
bool pc_gb_no_beat(const pc_gb_t *g);                               /* classic / off-beat, started, unlocked long enough with nothing in flight: say it and offer the metronome */
