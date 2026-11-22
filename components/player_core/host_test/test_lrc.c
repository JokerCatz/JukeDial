// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_lrc.h"
#include "pc_test.h"

static pc_lrc_t L;
static int parse(const char *s) { return pc_lrc_parse(&L, s, strlen(s)); }

static void test_basic_and_tags(void)
{
    CHECK_EQ_INT(parse("[ti:Song]\n[ar:Someone]\n[00:12.50]hello\n[01:02.5]world\n[00:05]first\n"), PC_LRC_OK);
    CHECK_EQ_INT(L.n, 3);
    CHECK_EQ_INT(L.line[0].ms, 5000); CHECK_STR(pc_lrc_text(&L, 0), "first");   /* sorted by time */
    CHECK_EQ_INT(L.line[1].ms, 12500); CHECK_STR(pc_lrc_text(&L, 1), "hello");
    CHECK_EQ_INT(L.line[2].ms, 62500); CHECK_STR(pc_lrc_text(&L, 2), "world");
    CHECK_EQ_INT(pc_lrc_find(&L, 0), -1);                    /* before the first line */
    CHECK_EQ_INT(pc_lrc_find(&L, 4999), -1);
    CHECK_EQ_INT(pc_lrc_find(&L, 5000), 0);
    CHECK_EQ_INT(pc_lrc_find(&L, 12499), 0);
    CHECK_EQ_INT(pc_lrc_find(&L, 12500), 1);
    CHECK_EQ_INT(pc_lrc_find(&L, 99999999), 2);
    CHECK_STR(pc_lrc_text(&L, 3), ""); CHECK_STR(pc_lrc_text(&L, -1), "");
}

static void test_fraction_forms(void)
{
    CHECK_EQ_INT(parse("[00:01.5]a\n[00:02.25]b\n[00:03.125]c\n[00:04:50]d\n[123:00.00]e\n"), PC_LRC_OK);
    CHECK_EQ_INT(L.line[0].ms, 1500); CHECK_EQ_INT(L.line[1].ms, 2250); CHECK_EQ_INT(L.line[2].ms, 3125);
    CHECK_EQ_INT(L.line[3].ms, 4500);                        /* [mm:ss:xx] */
    CHECK_EQ_INT(L.line[4].ms, 123 * 60 * 1000);             /* three digit minutes */
}

static void test_multiple_tags(void)
{
    CHECK_EQ_INT(parse("[00:30.00][00:10.00][01:00.00]la la\n[00:20.00]mid\n"), PC_LRC_OK);
    CHECK_EQ_INT(L.n, 4);
    CHECK_EQ_INT(L.line[0].ms, 10000); CHECK_STR(pc_lrc_text(&L, 0), "la la");
    CHECK_EQ_INT(L.line[1].ms, 20000); CHECK_STR(pc_lrc_text(&L, 1), "mid");
    CHECK_EQ_INT(L.line[2].ms, 30000); CHECK_STR(pc_lrc_text(&L, 2), "la la");
    CHECK_EQ_INT(L.line[3].ms, 60000);
    CHECK_EQ_INT(L.line[0].off, L.line[2].off);              /* one text, several times */
}

static void test_offset(void)
{
    CHECK_EQ_INT(parse("[offset:+500]\n[00:10.00]a\n[00:00.20]b\n"), PC_LRC_OK);
    CHECK_EQ_INT(L.line[0].ms, 0);                           /* 200 - 500 clamps at 0 */
    CHECK_EQ_INT(L.line[1].ms, 9500);                        /* positive offset: the line shows earlier */
    CHECK_EQ_INT(parse("[00:10.00]a\n[Offset: -250 ]\n"), PC_LRC_OK);   /* after the lines, any case, spaces, negative */
    CHECK_EQ_INT(L.line[0].ms, 10250);
    CHECK_EQ_INT(parse("[offset:abc]\n[00:10.00]a\n"), PC_LRC_OK);      /* a broken offset is an ignored tag */
    CHECK_EQ_INT(L.line[0].ms, 10000);
}

static void test_bad_lines(void)
{
    CHECK_EQ_INT(parse("hello\n[xx:yy]nope\n[00:10]\n[00:20.00]  spaced  \n[00:3"), PC_LRC_OK);
    CHECK_EQ_INT(L.n, 2);
    CHECK_EQ_INT(L.line[0].ms, 10000); CHECK_STR(pc_lrc_text(&L, 0), "");    /* a time with no text: an empty line (a pause) */
    CHECK_STR(pc_lrc_text(&L, 1), "spaced");                                 /* trimmed */
    CHECK_EQ_INT(parse("just text\n\n[ti:x]\n"), PC_LRC_E_EMPTY);
    CHECK_EQ_INT(L.n, 0);
    CHECK_EQ_INT(parse(""), PC_LRC_E_EMPTY);
    CHECK_EQ_INT(pc_lrc_parse(NULL, "x", 1), PC_LRC_E_ARG);
    CHECK_EQ_INT(pc_lrc_parse(&L, NULL, 1), PC_LRC_E_ARG);
    CHECK_EQ_INT(pc_lrc_find(&L, 100), -1);                  /* no lines */
    CHECK_EQ_INT(parse("[00:10.00][Chorus] sing\n"), PC_LRC_OK);
    CHECK_STR(pc_lrc_text(&L, 0), "[Chorus] sing");          /* a bracket after a time is text */
    CHECK_EQ_INT(parse("[00:10.00]<00:10.10>one <00:10.50>two <bad>x\n"), PC_LRC_OK);
    CHECK_STR(pc_lrc_text(&L, 0), "one two <bad>x");         /* enhanced word times are removed */
}

