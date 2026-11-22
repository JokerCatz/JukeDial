// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_extuac.h"
#include "pc_test.h"

static pc_extuac_t u;
static const uint32_t R[] = { 44100, 48000, 96000 };
static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static int rd16s(const uint8_t *p) { return (int16_t)(p[0] | p[1] << 8); }

static void test_clock(void)
{
    uint8_t b[64];
    pc_extuac_init(&u, R, 3);
    CHECK_EQ_INT(pc_extuac_get(&u, 1, 1, 0, 1, b, sizeof b), 4);
    CHECK_EQ_INT(rd32(b), 48000);                                   /* 48 k when advertised */
    CHECK_EQ_INT(pc_extuac_get(&u, 1, 1, 0, 2, b, sizeof b), 2 + 36);
    CHECK_EQ_INT(b[0], 3); CHECK_EQ_INT(b[1], 0);
    CHECK_EQ_INT(rd32(b + 2), 44100); CHECK_EQ_INT(rd32(b + 6), 44100); CHECK_EQ_INT(rd32(b + 10), 0);
    CHECK_EQ_INT(rd32(b + 14 + 12), 96000);                         /* the third point */
    CHECK_EQ_INT(pc_extuac_get(&u, 1, 2, 0, 1, b, sizeof b), 1); CHECK_EQ_INT(b[0], 1);
    CHECK_EQ_INT(pc_extuac_get(&u, 1, 1, 0, 2, b, 10), -1);        /* a buffer too small: refused, never cut */
    CHECK_EQ_INT(pc_extuac_get(&u, 1, 9, 0, 1, b, sizeof b), -1);
    const uint8_t f96[4] = { 0x00, 0x77, 0x01, 0x00 };
    CHECK(pc_extuac_set(&u, 1, 1, 0, f96, 4)); CHECK_EQ_INT(u.freq, 96000);
    const uint8_t f192[4] = { 0x00, 0xEE, 0x02, 0x00 };
    CHECK(!pc_extuac_set(&u, 1, 1, 0, f192, 4)); CHECK_EQ_INT(u.freq, 96000);   /* not advertised: refused, unchanged */
    CHECK(!pc_extuac_set(&u, 1, 1, 0, f96, 3));
}
static void test_range_8_rates(void)
{
    static const uint32_t R8[8] = { 44100, 48000, 88200, 96000, 176400, 192000, 352800, 384000 };
    uint8_t b[128];
    pc_extuac_init(&u, R8, 8);
    int n = pc_extuac_get(&u, 1, 1, 0, 2, b, sizeof b);
    CHECK_EQ_INT(n, 2 + 12 * 8);                                    /* 98 bytes: more than the old 64 byte buffer, within the 128 of the firmware */
    CHECK_EQ_INT(b[0], 8);
    CHECK_EQ_INT(rd32(b + 2 + 12 * 7), 384000); CHECK_EQ_INT(rd32(b + 6 + 12 * 7), 384000);
    CHECK_EQ_INT(pc_extuac_get(&u, 1, 1, 0, 2, b, 64), -1);        /* what the 64 byte buffer could not carry */
    CHECK(n <= 128);
}
static void test_fu(void)
{
    uint8_t b[16];
    pc_extuac_init(&u, R, 3);
    CHECK_EQ_INT(pc_extuac_get(&u, 3, 2, 0, 2, b, sizeof b), 8);
    CHECK_EQ_INT(b[0], 1); CHECK_EQ_INT(rd16s(b + 2), -90 * 256); CHECK_EQ_INT(rd16s(b + 4), 0); CHECK_EQ_INT(rd16s(b + 6), 256);   /* the range ends at 0 dB */
    const uint8_t up[2] = { 0x00, 0x05 };                           /* +5 dB asked */
    CHECK(pc_extuac_set(&u, 3, 2, 0, up, 2));
    CHECK_EQ_INT(pc_extuac_get(&u, 3, 2, 0, 1, b, sizeof b), 2); CHECK_EQ_INT(rd16s(b), 0);   /* clamped to 0 dB */
    const uint8_t low[2] = { 0x00, 0x80 };                          /* -128 dB asked */
    CHECK(pc_extuac_set(&u, 3, 2, 1, low, 2));
    pc_extuac_get(&u, 3, 2, 1, 1, b, sizeof b); CHECK_EQ_INT(rd16s(b), -90 * 256);
    CHECK(pc_extuac_get(&u, 3, 2, 3, 1, b, sizeof b) < 0);          /* channel 3 does not exist */
    const uint8_t m1[1] = { 1 };
    CHECK(pc_extuac_set(&u, 3, 1, 0, m1, 1));
    pc_extuac_get(&u, 3, 1, 0, 1, b, sizeof b); CHECK_EQ_INT(b[0], 1);
}
static void test_gain(void)
{
    pc_extuac_init(&u, R, 3);
    CHECK_NEAR(pc_extuac_gain(&u, 0), 1.0, 1e-6);                   /* 0 dB = 1.0, never more */
    const uint8_t m6[2] = { 0x00, 0xFA };                           /* -6 dB (-1536 = 0xFA00) */
    pc_extuac_set(&u, 3, 2, 0, m6, 2);
    CHECK_NEAR(pc_extuac_gain(&u, 0), 0.5012, 0.001);
    pc_extuac_set(&u, 3, 2, 1, m6, 2);
    CHECK_NEAR(pc_extuac_gain(&u, 0), 0.2512, 0.001);               /* master and channel add in dB */
    CHECK_NEAR(pc_extuac_gain(&u, 1), 0.5012, 0.001);
    const uint8_t mu[1] = { 1 };
    pc_extuac_set(&u, 3, 1, 2, mu, 1);
    CHECK_NEAR(pc_extuac_gain(&u, 1), 0.0, 1e-9); CHECK(pc_extuac_gain(&u, 0) > 0.1);
    for (int i = 0; i < 64; i++) {                                  /* whatever the host sends, the gain stays <= 1 */
        uint8_t v[2] = { (uint8_t)(i * 37), (uint8_t)(i * 91) };
        pc_extuac_set(&u, 3, 2, (uint8_t)(i % 3), v, 2);
        CHECK(pc_extuac_gain(&u, 0) <= 1.0f && pc_extuac_gain(&u, 1) <= 1.0f);
    }
    CHECK_NEAR(pc_extuac_gain(&u, 5), 0.0, 1e-9);
}
int main(void) { RUN(test_clock); RUN(test_range_8_rates); RUN(test_fu); RUN(test_gain); return 0; }
