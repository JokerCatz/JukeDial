// SPDX-License-Identifier: LGPL-2.1-or-later
/* pc_gamesave (ideas G2): the key/value store behind games.txt, its text, the corrupt cases, the debounce, the bests of a game. */
#include "pc/pc_gamesave.h"
#include "pc_test.h"

static pc_gs_t g, h;

static void test_set_get(void)
{
    pc_gs_init(&g);
    uint32_t v = 7;
    CHECK(!pc_gs_get(&g, "a.b", &v));
    CHECK_EQ_INT(v, 7);                               /* untouched */
    CHECK_EQ_INT(pc_gs_get_or(&g, "a.b", 42), 42);
    CHECK(!g.dirty);
    CHECK(pc_gs_set(&g, "a.b", 5));
    CHECK(g.dirty);
    CHECK(pc_gs_get(&g, "a.b", &v) && v == 5);
    pc_gs_written(&g, 100);
    CHECK(pc_gs_set(&g, "a.b", 5));                   /* the same value: not dirty */
    CHECK(!g.dirty);
    CHECK(pc_gs_set(&g, "a.b", 6) && g.dirty && g.n == 1);
    CHECK(pc_gs_set(&g, "z", 4000000000u));
    CHECK_EQ_INT(pc_gs_get_or(&g, "z", 0), (long long)4000000000u);
    CHECK(!pc_gs_set(&g, "Bad", 1) && !pc_gs_set(&g, "", 1) && !pc_gs_set(&g, "has space", 1) && !pc_gs_set(&g, "a=b", 1) && !pc_gs_set(&g, NULL, 1));
    CHECK(!pc_gs_set(&g, "abcdefghijklmnopqrst", 1));   /* 20 characters: one too many */
    CHECK(pc_gs_set(&g, "abcdefghijklmnopqrs", 1));     /* 19 */
    CHECK_EQ_INT(g.n, 3);
}

static void test_full(void)
{
    pc_gs_init(&g);
    char k[16];
    for (int i = 0; i < PC_GS_ENTRIES; i++) { snprintf(k, sizeof k, "k%d", i); CHECK(pc_gs_set(&g, k, (uint32_t)i)); }
    CHECK(!pc_gs_set(&g, "extra", 1));                 /* full: refused, nothing changed */
    CHECK(pc_gs_set(&g, "k0", 99));                    /* an existing key still changes */
    CHECK_EQ_INT(g.n, PC_GS_ENTRIES);
    static char buf[PC_GS_FILE_MAX + 64];
    size_t n = pc_gs_format(&g, buf, sizeof buf);
    CHECK(n > 0 && n <= PC_GS_FILE_MAX);               /* even a full store fits the cap */
    CHECK(pc_gs_parse(&h, buf, n) == PC_GS_ENTRIES);
}

static void test_roundtrip(void)
{
    static char buf[PC_GS_FILE_MAX + 64];
    pc_gs_init(&g);
    pc_gs_set(&g, "ctr.n", 4); pc_gs_set(&g, "ctr.v0", 12); pc_gs_set(&g, "ctr.v1", 0); pc_gs_set(&g, "big", 4294967295u);
    size_t n = pc_gs_format(&g, buf, sizeof buf);
    CHECK(n > 0 && strlen(buf) == n);
    CHECK(!strncmp(buf, "pcgs1\nctr.n=4\nctr.v0=12\nctr.v1=0\nbig=4294967295\nend=4 ", 52));
    CHECK_EQ_INT(pc_gs_parse(&h, buf, n), 4);
    CHECK(!h.dirty);
    CHECK_EQ_INT(pc_gs_get_or(&h, "ctr.v0", 0), 12);
    CHECK_EQ_INT(pc_gs_get_or(&h, "big", 0), (long long)4294967295u);
    CHECK(pc_gs_format(&g, buf, 20) == 0);             /* too small a buffer: nothing */
    CHECK(pc_gs_format(&g, buf, n) == 0);              /* no room for the NUL: nothing */
    CHECK(pc_gs_format(&g, buf, n + 1) == n);
    pc_gs_init(&g);                                    /* empty: header and end only, and it reads back */
    n = pc_gs_format(&g, buf, sizeof buf);
    CHECK_EQ_INT(pc_gs_parse(&h, buf, n), 0);
    CHECK_EQ_INT(pc_gs_parse(&h, "", 0), 0);           /* an empty file is an empty store, not a corrupt one */
}

