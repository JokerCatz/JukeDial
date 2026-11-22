// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_hist.h"
#include "pc_test.h"

static void test_walk(void)
{
    pc_hist_t h = {0};
    CHECK_EQ_INT(pc_hist_prev(&h), -1);          /* empty */
    CHECK_EQ_INT(pc_hist_next(&h), -1);
    CHECK_EQ_INT(pc_hist_peek_next(&h), -1);
    pc_hist_add(&h, 5); pc_hist_add(&h, 7); pc_hist_add(&h, 9);
    CHECK_EQ_INT(pc_hist_prev(&h), 7);
    CHECK_EQ_INT(pc_hist_prev(&h), 5);
    CHECK_EQ_INT(pc_hist_prev(&h), -1);          /* at the oldest: unchanged */
    CHECK_EQ_INT(h.back, 2);
    CHECK_EQ_INT(pc_hist_next(&h), 7);
    CHECK_EQ_INT(h.back, 1);
}

static void test_peek_and_ends(void)
{
    pc_hist_t h = {0};
    pc_hist_add(&h, 1); pc_hist_add(&h, 2); pc_hist_add(&h, 3);
    CHECK_EQ_INT(pc_hist_next(&h), -1);          /* at the newest */
    CHECK_EQ_INT(h.back, 0);
    CHECK_EQ_INT(pc_hist_peek_next(&h), -1);
    pc_hist_prev(&h); pc_hist_prev(&h);
    for (int i = 0; i < 2; i++) {
        int p = pc_hist_peek_next(&h);
        CHECK_EQ_INT(h.back, 2 - i);
        CHECK_EQ_INT(pc_hist_next(&h), p);
        CHECK_EQ_INT(p, i + 2);
    }
    CHECK_EQ_INT(pc_hist_peek_next(&h), -1);
}

static void test_add_drops_newer(void)
{
    pc_hist_t h = {0};
    pc_hist_add(&h, 1); pc_hist_add(&h, 2); pc_hist_add(&h, 3);
    pc_hist_prev(&h);                            /* on 2 */
    pc_hist_add(&h, 8);                          /* 3 is dropped */
    CHECK_EQ_INT(h.n, 3);
    CHECK_EQ_INT(h.back, 0);
    CHECK_EQ_INT(pc_hist_peek_next(&h), -1);
    CHECK_EQ_INT(pc_hist_prev(&h), 2);
    CHECK_EQ_INT(pc_hist_prev(&h), 1);
    CHECK_EQ_INT(pc_hist_prev(&h), -1);
}

static void test_full(void)
{
    pc_hist_t h = {0};
    for (int i = 0; i < 25; i++) pc_hist_add(&h, i);
    CHECK_EQ_INT(h.n, PC_HIST_N);
    int last = -1, steps = 0;
    for (int v; (v = pc_hist_prev(&h)) >= 0; ) { last = v; steps++; }
    CHECK_EQ_INT(steps, PC_HIST_N - 1);
    CHECK_EQ_INT(last, 5);                       /* 25 adds keep the last 20: 5..24 */
    CHECK_EQ_INT(h.idx[PC_HIST_N - 1], 24);
}

static void test_clear(void)
{
    pc_hist_t h = {0};
    pc_hist_add(&h, 1); pc_hist_add(&h, 2); pc_hist_prev(&h);
    pc_hist_clear(&h);
    CHECK_EQ_INT(h.n, 0);
    CHECK_EQ_INT(h.back, 0);
    CHECK_EQ_INT(pc_hist_prev(&h), -1);
    CHECK_EQ_INT(pc_hist_peek_next(&h), -1);
}

