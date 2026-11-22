// SPDX-License-Identifier: LGPL-2.1-or-later
#include <string.h>
#include "pc/pc_btmeta.h"
#include "pc_test.h"

#define U(s) (const uint8_t *)(s), (int)strlen(s)

static int valid_utf8(const char *s)       /* 1 when every sequence is whole */
{
    for (const unsigned char *p = (const unsigned char *)s; *p; ) {
        int n = *p < 0x80 ? 0 : (*p & 0xE0) == 0xC0 ? 1 : (*p & 0xF0) == 0xE0 ? 2 : (*p & 0xF8) == 0xF0 ? 3 : -1;
        if (n < 0) return 0;
        p++;
        for (int i = 0; i < n; i++, p++) if ((*p & 0xC0) != 0x80) return 0;
    }
    return 1;
}

static void test_copy_plain_and_cut(void)
{
    char b[16];
    CHECK_EQ_INT(pc_btmeta_copy(b, sizeof b, U("Hello")), 5);
    CHECK_STR(b, "Hello");
    CHECK_EQ_INT(pc_btmeta_copy(b, 8, U("夜航星圖")), 6);       /* 12 bytes into 7: two whole characters, never half of the third */
    CHECK_STR(b, "夜航");
    CHECK_EQ_INT(pc_btmeta_copy(b, 1, U("abc")), 0);
    CHECK_STR(b, "");
    const uint8_t nul[] = { 'a', 'b', 0, 'c' };                 /* a NUL inside the given length ends the text */
    pc_btmeta_copy(b, sizeof b, nul, 4);
    CHECK_STR(b, "ab");
    CHECK_EQ_INT(pc_btmeta_copy(b, sizeof b, NULL, 3), 0);
    CHECK_STR(b, "");
}

static void test_copy_sanitises(void)
{
    char b[32];
    pc_btmeta_copy(b, sizeof b, U("  a\tb\nc\x7f  "));          /* control characters -> spaces, the ends trimmed */
    CHECK_STR(b, "a b c");
    const uint8_t bad[] = { 'A', 0xE5, 0x9C, 'B', 0xFF, 0xC3, 0xA9, 0x80, 'C' };   /* a cut 3-byte sequence, a lone 0xFF and 0x80: dropped */
    pc_btmeta_copy(b, sizeof b, bad, (int)sizeof bad);
    CHECK_STR(b, "AB\xC3\xA9" "C");
    CHECK(valid_utf8(b));
    const uint8_t overlong[] = { 0xC0, 0xAF, 'z', 0xED, 0xA0, 0x80 };   /* overlong "/" and a surrogate: not valid UTF-8, dropped */
    pc_btmeta_copy(b, sizeof b, overlong, (int)sizeof overlong);
    CHECK_STR(b, "z");
    const uint8_t emoji[] = { 0xF0, 0x9F, 0x8E, 0xB5, '!' };
    pc_btmeta_copy(b, sizeof b, emoji, (int)sizeof emoji);
    CHECK_STR(b, "\xF0\x9F\x8E\xB5!");
    pc_btmeta_copy(b, 4, emoji, (int)sizeof emoji);             /* 4 bytes do not fit in 3: nothing of it */
    CHECK_STR(b, "");
}

static void test_copy_every_cut_is_whole(void)
{
    const char *s = "長い Title 夜航星圖 é ü 🎵 end";
    for (size_t cap = 1; cap < 48; cap++) {
        char b[48];
        memset(b, 'x', sizeof b);
        size_t n = pc_btmeta_copy(b, cap, U(s));
        CHECK(n < cap);
        CHECK_EQ_INT(strlen(b), n);
        CHECK(valid_utf8(b));
        CHECK(strncmp(b, s, n) == 0 || n == 0);
    }
}

