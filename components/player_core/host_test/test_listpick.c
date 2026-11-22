// SPDX-License-Identifier: LGPL-2.1-or-later
#include <errno.h>
#include "pc/pc_glyphpack.h"
#include "pc/pc_listpick.h"
#include "pc/pc_io.h"
#include "pc_test.h"

/* fixtures/make_glyphpacks.py picks(): root -> 5 groups
 *   g1 (node 1): items weights 1 3 0 2      g2: weights 0 0 (nothing pickable)      g3: group weight 0, one item
 *   g4: 100 items weight 1                  g5: no children */
static uint8_t *s_zip;
static pc_reader_t s_r;
static pc_gp_t *open_picks(void)
{
    char p[256];
    snprintf(p, sizeof p, "%s/gp_picks.zip", FIXTURES);
    FILE *f = fopen(p, "rb");
    CHECK(f != NULL);
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    s_zip = malloc((size_t)n);
    CHECK(fread(s_zip, 1, (size_t)n, f) == (size_t)n);
    fclose(f);
    pc_reader_mem(&s_r, s_zip, (uint32_t)n);
    pc_gp_t *gp;
    CHECK_EQ_INT(pc_gp_open(&s_r, NULL, &gp), 0);
    return gp;
}
static void close_picks(pc_gp_t *gp) { pc_gp_close(gp); free(s_zip); s_zip = NULL; }

static void test_count_pick(void)
{
    pc_gp_t *gp = open_picks();
    CHECK_EQ_INT(pc_lp_count(gp, 1, NULL), 3);
    CHECK_EQ_INT(pc_lp_count(gp, 2, NULL), 0);
    CHECK_EQ_INT(pc_lp_count(gp, 4, NULL), 100);
    CHECK_EQ_INT(pc_lp_count(gp, 5, NULL), 0);
    CHECK_EQ_INT(pc_lp_count(gp, 9999, NULL), 0);
    uint16_t it[8];
    CHECK_EQ_INT(pc_lp_items(gp, 1, NULL, it, 8), 3);
    CHECK_EQ_INT(it[0], 0); CHECK_EQ_INT(it[1], 1); CHECK_EQ_INT(it[2], 3);
    CHECK_EQ_INT(pc_lp_items(gp, 1, NULL, it, 2), 2);
    uint8_t ex[1] = { 0x02 };                                  /* item 1 excluded */
    CHECK_EQ_INT(pc_lp_count(gp, 1, ex), 2);
    CHECK_EQ_INT(pc_lp_items(gp, 1, ex, it, 8), 2);
    CHECK_EQ_INT(it[0], 0); CHECK_EQ_INT(it[1], 3);
    /* weights 1 3 0 2: frequencies 1/6 3/6 0 2/6 */
    pc_rng_t r;
    pc_rng_seed(&r, 7);
    int hits[4] = { 0 };
    for (int i = 0; i < 60000; i++) { int k = pc_lp_pick(&r, gp, 1, NULL); CHECK(k >= 0 && k < 4); hits[k]++; }
    CHECK_EQ_INT(hits[2], 0);
    CHECK_NEAR(hits[0] / 60000.0, 1 / 6.0, 0.01);
    CHECK_NEAR(hits[1] / 60000.0, 3 / 6.0, 0.01);
    CHECK_NEAR(hits[3] / 60000.0, 2 / 6.0, 0.01);
    for (int i = 0; i < 1000; i++) CHECK(pc_lp_pick(&r, gp, 1, ex) != 1);
    uint8_t all[1] = { 0xFF };
    CHECK_EQ_INT(pc_lp_pick(&r, gp, 1, all), -1);
    CHECK_EQ_INT(pc_lp_pick(&r, gp, 2, NULL), -1);
    CHECK_EQ_INT(pc_lp_pick(&r, gp, 5, NULL), -1);
    CHECK_EQ_INT(pc_lp_pick(&r, gp, 70000, NULL), -1);
    close_picks(gp);
}

