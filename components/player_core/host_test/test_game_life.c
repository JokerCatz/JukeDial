// SPDX-License-Identifier: LGPL-2.1-or-later
/* 生命遊戲 (ideas G6): the bit-parallel step against a plain reference on every rule and edge (grid widths that are not a multiple of 64), the classic patterns
 * (blinker, block, glider 1 cell a 4 generations, the Gosper gun's period 30, the pulsar's 3), the age level (+1 every 6 generations, saturating at 7), the trail
 * (exactly the cells that died in the last N generations), the changed plane (a shadow picture kept only from it equals the grid), the torus, the wall, the RLE library. */
#include <time.h>
#include "pc/pc_game_life.h"
#include "pc_test.h"

static pc_life_t S;
static void *mem;
static void mk(int w, int h, uint32_t seed)
{
    free(mem);
    mem = malloc(pc_life_mem(w, h));
    CHECK(mem && pc_life_init(&S, mem, pc_life_mem(w, h), w, h, seed));
}
static int alive(int x, int y) { const int v = pc_life_value(&S, x, y); return v > 0 && v < PC_LIFE_TRAIL0; }
static int count_alive(void) { int n = 0; for (int y = 0; y < S.h; y++) for (int x = 0; x < S.w; x++) n += alive(x, y); return n; }
static int count_trail(void) { int n = 0; for (int y = 0; y < S.h; y++) for (int x = 0; x < S.w; x++) n += pc_life_value(&S, x, y) >= PC_LIFE_TRAIL0; return n; }

static void test_init(void)
{
    CHECK(!pc_life_init(&S, NULL, 0, 64, 64, 1));
    char small[16];
    CHECK(!pc_life_init(&S, small, sizeof small, 64, 64, 1));
    mk(100, 90, 5);
    CHECK_EQ_INT(S.nw, 2);
    CHECK(pc_life_in_circle(&S, 50, 45));
    CHECK(pc_life_in_circle(&S, 0, 0));                           /* the default shape is the full square: the corners are field */
    CHECK(pc_life_in_circle(&S, 99, 89));
    pc_life_shape(&S, true);
    CHECK(pc_life_in_circle(&S, 50, 45) && !pc_life_in_circle(&S, 0, 0) && !pc_life_in_circle(&S, 99, 89));   /* round: the circle of diameter 90 */
    pc_life_shape(&S, false);
    CHECK(!pc_life_in_circle(&S, 100, 89) && !pc_life_in_circle(&S, 99, 90));
    CHECK_EQ_INT(pc_life_value(&S, -1, 0), 0);
    pc_life_config(&S, 9, 5, 2);
    CHECK_EQ_INT(S.rule, 0);
    CHECK_EQ_INT(S.torus, 1);
    CHECK_EQ_INT(S.trail, 3);
}

/* the reference: a cell by cell count on the alive grid */
static void ref_step(const uint8_t *a, uint8_t *n, int w, int h, int rule, int torus, const pc_life_t *s)
{
    static const uint16_t B[] = { 1 << 3, 1 << 3, (1 << 3) | (1 << 6), (1 << 3) | (1 << 6) | (1 << 7) | (1 << 8) };
    const uint16_t stay[] = { (1 << 2) | (1 << 3), (1 << 2) | (1 << 3), (1 << 2) | (1 << 3), (1 << 3) | (1 << 4) | (1 << 6) | (1 << 7) | (1 << 8) };
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int c = 0;
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++) {
                    if (!dx && !dy) continue;
                    int xx = x + dx, yy = y + dy;
                    if (torus) { xx = (xx + w) % w; yy = (yy + h) % h; }
                    else if (xx < 0 || yy < 0 || xx >= w || yy >= h) continue;
                    c += a[yy * w + xx];
                }
            int v = a[y * w + x] ? (stay[rule] >> c & 1) : (B[rule] >> c & 1);
            if (!torus && !pc_life_in_circle(s, x, y)) v = 0;
            n[y * w + x] = (uint8_t)v;
        }
}
static void test_matches_reference(void)
{
    const int dims[][2] = { { 100, 90 }, { 130, 130 }, { 64, 64 }, { 37, 61 } };
    for (int d = 0; d < 4; d++)
        for (int rule = 0; rule < PC_LIFE_RULES; rule++)
            for (int torus = 0; torus < 2; torus++) {
                const int w = dims[d][0], h = dims[d][1];
                mk(w, h, 11 + (uint32_t)rule);
                pc_life_config(&S, rule, torus, 0);
                for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) if (pc_life_rand(&S) % 100 < 35) pc_life_set(&S, x, y, true);
                uint8_t *a = malloc((size_t)w * h), *n = malloc((size_t)w * h);
                for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) a[y * w + x] = (uint8_t)alive(x, y);
                for (int g = 0; g < 25; g++) {
                    ref_step(a, n, w, h, rule, torus, &S);
                    int pop = pc_life_step(&S), want = 0;
                    for (int i = 0; i < w * h; i++) want += n[i];
                    CHECK_EQ_INT(pop, want);
                    CHECK_EQ_INT(count_alive(), want);
                    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) CHECK_EQ_INT(alive(x, y), n[y * w + x]);
                    memcpy(a, n, (size_t)w * h);
                }
                free(a); free(n);
            }
}

