// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_remote.h"
#include "pc_test.h"

static void test_abs_ends_and_clamp(void)
{
    CHECK_EQ_INT(pc_remote_abs_from_vol(0), 0);
    CHECK_EQ_INT(pc_remote_abs_from_vol(100), 127);
    CHECK_EQ_INT(pc_remote_abs_from_vol(50), 64);        /* 63.5 rounds up */
    CHECK_EQ_INT(pc_remote_abs_from_vol(-5), 0);
    CHECK_EQ_INT(pc_remote_abs_from_vol(250), 127);
    CHECK_EQ_INT(pc_remote_vol_from_abs(0, 40), 0);
    CHECK_EQ_INT(pc_remote_vol_from_abs(127, 40), 100);
    CHECK_EQ_INT(pc_remote_vol_from_abs(200, 40), 100);  /* out of range clamps */
}

static void test_round_trip_every_volume(void)
{
    for (int v = 0; v <= 100; v++) CHECK_EQ_INT(pc_remote_vol_from_abs(pc_remote_abs_from_vol(v), v), v);
    for (int v = 0; v <= 100; v++)                        /* also with another current value: the mapping alone is exact */
        CHECK_EQ_INT(pc_remote_vol_from_abs(pc_remote_abs_from_vol(v), 100 - v), v);
}

static void test_monotonic(void)
{
    for (int v = 1; v <= 100; v++) CHECK(pc_remote_abs_from_vol(v) > pc_remote_abs_from_vol(v - 1));
    for (int cur = 0; cur <= 100; cur += 10)
        for (int a = 1; a <= 127; a++) CHECK(pc_remote_vol_from_abs((uint8_t)a, cur) >= pc_remote_vol_from_abs((uint8_t)(a - 1), cur));
}

static void test_key_codes(void)
{
    CHECK_EQ_INT(pc_remote_key(0x44), PC_RK_PLAY);
    CHECK_EQ_INT(pc_remote_key(0x45), PC_RK_STOP);
    CHECK_EQ_INT(pc_remote_key(0x46), PC_RK_PAUSE);
    CHECK_EQ_INT(pc_remote_key(0x4B), PC_RK_NEXT);
    CHECK_EQ_INT(pc_remote_key(0x4C), PC_RK_PREV);
    CHECK_EQ_INT(pc_remote_key(0x41), PC_RK_NONE);       /* VOL_UP: not ours */
    CHECK_EQ_INT(pc_remote_key(0x00), PC_RK_NONE);
    CHECK_STR(pc_remote_key_name(PC_RK_NEXT), "next");
    CHECK_STR(pc_remote_key_name(PC_RK_NONE), "none");
    CHECK_STR(pc_remote_act_name(PC_RA_RESUME), "resume");
    CHECK_STR(pc_remote_act_name(PC_RA_NONE), "none");
}

/* every key x owner x playing x prev x ringing alarm */
static void test_actions_every_state(void)
{
    static const pc_remote_key_t keys[] = { PC_RK_NONE, PC_RK_PLAY, PC_RK_PAUSE, PC_RK_STOP, PC_RK_NEXT, PC_RK_PREV };
    for (int k = 0; k < 6; k++)
        for (int own = 0; own < 2; own++)
            for (int pl = 0; pl < 2; pl++)
                for (int pv = 0; pv < 2; pv++)
                    for (int al = 0; al < 2; al++) {
                        pc_remote_act_t a = pc_remote_action(keys[k], own, pl, pv, al), want = PC_RA_NONE;
                        if (own && !al) switch (keys[k]) {
                            case PC_RK_PLAY: case PC_RK_PAUSE: want = pl ? PC_RA_PAUSE : PC_RA_RESUME; break;
                            case PC_RK_STOP: want = PC_RA_STOP; break;
                            case PC_RK_NEXT: want = PC_RA_NEXT; break;
                            case PC_RK_PREV: want = pv ? PC_RA_PREV : PC_RA_NONE; break;
                            default: break;
                        }
                        if (a != want) fprintf(stderr, "key=%d owner=%d playing=%d prev=%d alarm=%d\n", (int)keys[k], own, pl, pv, al);
                        CHECK_EQ_INT(a, want);
                    }
}

