// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
/* The budget of one computer-player slice (games hub G0): a UI tick runs pc_<game>_ai_step() once; the step searches until it is done or its budget is spent
 * and returns (the search state is kept in the AI struct and resumes at the next call). The result does not depend on how the work was cut into slices.
 *   now_us/user  the clock, microseconds (esp_timer_get_time on the board and in the simulator; a fake in host tests); NULL: no time limit
 *   budget_us    the wall-clock budget of one slice, 0: none; the clock is read every few nodes, so a slice may overrun by those few nodes
 *   node_cap     the second limit: nodes of one slice, 0: none
 * A NULL budget pointer (or all fields 0) runs the whole search in one call. */
typedef struct {
    int64_t (*now_us)(void *user);
    void *user;
    int budget_us;
    int node_cap;
} pc_ai_budget_t;

#define PC_AI_CLOCK_EVERY 8                                   /* nodes between two reads of the clock */
static inline bool pc_ai_slice_over(const pc_ai_budget_t *b, int64_t t0, int nodes)
{
    if (!b) return false;
    if (b->node_cap && nodes >= b->node_cap) return true;
    return b->now_us && b->budget_us && nodes % PC_AI_CLOCK_EVERY == 0 && b->now_us(b->user) - t0 >= b->budget_us;
}