static void test_crlf_bom(void)
{
    CHECK_EQ_INT(parse("\xEF\xBB\xBF[00:01.00]\xE4\xBD\xA0\xE5\xA5\xBD\r\n[00:02.00]world\r\n\r\n[00:03.00]cr\r[00:04.00]end"), PC_LRC_OK);
    CHECK_EQ_INT(L.n, 4);
    CHECK_STR(pc_lrc_text(&L, 0), "\xE4\xBD\xA0\xE5\xA5\xBD");   /* the BOM did not stop the first tag; UTF-8 kept */
    CHECK_STR(pc_lrc_text(&L, 1), "world");                      /* no stray CR */
    CHECK_STR(pc_lrc_text(&L, 2), "cr");                         /* a lone CR ends a line too */
    CHECK_STR(pc_lrc_text(&L, 3), "end");                        /* no trailing newline */
}

static void test_encoding(void)
{
    /* GBK bytes for "你好" (C4 E3 BA C3) repeated: invalid UTF-8 -> reported, not shown as garbage */
    CHECK_EQ_INT(parse("[00:01.00]\xC4\xE3\xBA\xC3\xC4\xE3\xBA\xC3\n"), PC_LRC_E_ENCODING);
    CHECK_EQ_INT(L.n, 0);
    CHECK_EQ_INT(parse("[00:01.00]ok\xFF" "x\n"), PC_LRC_OK);   /* one stray byte: dropped */
    CHECK_STR(pc_lrc_text(&L, 0), "okx");
}

static void test_limits(void)
{
    static char big[PC_LRC_MAX_FILE + 4096];
    size_t n = 0;
    for (int i = 0; i < 1000 && n < sizeof big - 64; i++) n += (size_t)snprintf(big + n, sizeof big - n, "[%02d:%02d.00]line number %d\n", i / 60, i % 60, i);
    CHECK_EQ_INT(pc_lrc_parse(&L, big, n), PC_LRC_OK);
    CHECK_EQ_INT(L.n, PC_LRC_MAX_LINES);                     /* the line cap */
    CHECK(L.truncated);
    CHECK(L.text_len <= PC_LRC_TEXT_BYTES);
    CHECK_EQ_INT(L.line[399].ms, 399000);
    /* a long line is cut at a character boundary: 100 x 3-byte characters = 300 bytes, 160 allowed -> 53 characters */
    char longl[400] = "[00:01.00]";
    size_t k = strlen(longl);
    for (int i = 0; i < 100; i++) { memcpy(longl + k, "\xE4\xBD\xA0", 3); k += 3; }
    longl[k] = 0;
    CHECK_EQ_INT(parse(longl), PC_LRC_OK);
    CHECK_EQ_INT((int)strlen(pc_lrc_text(&L, 0)), 159);
    CHECK(L.truncated);
    /* the text pool: many long lines fill it, later lines are dropped, nothing overruns */
    static char fat[PC_LRC_MAX_FILE];
    size_t m = 0;
    for (int i = 0; m + 200 < sizeof fat; i++) { m += (size_t)snprintf(fat + m, sizeof fat - m, "[%02d:%02d.00]", i / 60 % 100, i % 60); memset(fat + m, 'a', 150); m += 150; fat[m++] = '\n'; }
    CHECK_EQ_INT(pc_lrc_parse(&L, fat, m), PC_LRC_OK);
    CHECK(L.text_len <= PC_LRC_TEXT_BYTES);
    CHECK(L.n > 100 && L.n <= PC_LRC_MAX_LINES);
    for (int i = 0; i < L.n; i++) CHECK(L.line[i].off < L.text_len);
}

static void test_path(void)
{
    char o[64];
    CHECK(pc_lrc_path("/sdcard/music/a b.mp3", o, sizeof o)); CHECK_STR(o, "/sdcard/music/a b.lrc");
    CHECK(pc_lrc_path("/sdcard/x.tar.flac", o, sizeof o)); CHECK_STR(o, "/sdcard/x.tar.lrc");
    CHECK(pc_lrc_path("/sdcard/noext", o, sizeof o)); CHECK_STR(o, "/sdcard/noext.lrc");
    CHECK(pc_lrc_path("/sd.dir/noext", o, sizeof o)); CHECK_STR(o, "/sd.dir/noext.lrc");   /* a dot in a folder is not an extension */
    CHECK(pc_lrc_path("/sdcard/.hidden", o, sizeof o)); CHECK_STR(o, "/sdcard/.hidden.lrc");
    CHECK(!pc_lrc_path("", o, sizeof o)); CHECK(!pc_lrc_path(NULL, o, sizeof o));
    CHECK(!pc_lrc_path("/sdcard/music/long name.mp3", o, 10));
}

int main(void)
{
    RUN(test_basic_and_tags); RUN(test_fraction_forms); RUN(test_multiple_tags); RUN(test_offset); RUN(test_bad_lines);
    RUN(test_crlf_bom); RUN(test_encoding); RUN(test_limits); RUN(test_path);
    return 0;
}
