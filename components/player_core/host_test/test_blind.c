// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_blind.h"
#include "pc/pc_keys.h"
#include "pc_test.h"

static void test_single_click_is_late_volume(void)
{
    pc_blind_t b; pc_blind_init(&b);
    CHECK_EQ_INT(pc_blind_feed(&b, 1, false, 1000), PC_BLIND_NONE);
    CHECK_EQ_INT(pc_blind_poll(&b, 1200), PC_BLIND_NONE);
    CHECK_EQ_INT(pc_blind_poll(&b, 1300), PC_BLIND_VOL_UP);
    CHECK_EQ_INT(pc_blind_poll(&b, 1400), PC_BLIND_NONE);
    pc_blind_feed(&b, -1, false, 2000);
    CHECK_EQ_INT(pc_blind_poll(&b, 2300), PC_BLIND_VOL_DOWN);
}

static void test_double_clicks(void)
{
    pc_blind_t b; pc_blind_init(&b);
    pc_blind_feed(&b, 1, false, 1000);
    CHECK_EQ_INT(pc_blind_feed(&b, 1, false, 1250), PC_BLIND_NEXT);
    CHECK_EQ_INT(pc_blind_poll(&b, 2000), PC_BLIND_NONE);                 /* the pair gave no volume step */
    pc_blind_feed(&b, -1, false, 3000);
    CHECK_EQ_INT(pc_blind_feed(&b, -1, false, 3100), PC_BLIND_PLAYPAUSE);
    pc_blind_feed(&b, 1, false, 4000);
    CHECK_EQ_INT(pc_blind_feed(&b, 1, false, 4400), PC_BLIND_VOL_UP);     /* too slow: two separate clicks (the first comes out here) ... */
    CHECK_EQ_INT(pc_blind_poll(&b, 4800), PC_BLIND_VOL_UP);               /* ... and the second by its poll */
}

/* LAB: dbl_ms is the double-click window (0 = PC_BLIND_DBL_MS) */
static void test_lab_window(void)
{
    pc_blind_t b; pc_blind_init(&b);
    b.dbl_ms = 600;
    pc_blind_feed(&b, 1, false, 1000);
    CHECK_EQ_INT(pc_blind_poll(&b, 1400), PC_BLIND_NONE);                 /* still inside 600 ms */
    CHECK_EQ_INT(pc_blind_feed(&b, 1, false, 1500), PC_BLIND_NEXT);
    b.dbl_ms = 150;
    pc_blind_feed(&b, -1, false, 3000);
    CHECK_EQ_INT(pc_blind_poll(&b, 3150), PC_BLIND_VOL_DOWN);
}

static void test_other_key_flushes(void)
{
    pc_blind_t b; pc_blind_init(&b);
    pc_blind_feed(&b, 1, false, 1000);
    CHECK_EQ_INT(pc_blind_feed(&b, -1, false, 1100), PC_BLIND_VOL_UP);
    CHECK_EQ_INT(pc_blind_poll(&b, 1500), PC_BLIND_VOL_DOWN);
}

static void test_repeat_is_volume(void)
{
    pc_blind_t b; pc_blind_init(&b);
    pc_blind_feed(&b, 1, false, 1000);
    CHECK_EQ_INT(pc_blind_poll(&b, 1300), PC_BLIND_VOL_UP);
    CHECK_EQ_INT(pc_blind_feed(&b, 1, true, 1450), PC_BLIND_VOL_UP);
    CHECK_EQ_INT(pc_blind_feed(&b, 1, true, 1590), PC_BLIND_VOL_UP);      /* repeats 140 ms apart never become a double click */
    CHECK_EQ_INT(pc_blind_poll(&b, 2000), PC_BLIND_NONE);
}

/* the real recogniser's hold: its events fed through the blind layer by the same loop keys_svc runs (5 ms poll) */
static void test_with_pc_keys_hold_and_double(void)
{
    pc_keys_t k; pc_keys_init(&k);
    pc_blind_t b; pc_blind_init(&b);
    int ups = 0, nexts = 0;
    bool top = false;
    for (uint32_t t = 0; t < 3000; t += 5) {
        if (t >= 100 && t < 1500) top = true;                              /* a 1.4 s hold */
        else if (t >= 2000 && t < 2080) top = true;                        /* click 1 of a double */
        else if (t >= 2200 && t < 2280) top = true;                        /* click 2 */
        else top = false;
        const int e = pc_keys_step(&k, top, false, t);
        int a = PC_BLIND_NONE;
        if (e == PC_KEYS_UP || e == PC_KEYS_DOWN) a = pc_blind_feed(&b, e, pc_keys_was_repeat(&k), t);
        const int p = pc_blind_poll(&b, t);
        if (a == PC_BLIND_VOL_UP || p == PC_BLIND_VOL_UP) ups++;
        if (a == PC_BLIND_NEXT) nexts++;
        if (a == PC_BLIND_PLAYPAUSE) CHECK(0);
    }
    CHECK(ups >= 5);                                                       /* the click and the repeats of the hold */
    CHECK_EQ_INT(nexts, 1);                                                /* and exactly one next from the double click */
}

int main(void)
{
    RUN(test_single_click_is_late_volume);
    RUN(test_double_clicks);
    RUN(test_lab_window);
    RUN(test_other_key_flushes);
    RUN(test_repeat_is_volume);
    RUN(test_with_pc_keys_hold_and_double);
    return 0;
}
