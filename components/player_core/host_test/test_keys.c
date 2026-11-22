// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_keys.h"
#include "pc_test.h"

/* drive the recogniser: levels top/bottom from t0 for ms, polled every 5 ms; returns the events as a string of U / D / B */
typedef struct { pc_keys_t k; uint32_t t; char ev[64]; int n; } rig_t;
static void rig_init(rig_t *r) { pc_keys_init(&r->k); r->t = 1000; r->n = 0; r->ev[0] = 0; pc_keys_step(&r->k, false, false, r->t); }
static void run(rig_t *r, bool top, bool bot, uint32_t ms)
{
    for (uint32_t i = 0; i < ms; i += 5) {
        r->t += 5;
        int e = pc_keys_step(&r->k, top, bot, r->t);
        if (e && r->n < 62) { r->ev[r->n++] = e == PC_KEYS_UP ? 'U' : e == PC_KEYS_DOWN ? 'D' : 'B'; r->ev[r->n] = 0; }
    }
}

static void test_click(void)
{
    rig_t r; rig_init(&r);
    run(&r, true, false, 200); run(&r, false, false, 100);
    CHECK_STR(r.ev, "U");
    run(&r, false, true, 200); run(&r, false, false, 100);
    CHECK_STR(r.ev, "UD");
}

static void test_short_click_on_release(void)
{
    rig_t r; rig_init(&r);
    run(&r, true, false, 40); run(&r, false, false, 100);   /* shorter than the combo window, longer than the debounce */
    CHECK_STR(r.ev, "U");
}

static void test_pulse_press_release(void)
{
    rig_t r; rig_init(&r);
    run(&r, true, false, 25); run(&r, false, false, 100);   /* a press + release read in one PMU poll is held for 25 ms by bsp_keys: it must still be a click */
    CHECK_STR(r.ev, "U");
}

static void test_bounce_ignored(void)
{
    rig_t r; rig_init(&r);
    run(&r, true, false, 5); run(&r, false, false, 100);    /* a 5 ms blip: under the debounce */
    CHECK_STR(r.ev, "");
}

static void test_hold_repeats_then_stops(void)
{
    rig_t r; rig_init(&r);
    run(&r, false, true, PC_KEYS_CAP_MS + 3000);            /* held far past the cap (the PMU's power-off is near 6 s) */
    run(&r, false, false, 100);
    int n = r.n;
    CHECK(n >= 2);
    for (int i = 0; i < n; i++) CHECK(r.ev[i] == 'D');
    /* the click, then one per PC_KEYS_REPEAT_MS between HOLD and CAP */
    CHECK(n <= 1 + (PC_KEYS_CAP_MS - PC_KEYS_HOLD_MS) / PC_KEYS_REPEAT_MS + 1);
    CHECK(n >= 1 + (PC_KEYS_CAP_MS - PC_KEYS_HOLD_MS) / PC_KEYS_REPEAT_MS - 2);
    const int at_cap = r.n;
    rig_t q; rig_init(&q);
    run(&q, true, false, PC_KEYS_CAP_MS + 200);
    const int a = q.n;
    run(&q, true, false, 3000);
    CHECK_EQ_INT(q.n, a);                                    /* nothing more after the cap */
    (void)at_cap;
}

static void test_both_keys(void)
{
    rig_t r; rig_init(&r);
    run(&r, true, false, 30); run(&r, true, true, 600); run(&r, false, false, 100);
    CHECK_STR(r.ev, "B");                                    /* no detent, no repeat while both are down */
    run(&r, true, false, 30); run(&r, true, true, 30); run(&r, false, true, 600); run(&r, false, false, 100);
    CHECK_STR(r.ev, "BB");                                   /* the one left down after the first release stays silent */
}

static void test_second_key_late_is_no_combo(void)
{
    rig_t r; rig_init(&r);
    run(&r, true, false, 300);                               /* the first key has given its detent */
    run(&r, true, true, 300); run(&r, false, false, 100);
    CHECK_STR(r.ev, "U");
}

static void test_held_at_start_is_silent(void)
{
    pc_keys_t k; pc_keys_init(&k);
    uint32_t t = 1000; int n = 0;
    for (int i = 0; i < 300; i++) { t += 5; if (pc_keys_step(&k, false, true, t)) n++; }   /* the BOOT strap held through power-up */
    CHECK_EQ_INT(n, 0);
    for (int i = 0; i < 60; i++) { t += 5; if (pc_keys_step(&k, false, false, t)) n++; }
    CHECK_EQ_INT(n, 0);
    for (int i = 0; i < 60; i++) { t += 5; if (pc_keys_step(&k, false, true, t)) n++; }
    CHECK_EQ_INT(n, 1);                                      /* a later press works */
}

static void test_clock_wrap(void)
{
    pc_keys_t k; pc_keys_init(&k);
    uint32_t t = 0xFFFFFF00u; int n = 0;
    pc_keys_step(&k, false, false, t);
    for (int i = 0; i < 80; i++) { t += 5; if (pc_keys_step(&k, true, false, t) == PC_KEYS_UP) n++; }
    CHECK_EQ_INT(n, 1);
}

/* LAB: hold_ms / rep_ms override the hold-to-repeat times (0 = the constants) */
static void test_lab_hold_repeat(void)
{
    rig_t r; rig_init(&r);
    r.k.hold_ms = 900; r.k.rep_ms = 100;
    run(&r, true, false, 800);
    CHECK_STR(r.ev, "U");                                  /* the click only: a repeat would have begun at 450 ms by default */
    run(&r, true, false, 400);
    CHECK(r.n >= 4);                                       /* from 900 ms every 100 ms */
    rig_t d; rig_init(&d);
    run(&d, true, false, 800);
    CHECK(d.n >= 3);                                       /* the default: 450 ms / 140 ms */
}

int main(void)
{
    RUN(test_click);
    RUN(test_short_click_on_release);
    RUN(test_pulse_press_release);
    RUN(test_bounce_ignored);
    RUN(test_hold_repeats_then_stops);
    RUN(test_both_keys);
    RUN(test_second_key_late_is_no_combo);
    RUN(test_held_at_start_is_silent);
    RUN(test_clock_wrap);
    RUN(test_lab_hold_repeat);
    return 0;
}
