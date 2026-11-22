// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_ring.h"
#include "pc_test.h"

static void test_wraps(void)
{
    uint8_t mem[10], out[10];
    pc_ring_t g;
    pc_ring_init(&g, mem, sizeof mem);
    CHECK_EQ_INT(pc_ring_write(&g, "abcdefgh", 8), 8);
    CHECK_EQ_INT(pc_ring_read(&g, out, 6), 6);
    CHECK_EQ_INT(pc_ring_write(&g, "ijklmnop", 8), 8);   /* crosses the end */
    CHECK_EQ_INT(pc_ring_used(&g), 10);
    CHECK_EQ_INT(pc_ring_free(&g), 0);
    CHECK_EQ_INT(pc_ring_write(&g, "z", 1), 0);           /* full */
    CHECK_EQ_INT(pc_ring_read(&g, out, 10), 10);
    CHECK(!memcmp(out, "ghijklmnop", 10));
    CHECK_EQ_INT(pc_ring_read(&g, out, 1), 0);            /* empty */
}

static void test_partial(void)
{
    uint8_t mem[4], out[8];
    pc_ring_t g;
    pc_ring_init(&g, mem, sizeof mem);
    CHECK_EQ_INT(pc_ring_write(&g, "123456", 6), 4);
    CHECK_EQ_INT(pc_ring_read(&g, out, 8), 4);
    CHECK(!memcmp(out, "1234", 4));
}

static void test_counters_wrap_around_32_bits(void)
{
    uint8_t mem[8], out[8];
    pc_ring_t g;
    pc_ring_init(&g, mem, sizeof mem);
    g.w = g.r = 0xFFFFFFFCu;                              /* after ~4 GB of stream */
    CHECK_EQ_INT(pc_ring_write(&g, "abcdef", 6), 6);
    CHECK_EQ_INT(pc_ring_used(&g), 6);
    CHECK_EQ_INT(pc_ring_read(&g, out, 6), 6);
    CHECK(!memcmp(out, "abcdef", 6));
}

static void test_peek_leaves_bytes(void)
{
    uint8_t mem[8], out[8];
    pc_ring_t g;
    pc_ring_init(&g, mem, sizeof mem);
    pc_ring_write(&g, "abcdef", 6);
    pc_ring_read(&g, out, 4);
    pc_ring_write(&g, "ghij", 4);                      /* wraps */
    CHECK_EQ_INT(pc_ring_peek(&g, out, 8), 6);
    CHECK(!memcmp(out, "efghij", 6));
    CHECK_EQ_INT(pc_ring_used(&g), 6);
}

int main(void)
{
    RUN(test_peek_leaves_bytes);
    RUN(test_wraps);
    RUN(test_partial);
    RUN(test_counters_wrap_around_32_bits);
    return 0;
}
