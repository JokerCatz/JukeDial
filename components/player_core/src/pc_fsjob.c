// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_fsjob.h"

void pc_fsjob_init(pc_fsjob_t *j, void (*fn)(void *), void *arg) { j->fn = fn; j->arg = arg; j->state = PC_FSJ_PENDING; }

bool pc_fsjob_run(pc_fsjob_t *j)
{
    int exp = PC_FSJ_PENDING;
    if (!__atomic_compare_exchange_n(&j->state, &exp, PC_FSJ_RUNNING, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return true;   /* abandoned before it began: do not write at all */
    if (j->fn) j->fn(j->arg);
    return __atomic_exchange_n(&j->state, PC_FSJ_DONE, __ATOMIC_ACQ_REL) == PC_FSJ_ABANDONED;
}

bool pc_fsjob_collect(pc_fsjob_t *j)
{
    int s = __atomic_load_n(&j->state, __ATOMIC_ACQUIRE);
    while (s != PC_FSJ_DONE) {
        if (__atomic_compare_exchange_n(&j->state, &s, PC_FSJ_ABANDONED, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return false;
    }
    return true;
}
