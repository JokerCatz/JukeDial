// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_reel.h"
#include "pc_test.h"

static void test_weighted_pick(void)
{
    pc_rng_t r;
    pc_rng_seed(&r, 7);
    const uint16_t w[4] = { 1, 0, 3, 6 };
    int hit[4] = { 0 };
    for (int i = 0; i < 100000; i++) { int k = pc_weighted_pick(&r, w, 4); CHECK(k >= 0 && k < 4); hit[k]++; }
    CHECK_EQ_INT(hit[1], 0);
    CHECK_NEAR(hit[0] / 100000.0, 0.1, 0.01);
    CHECK_NEAR(hit[2] / 100000.0, 0.3, 0.01);
    CHECK_NEAR(hit[3] / 100000.0, 0.6, 0.01);
    const uint16_t z[2] = { 0, 0 };
    CHECK_EQ_INT(pc_weighted_pick(&r, z, 2), -1);
    CHECK_EQ_INT(pc_weighted_pick(&r, w, 0), -1);
}

static void test_strip_counts_and_spread(void)
{
    for (uint64_t seed = 1; seed < 200; seed++) {
        pc_rng_t r;
        pc_rng_seed(&r, seed);
        uint8_t s[PC_SLOT_LEN];
        CHECK_EQ_INT(pc_reel_strip(&r, pc_slot_counts, PC_SYM_N, s, PC_SLOT_LEN), PC_SLOT_LEN);
        int n[PC_SYM_N] = { 0 };
        for (int i = 0; i < PC_SLOT_LEN; i++) { CHECK(s[i] < PC_SYM_N); n[s[i]]++; }
        for (int k = 0; k < PC_SYM_N; k++) CHECK_EQ_INT(n[k], pc_slot_counts[k]);
        for (int i = 0; i < PC_SLOT_LEN; i++) CHECK(s[i] != s[(i + 1) % PC_SLOT_LEN]);   /* possible: max count 6 of 27 */
    }
    pc_rng_t r;
    pc_rng_seed(&r, 1);
    uint8_t s[4];
    const uint8_t bad[2] = { 2, 1 };
    CHECK_EQ_INT(pc_reel_strip(&r, bad, 2, s, 4), -1);
    const uint8_t lop[2] = { 3, 1 };                  /* cannot be spread: still the right counts */
    CHECK_EQ_INT(pc_reel_strip(&r, lop, 2, s, 4), 4);
    CHECK_EQ_INT(s[0] + s[1] + s[2] + s[3], 1);
}

static void test_motion_lands_on_stop(void)
{
    const pc_reel_cfg_t *c = &pc_reel_default;
    for (int stop = 0; stop < PC_SLOT_LEN; stop++)
        for (int i = 0; i < 3; i++) {
            pc_reel_motion_t m;
            double p0 = 5.0 + stop * 0.37;      /* any start, also off-grid (a reel left mid-settle is not) */
            p0 = (double)(int)p0;
            pc_reel_plan(&m, p0, stop, PC_SLOT_LEN, 1000, i, c);
            CHECK_EQ_INT(m.t_dec, 1000 + c->spin_ms + i * c->stagger_ms);
            CHECK(m.v * 1000.0 > c->v_nom * 0.6 && m.v * 1000.0 < c->v_nom * 1.4);
            CHECK_NEAR(pc_reel_pos(&m, 999), p0, 1e-9);
            CHECK_NEAR(pc_reel_pos(&m, m.t_end), m.target, 1e-9);
            CHECK_NEAR(pc_reel_pos(&m, m.t_end + 5000), m.target, 1e-9);
            CHECK_EQ_INT(pc_reel_stop_at(pc_reel_pos(&m, m.t_end), PC_SLOT_LEN), stop);
            CHECK_NEAR(pc_reel_pos(&m, m.t_set), m.target + c->overshoot, 1e-6);   /* the overshoot */
            /* never runs backwards before the settle, and no jumps: speed at most the cruise speed */
            double prev = p0;
            for (int32_t t = 1000; t <= m.t_set; t++) {
                double p = pc_reel_pos(&m, t);
                CHECK(p >= prev - 1e-9);
                CHECK(p - prev <= m.v * 1.0001 + 1e-9);
                prev = p;
            }
            /* speed is continuous at the slow-down start */
            double vb = pc_reel_pos(&m, m.t_dec) - pc_reel_pos(&m, m.t_dec - 1);
            double va = pc_reel_pos(&m, m.t_dec + 1) - pc_reel_pos(&m, m.t_dec);
            CHECK_NEAR(va, vb, m.v * 0.01);
            CHECK(!pc_reel_done(&m, m.t_end - 1) && pc_reel_done(&m, m.t_end));
            CHECK(!pc_reel_landed(&m, m.t_set - 1) && pc_reel_landed(&m, m.t_set));
        }
}

