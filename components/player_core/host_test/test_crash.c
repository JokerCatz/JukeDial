// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_crash.h"
#include "pc_test.h"

static pc_crash_t full(void)
{
    pc_crash_t c;
    memset(&c, 0, sizeof c);
    c.magic = PC_CRASH_MAGIC; c.wall = 1794400496u; c.mode = PC_CRASH_MODE_NORMAL; c.uptime_s = 123;
    strcpy(c.screen, "tools_dice"); c.bt_mode = 2; c.src = 1; c.internal_min = 41234;
    c.pc = 0x40123456u; c.ra = 0x40abcdefu; c.cause = 7; c.has_pc = 1;
    return c;
}

static void test_full_line(void)
{
    pc_crash_t c = full();
    char out[256];
    int n = pc_crash_format(&c, 4, "16dd4ee", out, sizeof out);
    const char *want = "2026-11-11T12:34:56Z fw=16dd4ee reason=4 mode=normal up=123 screen=tools_dice bt=2 src=1 imin=41234 pc=0x40123456 ra=0x40abcdef cause=0x7\n";
    CHECK_STR(out, want);
    CHECK_EQ_INT(n, strlen(want));
}

static void test_no_time_no_pc(void)
{
    pc_crash_t c = full();
    char out[256];
    c.wall = 0; c.has_pc = 0; memset(c.screen, 0, sizeof c.screen);
    CHECK(pc_crash_format(&c, 1, "x", out, sizeof out) > 0);
    CHECK(strncmp(out, "- fw=", 5) == 0);
    CHECK(strstr(out, "screen=- ") != NULL);
    CHECK(strstr(out, "pc=- ra=- cause=-") != NULL);
}

static void test_screen_unsafe(void)
{
    pc_crash_t c = full();
    char out[256];
    memset(c.screen, 0xff, sizeof c.screen);
    c.mode = 200;
    CHECK(pc_crash_format(&c, 0, "x", out, sizeof out) > 0);
    for (const char *p = out; *p; p++) CHECK((unsigned char)*p < 0x80);
    CHECK(strstr(out, "mode=unknown") != NULL);
    memset(c.screen, 'a', sizeof c.screen);   /* 16 chars, no terminator */
    CHECK(pc_crash_format(&c, 0, "x", out, sizeof out) > 0);
    CHECK(strstr(out, "screen=aaaaaaaaaaaaaaaa bt=") != NULL);
}

static void test_cap(void)
{
    pc_crash_t c = full();
    char out[256], small[256];
    int n = pc_crash_format(&c, 4, "16dd4ee", out, sizeof out);
    CHECK(n > 0);
    CHECK_EQ_INT(pc_crash_format(&c, 4, "16dd4ee", small, (size_t)n), 0);
    CHECK(small[0] == '\0');
    CHECK_EQ_INT(pc_crash_format(&c, 4, "16dd4ee", small, (size_t)n + 1), n);
}

static void test_tail(void)
{
    CHECK_EQ_INT(pc_crash_tail("a\nbb\nccc\n", 9, 5), 5);
    CHECK_EQ_INT(pc_crash_tail("a\nbb\nccc\n", 9, 9), 0);
    CHECK_EQ_INT(pc_crash_tail("a\nbb\nccc\n", 9, 100), 0);
    CHECK_EQ_INT(pc_crash_tail("xxxxxxxx", 8, 4), 4);
    CHECK_EQ_INT(pc_crash_tail("", 0, 4), 0);
    CHECK_EQ_INT(pc_crash_tail("a\nbbbbbb", 8, 3), 5);
    CHECK_EQ_INT(pc_crash_tail("a\nbb\nccc\n", 9, 4), 5);   /* start lands on a line start */
}

static int count_nl(const char *s) { int n = 0; for (; *s; s++) n += *s == '\n'; return n; }

static void test_tokens_clean(void)
{
    pc_crash_t c = full();
    char out[256];
    memset(c.screen, 0xff, sizeof c.screen);
    CHECK(pc_crash_format(&c, 0, "x", out, sizeof out) > 0);
    CHECK(strstr(out, " screen=- bt=") != NULL);
    memset(c.screen, 0, sizeof c.screen);
    memcpy(c.screen, "a b\nc", 6);
    CHECK(pc_crash_format(&c, 0, "f w\nx", out, sizeof out) > 0);
    CHECK(strstr(out, " fw=fwx reason=") != NULL);
    CHECK(strstr(out, " screen=abc bt=") != NULL);
    CHECK_EQ_INT(count_nl(out), 1);
    CHECK(out[strlen(out) - 1] == '\n');
    CHECK(pc_crash_format(&c, 0, "0123456789abcdefXYZ", out, sizeof out) > 0);
    CHECK(strstr(out, " fw=0123456789abcdef reason=") != NULL);
}

static void test_fw_fallback(void)
{
    pc_crash_t c = full();
    char out[256];
    CHECK(pc_crash_format(&c, 0, NULL, out, sizeof out) > 0);
    CHECK(strstr(out, " fw=- reason=") != NULL);
    CHECK(pc_crash_format(&c, 0, "", out, sizeof out) > 0);
    CHECK(strstr(out, " fw=- reason=") != NULL);
    CHECK(pc_crash_format(&c, 0, " \n", out, sizeof out) > 0);
    CHECK(strstr(out, " fw=- reason=") != NULL);
}

static void test_tail_edges(void)
{
    CHECK_EQ_INT(pc_crash_tail("abc\n", 4, 2), 2);
    CHECK_EQ_INT(pc_crash_tail("abc\n", 4, 0), 4);
    CHECK_EQ_INT(pc_crash_tail("ab", 2, 0), 2);
}

int main(void)
{
    RUN(test_full_line);
    RUN(test_no_time_no_pc);
    RUN(test_screen_unsafe);
    RUN(test_cap);
    RUN(test_tail);
    RUN(test_tokens_clean);
    RUN(test_fw_fallback);
    RUN(test_tail_edges);
    return 0;
}
