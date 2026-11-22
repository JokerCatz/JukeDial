// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_io.h"
#include "pc/pc_utf.h"
#include "pc_test.h"

/* games G0 (ui_board cell text, 5 bytes + NUL): a two-character label (6 bytes) is cut after its first character, never inside it; one character and ASCII fit whole */
static void test_cell_label_cut(void)
{
    char t[6];
    pc_utf8_copy(t, sizeof t, "\xe8\xb1\xa1\xe6\xa3\x8b");          /* 象棋 (xiangqi) */
    CHECK_STR(t, "\xe8\xb1\xa1");
    pc_utf8_copy(t, sizeof t, "\xe8\xb1\xa1");
    CHECK_STR(t, "\xe8\xb1\xa1");
    pc_utf8_copy(t, sizeof t, "ab\xe8\xb1\xa1\xe6");                  /* 2 + 3 bytes fit, the next character does not */
    CHECK_STR(t, "ab\xe8\xb1\xa1");
    pc_utf8_copy(t, sizeof t, "abcdefgh");
    CHECK_STR(t, "abcde");
}

static void test_utf16_basic(void)
{
    const uint8_t le[] = { 0x2f, 0x6e, 0xa8, 0x98 };   /* 港 U+6E2F, 風 U+98A8 */
    char out[16];
    size_t n = pc_utf16_to_utf8(le, sizeof le, false, out, sizeof out);
    CHECK_STR(out, "港風");
    CHECK_EQ_INT(n, 6);
    const uint8_t be[] = { 0x6e, 0x2f };
    pc_utf16_to_utf8(be, sizeof be, true, out, sizeof out);
    CHECK_STR(out, "港");
}

static void test_utf16_surrogate(void)
{
    const uint8_t le[] = { 0x3c, 0xd8, 0xb5, 0xdf };   /* U+1F3B5 */
    char out[8];
    CHECK_EQ_INT(pc_utf16_to_utf8(le, sizeof le, false, out, sizeof out), 4);
    CHECK_STR(out, "\xF0\x9F\x8E\xB5");
}

static void test_utf16_lone_surrogate(void)
{
    const uint8_t le[] = { 0x3c, 0xd8, 0x41, 0x00 };   /* high surrogate then 'A' */
    char out[8];
    pc_utf16_to_utf8(le, sizeof le, false, out, sizeof out);
    CHECK_STR(out, "\xEF\xBF\xBD" "A");
}

static void test_utf16_stops_at_nul_and_fits(void)
{
    const uint8_t le[] = { 'a', 0, 'b', 0, 0, 0, 'c', 0 };
    char out[8];
    pc_utf16_to_utf8(le, sizeof le, false, out, sizeof out);
    CHECK_STR(out, "ab");
    const uint8_t cjk[] = { 0x2f, 0x6e, 0x2f, 0x6e };
    char small[5];                                      /* room for one 3-byte char only */
    pc_utf16_to_utf8(cjk, sizeof cjk, false, small, sizeof small);
    CHECK_STR(small, "港");
}

static void test_utf8_truncate(void)
{
    char s[16] = "ab港";
    CHECK_EQ_INT(pc_utf8_truncate(s, 4), 2);
    CHECK_STR(s, "ab");
    char t[16] = "abc";
    CHECK_EQ_INT(pc_utf8_truncate(t, 16), 3);
}

static void test_latin1(void)
{
    const uint8_t in[] = { 'C', 'a', 'f', 0xE9 };
    char out[8];
    pc_latin1_to_utf8(in, sizeof in, out, sizeof out);
    CHECK_STR(out, "Caf\xC3\xA9");
}

static void test_reader_mem(void)
{
    pc_reader_t r;
    pc_reader_mem(&r, "hello", 5);
    char b[8] = { 0 };
    CHECK_EQ_INT(r.size, 5);
    CHECK_EQ_INT(r.read(r.u, 1, b, 10), 4);
    CHECK_STR(b, "ello");
    CHECK_EQ_INT(r.read(r.u, 9, b, 1), 0);
}

