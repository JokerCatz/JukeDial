// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Cooperative long jobs (night3 B): a CPU-bound background task (the own JPEG decoder, ~3.4 s a cover on the FakePod) calls pc_yield_poll() often; it answers
 * true once the task has worked `slice_us` without a break, and the caller then sleeps (vTaskDelay) and tells pc_yield_resumed(). Pure: the caller supplies
 * the clock. longest = the longest stretch of work between two breaks (the number the board log prints, so a starving decoder shows up as a big one). */
#define PC_YIELD_SLICE_US 5000   /* work this long ... */
#define PC_YIELD_SLEEP_MS 2      /* ... then sleep this long (IDLE0 and the UI get the core) */
typedef struct { int64_t slice_us, stretch_start; int64_t longest; uint32_t yields; int64_t t0; } pc_yield_t;
void pc_yield_begin(pc_yield_t *y, int64_t now_us, int64_t slice_us);
bool pc_yield_poll(pc_yield_t *y, int64_t now_us);          /* true: sleep now (the stretch is closed) */
void pc_yield_resumed(pc_yield_t *y, int64_t now_us);       /* a new stretch starts */
void pc_yield_end(pc_yield_t *y, int64_t now_us);           /* closes the last stretch */
#ifdef __cplusplus
}
#endif
