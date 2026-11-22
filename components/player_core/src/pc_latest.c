// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_latest.h"
#include <stddef.h>

void *pc_latest_put(pc_latest_t *l, void *job, bool *send_wake)
{
    void *old = l->waiting;
    l->waiting = job;
    *send_wake = !l->wake_sent;
    l->wake_sent = true;
    return old;
}

void *pc_latest_take(pc_latest_t *l)
{
    void *job = l->waiting;
    l->waiting = NULL;
    l->wake_sent = false;
    return job;
}

void pc_latest_wake_lost(pc_latest_t *l) { l->wake_sent = false; }
