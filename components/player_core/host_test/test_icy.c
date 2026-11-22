// SPDX-License-Identifier: LGPL-2.1-or-later
#include <string.h>
#include "pc/pc_icy.h"
#include "pc_test.h"

/* builds metaint=8 stream: 8 audio bytes, meta block, 8 audio bytes, empty meta (0), 4 audio bytes */
static size_t sample(uint8_t *b)
{
    size_t n = 0;
    memcpy(b + n, "AAAAAAAA", 8); n += 8;
    const char *m = "StreamTitle='Artist - Song';";
    size_t ml = strlen(m), blocks = (ml + 15) / 16;
    b[n++] = (uint8_t)blocks;
    memset(b + n, 0, blocks * 16); memcpy(b + n, m, ml); n += blocks * 16;
    memcpy(b + n, "BBBBBBBB", 8); n += 8;
    b[n++] = 0;
    memcpy(b + n, "CCCC", 4); n += 4;
    return n;
}

static void test_one_chunk(void)
{
    static pc_icy_t s;
    uint8_t b[128];
    size_t n = sample(b);
    pc_icy_init(&s, 8);
    size_t a = pc_icy_feed(&s, b, n);
    CHECK_EQ_INT(a, 20);
    CHECK(!memcmp(b, "AAAAAAAABBBBBBBBCCCC", 20));
    char t[160];
    CHECK(pc_icy_take_title(&s, t, sizeof t));
    CHECK_STR(t, "Artist - Song");
    CHECK(!pc_icy_take_title(&s, t, sizeof t));       /* once */
}

static void test_byte_by_byte(void)
{
    static pc_icy_t s;
    uint8_t b[128], out[128];
    size_t n = sample(b), o = 0;
    pc_icy_init(&s, 8);
    for (size_t i = 0; i < n; i++) {                  /* every boundary inside the meta block */
        uint8_t c = b[i];
        if (pc_icy_feed(&s, &c, 1)) out[o++] = c;
    }
    CHECK_EQ_INT(o, 20);
    CHECK(!memcmp(out, "AAAAAAAABBBBBBBBCCCC", 20));
    char t[160];
    CHECK(pc_icy_take_title(&s, t, sizeof t));
    CHECK_STR(t, "Artist - Song");
}

static void test_no_metadata(void)
{
    static pc_icy_t s;
    uint8_t b[6] = "hello";
    pc_icy_init(&s, 0);
    CHECK_EQ_INT(pc_icy_feed(&s, b, 5), 5);
    char t[16];
    CHECK(!pc_icy_take_title(&s, t, sizeof t));
}

static void test_quote_in_title(void)
{
    static pc_icy_t s;
    uint8_t b[64];
    const char *m = "StreamTitle='It's Here';StreamUrl='';";
    size_t ml = strlen(m), blocks = (ml + 15) / 16;
    b[0] = 'x'; b[1] = (uint8_t)blocks;
    memset(b + 2, 0, blocks * 16); memcpy(b + 2, m, ml);
    pc_icy_init(&s, 1);
    CHECK_EQ_INT(pc_icy_feed(&s, b, 2 + blocks * 16), 1);
    char t[160];
    CHECK(pc_icy_take_title(&s, t, sizeof t));
    CHECK_STR(t, "It's Here");                         /* ends at "';", not at the first quote */
}

/* one metaint=1 chunk: 'x' + a meta block holding m */
static const char *title_of(const char *m, bool *got)
{
    static pc_icy_t s;
    static char t[160];
    uint8_t b[600];
    size_t ml = strlen(m), blocks = (ml + 15) / 16;
    b[0] = 'x'; b[1] = (uint8_t)blocks;
    memset(b + 2, 0, blocks * 16); memcpy(b + 2, m, ml);
    pc_icy_init(&s, 1);
    pc_icy_feed(&s, b, 2 + blocks * 16);
    *got = pc_icy_take_title(&s, t, sizeof t);
    return t;
}

static void test_title_forms(void)
{
    bool got;
    CHECK_STR(title_of("streamtitle='lower key';", &got), "lower key");
    CHECK(got);
    CHECK_STR(title_of("StreamTitle=\"Double\";", &got), "Double");
    CHECK_STR(title_of("StreamTitle='  padded  ';", &got), "padded");
    CHECK_STR(title_of("StreamTitle='no end", &got), "no end");
    CHECK_STR(title_of("StreamTitle='Guns N' Roses - Patience';", &got), "Guns N' Roses - Patience");
    CHECK_STR(title_of("StreamTitle='tab\there';", &got), "tab here");     /* control characters become spaces */
    title_of("StreamUrl='http://x';", &got);
    CHECK(!got);                                                            /* no title in the block: unchanged */
}

static void test_latin1_title(void)
{
    bool got;
    CHECK_STR(title_of("StreamTitle='Caf\xe9';", &got), "Caf\xc3\xa9");      /* not UTF-8: read as Latin-1 */
    CHECK_STR(title_of("StreamTitle='\xe5\x8f\xb0\xe5\x8c\x97';", &got), "\xe5\x8f\xb0\xe5\x8c\x97");   /* UTF-8 stays */
}

int main(void)
{
    RUN(test_title_forms);
    RUN(test_latin1_title);
    RUN(test_one_chunk);
    RUN(test_byte_by_byte);
    RUN(test_no_metadata);
    RUN(test_quote_in_title);
    return 0;
}
