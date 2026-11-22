// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_voice.h"
#include "pc_test.h"

static void short_is(const char *full, int idx, const char *want)
{
    char o[4];
    pc_voice_short(full, idx, o);
    CHECK_STR(o, want);
}

static void test_short_names(void)
{
    short_is("Square 1", 0, "S1"); short_is("Pulse 2", 1, "P2"); short_is("Triangle", 2, "T"); short_is("Noise", 3, "N");
    short_is("DMC", 4, "D"); short_is("Wave 3", 2, "W3"); short_is("FM 6", 5, "F6"); short_is("PCM", 6, "P"); short_is("Beeper", 3, "B");
    short_is("square 1", 0, "S1");               /* lower case input */
}

static void test_short_empty_is_index(void)
{
    short_is(NULL, 0, "1"); short_is("", 8, "9"); short_is(NULL, 15, "16"); short_is("  ", 9, "10");
}

static void test_short_long_and_digits(void)
{
    short_is("A very long voice name indeed", 0, "A");
    short_is("A very long voice name 12", 0, "A12");
    short_is("Channel 123", 0, "C23");           /* at most two trailing digits */
    short_is("12", 0, "12");                     /* only digits: as they are */
    short_is("12345", 0, "123");
    char o[4] = { 'x', 'x', 'x', 'x' };
    pc_voice_short("Seventeen characters!", 0, o);
    CHECK_EQ_INT(strlen(o), 1);
}

static void test_decay(void)
{
    CHECK_EQ_INT(pc_voice_decay(0, 200), 200);   /* rises at once */
    CHECK_EQ_INT(pc_voice_decay(100, 255), 255);
    CHECK_EQ_INT(pc_voice_decay(200, 0), 149);   /* 200 - 50 - 1 */
    CHECK_EQ_INT(pc_voice_decay(100, 80), 80 > 74 ? 80 : 74);
    CHECK_EQ_INT(pc_voice_decay(0, 0), 0);       /* never wraps below 0 */
    CHECK_EQ_INT(pc_voice_decay(1, 0), 0);
    CHECK_EQ_INT(pc_voice_decay(255, 0), 191);
    int steps = 0;
    for (uint8_t v = 255; v; steps++) { uint8_t nx = pc_voice_decay(v, 0); CHECK(nx < v); v = nx; }
    CHECK(steps > 5 && steps < 40);              /* bounded fall */
    for (int h = 0; h < 256; h++) for (int n = 0; n < 256; n++) {
        int d = pc_voice_decay((uint8_t)h, (uint8_t)n);
        CHECK(d >= n && d <= (h > n ? h : n));
    }
}

static void test_struct_size(void) { CHECK_EQ_INT(sizeof(pc_voices_t), 322); }

int main(void)
{
    RUN(test_short_names);
    RUN(test_short_empty_is_index);
    RUN(test_short_long_and_digits);
    RUN(test_decay);
    RUN(test_struct_size);
    return 0;
}
