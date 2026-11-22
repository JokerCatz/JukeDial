// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_yield.h"

static void close_stretch(pc_yield_t *y, int64_t now)
{
    const int64_t d = now - y->stretch_start;
    if (d > y->longest) y->longest = d;
}
void pc_yield_begin(pc_yield_t *y, int64_t now_us, int64_t slice_us)
{
    y->slice_us = slice_us > 0 ? slice_us : PC_YIELD_SLICE_US;
    y->stretch_start = y->t0 = now_us;
    y->longest = 0;
    y->yields = 0;
}
bool pc_yield_poll(pc_yield_t *y, int64_t now_us)
{
    if (now_us - y->stretch_start < y->slice_us) return false;
    close_stretch(y, now_us);
    y->yields++;
    return true;
}
void pc_yield_resumed(pc_yield_t *y, int64_t now_us) { y->stretch_start = now_us; }
void pc_yield_end(pc_yield_t *y, int64_t now_us) { close_stretch(y, now_us); }
