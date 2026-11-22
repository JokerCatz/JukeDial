// SPDX-License-Identifier: LGPL-2.1-or-later
#include <assert.h>
#include <stdio.h>
#include "bsp/bsp_knob_fsm.h"

#define MS 1000
static bsp_knob_fsm_t f;
static int ev(int a, int b, int64_t t) { return bsp_knob_fsm_step(&f, a, b, t); }

static void test_cw_click_counts_on_release_not_on_press(void)
{
    bsp_knob_fsm_init(&f, 5 * MS);
    assert(ev(1, 0, 100 * MS) == 0);    /* B pulled low: direction noted, nothing counted yet */
    assert(ev(1, 1, 240 * MS) == +1);   /* released into the detent: count now */
}

static void test_holding_half_way_does_not_count(void)
{
    bsp_knob_fsm_init(&f, 5 * MS);
    assert(ev(1, 0, 0) == 0);           /* user holds the knob half-way for 3 s */
    assert(f.pending == +1);
    assert(ev(1, 1, 3000 * MS) == +1);  /* counted only when it lets go */
}

static void test_ccw_click_with_b_bounce_on_release_is_minus_one(void)
{
    /* measured on this board: CCW pulls A low; B blips ~0.13 ms after A releases */
    bsp_knob_fsm_init(&f, 5 * MS);
    int sum = 0;
    sum += ev(0, 1, 100 * MS);
    sum += ev(1, 1, 1000 * MS);         /* A released -> -1 */
    sum += ev(1, 0, 1000 * MS + 130);
    sum += ev(1, 1, 1000 * MS + 170);
    sum += ev(1, 0, 1000 * MS + 260);
    sum += ev(1, 1, 1000 * MS + 270);
    assert(sum == -1);
}

static void test_next_click_after_lockout_counts(void)
{
    bsp_knob_fsm_init(&f, 5 * MS);
    assert(ev(1, 0, 0) == 0);
    assert(ev(1, 1, 20 * MS) == +1);
    assert(ev(1, 0, 26 * MS) == 0);     /* 6 ms after release: new click starts */
    assert(ev(1, 1, 40 * MS) == +1);
    assert(ev(0, 1, 50 * MS) == 0);     /* reverse direction */
    assert(ev(1, 1, 80 * MS) == -1);
}

static void test_release_bounce_inside_lockout_is_ignored(void)
{
    bsp_knob_fsm_init(&f, 5 * MS);
    assert(ev(0, 1, 0) == 0);
    assert(ev(1, 1, 10 * MS) == -1);
    assert(ev(1, 0, 13 * MS) == 0);     /* blip inside lockout: not a new click */
    assert(ev(1, 1, 14 * MS) == 0);     /* and its release does not count either */
    assert(ev(1, 0, 17 * MS) == 0);     /* 3 ms after last release: still locked */
    assert(ev(1, 1, 18 * MS) == 0);
    assert(ev(1, 0, 24 * MS) == 0);     /* 6 ms after last release: new click */
    assert(ev(1, 1, 60 * MS) == +1);
}

static void test_both_low_first_is_ignored(void)
{
    bsp_knob_fsm_init(&f, 5 * MS);
    assert(ev(0, 0, 0) == 0);           /* ambiguous: no direction */
    assert(ev(1, 1, 10 * MS) == 0);
    assert(ev(1, 0, 20 * MS) == 0);
    assert(ev(1, 1, 50 * MS) == +1);
}

static void test_other_contact_during_press_keeps_first_direction(void)
{
    bsp_knob_fsm_init(&f, 5 * MS);
    assert(ev(1, 0, 0) == 0);           /* B first: CW */
    assert(ev(0, 0, 5 * MS) == 0);      /* A also closes mid-press */
    assert(ev(0, 1, 6 * MS) == 0);
    assert(ev(1, 1, 30 * MS) == +1);    /* still CW */
}

int main(void)
{
    test_cw_click_counts_on_release_not_on_press();
    test_holding_half_way_does_not_count();
    test_ccw_click_with_b_bounce_on_release_is_minus_one();
    test_next_click_after_lockout_counts();
    test_release_bounce_inside_lockout_is_ignored();
    test_both_low_first_is_ignored();
    test_other_contact_during_press_keeps_first_direction();
    puts("knob_fsm: all tests passed");
    return 0;
}