static void test_press_filter(void)
{
    pc_remote_press_t p = { 0 };
    CHECK(pc_remote_press(&p, PC_RK_NEXT, true, 1000));
    CHECK(!pc_remote_press(&p, PC_RK_NEXT, false, 1050));   /* release */
    CHECK(!pc_remote_press(&p, PC_RK_NEXT, true, 1100));    /* held, repeating */
    CHECK(!pc_remote_press(&p, PC_RK_NEXT, true, 1299));
    CHECK(pc_remote_press(&p, PC_RK_NEXT, true, 1300));
    CHECK(pc_remote_press(&p, PC_RK_PAUSE, true, 1310));    /* another key at once */
    CHECK(pc_remote_press(&p, PC_RK_NEXT, true, 1320));
    CHECK(!pc_remote_press(&p, PC_RK_NONE, true, 5000));    /* not a key of ours */
    pc_remote_press_t q = { 0 };
    CHECK(pc_remote_press(&q, PC_RK_PLAY, true, 0));         /* the first press, at time 0 */
    CHECK(!pc_remote_press(&q, PC_RK_PLAY, true, 10));
    CHECK(pc_remote_press(&q, PC_RK_PLAY, true, 0xFFFFFFF0u)); /* a long time later (wrapping arithmetic) */
}

/* the volume state: T is the clock, every call gets it */
static uint32_t T;
static uint8_t A;      /* the command handed out */
static int V;          /* a heard volume handed out */
static pc_rv_done_t ok_with(pc_rv_t *r, int ans, int cur) { return pc_rv_answer(r, true, (uint8_t)ans, cur, T, &A, &V); }
static pc_rv_done_t ok_(pc_rv_t *r) { return ok_with(r, r->sent, r->want); }          /* the headset answers the value in flight */
static pc_rv_done_t fail_(pc_rv_t *r, int cur) { return pc_rv_answer(r, false, 0, cur, T, &A, &V); }
static bool offer(pc_rv_t *r, int vol) { return pc_rv_offer(r, vol, T, &A); }
static bool heard(pc_rv_t *r, int abs) { return pc_rv_heard(r, (uint8_t)abs, T, &V); }
static bool back(pc_rv_t *r, int dev) { return pc_rv_heard_back(r, dev, T, &A); }   /* after HEARD: the device's value once clamped */

/* link up, caps with VOLUME_CHANGE, the first command answered: ON */
static void bring_on(pc_rv_t *r, int vol)
{
    T = 10000;
    pc_rv_link(r, true);
    CHECK_EQ_INT(r->st, PC_RV_ASKING);
    CHECK(pc_rv_caps(r, true, true, vol, T, &A));
    CHECK_EQ_INT(A, pc_remote_abs_from_vol(vol));
    CHECK_EQ_INT(r->st, PC_RV_TRYING);
    CHECK(!pc_rv_unity(r));                              /* not before the answer */
    CHECK_EQ_INT(ok_(r), PC_RV_BECAME_ON);
    CHECK(pc_rv_unity(r));
    CHECK_EQ_INT(r->ans, pc_remote_abs_from_vol(vol));
    T += 5000;                                           /* past the echo window */
}

static void test_activation_needs_support_setting_and_answer(void)
{
    pc_rv_t r = { 0 };
    T = 0;
    CHECK(!pc_rv_caps(&r, true, true, 40, T, &A));      /* no link */
    pc_rv_link(&r, true);
    CHECK(!pc_rv_caps(&r, false, true, 40, T, &A));     /* no VOLUME_CHANGE (or no caps in time) */
    CHECK_EQ_INT(r.st, PC_RV_NONE);
    CHECK(!offer(&r, 50));
    pc_rv_link(&r, true);
    CHECK(!pc_rv_caps(&r, true, false, 40, T, &A));     /* hp_absvol 0 */
    CHECK_EQ_INT(r.st, PC_RV_NONE);
    pc_rv_link(&r, true);
    CHECK(pc_rv_caps(&r, true, true, 40, T, &A));
    CHECK(!offer(&r, 60));                              /* not ON yet: software gain, nothing forwarded */
    CHECK(!heard(&r, 100));
    CHECK_EQ_INT(ok_(&r), PC_RV_BECAME_ON);
    CHECK(offer(&r, 60));                               /* the glue offers the last forwarded value at once: it moved meanwhile */
    CHECK_EQ_INT(A, pc_remote_abs_from_vol(60));
    CHECK(!pc_rv_caps(&r, true, true, 40, T, &A));      /* a second caps answer changes nothing */
    CHECK_EQ_INT(r.st, PC_RV_ON);
}

