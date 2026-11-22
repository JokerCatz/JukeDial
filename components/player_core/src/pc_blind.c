// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_blind.h"

void pc_blind_init(pc_blind_t *b) { b->pend = 0; b->t = 0; b->dbl_ms = 0; }
static uint32_t dbl(const pc_blind_t *b) { return b->dbl_ms ? b->dbl_ms : PC_BLIND_DBL_MS; }

static int vol(int dir) { return dir > 0 ? PC_BLIND_VOL_UP : PC_BLIND_VOL_DOWN; }

int pc_blind_poll(pc_blind_t *b, uint32_t now)
{
    if (!b->pend || (uint32_t)(now - b->t) < dbl(b)) return PC_BLIND_NONE;
    const int a = vol(b->pend);
    b->pend = 0;
    return a;
}

int pc_blind_feed(pc_blind_t *b, int dir, bool repeat, uint32_t now)
{
    const int d = dir > 0 ? 1 : -1;
    if (repeat) { b->pend = 0; return vol(d); }               /* a hold: its first click was already given by poll (the repeats start later than the window) */
    if (b->pend && (uint32_t)(now - b->t) < dbl(b)) {
        if (b->pend == d) { b->pend = 0; return d > 0 ? PC_BLIND_NEXT : PC_BLIND_PLAYPAUSE; }
        const int a = vol(b->pend);                            /* the other key: the waiting click is a volume step now, the new one waits */
        b->pend = (int8_t)d; b->t = now;
        return a;
    }
    const int late = b->pend ? vol(b->pend) : PC_BLIND_NONE;   /* an old click whose poll did not come yet */
    b->pend = (int8_t)d; b->t = now;
    return late;
}
