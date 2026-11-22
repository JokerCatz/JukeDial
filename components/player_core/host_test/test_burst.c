// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_burst.h"
#include "pc_test.h"

static void test_mode(void)
{
    CHECK(!pc_burst_wanted(44100));
    CHECK(!pc_burst_wanted(48000));
    CHECK(!pc_burst_wanted(88200));
    CHECK(!pc_burst_wanted(96000));
    CHECK(pc_burst_wanted(176400));
    CHECK(pc_burst_wanted(192000));
    CHECK(pc_burst_wanted(352800));
    CHECK(pc_burst_wanted(384000));
    CHECK(pc_burst_slot_frames(192000) == 1920);
    CHECK(pc_burst_slot_frames(176400) == 1764);
    CHECK(pc_burst_slots_for(1920, 1920) == 1);
    CHECK(pc_burst_slots_for(1921, 1920) == 2);
    CHECK(pc_burst_slots_for(480, 1920) == 1);
    CHECK(pc_burst_slots_for(0, 1920) == 0);
    CHECK(PC_BURST_MS == 250);
}

static void test_fifo(void)
{
    pc_bq_t q;
    pc_bq_init(&q, 4);
    uint32_t slot, gen;
    CHECK(pc_bq_space(&q) == 4 && pc_bq_fill(&q) == 0);
    CHECK(!pc_bq_take(&q, &slot, &gen));
    for (int i = 0; i < 4; i++) { CHECK(pc_bq_wr_slot(&q) == (uint32_t)i); pc_bq_commit(&q); }
    CHECK(pc_bq_space(&q) == 0 && q.primed);
    CHECK(pc_bq_take(&q, &slot, &gen) && slot == 0);
    CHECK(pc_bq_space(&q) == 0);                 /* the slot being written still counts */
    pc_bq_done(&q, gen);
    CHECK(pc_bq_space(&q) == 1);
    pc_bq_commit(&q);                            /* wr = 5: wraps to slot 0 */
    for (int i = 0; i < 3; i++) { CHECK(pc_bq_take(&q, &slot, &gen) && slot == (uint32_t)(i + 1)); pc_bq_done(&q, gen); }
    CHECK(pc_bq_take(&q, &slot, &gen) && slot == 0 && pc_bq_fill(&q) == 1);
    bool v;
    CHECK(pc_bq_min_ms(&q, &v) == 10 && v);      /* the last take saw one slot */
    pc_bq_done(&q, gen);
    CHECK(pc_bq_fill(&q) == 0);
    pc_bq_window_reset(&q);
    pc_bq_min_ms(&q, &v);
    CHECK(v == true);                            /* still primed */
}

static void test_flush(void)
{
    pc_bq_t q;
    pc_bq_init(&q, 5);
    uint32_t slot, gen;
    bool v;
    pc_bq_commit(&q); pc_bq_commit(&q);
    CHECK(pc_bq_take(&q, &slot, &gen));
    pc_bq_min_ms(&q, &v);
    CHECK(!v);                                   /* not primed: the first 250 ms do not count */
    pc_bq_flush(&q);                             /* while a take is in flight */
    CHECK(pc_bq_fill(&q) == 0 && pc_bq_space(&q) == 5);
    pc_bq_commit(&q);
    pc_bq_done(&q, gen);                         /* stale: must not release the new slot */
    CHECK(pc_bq_fill(&q) == 1);
    for (int i = 0; i < 4; i++) pc_bq_commit(&q);
    CHECK(q.primed);
    pc_bq_flush(&q);
    CHECK(!q.primed);
    for (int n = 0; n < 1000; n++) {              /* many wraps of the 2*depth index: the slots never collide */
        uint32_t used[5] = {0};
        for (int i = 0; i < 5; i++) { used[pc_bq_wr_slot(&q)]++; pc_bq_commit(&q); }
        for (int i = 0; i < 5; i++) CHECK(used[i] == 1);
        CHECK(pc_bq_space(&q) == 0);
        for (int i = 0; i < 5; i++) { CHECK(pc_bq_take(&q, &slot, &gen)); pc_bq_done(&q, gen); }
        CHECK(pc_bq_fill(&q) == 0);
    }
}

static void test_gap(void)
{
    pc_gap_t g = { 0, 0 };
    CHECK_EQ_INT(pc_gap_underrun(&g), 0);               /* not armed: steady */
    pc_gap_arm(&g);
    CHECK_EQ_INT(pc_gap_underrun(&g), 1);
    CHECK_EQ_INT(pc_gap_underrun(&g), 1);
    CHECK_EQ_INT((int)g.start_n, 2);
    pc_gap_data(&g);                                    /* the first block is in: later ones are steady and are not counted as start */
    CHECK_EQ_INT(pc_gap_underrun(&g), 0);
    CHECK_EQ_INT((int)g.start_n, 2);
    pc_gap_arm(&g);                                     /* a flush / new source arms again; the count accumulates */
    CHECK_EQ_INT(pc_gap_underrun(&g), 1);
    CHECK_EQ_INT((int)g.start_n, 3);
}

int main(void)
{
    RUN(test_mode);
    RUN(test_fifo);
    RUN(test_flush);
    RUN(test_gap);
    return 0;
}
