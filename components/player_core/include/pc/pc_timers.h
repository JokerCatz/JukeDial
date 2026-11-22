// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Stopwatch and countdown timer (HUD task). Pure: the caller passes a monotonic clock in ms, nothing here reads one.
 * The stopwatch keeps the cumulative time of the last PC_SW_LAPS laps; a lap's split is the difference to the one before it. */
#define PC_SW_LAPS 20
typedef struct { bool running; int64_t acc_ms, t0_ms; int nlap; int64_t at[PC_SW_LAPS]; } pc_sw_t;   /* nlap counts every lap; at[] is a ring of the last PC_SW_LAPS cumulative times */
void    pc_sw_reset(pc_sw_t *s);
void    pc_sw_start(pc_sw_t *s, int64_t now_ms);          /* running or paused -> running (counts on) */
void    pc_sw_pause(pc_sw_t *s, int64_t now_ms);
int64_t pc_sw_elapsed(const pc_sw_t *s, int64_t now_ms);
bool    pc_sw_lap(pc_sw_t *s, int64_t now_ms);             /* only while running; false otherwise */
/* k = 0 the newest lap; false when k is not stored. no = its number (1 based), split / total in ms */
bool    pc_sw_lap_get(const pc_sw_t *s, int k, int *no, int64_t *split_ms, int64_t *total_ms);
int     pc_sw_lap_best(const pc_sw_t *s);                 /* the k of the fastest stored split, -1 with fewer than 2 stored laps (a lone lap has nothing to beat) */
/* "MM:SS.cc" under an hour, "H:MM:SS" from an hour on (hundredths are not shown there) */
void    pc_sw_fmt(int64_t ms, char *b, size_t n);

typedef enum { PC_CD_IDLE, PC_CD_RUN, PC_CD_PAUSE, PC_CD_DONE } pc_cd_state_t;
#define PC_CD_MAX_S (99 * 60 + 59)
#define PC_CD_WARN_MS 10000                               /* the last 10 s: red, the warning blinks */
typedef struct { int state; int set_s; int64_t end_ms; int64_t held_ms; } pc_cd_t;
void    pc_cd_init(pc_cd_t *c, int set_s);                /* idle with set_s (clamped 1..PC_CD_MAX_S) */
void    pc_cd_set(pc_cd_t *c, int set_s);                 /* idle or done -> idle with the new time; ignored while running or paused (false effect) */
void    pc_cd_start(pc_cd_t *c, int64_t now_ms);          /* idle or done: from set_s; paused: counts on */
void    pc_cd_pause(pc_cd_t *c, int64_t now_ms);
void    pc_cd_reset(pc_cd_t *c);                          /* idle, set_s kept */
int64_t pc_cd_left_ms(const pc_cd_t *c, int64_t now_ms);
bool    pc_cd_tick(pc_cd_t *c, int64_t now_ms);           /* true ONCE when the time ran out (the state becomes DONE) */
bool    pc_cd_warn(const pc_cd_t *c, int64_t now_ms);     /* running and 0 < left <= PC_CD_WARN_MS */
bool    pc_cd_blink(const pc_cd_t *c, int64_t now_ms);    /* the warning triangle's phase: lit in the first half of every second of the last 10 s */
void    pc_cd_fmt(int64_t left_ms, char *b, size_t n);    /* the seconds rounded UP: "MM:SS", "H:MM:SS" from 60 min (the page shows 00:10 until the last tenth is gone) */
#ifdef __cplusplus
}
#endif
