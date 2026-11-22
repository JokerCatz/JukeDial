// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

/* "The latest request wins" hand-off of one job to a worker (board batch A F1: chip pre-opens). At most one job waits; a new
 * one replaces it and the replaced one goes back to the caller to be released (the worker never sees it). A request is never
 * refused, so a burst never falls back to doing the work elsewhere. The worker is woken once per waiting job (wake_sent).
 * Pure state: the caller holds its own lock around every call (the job is a pointer the caller owns until take). */
typedef struct { void *waiting; bool wake_sent; } pc_latest_t;

void *pc_latest_put(pc_latest_t *l, void *job, bool *send_wake);   /* -> the job it replaced (or NULL); *send_wake: post a wake-up now */
void *pc_latest_take(pc_latest_t *l);                              /* worker: the waiting job (or NULL); the next put wakes it again */
void  pc_latest_wake_lost(pc_latest_t *l);                         /* the wake-up could not be posted: the next put posts one again */

#ifdef __cplusplus
}
#endif