static void test_utf8_copy(void)
{
    char d[5];
    CHECK_EQ_INT(pc_utf8_copy(d, sizeof d, "ab港x"), 2);   /* "ab" + 3-byte char does not fit in 4 */
    CHECK_STR(d, "ab");
    CHECK_EQ_INT(pc_utf8_copy(d, sizeof d, "abc"), 3);
    CHECK_STR(d, "abc");
}

static void test_common_bracket_prefix(void)
{
    const char *same[] = { "【Aimer】Kataomoi", "【Aimer】Ref:rain", "【Aimer】Brave Shine" };
    CHECK_EQ_INT(pc_title_common_prefix(same, 3), 11);                     /* 【 Aimer 】: 3 + 5 + 3 bytes */
    const char *mixed[] = { "【Aimer】Kataomoi", "【LiSA】Gurenge" };
    CHECK_EQ_INT(pc_title_common_prefix(mixed, 2), 0);
    const char *one_plain[] = { "【Aimer】Kataomoi", "Plain title" };
    CHECK_EQ_INT(pc_title_common_prefix(one_plain, 2), 0);
    const char *single[] = { "【Aimer】Kataomoi" };
    CHECK_EQ_INT(pc_title_common_prefix(single, 1), 0);                    /* one row: the artist is all the context there is */
    const char *none[] = { "Kataomoi", "Ref:rain" };
    CHECK_EQ_INT(pc_title_common_prefix(none, 2), 0);
    const char *open[] = { "【Aimer Kataomoi", "【Aimer Ref:rain" };
    CHECK_EQ_INT(pc_title_common_prefix(open, 2), 0);                      /* no closing bracket */
    const char *bare[] = { "【Aimer】", "【Aimer】Ref:rain" };
    CHECK_EQ_INT(pc_title_common_prefix(bare, 2), 0);                      /* a row that is only the prefix would go empty */
    const char *longer[] = { "【Aimer】Kataomoi", "【Aimer-X】Ref:rain" };
    CHECK_EQ_INT(pc_title_common_prefix(longer, 2), 0);                    /* same start, different bracket */
    CHECK_EQ_INT(pc_title_common_prefix(same, 0), 0);
    const char *bare_sp[] = { "【Aimer】 ", "【Aimer】Ref:rain" };
    CHECK_EQ_INT(pc_title_common_prefix(bare_sp, 2), 0);                   /* only the prefix and a space: would go empty too */
}

/* what a row shows after the prefix: the spaces after 】 go too, a row that would be empty keeps its whole title */
static void test_title_strip(void)
{
    CHECK_EQ_INT(pc_title_strip("【A】 title", 7), 8);                      /* 【 A 】 = 3 + 1 + 3 bytes, one space */
    CHECK_EQ_INT(pc_title_strip("【A】title", 7), 7);
    CHECK_EQ_INT(pc_title_strip("【A】", 7), 0);                            /* nothing after it: the title as it is */
    CHECK_EQ_INT(pc_title_strip("【A】   ", 7), 0);                         /* only spaces after it: the same */
    CHECK_EQ_INT(pc_title_strip("【A】  x", 7), 9);                         /* several spaces */
    CHECK_EQ_INT(pc_title_strip("【A】\xE3\x80\x80x", 7), 10);              /* an ideographic space (U+3000) counts as a space */
    CHECK_EQ_INT(pc_title_strip("【A】 title", 0), 0);                      /* no prefix: nothing stripped */
}

int main(void)
{
    RUN(test_utf8_copy);
    RUN(test_utf16_basic);
    RUN(test_common_bracket_prefix);
    RUN(test_title_strip);
    RUN(test_utf16_surrogate);
    RUN(test_utf16_lone_surrogate);
    RUN(test_cell_label_cut);
    RUN(test_utf16_stops_at_nul_and_fits);
    RUN(test_utf8_truncate);
    RUN(test_latin1);
    RUN(test_reader_mem);
    return 0;
}