static void test_blinker_block(void)
{
    mk(64, 64, 1);
    pc_life_set(&S, 30, 32, true); pc_life_set(&S, 31, 32, true); pc_life_set(&S, 32, 32, true);
    pc_life_step(&S);
    CHECK(alive(31, 31) && alive(31, 32) && alive(31, 33) && !alive(30, 32) && !alive(32, 32));
    pc_life_step(&S);
    CHECK(alive(30, 32) && alive(31, 32) && alive(32, 32) && !alive(31, 31));
    mk(64, 64, 1);
    pc_life_set(&S, 30, 30, true); pc_life_set(&S, 31, 30, true); pc_life_set(&S, 30, 31, true); pc_life_set(&S, 31, 31, true);
    for (int i = 0; i < 50; i++) CHECK_EQ_INT(pc_life_step(&S), 4);
    CHECK(alive(30, 30) && alive(31, 31));
}
static int glider_at(int ox, int oy)     /* the 5 cells of the glider's phase 0 with its box at (ox, oy) */
{
    return alive(ox + 1, oy) && alive(ox + 2, oy + 1) && alive(ox, oy + 2) && alive(ox + 1, oy + 2) && alive(ox + 2, oy + 2);
}
static void test_glider(void)
{
    mk(128, 128, 1);
    CHECK_EQ_INT(pc_life_place(&S, 0, 40, 40, 0), 5);
    CHECK(glider_at(39, 39));
    for (int k = 1; k <= 10; k++) {
        for (int i = 0; i < 4; i++) pc_life_step(&S);
        CHECK(glider_at(39 + k, 39 + k));                         /* one cell down and right a period of 4 */
        CHECK_EQ_INT(count_alive(), 5);
    }
}
static void test_torus_glider_returns(void)
{
    mk(32, 32, 1);
    pc_life_config(&S, PC_LIFE_CLASSIC, 1, 0);
    pc_life_place(&S, 0, 10, 10, 0);
    for (int i = 0; i < 128; i++) pc_life_step(&S);               /* 32 cells x 4 generations */
    CHECK(glider_at(9, 9));
    CHECK_EQ_INT(count_alive(), 5);
}
static void test_wall_stops_glider(void)
{
    mk(64, 64, 1);                                                /* a glider walks into the square's border wall and ends as a still life or nothing, never leaves */
    pc_life_place(&S, 0, 40, 40, 0);
    for (int i = 0; i < 400; i++) pc_life_step(&S);
    CHECK(count_alive() <= 4);
    for (int y = 0; y < 64; y++) for (int x = 0; x < 64; x++) if (!pc_life_in_circle(&S, x, y)) CHECK_EQ_INT(pc_life_value(&S, x, y), 0);
}
static void test_full_square(void)
{
    mk(64, 64, 1);                                                /* a blinker in the very corner lives (wall edge: the border is the wall, not a circle) */
    pc_life_set(&S, 0, 0, 1); pc_life_set(&S, 1, 0, 1); pc_life_set(&S, 2, 0, 1);
    CHECK_EQ_INT(count_alive(), 3);
    pc_life_step(&S);
    CHECK_EQ_INT(count_alive(), 2);                               /* a row of 3 on the border: the ends die, the middle has 2 neighbours */
    mk(64, 64, 1);
    pc_life_fill_random(&S, 100);
    CHECK_EQ_INT(count_alive(), 64 * 64);                         /* every cell of the square, corners included */
    mk(64, 64, 1);                                                /* a 2x2 block in the corner is a still life */
    pc_life_set(&S, 0, 0, 1); pc_life_set(&S, 1, 0, 1); pc_life_set(&S, 0, 1, 1); pc_life_set(&S, 1, 1, 1);
    for (int i = 0; i < 5; i++) pc_life_step(&S);
    CHECK(alive(0, 0) && alive(1, 0) && alive(0, 1) && alive(1, 1));
    CHECK_EQ_INT(count_alive(), 4);
    mk(64, 64, 1);                                                /* a glider walking to the corner is still caught by the wall: no cell leaves the grid */
    pc_life_place(&S, 0, 10, 10, 3);
    for (int i = 0; i < 400; i++) pc_life_step(&S);
    CHECK(count_alive() <= 4);
}
static void test_round_shape(void)
{
    mk(64, 64, 1);
    pc_life_shape(&S, true);
    CHECK(pc_life_in_circle(&S, 32, 0) && pc_life_in_circle(&S, 0, 32) && !pc_life_in_circle(&S, 0, 0) && !pc_life_in_circle(&S, 63, 63));   /* the circle reaches the panel edge, the corners are out */
    pc_life_fill_random(&S, 100);
    int inside = 0;
    for (int y = 0; y < 64; y++) for (int x = 0; x < 64; x++) inside += pc_life_in_circle(&S, x, y);
    CHECK_EQ_INT(count_alive(), inside);
    CHECK(inside > 64 * 64 * 76 / 100 && inside < 64 * 64 * 80 / 100);   /* pi/4 of the square */
    pc_life_clear(&S);
    CHECK_EQ_INT(pc_life_splash(&S, 0, 0, 3, 100), 0);              /* the corner is wall */
    pc_life_place(&S, 0, 10, 10, 3);
    for (int i = 0; i < 400; i++) pc_life_step(&S);
    CHECK(count_alive() <= 4);
    for (int y = 0; y < 64; y++) for (int x = 0; x < 64; x++) if (!pc_life_in_circle(&S, x, y)) CHECK_EQ_INT(pc_life_value(&S, x, y), 0);
}
static void test_library(void)
{
    const int cells[PC_LIFE_PATTERNS] = { 5, 9, 36, 48, 5, 7, 7, 9 };
    for (int i = 0; i < PC_LIFE_PATTERNS; i++) {
        int w, h;
        pc_life_pattern_size(i, &w, &h);
        for (int o = 0; o < 8; o++) {
            mk(100, 100, 1);
            pc_life_config(&S, PC_LIFE_CLASSIC, 1, 0);            /* torus: nothing refused at the corner */
            CHECK_EQ_INT(pc_life_place(&S, i, 50, 50, o), cells[i]);
            CHECK_EQ_INT(S.pop, cells[i]);
            int x0 = 999, x1 = -1, y0 = 999, y1 = -1;
            for (int y = 0; y < 100; y++) for (int x = 0; x < 100; x++) if (alive(x, y)) { x0 = x < x0 ? x : x0; x1 = x > x1 ? x : x1; y0 = y < y0 ? y : y0; y1 = y > y1 ? y : y1; }
            const int tw = o & 4 ? h : w, th = o & 4 ? w : h;
            CHECK(x1 - x0 + 1 <= tw && y1 - y0 + 1 <= th);         /* inside the pattern's own box (a row of dead cells at an edge is allowed) */
        }
    }
    CHECK_STR(pc_life_pattern_rle(99), "");
}
static int pattern_hash(void)             /* the alive set's shape: translation-free hash */
{
    int x0 = 9999, y0 = 9999;
    for (int y = 0; y < S.h; y++) for (int x = 0; x < S.w; x++) if (alive(x, y)) { if (y < y0) y0 = y; if (x < x0) x0 = x; }
    unsigned h = 17;
    for (int y = 0; y < S.h; y++) for (int x = 0; x < S.w; x++) if (alive(x, y)) h = h * 31 + (unsigned)((y - y0) * 1000 + (x - x0));
    return (int)h;
}
static void test_gun_and_pulsar(void)
{
    mk(200, 200, 1);
    pc_life_config(&S, PC_LIFE_CLASSIC, 1, 0);
    pc_life_place(&S, 2, 60, 80, 0);
    for (int g = 1; g <= 90; g++) {
        pc_life_step(&S);
        if (g % 30 == 0) CHECK_EQ_INT(count_alive(), 36 + 5 * (g / 30));   /* a glider a period of 30, the gun itself unchanged */
    }
    mk(64, 64, 1);
    pc_life_place(&S, 3, 32, 32, 0);
    const int h0 = pattern_hash();
    int ref[48] = { 0 }, n = 0;
    for (int y = 0; y < 64; y++) for (int x = 0; x < 64; x++) if (alive(x, y)) ref[n++ % 48] = y * 64 + x;
    for (int i = 0; i < 3; i++) pc_life_step(&S);
    CHECK_EQ_INT(pattern_hash(), h0);                              /* period 3, same place */
    n = 0;
    for (int y = 0; y < 64; y++) for (int x = 0; x < 64; x++) if (alive(x, y)) CHECK_EQ_INT(ref[n++ % 48], y * 64 + x);
}