static void test_shops(void)
{
    pc_gp_t *gp = open_picks();
    /* root children: g1 ok, g2 no dish, g3 weight 0, g4 ok, g5 empty -> shops 0 and 3 */
    CHECK_EQ_INT(pc_lp_shop_count(gp, NULL), 2);
    uint16_t s[8];
    CHECK_EQ_INT(pc_lp_shops(gp, NULL, s, 8), 2);
    CHECK_EQ_INT(s[0], 0); CHECK_EQ_INT(s[1], 3);
    pc_rng_t r;
    pc_rng_seed(&r, 3);
    int h[5] = { 0 };
    for (int i = 0; i < 4000; i++) { int k = pc_lp_pick_shop(&r, gp, NULL); CHECK(k == 0 || k == 3); h[k]++; }
    CHECK(h[0] > 1700 && h[3] > 1700);                         /* both shops weight 1 */
    uint8_t skip[1] = { 0x01 };                                /* 今天不想吃 (not today) shop 0 */
    CHECK_EQ_INT(pc_lp_shop_count(gp, skip), 1);
    for (int i = 0; i < 100; i++) CHECK_EQ_INT(pc_lp_pick_shop(&r, gp, skip), 3);
    skip[0] = 0x09;
    CHECK_EQ_INT(pc_lp_shop_count(gp, skip), 0);
    CHECK_EQ_INT(pc_lp_pick_shop(&r, gp, skip), -1);
    CHECK_EQ_INT(pc_lp_shops(gp, skip, s, 8), 0);
    close_picks(gp);
}

static void test_draw_rounds(void)
{
    pc_gp_t *gp = open_picks();
    pc_rng_t r;
    pc_rng_seed(&r, 11);
    uint8_t drawn[pc_lp_bitbytes(100)];
    memset(drawn, 0, sizeof drawn);
    /* group 1: three pickable people; no repeat -> all three in the first three draws, then a new round */
    for (int round = 0; round < 50; round++) {
        int seen[4] = { 0 };
        for (int k = 0; k < 3; k++) {
            bool nr = false;
            CHECK_EQ_INT(pc_lp_left(gp, 1, drawn), round > 0 && k == 0 ? 0 : 3 - k);
            int p = pc_lp_draw(&r, gp, 1, drawn, true, &nr);
            CHECK(p == 0 || p == 1 || p == 3);
            CHECK(!seen[p]);
            seen[p] = 1;
            CHECK_EQ_INT(nr, round > 0 && k == 0);
        }
        CHECK_EQ_INT(pc_lp_left(gp, 1, drawn), 0);
    }
    /* repeats allowed: weights decide, the drawn set still records */
    memset(drawn, 0, sizeof drawn);
    int h[4] = { 0 };
    for (int i = 0; i < 6000; i++) { bool nr = true; int p = pc_lp_draw(&r, gp, 1, drawn, false, &nr); CHECK(!nr); h[p]++; }
    CHECK_EQ_INT(h[2], 0);
    CHECK(h[1] > h[3] && h[3] > h[0]);
    CHECK_EQ_INT(pc_lp_left(gp, 1, drawn), 0);
    /* 100 people, no repeat: 100 different, then a new round */
    memset(drawn, 0, sizeof drawn);
    int seen[100] = { 0 };
    for (int k = 0; k < 100; k++) { bool nr = false; int p = pc_lp_draw(&r, gp, 4, drawn, true, &nr); CHECK(p >= 0 && p < 100 && !seen[p] && !nr); seen[p] = 1; }
    bool nr = false;
    CHECK(pc_lp_draw(&r, gp, 4, drawn, true, &nr) >= 0);
    CHECK(nr);
    CHECK_EQ_INT(pc_lp_left(gp, 4, drawn), 99);
    /* nothing to draw */
    CHECK_EQ_INT(pc_lp_draw(&r, gp, 2, drawn, true, &nr), -1);
    CHECK_EQ_INT(pc_lp_draw(&r, gp, 5, drawn, false, NULL), -1);
    close_picks(gp);
}

static void check_strip(const uint16_t *items, int n, int cap, int min_len, uint64_t seed)
{
    pc_rng_t r;
    pc_rng_seed(&r, seed);
    uint16_t map[256];
    int len = pc_lp_strip(&r, items, n, cap, min_len, map);
    if (n == 0) { CHECK_EQ_INT(len, 0); return; }
    CHECK(len >= 1);
    if (n > cap) CHECK_EQ_INT(len, cap);
    else {
        CHECK(len <= (cap > n ? cap : n));
        CHECK(len % n == 0);
        CHECK(len >= min_len || len + n > cap);
        int cnt[256] = { 0 };
        for (int i = 0; i < len; i++) for (int k = 0; k < n; k++) if (map[i] == items[k]) cnt[k]++;
        for (int k = 0; k < n; k++) CHECK_EQ_INT(cnt[k], len / n);
    }
    for (int i = 0; i < len; i++) {
        int found = 0;
        for (int k = 0; k < n; k++) if (map[i] == items[k]) found = 1;
        CHECK(found);
        if (n >= 2 && len >= 2) CHECK(map[i] != map[(i + 1) % len]);
    }
    if (n > cap) for (int i = 0; i < len; i++) for (int j = i + 1; j < len; j++) CHECK(map[i] != map[j]);   /* distinct */
}

