// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_bleadv.h"
#include "pc_test.h"

static pc_bleadv_t a;
static void test_basic(void)
{
    pc_bleadv_init(&a);
    CHECK_EQ_INT(pc_bleadv_next(&a), PC_ADV_NONE);
    pc_bleadv_want(&a, true);
    CHECK_EQ_INT(pc_bleadv_next(&a), PC_ADV_START);
    CHECK_EQ_INT(pc_bleadv_next(&a), PC_ADV_NONE);            /* a second start before the first completed: never sent */
    pc_bleadv_started(&a, true);
    CHECK(a.adv);
    CHECK_EQ_INT(pc_bleadv_next(&a), PC_ADV_NONE);
    pc_bleadv_want(&a, false);
    CHECK_EQ_INT(pc_bleadv_next(&a), PC_ADV_STOP);
    CHECK_EQ_INT(pc_bleadv_next(&a), PC_ADV_NONE);
    pc_bleadv_stopped(&a);
    CHECK(!a.adv);
}
static void test_want_changes_while_pending(void)
{
    pc_bleadv_init(&a);
    pc_bleadv_want(&a, true);
    CHECK_EQ_INT(pc_bleadv_next(&a), PC_ADV_START);
    pc_bleadv_want(&a, false);                                /* back to USB before the start completed */
    CHECK_EQ_INT(pc_bleadv_next(&a), PC_ADV_NONE);            /* nothing while one is outstanding */
    pc_bleadv_started(&a, true);
    CHECK_EQ_INT(pc_bleadv_next(&a), PC_ADV_STOP);            /* then it is stopped: the advertising never stays on */
    pc_bleadv_stopped(&a);
    CHECK_EQ_INT(pc_bleadv_next(&a), PC_ADV_NONE);
}
static void test_error_does_not_clear(void)
{
    pc_bleadv_init(&a);
    pc_bleadv_want(&a, true);
    pc_bleadv_next(&a); pc_bleadv_started(&a, true);
    a.want = true;
    pc_bleadv_next(&a);                                       /* nothing to send */
    pc_bleadv_started(&a, false);                             /* a stray error answer ("already advertising") */
    CHECK(a.adv);                                             /* the flag stays: the next stop is sent */
    pc_bleadv_want(&a, false);
    CHECK_EQ_INT(pc_bleadv_next(&a), PC_ADV_STOP);
    pc_bleadv_stopped(&a);
    pc_bleadv_want(&a, true);
    CHECK_EQ_INT(pc_bleadv_next(&a), PC_ADV_START);
    pc_bleadv_started(&a, false);                             /* a real failure from off: stays off, tried again */
    CHECK(!a.adv);
    CHECK_EQ_INT(pc_bleadv_next(&a), PC_ADV_START);
}
static void test_connection(void)
{
    pc_bleadv_init(&a);
    pc_bleadv_want(&a, true);
    pc_bleadv_next(&a); pc_bleadv_started(&a, true);
    pc_bleadv_conn(&a, true);                                 /* a computer connected: the set ended */
    CHECK(!a.adv);
    CHECK_EQ_INT(pc_bleadv_next(&a), PC_ADV_NONE);
    pc_bleadv_conn(&a, false);                                /* it left: advertise again while wanted */
    CHECK_EQ_INT(pc_bleadv_next(&a), PC_ADV_START);
    pc_bleadv_started(&a, true);
    pc_bleadv_want(&a, false);
    pc_bleadv_conn(&a, true);                                 /* connected while stopping */
    CHECK_EQ_INT(pc_bleadv_next(&a), PC_ADV_NONE);
}
int main(void) { RUN(test_basic); RUN(test_want_changes_while_pending); RUN(test_error_does_not_clear); RUN(test_connection); return 0; }
