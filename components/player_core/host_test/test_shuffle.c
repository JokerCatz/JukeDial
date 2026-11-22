// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_shuffle.h"
#include "pc/pc_stats.h"
#include "pc_test.h"

static uint32_t lcg(uint32_t *s) { *s = *s * 1664525u + 1013904223u; return *s; }

/* library with given dates; paths "/p<i>" */
static pc_library_t make(const uint32_t *dates, int n)
{
    pc_library_t l;
    memset(&l, 0, sizeof l);
    l.tracks = calloc((size_t)n, sizeof *l.tracks);
    l.pool = calloc((size_t)n, 8);
    for (int i = 0; i < n; i++) {
        sprintf(l.pool + 8 * i, "/p%d", i);
        l.tracks[i].path = (uint32_t)(8 * i);
        l.tracks[i].date = dates[i];
        if (dates[i] > l.newest_date) l.newest_date = dates[i];
    }
    l.n = n;
    l.pool_len = (uint32_t)(8 * n);
    return l;
}

static void test_days(void)
{
    CHECK_EQ_INT(pc_days_between(20240229, 20240301), 1);
    CHECK_EQ_INT(pc_days_between(20231231, 20240101), 1);
    CHECK_EQ_INT(pc_days_between(20240101, 20230101), -365);
}

static void test_weight(void)
{
    CHECK_EQ_INT(pc_shuffle_weight(20241020, 20241101, false), 4);
    CHECK_EQ_INT(pc_shuffle_weight(20240901, 20241101, false), 2);
    CHECK_EQ_INT(pc_shuffle_weight(20230101, 20241101, false), 1);
    CHECK_EQ_INT(pc_shuffle_weight(0, 20241101, false), 1);
    CHECK_EQ_INT(pc_shuffle_weight(20241020, 20241101, true), 8);
}

static void test_pick_distribution(void)
{
    const uint32_t dates[] = { 20241101, 20240901, 20200101 };   /* now = 20241101: weights 4, 2, 1 */
    pc_library_t l = make(dates, 3);
    int cand[] = { 0, 1, 2 }, hits[3] = { 0 };
    pc_recent_t rec = { 0 };
    uint32_t s = 1;
    for (int i = 0; i < 70000; i++) hits[pc_shuffle_pick(&l, cand, 3, NULL, &rec, lcg(&s))]++;
    CHECK_NEAR(hits[0] / 70000.0, 4 / 7.0, 0.02);
    CHECK_NEAR(hits[1] / 70000.0, 2 / 7.0, 0.02);
    CHECK_NEAR(hits[2] / 70000.0, 1 / 7.0, 0.02);
    pc_library_free(&l);
}

static void test_pick_favourite_doubles(void)
{
    const uint32_t dates[] = { 20200101, 20200101 };
    pc_library_t l = make(dates, 2);
    pc_stats_t *st = pc_stats_create();
    pc_stats_toggle_fav(st, "mp3:/p1");
    int cand[] = { 0, 1 }, hits[2] = { 0 };
    pc_recent_t rec = { 0 };
    uint32_t s = 7;
    for (int i = 0; i < 30000; i++) hits[pc_shuffle_pick(&l, cand, 2, st, &rec, lcg(&s))]++;
    CHECK_NEAR(hits[1] / 30000.0, 2 / 3.0, 0.02);
    pc_stats_destroy(st);
    pc_library_free(&l);
}

static void test_pick_skips_recent(void)
{
    uint32_t dates[30];
    for (int i = 0; i < 30; i++) dates[i] = 20240101;
    pc_library_t l = make(dates, 30);
    int cand[30];
    for (int i = 0; i < 30; i++) cand[i] = i;
    pc_recent_t rec = { 0 };
    for (int i = 0; i < 25; i++) pc_recent_push(&rec, i);     /* only the last 20 (5..24) count */
    CHECK(!pc_recent_has(&rec, 4));
    CHECK(pc_recent_has(&rec, 5));
    uint32_t s = 3;
    for (int i = 0; i < 5000; i++) {
        int p = pc_shuffle_pick(&l, cand, 30, NULL, &rec, lcg(&s));
        CHECK(p < 5 || p >= 25);
    }
    pc_library_free(&l);
}

static void test_pick_all_recent(void)
{
    const uint32_t dates[] = { 1, 1, 1, 1, 1 };
    pc_library_t l = make(dates, 5);
    int cand[] = { 0, 1, 2, 3, 4 };
    pc_recent_t rec = { 0 };
    for (int i = 0; i < 5; i++) pc_recent_push(&rec, i);      /* last played = 4 */
    uint32_t s = 9;
    int seen[5] = { 0 };
    for (int i = 0; i < 5000; i++) {
        int p = pc_shuffle_pick(&l, cand, 5, NULL, &rec, lcg(&s));
        CHECK(p != 4);
        seen[p]++;
    }
    for (int i = 0; i < 4; i++) CHECK(seen[i] > 0);
    pc_library_free(&l);
}