/* ruling A: the first answer louder than asked = no unity for this link */
static void test_activation_answer_louder_is_mismatch(void)
{
    pc_rv_t r = { 0 };
    T = 0;
    pc_rv_link(&r, true);
    CHECK(pc_rv_caps(&r, true, true, 30, T, &A));       /* sent 38 */
    CHECK_EQ_INT(A, 38);
    CHECK_EQ_INT(ok_with(&r, 38 + PC_RV_TOL + 1, 30), PC_RV_MISMATCH);   /* the headset stays at its own (louder) volume */
    CHECK(!pc_rv_unity(&r));
    CHECK_EQ_INT(r.st, PC_RV_NONE);
    CHECK_EQ_INT(r.ans, 38 + PC_RV_TOL + 1);
    CHECK(!r.busy);
    CHECK(!offer(&r, 50));
    pc_rv_link(&r, true);                                /* within the tolerance above: on */
    CHECK(pc_rv_caps(&r, true, true, 30, T, &A));
    CHECK_EQ_INT(ok_with(&r, 38 + PC_RV_TOL, 30), PC_RV_BECAME_ON);
    pc_rv_link(&r, true);                                /* quieter than asked: the safe side, on */
    CHECK(pc_rv_caps(&r, true, true, 30, T, &A));
    CHECK_EQ_INT(ok_with(&r, 0, 30), PC_RV_BECAME_ON);
    CHECK(pc_rv_unity(&r));
    CHECK_EQ_INT(r.ans, 0);
}

static void test_first_command_retries_then_gives_up(void)
{
    pc_rv_t r = { 0 };
    T = 0;
    pc_rv_link(&r, true);
    CHECK(pc_rv_caps(&r, true, true, 40, T, &A));
    uint8_t first = A;
    CHECK_EQ_INT(fail_(&r, 40), PC_RV_SEND);
    CHECK_EQ_INT(A, first);
    CHECK_EQ_INT(fail_(&r, 40), PC_RV_SEND);
    CHECK_EQ_INT(fail_(&r, 40), PC_RV_GAVE_UP);
    CHECK_EQ_INT(r.st, PC_RV_NONE);
    CHECK(!pc_rv_unity(&r));
    CHECK(!r.busy && !r.has_next);
}

/* ruling D: a resend of the first command carries the last value offered (an alarm ramp moved on meanwhile) */
static void test_first_resend_uses_the_last_offered_value(void)
{
    pc_rv_t r = { 0 };
    T = 0;
    pc_rv_link(&r, true);
    CHECK(pc_rv_caps(&r, true, true, 20, T, &A));       /* the ramp was at 20 */
    CHECK_EQ_INT(fail_(&r, 24), PC_RV_SEND);            /* now at 24 */
    CHECK_EQ_INT(A, pc_remote_abs_from_vol(24));
    CHECK_EQ_INT(r.want, 24);
    CHECK_EQ_INT(ok_(&r), PC_RV_BECAME_ON);
    CHECK(!offer(&r, 24));                              /* the headset has it */
}

static void test_one_in_flight_newest_wins(void)
{
    pc_rv_t r = { 0 };
    bring_on(&r, 40);
    CHECK(offer(&r, 42));
    CHECK_EQ_INT(A, pc_remote_abs_from_vol(42));
    CHECK(!offer(&r, 44));                              /* three while one is in flight */
    CHECK(!offer(&r, 46));
    CHECK(!offer(&r, 48));
    CHECK_EQ_INT(ok_with(&r, pc_remote_abs_from_vol(42), 48), PC_RV_SEND);
    CHECK_EQ_INT(A, pc_remote_abs_from_vol(48));        /* exactly the last one */
    CHECK_EQ_INT(ok_(&r), PC_RV_IDLE);
    CHECK(!r.busy && !r.has_next);
    CHECK_EQ_INT(ok_with(&r, 61, 48), PC_RV_IDLE);      /* a stale answer: nothing */
    CHECK_EQ_INT(fail_(&r, 48), PC_RV_IDLE);            /* a stale timeout: nothing, no failure counted */
    CHECK_EQ_INT(r.fails, 0);
    CHECK(!offer(&r, 48));                              /* the headset has it already */
}