static void test_add_oldest(void)
{
    pc_hist_t h = {0};
    pc_hist_add_oldest(&h, 4);                   /* on empty */
    CHECK_EQ_INT(h.n, 1);
    CHECK_EQ_INT(h.back, 0);
    CHECK_EQ_INT(pc_hist_prev(&h), -1);
    pc_hist_clear(&h);
    pc_hist_add(&h, 1); pc_hist_add(&h, 2); pc_hist_add(&h, 3);
    pc_hist_add_oldest(&h, 9);
    CHECK_EQ_INT(h.n, 4);
    CHECK_EQ_INT(pc_hist_prev(&h), -1);          /* the cursor is on the oldest */
    CHECK_EQ_INT(pc_hist_next(&h), 1);
    CHECK_EQ_INT(pc_hist_next(&h), 2);
    CHECK_EQ_INT(pc_hist_next(&h), 3);
    CHECK_EQ_INT(pc_hist_next(&h), -1);
    CHECK_EQ_INT(pc_hist_prev(&h), 2);           /* the old entries are all still there */
}

static void test_add_oldest_full(void)
{
    pc_hist_t h = {0};
    for (int i = 0; i < PC_HIST_N; i++) pc_hist_add(&h, i);
    pc_hist_add_oldest(&h, 99);
    CHECK_EQ_INT(h.n, PC_HIST_N);
    CHECK_EQ_INT(h.idx[0], 99);
    CHECK_EQ_INT(h.idx[PC_HIST_N - 1], PC_HIST_N - 2);   /* the newest (19) fell out */
    CHECK_EQ_INT(pc_hist_prev(&h), -1);
    CHECK_EQ_INT(pc_hist_next(&h), 0);
}

/* hires T8e: a rescan keeps the history - every entry follows its song to its new index; songs that are gone drop out, the cursor stays on
 * the same song (or the next older one that is still there) */
static int map_t8e(int idx, void *u) { const int *m = u; return m[idx]; }
static void test_remap(void)
{
    pc_hist_t h = {0};
    for (int i = 0; i < 5; i++) pc_hist_add(&h, i);           /* 0 1 2 3 4, cursor on 4 */
    pc_hist_prev(&h); pc_hist_prev(&h);                      /* cursor on 2 (back 2) */
    int m[5] = { 10, -1, 12, -1, 14 };                       /* 1 and 3 are gone */
    pc_hist_remap(&h, map_t8e, m);
    CHECK_EQ_INT(h.n, 3);
    CHECK_EQ_INT(h.idx[0], 10); CHECK_EQ_INT(h.idx[1], 12); CHECK_EQ_INT(h.idx[2], 14);
    CHECK_EQ_INT(h.back, 1);                                 /* still on 12 */
    CHECK_EQ_INT(pc_hist_next(&h), 14);
    pc_hist_t g = {0};
    pc_hist_add(&g, 0); pc_hist_add(&g, 1); pc_hist_add(&g, 2);
    pc_hist_prev(&g);                                        /* cursor on 1, which is gone: the next older one (0) */
    int m2[3] = { 5, -1, 7 };
    pc_hist_remap(&g, map_t8e, m2);
    CHECK_EQ_INT(g.n, 2); CHECK_EQ_INT(g.back, 1);
    CHECK_EQ_INT(pc_hist_peek_next(&g), 7);
    pc_hist_t k = {0};
    pc_hist_add(&k, 0); pc_hist_add(&k, 1);
    pc_hist_prev(&k);                                        /* cursor on 0, gone, nothing older: the oldest kept */
    int m3[2] = { -1, 3 };
    pc_hist_remap(&k, map_t8e, m3);
    CHECK_EQ_INT(k.n, 1); CHECK_EQ_INT(k.back, 0); CHECK_EQ_INT(k.idx[0], 3);
    int m4[4] = { -1, -1, -1, -1 };                          /* k holds 3 now */
    pc_hist_remap(&k, map_t8e, m4);                          /* all gone: empty */
    CHECK_EQ_INT(k.n, 0); CHECK_EQ_INT(k.back, 0);
    CHECK_EQ_INT(pc_hist_prev(&k), -1);
}

int main(void)
{
    RUN(test_walk);
    RUN(test_peek_and_ends);
    RUN(test_add_drops_newer);
    RUN(test_full);
    RUN(test_clear);
    RUN(test_add_oldest);
    RUN(test_add_oldest_full);
    RUN(test_remap);
    return 0;
}
