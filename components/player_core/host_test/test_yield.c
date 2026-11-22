// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_yield.h"
#include "pc_test.h"

/* pc_yield: a stretch of work is closed once it reaches the slice; the longest stretch is what the log prints */
int main(void)
{
    pc_yield_t y;
    pc_yield_begin(&y, 1000, 5000);
    CHECK(!pc_yield_poll(&y, 1000));
    CHECK(!pc_yield_poll(&y, 5999));
    CHECK(pc_yield_poll(&y, 6000));                 /* exactly the slice: sleep */
    CHECK_EQ_INT((int)y.longest, 5000);
    pc_yield_resumed(&y, 8000);
    CHECK(!pc_yield_poll(&y, 12999));
    CHECK(pc_yield_poll(&y, 13200));                /* a poll that came late: the stretch was longer */
    CHECK_EQ_INT((int)y.longest, 5200);
    pc_yield_resumed(&y, 15200);
    pc_yield_end(&y, 16000);                        /* the last, short stretch does not change the longest */
    CHECK_EQ_INT((int)y.longest, 5200);
    CHECK_EQ_INT((int)y.yields, 2);
    pc_yield_begin(&y, 0, 0);                       /* 0 = the default slice */
    CHECK_EQ_INT((int)y.slice_us, PC_YIELD_SLICE_US);
    CHECK(!pc_yield_poll(&y, PC_YIELD_SLICE_US - 1) && pc_yield_poll(&y, PC_YIELD_SLICE_US));
    return 0;
}