static void test_pick_single(void)
{
    const uint32_t dates[] = { 20240101, 20240101 };
    pc_library_t l = make(dates, 2);
    int cand[] = { 1 };
    pc_recent_t rec = { 0 };
    pc_recent_push(&rec, 1);
    CHECK_EQ_INT(pc_shuffle_pick(&l, cand, 1, NULL, &rec, 12345), 1);
    CHECK_EQ_INT(pc_shuffle_pick(&l, cand, 0, NULL, &rec, 12345), -1);
    pc_library_free(&l);
}

static void test_order_next(void)
{
    int cand[] = { 4, 7, 9 };
    CHECK_EQ_INT(pc_order_next(PC_ORDER_SEQUENTIAL, cand, 3, 4, 99), 7);
    CHECK_EQ_INT(pc_order_next(PC_ORDER_SEQUENTIAL, cand, 3, 9, 99), 4);     /* wraps */
    CHECK_EQ_INT(pc_order_next(PC_ORDER_SEQUENTIAL, cand, 3, -1, 99), 4);    /* nothing playing: first */
    CHECK_EQ_INT(pc_order_next(PC_ORDER_SEQUENTIAL, cand, 3, 5, 99), 4);     /* a track from elsewhere: first */
    CHECK_EQ_INT(pc_order_next(PC_ORDER_REPEAT_ONE, cand, 3, 7, 99), 7);
    CHECK_EQ_INT(pc_order_next(PC_ORDER_REPEAT_ONE, cand, 3, -1, 99), 99);   /* nothing to repeat: shuffle */
    CHECK_EQ_INT(pc_order_next(PC_ORDER_SHUFFLE, cand, 3, 7, 99), 99);
    CHECK_EQ_INT(pc_order_next(PC_ORDER_SEQUENTIAL, cand, 0, 7, 99), -1);
}

/* hires T8e: a rescan keeps the recent list (the shuffle's "not again soon"): entries follow their songs, gone ones drop out, order kept */
static int map_t8e(int idx, void *u) { const int *m = u; return idx >= 0 && idx < 30 ? m[idx] : -1; }
static void test_recent_remap(void)
{
    pc_recent_t r = {0};
    for (int i = 0; i < 25; i++) pc_recent_push(&r, i);     /* wrapped: 5..24 kept, 24 last */
    int m[30];
    for (int i = 0; i < 30; i++) m[i] = i % 2 ? i + 100 : -1;   /* the even ones are gone */
    pc_recent_remap(&r, map_t8e, m);
    CHECK_EQ_INT(r.n, 10);
    CHECK_EQ_INT(pc_recent_last(&r), 123);
    CHECK(pc_recent_has(&r, 105)); CHECK(!pc_recent_has(&r, 103)); CHECK(!pc_recent_has(&r, 6));
    pc_recent_push(&r, 7);
    CHECK_EQ_INT(pc_recent_last(&r), 7);
    CHECK(pc_recent_has(&r, 123));
    pc_recent_t e = {0};
    pc_recent_remap(&e, map_t8e, m);
    CHECK_EQ_INT(e.n, 0); CHECK_EQ_INT(pc_recent_last(&e), -1);
}

/* the weights made once (pc_wscope) give the SAME track as the old per-pick weighing for the same random number: a library of mixed dates and
 * favourites, ranges that do not start at 0, recent lists of every fill, the all-recent fallback; then the statistics over many picks */