/* ruling A (finding 2): an answer far from the value in flight is stale or not applied: the command stays in flight, the wait ends as a failure */
static void test_answer_far_from_sent_keeps_the_command(void)
{
    pc_rv_t r = { 0 };
    bring_on(&r, 60);
    CHECK(offer(&r, 20));                               /* lowering: sent 25 */
    CHECK_EQ_INT(A, 25);
    CHECK_EQ_INT(ok_with(&r, 76, 20), PC_RV_IDLE);      /* the late answer of an older command (60): not ours */
    CHECK(r.busy);
    CHECK_EQ_INT(r.ans, pc_remote_abs_from_vol(60));    /* not taken */
    CHECK_EQ_INT(ok_with(&r, 25 + PC_RV_TOL, 20), PC_RV_IDLE);   /* within the tolerance: the answer to 25 (a coarse headset) */
    CHECK(!r.busy);
    CHECK_EQ_INT(r.ans, 25 + PC_RV_TOL);
    CHECK_EQ_INT(r.far_n, 1);
    CHECK(offer(&r, 10));
    for (int i = 0; i < 3; i++) {                       /* a headset that answers but does not apply (ruling 7: reason=far) */
        CHECK_EQ_INT(ok_with(&r, 120, 10), PC_RV_IDLE);
        CHECK(r.busy);
        CHECK_EQ_INT(fail_(&r, 10), i < 2 ? PC_RV_SEND : PC_RV_GAVE_UP);
    }
    CHECK(!pc_rv_unity(&r));
    CHECK(r.far_seen);
    CHECK_EQ_INT(r.far_ans, 120);
    CHECK_EQ_INT(r.far_n, 4);
    bring_on(&r, 60);                                   /* real silence: not far */
    CHECK_EQ_INT(r.far_n, 0);
    CHECK(offer(&r, 20));
    for (int i = 0; i < 3; i++) fail_(&r, 20);
    CHECK(!pc_rv_unity(&r));
    CHECK(!r.far_seen);
}

static void test_back_to_the_value_in_flight(void)
{
    pc_rv_t r = { 0 };
    bring_on(&r, 40);
    CHECK(offer(&r, 50));
    CHECK(!offer(&r, 52));
    CHECK(!offer(&r, 50));                              /* turned back: the value in flight is the newest */
    CHECK_EQ_INT(ok_(&r), PC_RV_IDLE);
}

static void test_failures_count_to_three_in_a_row(void)
{
    pc_rv_t r = { 0 };
    bring_on(&r, 40);
    CHECK(offer(&r, 60));
    CHECK_EQ_INT(fail_(&r, 60), PC_RV_SEND);            /* no answer: sent again */
    CHECK_EQ_INT(A, pc_remote_abs_from_vol(60));
    CHECK_EQ_INT(fail_(&r, 60), PC_RV_SEND);
    CHECK_EQ_INT(ok_(&r), PC_RV_IDLE);                  /* answered: the count starts again */
    CHECK_EQ_INT(r.fails, 0);
    CHECK(offer(&r, 70));
    CHECK(!offer(&r, 72));
    CHECK_EQ_INT(fail_(&r, 72), PC_RV_SEND);            /* failed, but a newer value waits: that one goes */
    CHECK_EQ_INT(A, pc_remote_abs_from_vol(72));
    CHECK_EQ_INT(fail_(&r, 72), PC_RV_SEND);
    CHECK_EQ_INT(fail_(&r, 72), PC_RV_GAVE_UP);
    CHECK_EQ_INT(r.st, PC_RV_NONE);
    CHECK(!pc_rv_unity(&r));
    CHECK(!offer(&r, 80));
    CHECK_EQ_INT(ok_with(&r, 91, 80), PC_RV_IDLE);      /* a late answer after giving up: stays off */
    CHECK(!pc_rv_unity(&r));
}

static void test_headset_keys_move_the_device(void)
{
    pc_rv_t r = { 0 };
    bring_on(&r, 40);
    CHECK(!heard(&r, pc_remote_abs_from_vol(40)));      /* our own value echoed: nothing */
    CHECK(heard(&r, 90));
    CHECK_EQ_INT(V, pc_remote_vol_from_abs(90, 40));
    CHECK(!offer(&r, V));                               /* the device follows: nothing goes back */
    CHECK(heard(&r, 127));                              /* to 100, but the device clamps (volume_max 80) */
    CHECK_EQ_INT(V, 100);
    CHECK(offer(&r, 80));                               /* the clamped value goes back */
    CHECK_EQ_INT(A, pc_remote_abs_from_vol(80));
    CHECK_EQ_INT(ok_(&r), PC_RV_IDLE);
    CHECK(!heard(&r, pc_remote_abs_from_vol(80)));      /* the echo of that one */
}

