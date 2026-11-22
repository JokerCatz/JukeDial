// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

/* The state of one flash_safe_call job (app/main/flash_safe.c): pure, no RTOS, so the timeout path is host-tested. The caller starts a worker,
 * waits up to a timeout, and the worker runs fn. States: PENDING (queued) -> RUNNING -> DONE; a caller that gives up marks it ABANDONED, and
 * the worker then owns the job's memory (it must free it) and the caller never touches it again. Both sides decide through one atomic
 * exchange, so exactly one of them ends up owning the free. */
typedef enum { PC_FSJ_PENDING = 0, PC_FSJ_RUNNING, PC_FSJ_DONE, PC_FSJ_ABANDONED } pc_fsjob_state_t;
typedef struct { void (*fn)(void *); void *arg; volatile int state; } pc_fsjob_t;

void pc_fsjob_init(pc_fsjob_t *j, void (*fn)(void *), void *arg);
/* worker side: run fn and finish. Returns true when the caller had already abandoned the job (the worker frees it). */
bool pc_fsjob_run(pc_fsjob_t *j);
/* caller side, after the wait: true = the job finished (the caller frees it); false = timed out, now ABANDONED (the worker frees it). */
bool pc_fsjob_collect(pc_fsjob_t *j);

#ifdef __cplusplus
}
#endif
