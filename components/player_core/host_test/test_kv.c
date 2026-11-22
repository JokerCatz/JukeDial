// SPDX-License-Identifier: LGPL-2.1-or-later
#include <unistd.h>
#include "pc/pc_kv.h"
#include "pc_test.h"

static void test_parse_basic(void)
{
    pc_kv_t kv[8];
    int n = pc_kv_parse("a=1\n# c\n b = hello world \n\nc=x=y\n", kv, 8);
    CHECK_EQ_INT(n, 3);
    CHECK_STR(pc_kv_get(kv, n, "a"), "1");
    CHECK_STR(pc_kv_get(kv, n, "b"), "hello world");
    CHECK_STR(pc_kv_get(kv, n, "c"), "x=y");
    CHECK(pc_kv_get(kv, n, "zz") == NULL);
}

static void test_override(void)
{
    pc_kv_t kv[8];
    int n = pc_kv_parse("a=1\r\na=2\r\n", kv, 8);
    CHECK_EQ_INT(n, 1);
    CHECK_STR(pc_kv_get(kv, n, "a"), "2");
}

static void test_get_int_default(void)
{
    pc_kv_t kv[8];
    int n = pc_kv_parse("v=abc\nw=-7\n", kv, 8);
    CHECK_EQ_INT(pc_kv_get_int(kv, n, "missing", 40), 40);
    CHECK_EQ_INT(pc_kv_get_int(kv, n, "v", 40), 40);
    CHECK_EQ_INT(pc_kv_get_int(kv, n, "w", 40), -7);
}

static void test_save_load_roundtrip(void)
{
    char path[] = "/tmp/pc_kv_XXXXXX";
    int fd = mkstemp(path);
    CHECK(fd >= 0);
    close(fd);
    pc_kv_t kv[8];
    int n = 0;
    CHECK_EQ_INT(pc_kv_set(kv, &n, 8, "volume", "40"), 0);
    CHECK_EQ_INT(pc_kv_set(kv, &n, 8, "sound", "warm 暖.toml"), 0);
    CHECK_EQ_INT(pc_kv_set(kv, &n, 8, "volume", "42"), 0);
    CHECK_EQ_INT(n, 2);
    CHECK_EQ_INT(pc_kv_save(path, kv, n), 0);
    pc_kv_t back[8];
    int m = pc_kv_load(path, back, 8);
    CHECK_EQ_INT(m, 2);
    CHECK_STR(pc_kv_get(back, m, "volume"), "42");
    CHECK_STR(pc_kv_get(back, m, "sound"), "warm 暖.toml");
    char tmp[64];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    CHECK(access(tmp, F_OK) != 0);
    unlink(path);
    CHECK_EQ_INT(pc_kv_load(path, back, 8), 0);
}

static void test_long_value_truncated(void)
{
    char text[400] = "k=";
    memset(text + 2, 'x', 300);
    text[302] = '\n';
    text[303] = 0;
    pc_kv_t kv[2];
    int n = pc_kv_parse(text, kv, 2);
    CHECK_EQ_INT(n, 1);
    CHECK_EQ_INT(strlen(pc_kv_get(kv, n, "k")), 255);
    /* the instrument's one-line settings with a long kit name (~220 chars) survive whole */
    text[2 + 220] = '\n';
    text[2 + 221] = 0;
    n = pc_kv_parse(text, kv, 2);
    CHECK_EQ_INT(strlen(pc_kv_get(kv, n, "k")), 220);
}

static void test_full(void)
{
    pc_kv_t kv[1];
    int n = 0;
    CHECK_EQ_INT(pc_kv_set(kv, &n, 1, "a", "1"), 0);
    CHECK_EQ_INT(pc_kv_set(kv, &n, 1, "b", "2"), -1);
}

/* the settings table when it holds `max` entries (app/main/cfg.c keeps CFG_MAX of them): a new key is refused and nothing changes,
 * a key already there still updates. Documents today's behaviour so raising the limit later is a deliberate change. */
static void test_full_table_behaviour(void)
{
    enum { MAX = 3 };
    pc_kv_t kv[MAX];
    int n = 0;
    CHECK_EQ_INT(pc_kv_set(kv, &n, MAX, "a", "1"), 0);
    CHECK_EQ_INT(pc_kv_set(kv, &n, MAX, "b", "2"), 0);
    CHECK_EQ_INT(pc_kv_set(kv, &n, MAX, "c", "3"), 0);
    CHECK_EQ_INT(n, MAX);
    CHECK_EQ_INT(pc_kv_set(kv, &n, MAX, "d", "4"), -1);   /* a new key: refused */
    CHECK_EQ_INT(n, MAX);
    CHECK(pc_kv_get(kv, n, "d") == NULL);
    CHECK_STR(pc_kv_get(kv, n, "a"), "1");                /* the table is untouched */
    CHECK_STR(pc_kv_get(kv, n, "b"), "2");
    CHECK_STR(pc_kv_get(kv, n, "c"), "3");
    CHECK_STR(kv[0].key, "a");
    CHECK_STR(kv[2].key, "c");
    CHECK_EQ_INT(pc_kv_set(kv, &n, MAX, "b", "22"), 0);   /* an existing key: still updates */
    CHECK_EQ_INT(n, MAX);
    CHECK_STR(pc_kv_get(kv, n, "b"), "22");
    CHECK_EQ_INT(pc_kv_del(kv, &n, "a"), 1);              /* a slot frees up: the new key fits again */
    CHECK_EQ_INT(pc_kv_set(kv, &n, MAX, "d", "4"), 0);
    CHECK_EQ_INT(n, MAX);
    CHECK_STR(pc_kv_get(kv, n, "d"), "4");
}

static void test_del(void)
{
    pc_kv_t kv[4];
    int n = pc_kv_parse("a=1\nb=2\nc=3\n", kv, 4);
    CHECK_EQ_INT(pc_kv_del(kv, &n, "b"), 1);
    CHECK_EQ_INT(n, 2);
    CHECK(!pc_kv_get(kv, n, "b"));
    CHECK_STR(pc_kv_get(kv, n, "a"), "1");
    CHECK_STR(pc_kv_get(kv, n, "c"), "3");
    CHECK_EQ_INT(pc_kv_del(kv, &n, "zz"), 0);      /* missing: nothing changes */
    CHECK_EQ_INT(n, 2);
    CHECK_EQ_INT(pc_kv_del(kv, &n, "c"), 1);       /* the last one */
    CHECK_EQ_INT(pc_kv_del(kv, &n, "a"), 1);
    CHECK_EQ_INT(n, 0);
    CHECK_EQ_INT(pc_kv_del(kv, &n, "a"), 0);
}

int main(void)
{
    RUN(test_del);
    RUN(test_parse_basic);
    RUN(test_override);
    RUN(test_get_int_default);
    RUN(test_save_load_roundtrip);
    RUN(test_long_value_truncated);
    RUN(test_full);
    RUN(test_full_table_behaviour);
    return 0;
}