static void test_staggered_stops(void)
{
    const pc_reel_cfg_t *c = &pc_reel_default;
    pc_reel_motion_t m[3];
    for (int i = 0; i < 3; i++) pc_reel_plan(&m[i], 0, 3 + 4 * i, PC_SLOT_LEN, 0, i, c);
    CHECK(m[0].t_end < m[1].t_end && m[1].t_end < m[2].t_end);
    CHECK_EQ_INT(m[1].t_set - m[0].t_set, c->stagger_ms);
}

static void test_stop_early(void)
{
    const pc_reel_cfg_t *c = &pc_reel_default;
    int changed = 0;
    for (int stop = 0; stop < PC_SLOT_LEN; stop += 2)
        for (int i = 0; i < 3; i++) {
            pc_reel_motion_t m, e;
            pc_reel_plan(&m, 10, stop, PC_SLOT_LEN, 0, i, c);
            e = m;
            int32_t now = c->accel_ms + 50;
            int32_t lim = now + i * c->early_stagger_ms + (int32_t)(PC_SLOT_LEN / m.v) + 2;
            if (!pc_reel_stop_early(&e, PC_SLOT_LEN, now, i, c)) { CHECK(m.t_dec <= lim); CHECK(!memcmp(&e, &m, sizeof e)); continue; }
            changed++;
            CHECK(e.t_dec >= now + i * c->early_stagger_ms);
            CHECK(e.t_dec <= now + i * c->early_stagger_ms + (int32_t)(PC_SLOT_LEN / e.v) + 2);
            CHECK(e.t_end < m.t_end);
            CHECK_EQ_INT(pc_reel_stop_at(pc_reel_pos(&e, e.t_end), PC_SLOT_LEN), stop);
            /* the path up to now is unchanged */
            for (int32_t t = 0; t <= now; t += 7) CHECK_NEAR(pc_reel_pos(&e, t), pc_reel_pos(&m, t), 1e-9);
            /* still no jump */
            double prev = pc_reel_pos(&e, now);
            for (int32_t t = now; t <= e.t_set; t++) { double p = pc_reel_pos(&e, t); CHECK(p >= prev - 1e-9 && p - prev <= e.v * 1.0001 + 1e-9); prev = p; }
            /* once slowing, a request changes nothing */
            pc_reel_motion_t f = e;
            CHECK(!pc_reel_stop_early(&f, PC_SLOT_LEN, e.t_dec + 1, i, c));
            CHECK(!memcmp(&f, &e, sizeof f));
        }
    CHECK(changed > 14);                  /* most of the 42 */
    /* during the speed-up: slows from the cruise start at the earliest */
    int early = 0;
    for (int stop = 0; stop < PC_SLOT_LEN; stop++) {
        pc_reel_motion_t m;
        pc_reel_plan(&m, 0, stop, PC_SLOT_LEN, 0, 0, c);
        if (!pc_reel_stop_early(&m, PC_SLOT_LEN, 10, 0, c)) continue;
        early++;
        CHECK(m.t_dec >= m.t_acc);
        CHECK_EQ_INT(pc_reel_stop_at(pc_reel_pos(&m, m.t_end), PC_SLOT_LEN), stop);
    }
    CHECK(early > 10);
}

