// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_clkmeas.h"
#include "pc_test.h"

/* hires T12 fix 1: the read-back counts descriptors by ring position, so a lost interrupt cannot undercount */
static char RING[8];
static const void *B(int i) { return &RING[i]; }

/* a clean run at `rate` with frames-per-descriptor F: events every F/rate seconds; lost[] marks events that never reach the callback */
static uint32_t run(uint32_t rate, uint32_t F, int desc, int events, const int *lost, int nlost, int start, pc_clkmeas_t *m)
{
    pc_clkmeas_reset(m, (uint8_t)desc, 0);
    for (int k = 0; k < events; k++) {
        int skip = 0;
        for (int j = 0; j < nlost; j++) if (lost[j] == k) skip = 1;
        if (skip) continue;
        uint32_t t = (uint32_t)((uint64_t)(k + 1) * F * 1000000u / rate);
        pc_clkmeas_event(m, B((start + k) % desc), t);
    }
    return pc_clkmeas_rate(m, 1, m->t0, F, 8);
}

static void test_clean(void)
{
    pc_clkmeas_t m;
    uint32_t r = run(192000, 511, 8, 100, NULL, 0, 0, &m);
    CHECK(r > 191900 && r < 192100);
    r = run(176400, 511, 8, 100, NULL, 0, 3, &m);                 /* any starting descriptor */
    CHECK(r > 176300 && r < 176500);
    r = run(96000, 480, 8, 60, NULL, 0, 5, &m);
    CHECK(r > 95900 && r < 96100);
}

static void test_lost_interrupts(void)
{
    pc_clkmeas_t m;
    const int lost1[] = { 40 };                                    /* one lost event: 1.06 % short when counted one by one */
    uint32_t r = run(192000, 511, 8, 100, lost1, 1, 0, &m);
    CHECK(pc_clkmeas_close(192000, r, m.t1 - m.t0));
    CHECK(r > 191000 && r < 193000);
    const int lost3[] = { 30, 31, 60, 61, 62 };                    /* bursts of merged events */
    r = run(192000, 511, 8, 100, lost3, 5, 2, &m);
    CHECK(r > 191000 && r < 193000);
    const int lost_last[] = { 99 };
    r = run(176400, 511, 8, 100, lost_last, 1, 0, &m);             /* the last one lost: the window just ends one earlier */
    CHECK(r > 175000 && r < 177600);
}

static void test_wrong_clock(void)
{
    pc_clkmeas_t m;
    uint32_t r = run(192000 * 96 / 100, 511, 8, 100, NULL, 0, 0, &m);   /* the DMA really runs 4 % slow (a wrong divider) */
    CHECK(!pc_clkmeas_close(192000, r, m.t1 - m.t0));
    r = run(192000 * 2, 511, 8, 100, NULL, 0, 0, &m);                   /* twice as fast */
    CHECK(!pc_clkmeas_close(192000, r, m.t1 - m.t0));
    r = run(96000, 511, 8, 100, NULL, 0, 0, &m);                        /* half */
    CHECK(!pc_clkmeas_close(192000, r, m.t1 - m.t0));
}

static void test_learning(void)
{
    pc_clkmeas_t m;
    pc_clkmeas_reset(&m, 4, 0);
    pc_clkmeas_event(&m, B(0), 100);
    pc_clkmeas_event(&m, B(1), 200);
    pc_clkmeas_event(&m, B(3), 400);                               /* 2 lost while learning: the order is 0 1 3 ... */
    pc_clkmeas_event(&m, B(0), 500);                               /* a repeat before the ring is complete: start over */
    CHECK_EQ_INT(m.n, 0);
    pc_clkmeas_event(&m, B(1), 600);
    pc_clkmeas_event(&m, B(2), 700);
    pc_clkmeas_event(&m, B(3), 800);
    CHECK_EQ_INT(m.n, 1);                                          /* complete: the reference */
    CHECK_EQ_INT(m.t0, 800);
    pc_clkmeas_event(&m, B(0), 900);
    pc_clkmeas_event(&m, B(2), 1100);                              /* 1 lost: +2 */
    CHECK_EQ_INT(m.n, 4);
    pc_clkmeas_event(&m, B(2), 1500);                              /* the same one again: a whole lap */
    CHECK_EQ_INT(m.n, 8);
    pc_clkmeas_event(&m, (const void *)&m, 1600);                  /* a buffer outside the ring: relearn */
    CHECK_EQ_INT(m.n, 0);
    pc_clkmeas_reset(&m, 0, 0);                                    /* never measures */
    pc_clkmeas_event(&m, B(0), 1);
    CHECK_EQ_INT(m.n, 0);
    pc_clkmeas_reset(&m, 9, 0);
    pc_clkmeas_event(&m, B(0), 1);
    CHECK_EQ_INT(m.n, 0);
}

static void test_close(void)
{
    CHECK(pc_clkmeas_close(192000, 192000, 500000));
    CHECK(pc_clkmeas_close(192000, 192000 + 1900, 500000));        /* 1 % */
    CHECK(pc_clkmeas_close(192000, 192000 - 4000, 500000));        /* + 6 ms / 500 ms = 1.2 % more allowance */
    CHECK(!pc_clkmeas_close(192000, 192000 - 8000, 500000));
    CHECK(!pc_clkmeas_close(192000, 0, 500000));
    CHECK(!pc_clkmeas_close(0, 1, 500000));
    CHECK(!pc_clkmeas_close(192000, 192000, 0));
}

int main(void)
{
    RUN(test_clean);
    RUN(test_lost_interrupts);
    RUN(test_wrong_clock);
    RUN(test_learning);
    RUN(test_close);
    return 0;
}