static void test_age_levels(void)
{
    mk(64, 64, 1);
    pc_life_set(&S, 30, 30, true); pc_life_set(&S, 31, 30, true); pc_life_set(&S, 30, 31, true); pc_life_set(&S, 31, 31, true);
    CHECK_EQ_INT(pc_life_value(&S, 30, 30), 1);
    for (int g = 1; g <= 80; g++) {
        pc_life_step(&S);
        int want = 1 + g / PC_LIFE_AGE_GENS;
        if (want > PC_LIFE_GOLD) want = PC_LIFE_GOLD;
        CHECK_EQ_INT(pc_life_value(&S, 30, 30), want);            /* +1 every 6 generations, saturating at 7 (gold) */
        CHECK_EQ_INT(pc_life_value(&S, 31, 31), want);
    }
    CHECK_EQ_INT(pc_life_class(7), PC_LIFE_C_GOLD);
    CHECK_EQ_INT(pc_life_class(6), PC_LIFE_C_YOUNG);
    CHECK_EQ_INT(pc_life_class(0), PC_LIFE_C_EMPTY);
    CHECK_EQ_INT(pc_life_class(8), PC_LIFE_C_TRAIL0);
    CHECK_EQ_INT(pc_life_class(12), PC_LIFE_C_TRAIL0 + 4);
    /* a cell that dies and is born again starts at level 1 */
    mk(64, 64, 1);
    pc_life_set(&S, 30, 32, true); pc_life_set(&S, 31, 32, true); pc_life_set(&S, 32, 32, true);
    for (int g = 0; g < 13; g++) pc_life_step(&S);                 /* the blinker's ends die and are born every other generation: always young */
    CHECK(pc_life_value(&S, 30, 32) <= 2 && pc_life_value(&S, 32, 32) <= 2);
    CHECK(pc_life_value(&S, 31, 32) >= 2);                         /* the centre lives on */
}
static void test_trail_is_the_dead_of_the_last_n(void)
{
    const int T[] = { 0, 1, 3, 5 };
    for (int t = 0; t < 4; t++)
        for (int soup = 0; soup < 2; soup++) {
            pc_life_t a;                                           /* the same game without a trail, for the history */
            mk(96, 96, 7);
            void *ma = malloc(pc_life_mem(96, 96));
            CHECK(pc_life_init(&a, ma, pc_life_mem(96, 96), 96, 96, 7));
            pc_life_config(&S, PC_LIFE_CLASSIC, 0, T[t]);
            pc_life_config(&a, PC_LIFE_CLASSIC, 0, 0);
            if (soup) { for (int y = 0; y < 96; y++) for (int x = 0; x < 96; x++) if (pc_life_rand(&S) % 100 < 30) { pc_life_set(&S, x, y, true); pc_life_set(&a, x, y, true); } }
            else { pc_life_place(&S, 0, 40, 40, 0); pc_life_place(&a, 0, 40, 40, 0); }
            uint8_t hist[8][96 * 96];
            memset(hist, 0, sizeof hist);
            for (int y = 0; y < 96; y++) for (int x = 0; x < 96; x++) hist[0][y * 96 + x] = (uint8_t)(pc_life_value(&a, x, y) > 0);
            int shown = 0, maxk = -1;
            for (int g = 1; g <= 30; g++) {
                pc_life_step(&S); pc_life_step(&a);
                memmove(hist[1], hist[0], 7 * sizeof hist[0]);
                for (int y = 0; y < 96; y++) for (int x = 0; x < 96; x++) hist[0][y * 96 + x] = (uint8_t)(pc_life_value(&a, x, y) > 0);
                for (int y = 0; y < 96; y++)
                    for (int x = 0; x < 96; x++) {
                        int want = hist[0][y * 96 + x] ? -1 : 0;
                        if (!hist[0][y * 96 + x])
                            for (int j = 1; j <= T[t] && j < 8; j++) if (hist[j][y * 96 + x] && g - j >= 0) { want = PC_LIFE_TRAIL0 + j - 1; break; }
                        const int v = pc_life_value(&S, x, y);
                        if (want < 0) CHECK(v >= 1 && v < PC_LIFE_TRAIL0);
                        else CHECK_EQ_INT(v, want);
                        if (v >= PC_LIFE_TRAIL0) { shown++; if (v - PC_LIFE_TRAIL0 > maxk) maxk = v - PC_LIFE_TRAIL0; }
                    }
                CHECK_EQ_INT(S.pop, a.pop);
            }
            CHECK(maxk < T[t]);
            if (T[t]) CHECK_EQ_INT(maxk, T[t] - 1);                // the full length of the afterglow is reached
            else CHECK_EQ_INT(shown, 0);
            free(ma);
        }
}
static void test_glider_trail_count(void)
{
    mk(128, 128, 1);
    pc_life_config(&S, PC_LIFE_CLASSIC, 1, 3);
    pc_life_place(&S, 0, 40, 40, 0);
    int lo = 99, hi = 0;
    for (int g = 1; g <= 60; g++) {
        pc_life_step(&S);
        if (g < 8) continue;
        const int n = count_trail();
        lo = n < lo ? n : lo; hi = n > hi ? n : hi;
        CHECK_EQ_INT(count_alive(), 5);
    }
    CHECK(lo >= 3 && hi <= 9);                                      /* a glider leaves a short fading tail: never more than 3 generations of its dead cells */
}