/* ruling B: a report near our last command shortly after it is our echo (a coarse headset rounds) */
static void test_echo_window(void)
{
    pc_rv_t r = { 0 };
    bring_on(&r, 40);
    CHECK(offer(&r, 41));                               /* sent 52 */
    CHECK_EQ_INT(A, 52);
    T += 100;
    CHECK_EQ_INT(ok_(&r), PC_RV_IDLE);
    T += 300;
    CHECK(!heard(&r, 56));                              /* inside 1 s, within the tolerance: the headset's own step, no snap-back */
    CHECK_EQ_INT(r.want, 41);
    T += PC_RV_ECHO_MS;
    CHECK(heard(&r, 56));                               /* the same value after the window: a key press */
    CHECK_EQ_INT(V, pc_remote_vol_from_abs(56, 41));
    CHECK(!offer(&r, V));
    CHECK(offer(&r, 30));                               /* sent 38 */
    CHECK_EQ_INT(ok_(&r), PC_RV_IDLE);
    T += 200;
    CHECK(heard(&r, 38 + PC_RV_TOL + 1));               /* inside the window but further away: a key press */
    CHECK_EQ_INT(V, pc_remote_vol_from_abs(38 + PC_RV_TOL + 1, 30));
}

/* ruling C: a headset key during our command is kept and applied when the answer comes, unless the knob moved meanwhile */
static void test_key_during_command_is_kept(void)
{
    pc_rv_t r = { 0 };
    bring_on(&r, 40);
    CHECK(offer(&r, 42));
    CHECK(!heard(&r, 100));                             /* in flight: kept */
    CHECK(r.busy);
    T += 50;
    CHECK_EQ_INT(ok_(&r), PC_RV_HEARD);                 /* nothing newer from the device: the kept value is a heard one */
    CHECK_EQ_INT(V, pc_remote_vol_from_abs(100, 42));
    CHECK(!r.busy);                                     /* nothing sent until the device's clamp is known */
    CHECK(back(&r, V));
    CHECK_EQ_INT(A, 100);                               /* sent back as heard (raw), so both stand at it */
    CHECK(r.busy);
    CHECK(!offer(&r, V));                               /* the device follows: nothing more */
    CHECK_EQ_INT(ok_(&r), PC_RV_IDLE);

    CHECK(offer(&r, 50));
    CHECK(!heard(&r, 10));                              /* kept */
    CHECK(!offer(&r, 52));                              /* the knob moved meanwhile: it wins */
    CHECK_EQ_INT(ok_with(&r, pc_remote_abs_from_vol(50), 52), PC_RV_SEND);
    CHECK_EQ_INT(A, pc_remote_abs_from_vol(52));
    CHECK_EQ_INT(ok_(&r), PC_RV_IDLE);                  /* the kept value was dropped */
    CHECK_EQ_INT(r.want, 52);

    CHECK(offer(&r, 60));
    CHECK(!heard(&r, pc_remote_abs_from_vol(60) + 2));  /* the echo of the command in flight: dropped at once, not kept */
    CHECK(!r.has_kept);
    CHECK_EQ_INT(ok_(&r), PC_RV_IDLE);
    CHECK_EQ_INT(r.want, 60);
}

/* fix 2, N1: during a fast spin the headset's late echo of the previous command is dropped on arrival (busy) */
static void test_spin_late_echo_dropped_while_busy(void)
{
    pc_rv_t r = { 0 };
    bring_on(&r, 40);
    CHECK(offer(&r, 50));                               /* 64 in flight */
    CHECK(!offer(&r, 60));                              /* 76 queued */
    T += 50;
    CHECK_EQ_INT(ok_with(&r, 64, 60), PC_RV_SEND);
    CHECK_EQ_INT(A, 76);
    T += 20;
    CHECK(!heard(&r, 64));                              /* the headset's CHANGED(64), one LVGL cycle late */
    CHECK(!r.has_kept);
    T += 50;
    CHECK_EQ_INT(ok_(&r), PC_RV_IDLE);                  /* not HEARD: no snap-back */
    CHECK_EQ_INT(r.want, 60);
}