static void test_strip(void)
{
    uint16_t items[200];
    for (int i = 0; i < 200; i++) items[i] = (uint16_t)(i * 3 + 1);
    for (int n = 0; n <= 200; n += (n < 12 ? 1 : 17))
        for (int s = 0; s < 20; s++) {
            check_strip(items, n, 32, 12, (uint64_t)(n * 100 + s));
            check_strip(items, n, 40, 3, (uint64_t)(n * 100 + s + 7));
        }
    /* every one of 100 items shows up in some strip of 32 */
    pc_rng_t r;
    pc_rng_seed(&r, 5);
    int seen[200] = { 0 };
    uint16_t map[64];
    for (int k = 0; k < 200; k++) { int len = pc_lp_strip(&r, items, 100, 32, 12, map); for (int i = 0; i < len; i++) seen[(map[i] - 1) / 3] = 1; }
    for (int i = 0; i < 100; i++) CHECK(seen[i]);
    CHECK_EQ_INT(pc_lp_strip(&r, items, 5, 0, 12, map), 0);   /* no room */
}

static void test_strip_fixed(void)
{
    uint16_t items[64], map[64];
    for (int i = 0; i < 64; i++) items[i] = (uint16_t)(100 + i);
    pc_rng_t r;
    pc_rng_seed(&r, 21);
    for (int n = 0; n <= 40; n++)
        for (int len = 0; len <= 20; len++)
            for (int s = 0; s < 10; s++) {
                int got = pc_lp_strip_fixed(&r, items, n, len, map);
                if (n == 0 || len < 1) { CHECK_EQ_INT(got, 0); continue; }
                CHECK_EQ_INT(got, len);
                int cnt[64] = { 0 };
                for (int i = 0; i < len; i++) { CHECK(map[i] >= 100 && map[i] < 100 + n); cnt[map[i] - 100]++; }
                if (n > len) for (int k = 0; k < n; k++) CHECK(cnt[k] <= 1);              /* distinct */
                else for (int k = 0; k < n; k++) CHECK(cnt[k] >= len / n - 1 && cnt[k] <= len / n + 1);   /* even */
                bool possible = n >= 3 || (n == 2 && len % 2 == 0);
                if (possible && len >= 2) for (int i = 0; i < len; i++) CHECK(map[i] != map[(i + 1) % len]);
            }
}

static void test_slot(void)
{
    pc_rng_t r;
    pc_rng_seed(&r, 9);
    uint16_t map[10] = { 5, 6, 7, 5, 6, 7, 5, 6, 7, 8 };
    for (int i = 0; i < 200; i++) {
        bool rep = true;
        int s = pc_lp_slot(&r, map, 10, 6, 0, 3, &rep);
        CHECK(!rep);
        CHECK(s == 1 || s == 4 || s == 7);
    }
    /* item 9 is not on the strip: a slot outside the window 8, 9, 0 gets it */
    int hit[10] = { 0 };
    for (int i = 0; i < 2000; i++) {
        uint16_t m[10];
        memcpy(m, map, sizeof m);
        bool rep = false;
        int s = pc_lp_slot(&r, m, 10, 9, 8, 3, &rep);
        CHECK(rep);
        CHECK(s >= 1 && s <= 7);
        CHECK_EQ_INT(m[s], 9);
        for (int k = 0; k < 10; k++) if (k != s) CHECK_EQ_INT(m[k], map[k]);
        hit[s]++;
    }
    for (int s = 1; s <= 7; s++) CHECK(hit[s] > 150);
    /* the window covers it all: any slot */
    uint16_t m2[2] = { 1, 2 };
    bool rep = false;
    int s = pc_lp_slot(&r, m2, 2, 3, 1, 3, &rep);
    CHECK(rep && (s == 0 || s == 1) && m2[s] == 3);
    CHECK_EQ_INT(pc_lp_slot(&r, m2, 0, 3, 0, 1, &rep), -1);
    /* top outside 0..len-1 or negative still works */
    for (int i = 0; i < 200; i++) { uint16_t m[10]; memcpy(m, map, sizeof m); int t = pc_lp_slot(&r, m, 10, 42, -1, 1, &rep); CHECK(t >= 0 && t < 10 && t != 9); }
}

int main(void)
{
    RUN(test_count_pick);
    RUN(test_shops);
    RUN(test_draw_rounds);
    RUN(test_strip);
    RUN(test_strip_fixed);
    RUN(test_slot);
    return 0;
}
