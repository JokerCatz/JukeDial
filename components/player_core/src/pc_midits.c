// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_midits.h"
#include <string.h>

void pc_midits_reset(pc_midits_t *s) { memset(s, 0, sizeof *s); }

int64_t pc_midits_map(pc_midits_t *s, uint16_t ts13, int64_t arrival_us)
{
    int64_t remote;
    if (!s->have) remote = ts13;
    else {
        int64_t guess = (arrival_us - s->off_us) / 1000;           /* sender ms, unwrapped */
        remote = guess - ((guess - ts13) & 0x1FFF);                /* congruent to ts13, <= guess */
        if (guess - remote > 4096) remote += 8192;                 /* nearest one */
    }
    int64_t d = arrival_us - remote * 1000;
    if (!s->have || d < s->off_us - 1000000 || d > s->off_us + 1000000) {
        s->have = true;
        s->off_us = d;
    } else {
        int64_t creep = (arrival_us - s->last_us) / 5000;          /* 200 ppm */
        if (creep < 0) creep = 0;
        s->off_us += creep;
        if (d < s->off_us) s->off_us = d;
    }
    s->last_us = arrival_us;
    s->remote_ms = remote;
    return remote * 1000 + s->off_us;
}

int64_t pc_midits_due(pc_midits_t *s, uint16_t ts13, int64_t arrival_us, int64_t delay_us)
{
    int64_t due = pc_midits_map(s, ts13, arrival_us) + delay_us;
    if (due < arrival_us) due = arrival_us;
    if (due > arrival_us + delay_us) due = arrival_us + delay_us;
    return due;
}
