// SPDX-License-Identifier: LGPL-2.1-or-later
#include "bsp/bsp_knob_fsm.h"

void bsp_knob_fsm_init(bsp_knob_fsm_t *f, int64_t lockout_us)
{
    f->lockout_us = lockout_us;
    f->release_t = 0;
    f->high = true;
    f->first = true;
    f->pending = 0;
}

int bsp_knob_fsm_step(bsp_knob_fsm_t *f, int a, int b, int64_t t_us)
{
    if (a && b) {
        if (!f->high) f->release_t = t_us;   /* time of the last transition to both-high */
        f->high = true;
        int done = f->pending;
        f->pending = 0;
        return done;
    }
    if (!f->high) return 0;                  /* already inside a press */
    f->high = false;
    if (!f->first && t_us - f->release_t < f->lockout_us) return 0; /* release bounce */
    f->first = false;
    if (a && !b) f->pending = +1;
    else if (!a && b) f->pending = -1;
    return 0;                                /* both low at once: no direction */
}