static void test_corrupt(void)
{
    static char buf[PC_GS_FILE_MAX + 64], bad[PC_GS_FILE_MAX + 64];
    pc_gs_init(&g);
    pc_gs_set(&g, "a", 1); pc_gs_set(&g, "b", 22);
    size_t n = pc_gs_format(&g, buf, sizeof buf);
    pc_gs_set(&h, "keep", 1);
    CHECK_EQ_INT(pc_gs_parse(&h, buf, n - 1), PC_GS_BAD_END);       /* the cut file (no last newline) */
    CHECK_EQ_INT(h.n, 0);                                             /* a corrupt parse leaves the store empty */
    CHECK_EQ_INT(pc_gs_parse(&h, buf, n - 12), PC_GS_BAD_END);        /* cut inside the end line's line before it: no end */
    memcpy(bad, buf, n); bad[6] = 'A';                                /* a key that is not valid */
    CHECK_EQ_INT(pc_gs_parse(&h, bad, n), PC_GS_BAD_LINE);
    memcpy(bad, buf, n); bad[6] = 'x';                                /* a valid key, but another: the checksum says so */
    CHECK_EQ_INT(pc_gs_parse(&h, bad, n), PC_GS_BAD_SUM);
    memcpy(bad, buf, n); bad[8] = '9';                                /* a=9 */
    CHECK_EQ_INT(pc_gs_parse(&h, bad, n), PC_GS_BAD_SUM);
    memcpy(bad, buf, n); bad[0] = 'P';
    CHECK_EQ_INT(pc_gs_parse(&h, bad, n), PC_GS_BAD_HEADER);
    CHECK_EQ_INT(pc_gs_parse(&h, "hello world\n", 12), PC_GS_BAD_HEADER);
    CHECK_EQ_INT(pc_gs_parse(&h, "pcgs1\na=1\nend=2 00000000\n", 25), PC_GS_BAD_END);   /* the count is wrong */
    CHECK_EQ_INT(pc_gs_parse(&h, "pcgs1\na=1\na=2\nend=2 00000000\n", 29), PC_GS_BAD_LINE);   /* a key twice */
    CHECK_EQ_INT(pc_gs_parse(&h, "pcgs1\na=-1\nend=1 00000000\n", 26), PC_GS_BAD_LINE);
    CHECK_EQ_INT(pc_gs_parse(&h, "pcgs1\na=4294967296\nend=1 00000000\n", 34), PC_GS_BAD_LINE);   /* over 32 bits */
    CHECK_EQ_INT(pc_gs_parse(&h, "pcgs1\nnoequals\nend=0 00000000\n", 30), PC_GS_BAD_LINE);
    memcpy(bad, buf, n); memcpy(bad + n, "a=1\n", 4);                  /* something after the end */
    CHECK_EQ_INT(pc_gs_parse(&h, bad, n + 4), PC_GS_BAD_END);
    CHECK_EQ_INT(pc_gs_parse(&h, buf, PC_GS_FILE_MAX + 1), PC_GS_BAD_SIZE);
    CHECK_EQ_INT(h.n, 0);
    CHECK_EQ_INT(pc_gs_parse(&h, buf, n), 2);                         /* and the good text still reads */
}

