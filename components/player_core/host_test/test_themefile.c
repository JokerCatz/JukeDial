// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_themefile.h"
#include "pc_test.h"
/* The theme file reader (pc_themefile.h): good lines, bad lines, missing keys, odd text. The callback here knows three keys. */
typedef struct { uint32_t bg, gold; char base[16]; int calls; } got_t;
static int cb(void *c, const pc_themefile_kv_t *kv)
{
    got_t *g = c;
    g->calls++;
    if (!strcmp(kv->key, "base")) { snprintf(g->base, sizeof g->base, "%s", kv->val); return 0; }
    uint32_t v;
    if (pc_themefile_rgb(kv->val, &v)) return 1;
    if (!strcmp(kv->key, "bg")) { g->bg = v; return 0; }
    if (!strcmp(kv->key, "gold")) { g->gold = v; return 0; }
    return 1;                                                     /* unknown key */
}
static int parse(const char *t, got_t *g) { memset(g, 0, sizeof *g); return pc_themefile_parse(t, strlen(t), cb, g); }

static void test_good(void)
{
    got_t g;
    CHECK_EQ_INT(parse("# a theme\nbase = neon\nbg = #102030\n  GOLD=#ffc940   # trailing\n; also a comment\n\n", &g), 0);
    CHECK_STR(g.base, "neon"); CHECK_EQ_INT(g.bg, 0x102030); CHECK_EQ_INT(g.gold, 0xFFC940); CHECK_EQ_INT(g.calls, 3);
}
static void test_odd_text(void)
{
    got_t g;
    CHECK_EQ_INT(parse("\xEF\xBB\xBF" "bg = #000001\r\ngold = #000002", &g), 0);      /* BOM, CR LF, no final newline */
    CHECK_EQ_INT(g.bg, 1); CHECK_EQ_INT(g.gold, 2);
    CHECK_EQ_INT(parse("", &g), 0);
    CHECK_EQ_INT(pc_themefile_parse(NULL, 0, cb, &g), 0);
    CHECK_EQ_INT(parse("bg = #0000ff;c\n", &g), 0); CHECK_EQ_INT(g.bg, 255);
}
static void test_bad(void)
{
    got_t g;
    CHECK_EQ_INT(parse("bg = 102030\n", &g), 1);                   /* no # */
    CHECK_EQ_INT(parse("bg = #12345\n", &g), 1);                   /* short */
    CHECK_EQ_INT(parse("bg = #1234567\n", &g), 1);                 /* long */
    CHECK_EQ_INT(parse("bg = #12345g\n", &g), 1);                  /* not hex */
    CHECK_EQ_INT(parse("bg #102030\n", &g), 1);                    /* no = */
    CHECK_EQ_INT(parse("= #102030\n", &g), 1);                     /* no key */
    CHECK_EQ_INT(parse("nosuch = #102030\n", &g), 1);              /* unknown key */
    CHECK_EQ_INT(parse("bg =\n", &g), 1);                          /* no value */
    char big[300]; memset(big, 'a', sizeof big - 1); big[sizeof big - 1] = 0;
    CHECK_EQ_INT(parse(big, &g), 1);                               /* an over-long line */
    CHECK_EQ_INT(parse("bg = #111111\nzzz = #1\ngold = #222222\n", &g), 1);   /* a bad line does not stop the rest */
    CHECK_EQ_INT(g.bg, 0x111111); CHECK_EQ_INT(g.gold, 0x222222);
}
static void test_missing_keys(void)
{
    got_t g;
    CHECK_EQ_INT(parse("bg = #010203\n", &g), 0);                  /* gold missing: untouched (the caller keeps its default) */
    CHECK_EQ_INT(g.gold, 0);
    uint32_t v = 7;
    CHECK_EQ_INT(pc_themefile_rgb("#abcdef", &v), 0); CHECK_EQ_INT(v, 0xABCDEF);
    CHECK_EQ_INT(pc_themefile_rgb(NULL, &v), -1); CHECK_EQ_INT(v, 0xABCDEF);
}
int main(void) { RUN(test_good); RUN(test_odd_text); RUN(test_bad); RUN(test_missing_keys); printf("themefile ok\n"); return 0; }