static void test_trail_shortened(void)
{
    mk(96, 96, 3);
    pc_life_config(&S, PC_LIFE_CLASSIC, 0, 5);
    pc_life_fill_random(&S, 30);
    for (int g = 0; g < 12; g++) pc_life_step(&S);
    CHECK(count_trail() > 0);
    pc_life_config(&S, PC_LIFE_CLASSIC, 0, 1);
    for (int y = 0; y < 96; y++) for (int x = 0; x < 96; x++) CHECK(pc_life_value(&S, x, y) <= PC_LIFE_TRAIL0);   /* no step past the new length is left */
    for (int g = 0; g < 40; g++) { pc_life_step(&S); for (int y = 0; y < 96; y++) for (int x = 0; x < 96; x++) CHECK(pc_life_value(&S, x, y) <= PC_LIFE_TRAIL0); }
    pc_life_config(&S, PC_LIFE_CLASSIC, 0, 0);
    CHECK_EQ_INT(count_trail(), 0);
}
static void test_changed_plane(void)
{
    const int rules[] = { PC_LIFE_CLASSIC, PC_LIFE_HIGH };
    for (int r = 0; r < 2; r++)
        for (int t = 0; t < 4; t++) {
            const int T[] = { 0, 1, 3, 5 };
            mk(130, 100, 3);
            pc_life_config(&S, rules[r], 0, T[t]);
            pc_life_fill_random(&S, 30);
            uint8_t *shadow = calloc(130 * 100, 1);
            for (int g = 0; g < 50; g++) {
                if (g == 20) pc_life_splash(&S, 60, 50, 10, 60);
                if (g) pc_life_step(&S);
                const uint64_t *c = pc_life_chg(&S);
                for (int y = 0; y < 100; y++) for (int x = 0; x < 130; x++) if (c[y * S.nw + (x >> 6)] >> (x & 63) & 1) shadow[y * 130 + x] = (uint8_t)pc_life_class(pc_life_value(&S, x, y));
                pc_life_chg_clear(&S);
                for (int y = 0; y < 100; y++) for (int x = 0; x < 130; x++) CHECK_EQ_INT(shadow[y * 130 + x], pc_life_class(pc_life_value(&S, x, y)));
            }
            free(shadow);
        }
    mk(64, 64, 1);                                                  /* level steps inside 1..6 are not changes: a still block does not repaint every 6 generations */
    for (int dy = 0; dy < 2; dy++) for (int dx = 0; dx < 2; dx++) pc_life_set(&S, 30 + dx, 30 + dy, true);
    pc_life_chg_clear(&S);
    int changes = 0;
    for (int g = 0; g < 80; g++) {
        pc_life_step(&S);
        for (int i = 0; i < S.nw * S.h; i++) changes += __builtin_popcountll(S.chg[i]);
        pc_life_chg_clear(&S);
    }
    CHECK_EQ_INT(changes, 4);                                       /* only the turn to gold */
}
static void test_seeding(void)
{
    mk(100, 100, 42);
    pc_life_fill_random(&S, 25);
    int inside = 0;
    for (int y = 0; y < 100; y++) for (int x = 0; x < 100; x++) inside += pc_life_in_circle(&S, x, y);
    CHECK((int)S.pop > inside * 20 / 100 && (int)S.pop < inside * 30 / 100);
    CHECK_EQ_INT((int)S.pop, count_alive());
    CHECK_EQ_INT((int)S.peak, (int)S.pop);
    pc_life_clear(&S);
    CHECK_EQ_INT(count_alive(), 0);
    CHECK_EQ_INT((int)S.pop, 0);
    const int n = pc_life_splash(&S, 50, 50, 12, 100);
    CHECK(n > 400 && n < 500);                                      /* a disc of radius 12: ~452 cells */
    const int nc = pc_life_splash(&S, 0, 0, 3, 100);
    CHECK_EQ_INT(nc, 11);                                            /* the corner is field now: the quarter of the disc inside the square (11 cells) */
    pc_life_clear(&S);
    pc_life_set(&S, -1, 0, true);
    CHECK_EQ_INT(count_alive(), 0);                                 /* outside the grid is the wall */
    pc_life_config(&S, 0, 1, 0);
    pc_life_set(&S, 0, 0, true);
    CHECK_EQ_INT(count_alive(), 1);
    /* the same seed gives the same soup */
    uint32_t pa;
    mk(100, 100, 9); pc_life_fill_random(&S, 25); pa = S.pop;
    mk(100, 100, 9); pc_life_fill_random(&S, 25);
    CHECK_EQ_INT(pa, S.pop);
}
static void test_speed(void)
{
    const int px[] = { 1, 2, 3, 4, 6 };
    for (int i = 0; i < 5; i++) {
        const int n = 360 / px[i];
        mk(n, n, 5);
        pc_life_fill_random(&S, 25);
        const int gens = i < 2 ? 200 : 400;
        const clock_t t0 = clock();
        for (int g = 0; g < gens; g++) { pc_life_step(&S); pc_life_chg_clear(&S); }
        const double sec = (double)(clock() - t0) / CLOCKS_PER_SEC;
        printf("life %d px (%dx%d): %.0f generations/s on the host\n", px[i], n, n, sec > 0 ? gens / sec : 0.0);
    }
    CHECK(pc_life_cap_gens(1) < pc_life_cap_gens(6));
}
int main(void)
{
    RUN(test_init);
    RUN(test_matches_reference);
    RUN(test_blinker_block);
    RUN(test_glider);
    RUN(test_torus_glider_returns);
    RUN(test_wall_stops_glider);
    RUN(test_full_square);
    RUN(test_round_shape);
    RUN(test_library);
    RUN(test_gun_and_pulsar);
    RUN(test_age_levels);
    RUN(test_trail_is_the_dead_of_the_last_n);
    RUN(test_glider_trail_count);
    RUN(test_trail_shortened);
    RUN(test_changed_plane);
    RUN(test_seeding);
    RUN(test_speed);
    return 0;
}
