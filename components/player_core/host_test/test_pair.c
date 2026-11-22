// SPDX-License-Identifier: LGPL-2.1-or-later
#include <string.h>
#include "pc/pc_pair.h"
#include "pc_test.h"

static const uint8_t A[6] = { 1, 2, 3, 4, 5, 6 }, B[6] = { 9, 9, 9, 9, 9, 9 };

static void test_phone_request_prompts(void)
{
    pc_pair_t p = { 0 };
    CHECK_EQ_INT(pc_pair_request(&p, A, 123456, false, 1000), PC_PAIR_PROMPT);
    CHECK(p.asking);
    CHECK_EQ_INT(p.num, 123456);
}

static void test_headphones_we_started_accepts(void)
{
    pc_pair_t p = { 0 };
    CHECK_EQ_INT(pc_pair_request(&p, A, 0, true, 1000), PC_PAIR_ACCEPT);
    CHECK(!p.asking);
}

static void test_second_phone_while_asking_rejected(void)
{
    pc_pair_t p = { 0 };
    pc_pair_request(&p, A, 111111, false, 1000);
    CHECK_EQ_INT(pc_pair_request(&p, B, 222222, false, 2000), PC_PAIR_REJECT);
    CHECK_EQ_INT(p.num, 111111);                       /* the open prompt is untouched */
}

static void test_expired_request_does_not_block_other_phone(void)
{
    pc_pair_t p = { 0 };
    pc_pair_request(&p, A, 111111, false, 1000);
    CHECK_EQ_INT(pc_pair_request(&p, B, 222222, false, 1000 + PC_PAIR_TIMEOUT_MS - 1), PC_PAIR_REJECT);
    CHECK_EQ_INT(pc_pair_request(&p, B, 222222, false, 1000 + PC_PAIR_TIMEOUT_MS), PC_PAIR_PROMPT);   /* nobody polled pc_pair_expired */
    CHECK_EQ_INT(p.num, 222222);
    CHECK(!memcmp(p.bda, B, 6));
}

static void test_same_phone_again_refreshes(void)
{
    pc_pair_t p = { 0 };
    pc_pair_request(&p, A, 111111, false, 1000);
    CHECK_EQ_INT(pc_pair_request(&p, A, 333333, false, 5000), PC_PAIR_PROMPT);
    CHECK_EQ_INT(p.num, 333333);
    CHECK(!pc_pair_expired(&p, 5000 + PC_PAIR_TIMEOUT_MS - 1));
}

static void test_timeout_fires_once(void)
{
    pc_pair_t p = { 0 };
    uint8_t out[6];
    pc_pair_request(&p, A, 1, false, 1000);
    CHECK(!pc_pair_expired(&p, 1000 + PC_PAIR_TIMEOUT_MS - 1));
    CHECK(pc_pair_expired(&p, 1000 + PC_PAIR_TIMEOUT_MS));
    CHECK(!p.asking);
    CHECK(!pc_pair_expired(&p, 1000 + 2 * PC_PAIR_TIMEOUT_MS));
    CHECK(!pc_pair_answer(&p, out));                   /* a late tap answers nothing */
}

static void test_timeout_across_tick_wrap(void)
{
    pc_pair_t p = { 0 };
    pc_pair_request(&p, A, 1, false, 0xFFFFF000u);
    CHECK(!pc_pair_expired(&p, 0x00000010u));          /* 4 s later, counter wrapped */
    CHECK(pc_pair_expired(&p, 0xFFFFF000u + PC_PAIR_TIMEOUT_MS));
}

static void test_answer_returns_address_and_closes(void)
{
    pc_pair_t p = { 0 };
    uint8_t out[6] = { 0 };
    pc_pair_request(&p, A, 42, false, 0);
    CHECK(pc_pair_answer(&p, out));
    CHECK(!memcmp(out, A, 6));
    CHECK(!p.asking);
    CHECK(!pc_pair_answer(&p, out));
    CHECK_EQ_INT(pc_pair_request(&p, B, 7, false, 10), PC_PAIR_PROMPT);   /* free for the next phone */
}

static void test_digits_keep_leading_zeros(void)
{
    char d[7];
    pc_pair_digits(42, d);
    CHECK_STR(d, "000042");
    pc_pair_digits(999999, d);
    CHECK_STR(d, "999999");
    pc_pair_digits(1234567, d);                        /* out of range: last six digits */
    CHECK_STR(d, "234567");
}

int main(void)
{
    RUN(test_phone_request_prompts);
    RUN(test_headphones_we_started_accepts);
    RUN(test_second_phone_while_asking_rejected);
    RUN(test_expired_request_does_not_block_other_phone);
    RUN(test_same_phone_again_refreshes);
    RUN(test_timeout_fires_once);
    RUN(test_timeout_across_tick_wrap);
    RUN(test_answer_returns_address_and_closes);
    RUN(test_digits_keep_leading_zeros);
    return 0;
}