static void test_debounce(void)
{
    pc_gs_init(&g);
    CHECK(!pc_gs_due(&g, 1000, 10000));                               /* nothing changed */
    pc_gs_set(&g, "a", 1);
    CHECK(pc_gs_due(&g, 5, 10000));                                   /* the first write is never delayed */
    pc_gs_written(&g, 5);
    CHECK(!pc_gs_due(&g, 6, 10000));                                  /* clean */
    pc_gs_set(&g, "a", 2);
    CHECK(!pc_gs_due(&g, 9999, 10000));                               /* too soon after the last write */
    CHECK(pc_gs_due(&g, 10005, 10000));
    pc_gs_written(&g, 10005);
    pc_gs_set(&g, "a", 3);
    CHECK(!pc_gs_due(&g, 10006, 10000));
    CHECK(pc_gs_due(&g, 20005, 10000));
    pc_gs_written(&g, 0xFFFFFFF0u);                                   /* the millisecond counter wraps */
    pc_gs_set(&g, "a", 4);
    CHECK(!pc_gs_due(&g, 100, 10000) && pc_gs_due(&g, 0x2800, 10000));
}

static void test_score(void)
{
    pc_game_score_t s, t;
    pc_game_score_reset(&s);
    pc_gs_init(&g);
    pc_game_score_record(&s, 0, PC_GAME_WIN, true, 7, 12000);
    pc_game_score_record(&s, 2, PC_GAME_WIN, true, 9, 8000);
    pc_game_score_points(&s, 1, 4500, 30000);
    pc_game_score_record(&s, 3, PC_GAME_LOSS, true, 5, 100);          /* a loss: no best, no keys */
    pc_gs_store_score(&g, "breakout", &s);
    CHECK_EQ_INT(g.n, 6);                                        /* m+t, p+t, m+t */
    CHECK(!pc_gs_get(&g, "breakout.m3", NULL));
    static char buf[PC_GS_FILE_MAX + 64];
    size_t n = pc_gs_format(&g, buf, sizeof buf);
    CHECK_EQ_INT(pc_gs_parse(&h, buf, n), 6);                         /* through the text, as after a restart */
    pc_game_score_reset(&t);
    t.played = 3; t.streak = 2;
    pc_gs_load_score(&h, "breakout", &t);
    CHECK_EQ_INT(t.best_moves[0], 7); CHECK_EQ_INT(t.best_ms[0], 12000);
    CHECK_EQ_INT(t.best_moves[2], 9); CHECK_EQ_INT(t.best_ms[2], 8000);
    CHECK_EQ_INT(t.best_points[1], 4500); CHECK_EQ_INT(t.best_ms[1], 30000);
    CHECK_EQ_INT(t.best_moves[3], 0);
    CHECK_EQ_INT(t.played, 3); CHECK_EQ_INT(t.streak, 2);             /* the session's own counts stay */
    pc_gs_load_score(&h, "c4", &t);                                   /* another game: nothing of this one */
    CHECK_EQ_INT(t.best_moves[0], 7);
    pc_game_rec_t r = pc_game_score_record(&t, 0, PC_GAME_WIN, true, 9, 1);   /* a restart keeps the bests: 9 moves does not beat 7 */
    CHECK(!r.new_best && r.best_moves == 7);
    CHECK(pc_gs_store_score(&g, "breakout", &s));
    pc_game_score_t z = s;
    CHECK(!pc_gs_store_score(&g, "a_very_long_game_id", &z));         /* an id over PC_GS_ID_MAX is refused, not dropped silently */
    CHECK(pc_gs_store_score(&g, "sixteen_chars_ab", &z) && pc_gs_valid_key("sixteen_chars_ab.m0"));   /* 16 is the longest that fits */
    pc_gs_written(&g, 1);
    pc_gs_store_score(&g, "breakout", &s);                            /* the same score again: nothing to write */
    CHECK(!g.dirty);
    s.best_moves[0] = 6;
    pc_gs_store_score(&g, "breakout", &s);
    CHECK(g.dirty);
}

int main(void)
{
    RUN(test_set_get);
    RUN(test_full);
    RUN(test_roundtrip);
    RUN(test_corrupt);
    RUN(test_debounce);
    RUN(test_score);
    return 0;
}