/* N1, the idle path: the same late echo after the second answer */
static void test_spin_late_echo_dropped_when_idle(void)
{
    pc_rv_t r = { 0 };
    bring_on(&r, 40);
    CHECK(offer(&r, 50));
    CHECK(!offer(&r, 60));
    T += 50;
    CHECK_EQ_INT(ok_with(&r, 64, 60), PC_RV_SEND);
    T += 50;
    CHECK_EQ_INT(ok_(&r), PC_RV_IDLE);
    T += 100;
    CHECK(!heard(&r, 64));                              /* far from 76 but within one step of 64, sent 200 ms ago */
    CHECK_EQ_INT(r.want, 60);
}

/* N1: a report equal to a value sent 1.5 s ago, not in flight, is a key */
static void test_old_sent_value_is_a_key(void)
{
    pc_rv_t r = { 0 };
    bring_on(&r, 40);
    CHECK(offer(&r, 50));                               /* 64 */
    CHECK_EQ_INT(ok_(&r), PC_RV_IDLE);
    CHECK(offer(&r, 60));                               /* 76 */
    CHECK_EQ_INT(ok_(&r), PC_RV_IDLE);
    T += 1500;
    CHECK(heard(&r, 64));
    CHECK_EQ_INT(V, 50);
}

/* N1: a far value inside the window is a key: kept while busy, applied at the answer */
static void test_far_value_in_window_is_a_key(void)
{
    pc_rv_t r = { 0 };
    bring_on(&r, 40);
    CHECK(offer(&r, 50));                               /* 64 */
    T += 10;
    CHECK(!heard(&r, 110));
    CHECK(r.has_kept);
    T += 10;
    CHECK_EQ_INT(ok_(&r), PC_RV_HEARD);
    CHECK(back(&r, V));
    CHECK_EQ_INT(A, 110);
    CHECK_EQ_INT(V, pc_remote_vol_from_abs(110, 50));
}

/* N1: nine sends in one second: the ring keeps the last eight (ASan: no out-of-bounds) */
static void test_sent_ring_overflow(void)
{
    pc_rv_t r = { 0 };
    bring_on(&r, 90);
    for (int i = 0; i < 9; i++) {                       /* abs 0, 13, 25, 38, 51, 64, 76, 89, 102 */
        CHECK(offer(&r, i * 10));
        T += 50;
        CHECK_EQ_INT(ok_(&r), PC_RV_IDLE);
    }
    CHECK(!heard(&r, 13));                              /* still remembered: an echo */
    CHECK(heard(&r, 0));                                /* the oldest is forgotten: a key */
    CHECK_EQ_INT(V, 0);
}

/* N2: the failure path that sends the queued device value drops the kept one */
static void test_failure_with_queued_value_drops_kept(void)
{
    pc_rv_t r = { 0 };
    bring_on(&r, 40);
    CHECK(offer(&r, 42));                               /* 53 */
    CHECK(!heard(&r, 100));                             /* kept */
    CHECK(!offer(&r, 30));                              /* the knob down: 38 queued */
    CHECK_EQ_INT(fail_(&r, 30), PC_RV_SEND);
    CHECK_EQ_INT(A, 38);
    CHECK(!r.has_kept);
    CHECK_EQ_INT(ok_(&r), PC_RV_IDLE);
    CHECK_EQ_INT(r.want, 30);
}

/* N3a: a kept value goes back raw (59, not abs(vol(59)) = 58); a clamp sends the clamped value after it */
static void test_kept_value_sent_back_raw(void)
{
    pc_rv_t r = { 0 };
    bring_on(&r, 20);                                   /* 25 */
    CHECK(offer(&r, 22));                               /* 28 */
    CHECK(!heard(&r, 59));
    CHECK_EQ_INT(ok_(&r), PC_RV_HEARD);
    CHECK(back(&r, V));                                 /* unclamped: one send, the raw value */
    CHECK_EQ_INT(A, 59);
    CHECK(pc_remote_abs_from_vol(V) != 59);             /* the case: rounding would have moved the headset */
    CHECK(!offer(&r, V));
    CHECK_EQ_INT(ok_(&r), PC_RV_IDLE);
}

