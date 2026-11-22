// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_latest.h"
#include "pc_test.h"

/* the board's case (batch A F1): a chip open runs, five more "next" presses arrive before it ends.
 * Only the newest waits; the three in between are handed back to be released; none is refused. */
static void test_latest_burst(void)
{
    pc_latest_t l = { 0 };
    int j[6] = { 1, 2, 3, 4, 5, 6 };
    bool wake;
    CHECK(pc_latest_put(&l, &j[0], &wake) == NULL);
    CHECK(wake);                                        /* the opener is told once */
    CHECK(pc_latest_take(&l) == &j[0]);                 /* the opener takes it: job 1 runs */
    CHECK(pc_latest_put(&l, &j[1], &wake) == NULL);
    CHECK(wake);
    for (int k = 2; k < 6; k++) {                       /* while job 1 still runs */
        CHECK(pc_latest_put(&l, &j[k], &wake) == &j[k - 1]);   /* the one waiting is replaced: the caller releases it */
        CHECK(!wake);                                   /* a wake-up is already on its way */
    }
    CHECK(pc_latest_take(&l) == &j[5]);                 /* job 1 ended: only the newest is opened */
    CHECK(pc_latest_take(&l) == NULL);
}

/* the wake-up could not be posted (the opener's queue is full of other work): the job still waits and the next
 * request posts a wake-up again; the opener finds the job after the work it was busy with */
static void test_latest_wake_lost(void)
{
    pc_latest_t l = { 0 };
    int a = 1, b = 2;
    bool wake;
    CHECK(pc_latest_put(&l, &a, &wake) == NULL);
    CHECK(wake);
    pc_latest_wake_lost(&l);
    CHECK(pc_latest_put(&l, &b, &wake) == &a);
    CHECK(wake);
    CHECK(pc_latest_take(&l) == &b);
    CHECK(pc_latest_take(&l) == NULL);
}

/* a take with nothing waiting (a spurious wake-up) gives NULL and leaves the next put to wake the opener */
static void test_latest_empty_take(void)
{
    pc_latest_t l = { 0 };
    int a = 1;
    bool wake = false;
    CHECK(pc_latest_take(&l) == NULL);
    CHECK(pc_latest_put(&l, &a, &wake) == NULL);
    CHECK(wake);
    CHECK(pc_latest_take(&l) == &a);
}

int main(void)
{
    RUN(test_latest_burst);
    RUN(test_latest_wake_lost);
    RUN(test_latest_empty_take);
    return 0;
}