static void test_slot_lines(void)
{
    enum { C = PC_SYM_CHERRY, L = PC_SYM_LEMON, G = PC_SYM_GRAPE, B = PC_SYM_BELL, S = PC_SYM_STAR, D = PC_SYM_DIAMOND, X = PC_SYM_SEVEN, W = PC_SYM_CLOVER };
    int lp[PC_SLOT_LINES];
    uint8_t none[3][3] = { { C, L, G }, { L, G, B }, { S, B, C } };
    CHECK_EQ_INT(pc_slot_eval(none, lp), 0);
    for (int k = 0; k < 5; k++) CHECK_EQ_INT(lp[k], 0);
    uint8_t mid[3][3] = { { L, G, B }, { X, X, X }, { G, B, S } };
    CHECK_EQ_INT(pc_slot_eval(mid, lp), pc_slot_pay3[PC_SYM_SEVEN]);
    CHECK_EQ_INT(lp[1], pc_slot_pay3[PC_SYM_SEVEN]);
    CHECK_EQ_INT(lp[0] + lp[2] + lp[3] + lp[4], 0);
    uint8_t diag[3][3] = { { D, L, B }, { G, W, S }, { B, C, D } };   /* wild in the middle: both diagonals */
    CHECK_EQ_INT(pc_slot_eval(diag, lp), pc_slot_pay3[PC_SYM_DIAMOND] + pc_slot_pay3[PC_SYM_BELL]);
    CHECK_EQ_INT(lp[3], pc_slot_pay3[PC_SYM_DIAMOND]);
    CHECK_EQ_INT(lp[4], pc_slot_pay3[PC_SYM_BELL]);
    uint8_t wild3[3][3] = { { W, W, W }, { C, C, L }, { W, L, C } };
    CHECK_EQ_INT(pc_slot_eval(wild3, lp), pc_slot_pay3[PC_SYM_CLOVER] + PC_SLOT_PAY_CHERRY2 + 2 * pc_slot_pay3[PC_SYM_CHERRY]);
    CHECK_EQ_INT(lp[3], pc_slot_pay3[PC_SYM_CHERRY]);   /* clover, cherry, cherry */
    CHECK_EQ_INT(lp[4], pc_slot_pay3[PC_SYM_CHERRY]);   /* clover, cherry, clover */
    CHECK_EQ_INT(lp[0], pc_slot_pay3[PC_SYM_CLOVER]);
    CHECK_EQ_INT(lp[1], PC_SLOT_PAY_CHERRY2);
    CHECK_EQ_INT(lp[2], 0);    /* clover, lemon, cherry: no */
    uint8_t ww[3][3] = { { W, W, L }, { L, G, G }, { G, B, S } };   /* two wilds + lemon = three lemons */
    CHECK_EQ_INT(pc_slot_eval(ww, NULL), pc_slot_pay3[PC_SYM_LEMON]);
    uint8_t ch[3][3] = { { C, W, B }, { L, G, B }, { G, B, S } };   /* cherry + wild from the left */
    CHECK_EQ_INT(pc_slot_eval(ch, NULL), PC_SLOT_PAY_CHERRY2);
    uint8_t chr[3][3] = { { B, C, C }, { L, G, B }, { G, B, S } };  /* cherries not from the left */
    CHECK_EQ_INT(pc_slot_eval(chr, NULL), 0);
}

/* one line (row 0) of a grid, the rest lemon-free filler that pays nothing: bell/star/diamond mix */
static int wins_row(uint8_t a, uint8_t b, uint8_t c, pc_slot_win_t *w)
{
    uint8_t g[3][3] = { { a, b, c }, { PC_SYM_BELL, PC_SYM_STAR, PC_SYM_BELL }, { PC_SYM_DIAMOND, PC_SYM_BELL, PC_SYM_LEMON } };
    pc_slot_win_t out[5];
    int n = pc_slot_wins(g, out);
    if (n > 0) *w = out[0];
    return n;
}