static void test_put_fields(void)
{
    pc_btmeta_t m;
    memset(&m, 0, sizeof m);
    CHECK(!pc_btmeta_known(&m));
    CHECK(pc_btmeta_put(&m, PC_BTMETA_TITLE, U("Paper Planes")));
    CHECK(!pc_btmeta_put(&m, PC_BTMETA_TITLE, U("Paper Planes")));   /* the same again: no change */
    CHECK(pc_btmeta_known(&m));
    CHECK(pc_btmeta_put(&m, PC_BTMETA_ARTIST, U("紙飛機工廠")));
    CHECK(pc_btmeta_put(&m, PC_BTMETA_ALBUM, U("Night Train")));
    CHECK(pc_btmeta_put(&m, PC_BTMETA_TRACK, U("3")));
    CHECK(pc_btmeta_put(&m, PC_BTMETA_TOTAL, U("12")));
    CHECK(pc_btmeta_put(&m, PC_BTMETA_TIME, U("215000")));
    CHECK(!pc_btmeta_put(&m, PC_BTMETA_GENRE, U("Pop")));          /* not kept */
    CHECK(!pc_btmeta_put(&m, 0x55, U("x")));
    CHECK_STR(m.title, "Paper Planes");
    CHECK_STR(m.artist, "紙飛機工廠");
    CHECK_STR(m.album, "Night Train");
    CHECK_EQ_INT(m.track, 3);
    CHECK_EQ_INT(m.total, 12);
    CHECK_EQ_INT(m.play_ms, 215000);
    CHECK(pc_btmeta_put(&m, PC_BTMETA_TRACK, U("abc")));          /* not a number: unknown */
    CHECK_EQ_INT(m.track, 0);
    pc_btmeta_put(&m, PC_BTMETA_TOTAL, U("99999999999"));          /* too large: clamped, never wraps */
    CHECK_EQ_INT(m.total, 65535);
    pc_btmeta_put(&m, PC_BTMETA_TIME, U("99999999999"));
    CHECK_EQ_INT(m.play_ms, 4294967295u);
    pc_btmeta_put(&m, PC_BTMETA_TIME, U(" 42 "));                  /* spaces around the digits are fine */
    CHECK_EQ_INT(m.play_ms, 42);
    CHECK(pc_btmeta_put(&m, PC_BTMETA_TITLE, U("")));              /* an empty title: unknown again */
    CHECK(!pc_btmeta_known(&m));
    char big[400];
    memset(big, 'a', sizeof big);
    pc_btmeta_put(&m, PC_BTMETA_ALBUM, (const uint8_t *)big, (int)sizeof big);   /* longer than the field: cut */
    CHECK_EQ_INT(strlen(m.album), PC_BTMETA_TEXT - 1);
    CHECK(!pc_btmeta_put(&m, PC_BTMETA_TITLE, NULL, -1));          /* no text, negative length: nothing (was already "") */
}

static void test_put_same_bytes(void)     /* the same texts give the same struct bytes (bt_meta.c compares whole structs) */
{
    pc_btmeta_t a, b;
    memset(&a, 0, sizeof a);
    memset(&b, 0, sizeof b);
    pc_btmeta_put(&b, PC_BTMETA_TITLE, U("A much longer title first"));   /* the shorter one later must not leave its tail behind */
    pc_btmeta_put(&b, PC_BTMETA_ART, U("1234567"));
    pc_btmeta_put(&b, PC_BTMETA_ART, U("bad"));
    pc_btmeta_put(&a, PC_BTMETA_TITLE, U("Short"));
    pc_btmeta_put(&b, PC_BTMETA_TITLE, U("Short"));
    pc_btmeta_put(&b, PC_BTMETA_ARTIST, U("x"));
    pc_btmeta_put(&a, PC_BTMETA_ARTIST, U("x"));
    CHECK(!memcmp(&a, &b, sizeof a));
}

static void test_put_art_handle(void)
{
    pc_btmeta_t m;
    memset(&m, 0, sizeof m);
    CHECK(pc_btmeta_put(&m, PC_BTMETA_ART, U("1000001")));
    CHECK_STR(m.art, "1000001");
    CHECK(!pc_btmeta_put(&m, PC_BTMETA_ART, U("1000001")));
    CHECK(pc_btmeta_put(&m, PC_BTMETA_ART, U("123456")));          /* six digits: not a handle -> none */
    CHECK_STR(m.art, "");
    pc_btmeta_put(&m, PC_BTMETA_ART, U("12345678"));
    CHECK_STR(m.art, "");
    pc_btmeta_put(&m, PC_BTMETA_ART, U("12a4567"));
    CHECK_STR(m.art, "");
    const uint8_t z[] = { '0', '0', '0', '0', '0', '0', '7', 0 };   /* a trailing NUL counted in the length is fine */
    pc_btmeta_put(&m, PC_BTMETA_ART, z, 8);
    CHECK_STR(m.art, "0000007");
}

static void test_line(void)
{
    pc_btmeta_t m;
    memset(&m, 0, sizeof m);
    char b[64];
    pc_btmeta_line(&m, b, sizeof b);
    CHECK_STR(b, "");
    pc_btmeta_put(&m, PC_BTMETA_TITLE, U("Paper Planes"));
    pc_btmeta_line(&m, b, sizeof b);
    CHECK_STR(b, "Paper Planes");
    pc_btmeta_put(&m, PC_BTMETA_ARTIST, U("紙飛機工廠"));
    pc_btmeta_line(&m, b, sizeof b);
    CHECK_STR(b, "Paper Planes  -  紙飛機工廠");                   /* the separator of a local track (mp3_ctl.c title) */
    pc_btmeta_line(&m, b, 21);                                     /* "Paper Planes  -  " is 17 bytes: one 3-byte character more fits in 20 */
    CHECK_STR(b, "Paper Planes  -  紙");
    pc_btmeta_line(&m, b, 20);
    CHECK_STR(b, "Paper Planes  -  ");
    CHECK(valid_utf8(b));
}

int main(void)
{
    RUN(test_copy_plain_and_cut);
    RUN(test_copy_sanitises);
    RUN(test_copy_every_cut_is_whole);
    RUN(test_put_fields);
    RUN(test_put_same_bytes);
    RUN(test_put_art_handle);
    RUN(test_line);
    return 0;
}