/* fix 3, M1: a kept value the device clamps (volume_max 60) goes back once, as the clamped value only */
static void test_kept_value_clamped_sent_once(void)
{
    pc_rv_t r = { 0 };
    bring_on(&r, 20);
    CHECK(offer(&r, 22));
    CHECK(!heard(&r, 100));
    CHECK_EQ_INT(ok_(&r), PC_RV_HEARD);
    CHECK(V > 60);
    CHECK(!r.busy);
    uint8_t n0 = r.sent_i;
    CHECK(back(&r, 60));                                /* the device stands at 60 */
    CHECK_EQ_INT(A, pc_remote_abs_from_vol(60));
    CHECK(!offer(&r, 60));                              /* the glue's offer of the clamped value: already sent */
    CHECK_EQ_INT(ok_(&r), PC_RV_IDLE);                  /* nothing queued behind it */
    CHECK_EQ_INT((r.sent_i + PC_RV_SENT_N - n0) % PC_RV_SENT_N, 1);   /* exactly one send */
    CHECK_EQ_INT(r.want, 60);
}

/* fix 3, M3a: an echo of the value in flight older than the window (no answer yet) is still dropped */
static void test_echo_of_old_value_in_flight_dropped(void)
{
    pc_rv_t r = { 0 };
    bring_on(&r, 40);
    CHECK(offer(&r, 50));                               /* 64 in flight */
    T += 1500;
    CHECK(!heard(&r, 66));
    CHECK(!r.has_kept);
}

/* fix 3, M3b: the answer refreshes the time of the value in flight: 0.9 s after the answer, 1.5 s after the send, still an echo */
static void test_answer_refreshes_echo_window(void)
{
    pc_rv_t r = { 0 };
    bring_on(&r, 40);
    CHECK(offer(&r, 50));                               /* 64 */
    T += 600;
    CHECK_EQ_INT(ok_with(&r, 66, 50), PC_RV_IDLE);      /* answered 66 (a coarse headset) */
    T += 900;
    CHECK(!heard(&r, 66));                              /* the answered value reported: our echo */
    CHECK_EQ_INT(r.want, 50);
}

/* N3b: during activation a late answer to an earlier copy is ignored, not a mismatch */
static void test_activation_stale_answer_ignored(void)
{
    pc_rv_t r = { 0 };
    T = 0;
    pc_rv_link(&r, true);
    CHECK(pc_rv_caps(&r, true, true, 60, T, &A));       /* 76 */
    CHECK_EQ_INT(fail_(&r, 20), PC_RV_SEND);            /* the volume dropped: 25 */
    CHECK_EQ_INT(A, 25);
    CHECK_EQ_INT(ok_with(&r, 76, 20), PC_RV_IDLE);      /* the late answer to 76 */
    CHECK(r.busy);
    CHECK_EQ_INT(r.st, PC_RV_TRYING);
    CHECK(!pc_rv_unity(&r));
    CHECK_EQ_INT(ok_with(&r, 25, 20), PC_RV_BECAME_ON);

    pc_rv_link(&r, true);                               /* a headset that always answers 76: never on */
    CHECK(pc_rv_caps(&r, true, true, 60, T, &A));
    CHECK_EQ_INT(fail_(&r, 20), PC_RV_SEND);
    for (int i = 0; i < 2; i++) {
        CHECK_EQ_INT(ok_with(&r, 76, 20), PC_RV_IDLE);
        CHECK(!pc_rv_unity(&r));
        CHECK_EQ_INT(fail_(&r, 20), i == 0 ? PC_RV_SEND : PC_RV_GAVE_UP);
    }
    CHECK(!pc_rv_unity(&r));
    CHECK_EQ_INT(r.st, PC_RV_NONE);
    CHECK(r.far_seen);

    pc_rv_link(&r, true);                               /* the first command answered 100: nothing of ours, mismatch as before */
    CHECK(pc_rv_caps(&r, true, true, 60, T, &A));
    CHECK_EQ_INT(ok_with(&r, 100, 60), PC_RV_MISMATCH);
}


static void test_link_down_and_leave(void)
{
    pc_rv_t r = { 0 };
    bring_on(&r, 40);
    CHECK(offer(&r, 60));
    pc_rv_link(&r, false);
    CHECK_EQ_INT(r.st, PC_RV_OFF);
    CHECK(!pc_rv_unity(&r) && !r.busy && !r.has_next && r.fails == 0);
    CHECK_EQ_INT(ok_with(&r, 76, 60), PC_RV_IDLE);
    bring_on(&r, 40);
    pc_rv_leave(&r);
    CHECK_EQ_INT(r.st, PC_RV_NONE);
    CHECK(!pc_rv_unity(&r) && !r.busy);
}

