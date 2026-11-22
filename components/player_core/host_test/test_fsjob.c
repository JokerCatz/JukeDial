// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_fsjob.h"
#include "pc_test.h"

/* flash_safe_call's job states: normal finish, timeout while it still runs (the worker frees), abandoned before it began (fn never runs) */
static int s_ran;
static void fn(void *a) { s_ran += (int)(long)a; }

static void test_normal(void)
{
    pc_fsjob_t j; s_ran = 0;
    pc_fsjob_init(&j, fn, (void *)1);
    CHECK(!pc_fsjob_run(&j));            /* the caller is still waiting: the worker does not free */
    CHECK_EQ_INT(s_ran, 1);
    CHECK(pc_fsjob_collect(&j));         /* finished in time: the caller frees */
}
static void test_timeout_while_running(void)
{
    pc_fsjob_t j; s_ran = 0;
    pc_fsjob_init(&j, fn, (void *)1);
    j.state = PC_FSJ_RUNNING;            /* the worker is inside fn when the wait ends */
    CHECK(!pc_fsjob_collect(&j));        /* timed out: the caller leaves it */
    CHECK_EQ_INT(j.state, PC_FSJ_ABANDONED);
    CHECK(__atomic_exchange_n(&j.state, PC_FSJ_DONE, __ATOMIC_ACQ_REL) == PC_FSJ_ABANDONED);   /* what pc_fsjob_run's end does: it learns it owns the free */
}
static void test_timeout_before_start(void)
{
    pc_fsjob_t j; s_ran = 0;
    pc_fsjob_init(&j, fn, (void *)1);
    CHECK(!pc_fsjob_collect(&j));        /* the worker never got the CPU in time */
    CHECK(pc_fsjob_run(&j));             /* it frees and does not write at all */
    CHECK_EQ_INT(s_ran, 0);
}
static void test_late_finish(void)
{
    pc_fsjob_t j; s_ran = 0;
    pc_fsjob_init(&j, fn, (void *)2);
    CHECK(!pc_fsjob_collect(&j) && !s_ran);
    pc_fsjob_t k;
    pc_fsjob_init(&k, fn, (void *)2);
    CHECK(!pc_fsjob_run(&k)); CHECK(pc_fsjob_collect(&k)); CHECK_EQ_INT(s_ran, 2);
    CHECK(pc_fsjob_collect(&k));         /* DONE stays DONE */
}

int main(void)
{
    RUN(test_normal);
    RUN(test_timeout_while_running);
    RUN(test_timeout_before_start);
    RUN(test_late_finish);
    return 0;
}