static void test_wscope_matches_old(void)
{
    enum { N = 700 };
    uint32_t dates[N], s = 99;
    for (int i = 0; i < N; i++) { const uint32_t r = lcg(&s) >> 8; dates[i] = r % 5 == 0 ? 0 : (r % 3 == 0 ? 20241001 + r % 28 : (r % 3 == 1 ? 20240901 : 20200101));  }
    pc_library_t l = make(dates, N);
    pc_stats_t *st = pc_stats_create();
    char id[32];
    for (int i = 0; i < N; i += 13) { snprintf(id, sizeof id, "mp3:/p%d", i); pc_stats_toggle_fav(st, id); }
    static const int range[][2] = { { 0, N }, { 100, 300 }, { 5, 1 }, { 640, 60 }, { 0, 64 }, { 63, 66 }, { 0, 2 }, { 300, 20 } };
    for (unsigned ri = 0; ri < sizeof range / sizeof range[0]; ri++)
        for (int use_st = 0; use_st < 2; use_st++) {
            const int first = range[ri][0], n = range[ri][1];
            int cand[N];
            for (int i = 0; i < n; i++) cand[i] = first + i;
            pc_wscope_t ws = { 0 };
            CHECK_EQ_INT(pc_wscope_build(&ws, &l, first, n, use_st ? st : NULL), 0);
            CHECK(pc_wscope_fresh(&ws, &l, first, n, use_st ? st : NULL));
            for (int round = 0; round < 3000; round++) {
                pc_recent_t rec = { 0 };
                const int fill = round % 25;                          /* 0..24 recent entries (more than the list holds), from inside and outside the range */
                for (int k = 0; k < fill; k++) pc_recent_push(&rec, (round % 5 == 0 && n > 1) ? first + (int)(lcg(&s) >> 8) % n : (int)(lcg(&s) >> 8) % N);
                const uint32_t rnd = lcg(&s) >> 3;
                CHECK_EQ_INT(pc_wscope_pick(&ws, &rec, rnd), pc_shuffle_pick(&l, cand, n, use_st ? st : NULL, &rec, rnd));
            }
            pc_wscope_free(&ws);
        }
    /* every candidate recent: only the last played is skipped */
    pc_wscope_t ws = { 0 };
    CHECK_EQ_INT(pc_wscope_build(&ws, &l, 10, 15, NULL), 0);
    pc_recent_t rec = { 0 };
    for (int i = 10; i < 25; i++) pc_recent_push(&rec, i);
    for (int k = 0; k < 500; k++) CHECK(pc_wscope_pick(&ws, &rec, lcg(&s) >> 3) != 24);
    pc_wscope_free(&ws);
    CHECK_EQ_INT(pc_wscope_pick(&ws, &rec, 1), -1);                   /* empty */
    pc_stats_destroy(st);
    pc_library_free(&l);
}

static void test_wscope_distribution(void)
{
    const uint32_t dates[] = { 20241101, 20240901, 20200101, 20200101 };   /* weights 4, 2, 1, 1 (index 3 a favourite: 2) */
    pc_library_t l = make(dates, 4);
    pc_stats_t *st = pc_stats_create();
    pc_stats_toggle_fav(st, "mp3:/p3");
    pc_wscope_t ws = { 0 };
    CHECK_EQ_INT(pc_wscope_build(&ws, &l, 0, 4, st), 0);
    int hits[4] = { 0 };
    pc_recent_t rec = { 0 };
    uint32_t s = 5;
    for (int i = 0; i < 90000; i++) hits[pc_wscope_pick(&ws, &rec, lcg(&s) >> 3)]++;
    CHECK_NEAR(hits[0] / 90000.0, 4 / 9.0, 0.01);
    CHECK_NEAR(hits[1] / 90000.0, 2 / 9.0, 0.01);
    CHECK_NEAR(hits[2] / 90000.0, 1 / 9.0, 0.01);
    CHECK_NEAR(hits[3] / 90000.0, 2 / 9.0, 0.01);
    pc_stats_toggle_fav(st, "mp3:/p3");                                 /* a toggled favourite makes the weights stale */
    CHECK(!pc_wscope_fresh(&ws, &l, 0, 4, st));
    pc_wscope_free(&ws);
    pc_stats_destroy(st);
    pc_library_free(&l);
}

static void test_order_next_range(void)
{
    CHECK_EQ_INT(pc_order_next_range(PC_ORDER_SEQUENTIAL, 4, 3, 4, 99), 5);
    CHECK_EQ_INT(pc_order_next_range(PC_ORDER_SEQUENTIAL, 4, 3, 6, 99), 4);   /* wraps */
    CHECK_EQ_INT(pc_order_next_range(PC_ORDER_SEQUENTIAL, 4, 3, -1, 99), 4);
    CHECK_EQ_INT(pc_order_next_range(PC_ORDER_SEQUENTIAL, 4, 3, 9, 99), 4);   /* from elsewhere: first */
    CHECK_EQ_INT(pc_order_next_range(PC_ORDER_REPEAT_ONE, 4, 3, 5, 99), 5);
    CHECK_EQ_INT(pc_order_next_range(PC_ORDER_REPEAT_ONE, 4, 3, -1, 99), 99);
    CHECK_EQ_INT(pc_order_next_range(PC_ORDER_SHUFFLE, 4, 3, 5, 99), 99);
    CHECK_EQ_INT(pc_order_next_range(PC_ORDER_SEQUENTIAL, 4, 0, 5, 99), -1);
}

int main(void)
{
    RUN(test_wscope_matches_old);
    RUN(test_wscope_distribution);
    RUN(test_order_next_range);
    RUN(test_order_next);
    RUN(test_days);
    RUN(test_weight);
    RUN(test_pick_distribution);
    RUN(test_pick_favourite_doubles);
    RUN(test_pick_skips_recent);
    RUN(test_pick_all_recent);
    RUN(test_pick_single);
    RUN(test_recent_remap);
    return 0;
}