/* ruling I: the per-block gain decision of the audio task, row by row (review: the safety table) */
static void test_block_gain(void)
{
    const float dev = 0.3f;
    for (int sink = 0; sink < 2; sink++)
        for (int rem = 0; rem < 2; rem++)
            for (int prev = 0; prev < 2; prev++)
                for (int pa = 0; pa < 2; pa++)
                    for (int g = 0; g < 3; g++) {
                        float gain = g == 0 ? 0.0f : g == 1 ? dev : 1.0f;
                        if (prev && !(sink && rem) && gain != 1.0f) continue;   /* after a unity block the carried gain is 1.0 (or the fade of it) */
                        pc_rv_block_t b = pc_rv_block(sink, rem, pa, prev, gain, dev);
                        CHECK_EQ_INT(b.unity, sink && rem);
                        if (!sink) {
                            CHECK(b.target <= dev);                     /* no sink: never 1.0 from the remote flag */
                            if (prev) CHECK(b.start <= dev);            /* and never a ramp from full scale after a unity block */
                        }
                        if (pa) CHECK_EQ_INT(b.target == 0.0f, 1);
                        else CHECK_NEAR(b.target, sink && rem ? 1.0f : dev, 1e-9);
                        if (!(prev && !sink)) CHECK_NEAR(b.start, gain, 1e-9);   /* otherwise the ramp starts where the last block ended */
                    }
    /* the rows of the table */
    pc_rv_block_t b = pc_rv_block(false, true, false, true, 1.0f, dev);   /* unity block, then the sink goes: I2S from silence at the device gain */
    CHECK_NEAR(b.start, 0.0f, 1e-9);
    CHECK_NEAR(b.target, dev, 1e-9);
    CHECK(!b.unity);
    b = pc_rv_block(false, true, false, false, dev, dev);                 /* a stale remote flag without a sink: no effect */
    CHECK_NEAR(b.target, dev, 1e-9);
    b = pc_rv_block(true, false, false, false, dev, dev);                 /* a sink without remote: the device gain */
    CHECK_NEAR(b.target, dev, 1e-9);
    b = pc_rv_block(true, true, false, false, dev, dev);                  /* sink + remote: unity, ramping up from the device gain */
    CHECK_NEAR(b.target, 1.0f, 1e-9);
    CHECK_NEAR(b.start, dev, 1e-9);
    CHECK(b.unity);
    b = pc_rv_block(true, false, false, true, 1.0f, dev);                 /* remote ends, the sink stays: down to the device gain inside the sink */
    CHECK_NEAR(b.start, 1.0f, 1e-9);
    CHECK_NEAR(b.target, dev, 1e-9);
    CHECK(!b.unity);
}

int main(void)
{
    RUN(test_abs_ends_and_clamp);
    RUN(test_round_trip_every_volume);
    RUN(test_monotonic);
    RUN(test_key_codes);
    RUN(test_actions_every_state);
    RUN(test_press_filter);
    RUN(test_activation_needs_support_setting_and_answer);
    RUN(test_activation_answer_louder_is_mismatch);
    RUN(test_first_command_retries_then_gives_up);
    RUN(test_first_resend_uses_the_last_offered_value);
    RUN(test_one_in_flight_newest_wins);
    RUN(test_answer_far_from_sent_keeps_the_command);
    RUN(test_back_to_the_value_in_flight);
    RUN(test_failures_count_to_three_in_a_row);
    RUN(test_headset_keys_move_the_device);
    RUN(test_echo_window);
    RUN(test_key_during_command_is_kept);
    RUN(test_spin_late_echo_dropped_while_busy);
    RUN(test_spin_late_echo_dropped_when_idle);
    RUN(test_old_sent_value_is_a_key);
    RUN(test_far_value_in_window_is_a_key);
    RUN(test_sent_ring_overflow);
    RUN(test_failure_with_queued_value_drops_kept);
    RUN(test_kept_value_sent_back_raw);
    RUN(test_kept_value_clamped_sent_once);
    RUN(test_echo_of_old_value_in_flight_dropped);
    RUN(test_answer_refreshes_echo_window);
    RUN(test_activation_stale_answer_ignored);
    RUN(test_link_down_and_leave);
    RUN(test_block_gain);
    return 0;
}