static void test_slot_wins(void)
{
    pc_slot_win_t w = { 0 };
    const uint8_t C = PC_SYM_CHERRY, L = PC_SYM_LEMON, G = PC_SYM_GRAPE, B = PC_SYM_BELL, K = PC_SYM_CLOVER;
    CHECK_EQ_INT(wins_row(G, G, G, &w), 1);
    CHECK_EQ_INT(w.line, 0); CHECK_EQ_INT(w.sym, G); CHECK_EQ_INT(w.cells, 3); CHECK_EQ_INT(w.wild, 0); CHECK_EQ_INT(w.pay, 15);
    CHECK_EQ_INT(wins_row(G, G, K, &w), 1);
    CHECK_EQ_INT(w.sym, G); CHECK_EQ_INT(w.cells, 3); CHECK_EQ_INT(w.wild, 1); CHECK_EQ_INT(w.pay, 15);
    CHECK_EQ_INT(wins_row(C, C, L, &w), 1);
    CHECK_EQ_INT(w.sym, C); CHECK_EQ_INT(w.cells, 2); CHECK_EQ_INT(w.wild, 0); CHECK_EQ_INT(w.pay, 2);
    CHECK_EQ_INT(wins_row(L, C, C, &w), 0);
    CHECK_EQ_INT(wins_row(B, B, L, &w), 0);
    CHECK_EQ_INT(wins_row(C, K, G, &w), 1);
    CHECK_EQ_INT(w.sym, C); CHECK_EQ_INT(w.cells, 2); CHECK_EQ_INT(w.wild, 1); CHECK_EQ_INT(w.pay, 2);
    CHECK_EQ_INT(wins_row(K, K, K, &w), 1);
    CHECK_EQ_INT(w.sym, K); CHECK_EQ_INT(w.cells, 3); CHECK_EQ_INT(w.wild, 3); CHECK_EQ_INT(w.pay, 120);
    CHECK_EQ_INT(wins_row(K, K, L, &w), 1);                    /* two clovers + lemon: three lemons */
    CHECK_EQ_INT(w.sym, L); CHECK_EQ_INT(w.cells, 3); CHECK_EQ_INT(w.wild, 2); CHECK_EQ_INT(w.pay, 10);
    CHECK_EQ_INT(wins_row(K, C, B, &w), 1);                    /* clover, cherry from the left: two cherries */
    CHECK_EQ_INT(w.sym, C); CHECK_EQ_INT(w.cells, 2); CHECK_EQ_INT(w.wild, 1);
    /* two paying lines, then 1000 random grids: same lines and total as pc_slot_eval */
    uint8_t two[3][3] = { { G, G, G }, { B, L, B }, { C, C, B } };
    pc_slot_win_t ow[5];
    CHECK_EQ_INT(pc_slot_wins(two, ow), 2);
    CHECK_EQ_INT(ow[0].line, 0); CHECK_EQ_INT(ow[1].line, 2); CHECK_EQ_INT(ow[1].cells, 2);
    pc_rng_t r;
    pc_rng_seed(&r, 99);
    for (int i = 0; i < 1000; i++) {
        uint8_t g[3][3];
        for (int y = 0; y < 3; y++) for (int x = 0; x < 3; x++) g[y][x] = (uint8_t)pc_rng_below(&r, i % 2 ? 4 : PC_SYM_N);   /* the odd ones: few symbols, many wins */
        int lp[5], tot = pc_slot_eval(g, lp), sum = 0, n = pc_slot_wins(g, ow), lines = 0;
        for (int l = 0; l < 5; l++) lines += lp[l] > 0;
        CHECK_EQ_INT(n, lines);
        for (int k = 0; k < n; k++) { sum += ow[k].pay; CHECK_EQ_INT(ow[k].pay, lp[ow[k].line]); }
        CHECK_EQ_INT(sum, tot);
    }
}

static void test_slot_grid_and_odds(void)
{
    uint8_t s0[PC_SLOT_LEN], s1[PC_SLOT_LEN], s2[PC_SLOT_LEN];
    pc_rng_t r;
    pc_rng_seed(&r, 0x510750u);
    pc_reel_strip(&r, pc_slot_counts, PC_SYM_N, s0, PC_SLOT_LEN);
    pc_reel_strip(&r, pc_slot_counts, PC_SYM_N, s1, PC_SLOT_LEN);
    pc_reel_strip(&r, pc_slot_counts, PC_SYM_N, s2, PC_SLOT_LEN);
    const uint8_t *st[3] = { s0, s1, s2 };
    int stops[3] = { 0, 26, 13 };
    uint8_t g[3][3];
    pc_slot_grid(st, PC_SLOT_LEN, stops, g);
    CHECK_EQ_INT(g[0][0], s0[0]); CHECK_EQ_INT(g[2][0], s0[2]);
    CHECK_EQ_INT(g[0][1], s1[26]); CHECK_EQ_INT(g[1][1], s1[0]); CHECK_EQ_INT(g[2][1], s1[1]);
    CHECK_EQ_INT(g[1][2], s2[14]);
    /* every outcome: about a third of spins win something, points per spin about 0.6-1.0 of five lines */
    long tot = 0, hits = 0, n = 0;
    for (stops[0] = 0; stops[0] < PC_SLOT_LEN; stops[0]++)
        for (stops[1] = 0; stops[1] < PC_SLOT_LEN; stops[1]++)
            for (stops[2] = 0; stops[2] < PC_SLOT_LEN; stops[2]++) {
                pc_slot_grid(st, PC_SLOT_LEN, stops, g);
                int w = pc_slot_eval(g, NULL);
                tot += w; hits += w > 0; n++;
            }
    printf("slot: hit %.3f points/spin %.3f\n", (double)hits / n, (double)tot / n);
    CHECK((double)hits / n > 0.25 && (double)hits / n < 0.45);
    CHECK((double)tot / n / 5 > 0.6 && (double)tot / n / 5 < 1.0);
    int sum = 0;
    for (int k = 0; k < PC_SYM_N; k++) sum += pc_slot_counts[k];
    CHECK_EQ_INT(sum, PC_SLOT_LEN);
}

int main(void)
{
    RUN(test_weighted_pick);
    RUN(test_strip_counts_and_spread);
    RUN(test_motion_lands_on_stop);
    RUN(test_staggered_stops);
    RUN(test_stop_early);
    RUN(test_slot_lines);
    RUN(test_slot_wins);
    RUN(test_slot_grid_and_odds);
    return 0;
}
