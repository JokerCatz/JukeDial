// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_extkeys.h"
#include "pc_test.h"

static pc_extkeys_cfg_t cfg;
static pc_extkeys_res_t res;

static void parse(const char *s) { pc_extkeys_parse(s, s ? strlen(s) : 0, &cfg, &res); }
static void expect_default(void)
{
    CHECK_EQ_INT(cfg.nlayers, 1);
    CHECK_STR(cfg.layers[0].name, "媒體鍵");
    CHECK_EQ_INT(cfg.layers[0].nkeys, 4);
    CHECK_EQ_INT(cfg.layers[0].keys[0].code, 0xCD);
    CHECK_EQ_INT(cfg.layers[0].knob, PC_KNOB_VOLUME);
}
static void expect_err(int line, int code)
{
    CHECK(!res.ok);
    CHECK(!res.from_file);
    expect_default();
    CHECK(res.nerr >= 1);
    CHECK_EQ_INT(res.err[0].line, line);
    CHECK_EQ_INT(res.err[0].code, code);
}

static void test_missing(void)
{
    pc_extkeys_parse(NULL, 0, &cfg, &res);
    CHECK(res.ok); CHECK(!res.from_file); CHECK_EQ_INT(res.total, 0); expect_default();
    parse("");
    CHECK(res.ok); expect_default();
    parse("# just a comment\n\n   \n");
    CHECK(res.ok); CHECK(!res.from_file); expect_default();
}

static void test_spec_example(void)
{
    parse("layer media            # name\n"
          "knob  volume\n"
          "key   播放  consumer 0xCD\n"
          "key   下首  consumer 0xB5\n"
          "layer edit\r\n"
          "knob scroll\r\n"
          "key   複製  combo ctrl+c\n"
          "key   簽名  text \"Hello\\n\"\n"
          "key   巨集  macro ctrl+a, wait 50, ctrl+c\n");
    CHECK(res.ok); CHECK(res.from_file); CHECK_EQ_INT(res.nerr, 0);
    CHECK_EQ_INT(cfg.nlayers, 2);
    CHECK_STR(cfg.layers[0].name, "media");
    CHECK_EQ_INT(cfg.layers[0].nkeys, 2);
    CHECK_STR(cfg.layers[0].keys[1].name, "下首");
    CHECK_EQ_INT(cfg.layers[0].keys[1].act, PC_XACT_CONSUMER);
    CHECK_EQ_INT(cfg.layers[0].keys[1].code, 0xB5);
    CHECK_EQ_INT(cfg.layers[1].knob, PC_KNOB_SCROLL);
    const pc_extkeys_key_t *k = &cfg.layers[1].keys[0];
    CHECK_EQ_INT(k->act, PC_XACT_COMBO); CHECK_EQ_INT(k->mod, 1); CHECK_EQ_INT(k->code, 0x06);
    k = &cfg.layers[1].keys[1];
    CHECK_EQ_INT(k->act, PC_XACT_TEXT); CHECK_EQ_INT(k->n, 6); CHECK_STR(k->u.text, "Hello\n");
    k = &cfg.layers[1].keys[2];
    CHECK_EQ_INT(k->act, PC_XACT_MACRO); CHECK_EQ_INT(k->n, 3);
    CHECK_EQ_INT(k->u.steps[0].type, PC_XSTEP_COMBO); CHECK_EQ_INT(k->u.steps[0].mod, 1); CHECK_EQ_INT(k->u.steps[0].val, 0x04);
    CHECK_EQ_INT(k->u.steps[1].type, PC_XSTEP_WAIT); CHECK_EQ_INT(k->u.steps[1].val, 50);
    CHECK_EQ_INT(k->u.steps[2].val, 0x06);
}

static void test_combos(void)
{
    parse("layer a\nkey x combo ctrl+shift+alt+gui+f12\nkey y combo gui\nkey z combo Shift+TAB\nkey w combo pgdn\nkey v combo ctrl+plus\n");
    CHECK(res.ok);
    CHECK_EQ_INT(cfg.layers[0].keys[0].mod, 0x0F); CHECK_EQ_INT(cfg.layers[0].keys[0].code, 0x45);
    CHECK_EQ_INT(cfg.layers[0].keys[1].mod, 0x08); CHECK_EQ_INT(cfg.layers[0].keys[1].code, 0);
    CHECK_EQ_INT(cfg.layers[0].keys[2].mod, 0x02); CHECK_EQ_INT(cfg.layers[0].keys[2].code, 0x2B);
    CHECK_EQ_INT(cfg.layers[0].keys[3].code, 0x4E);
    CHECK_EQ_INT(cfg.layers[0].keys[4].code, 0x2E);
    parse("layer a\nkey x combo ctrl+\n"); expect_err(2, PC_XERR_COMBO);
    parse("layer a\nkey x combo +c\n"); expect_err(2, PC_XERR_COMBO);
    parse("layer a\nkey x combo c+ctrl\n"); expect_err(2, PC_XERR_COMBO);
    parse("layer a\nkey x combo f13\n"); expect_err(2, PC_XERR_COMBO);
    parse("layer a\nkey x combo ctrl c\n"); expect_err(2, PC_XERR_COMBO);
    parse("layer a\nkey x combo\n"); expect_err(2, PC_XERR_COMBO);
}

static void test_errors(void)
{
    parse("key a consumer 0xCD\n"); expect_err(1, PC_XERR_NO_LAYER);
    parse("knob volume\n"); expect_err(1, PC_XERR_NO_LAYER);
    parse("layer a\nbogus\n"); expect_err(2, PC_XERR_UNKNOWN_WORD);
    parse("layer\n"); expect_err(1, PC_XERR_LAYER_NAME);
    parse("layer abcdefghijklm\nkey a consumer 1\n"); expect_err(1, PC_XERR_LAYER_NAME);
    parse("layer a\nknob wiggle\n"); expect_err(2, PC_XERR_KNOB);
    parse("layer a\nkey k flick 1\n"); expect_err(2, PC_XERR_ACTION);
    parse("layer a\nkey abcdefghijklmnopqrstuvwx consumer 1\n"); expect_err(2, PC_XERR_KEY_NAME);
    parse("layer a\nkey\n"); expect_err(2, PC_XERR_KEY_NAME);
    parse("layer a\nkey k consumer 0\n"); expect_err(2, PC_XERR_CONSUMER);
    parse("layer a\nkey k consumer 0x400\n"); expect_err(2, PC_XERR_CONSUMER);
    parse("layer a\nkey k consumer zz\n"); expect_err(2, PC_XERR_CONSUMER);
    parse("layer a\nkey k text \"caf\xc3\xa9\"\n"); expect_err(2, PC_XERR_TEXT_ASCII);
    parse("layer a\nkey k text \"a\\q\"\n"); expect_err(2, PC_XERR_TEXT_ASCII);
    parse("layer a\nkey k text \"abc\n"); expect_err(2, PC_XERR_TEXT_QUOTE);
    parse("layer a\nkey k text abc\n"); expect_err(2, PC_XERR_TEXT_QUOTE);
    parse("layer a\nkey k text \"abc\" junk\n"); expect_err(2, PC_XERR_TEXT_QUOTE);
    parse("layer a\nkey k macro ctrl+a,,x\n"); expect_err(2, PC_XERR_MACRO_STEP);
    parse("layer a\nkey k macro ctrl+a,\n"); expect_err(2, PC_XERR_MACRO_STEP);
    parse("layer a\nkey k macro wait\n"); expect_err(2, PC_XERR_WAIT);
    parse("layer a\nkey k macro wait 2001\n"); expect_err(2, PC_XERR_WAIT);
    parse("layer a\nkey k macro a b\n"); expect_err(2, PC_XERR_MACRO_STEP);
    parse("layer a\nkey k consumer 1\nlayer b\n"); expect_err(3, PC_XERR_EMPTY_LAYER);
    parse("layer a\nkey k consumer 1\x01\n"); expect_err(2, PC_XERR_BAD_BYTE);
    pc_extkeys_parse("layer a\0key k consumer 1\n", 25, &cfg, &res); expect_err(1, PC_XERR_BAD_BYTE);
}

static void test_limits(void)
{
    char big[2048];
    char *p = big;
    p += sprintf(p, "layer a\n");
    for (int i = 0; i < 8; i++) p += sprintf(p, "key k%d consumer 1\n", i);
    parse(big);
    CHECK(res.ok); CHECK_EQ_INT(cfg.layers[0].nkeys, 8);
    sprintf(p, "key k9 consumer 1\n");
    parse(big); expect_err(10, PC_XERR_TOO_MANY_KEYS);
    p = big;
    for (int i = 0; i < 8; i++) p += sprintf(p, "layer l%d\nkey k consumer 1\n", i);
    parse(big); CHECK(res.ok); CHECK_EQ_INT(cfg.nlayers, 8);
    sprintf(p, "layer l8\nkey k consumer 1\n");
    parse(big); expect_err(17, PC_XERR_TOO_MANY_LAYERS);
    /* text: 64 ok, 65 not; exactly 16 macro steps ok, 17 not; wait 2000 ok */
    char t[300], txt[80];
    memset(txt, 'a', 64); txt[64] = 0;
    sprintf(t, "layer a\nkey k text \"%s\"\n", txt);
    parse(t); CHECK(res.ok); CHECK_EQ_INT(cfg.layers[0].keys[0].n, 64);
    txt[64] = 'a'; txt[65] = 0;
    sprintf(t, "layer a\nkey k text \"%s\"\n", txt);
    parse(t); expect_err(2, PC_XERR_TEXT_LONG);
    sprintf(t, "layer a\nkey k text \"%.60s\\n\\n\\n\\n\\n\"\n", txt);
    parse(t); expect_err(2, PC_XERR_TEXT_LONG);
    strcpy(t, "layer a\nkey k macro a");
    for (int i = 1; i < 16; i++) strcat(t, ",a");
    strcat(t, "\n");
    parse(t); CHECK(res.ok); CHECK_EQ_INT(cfg.layers[0].keys[0].n, 16);
    strcpy(t + strlen(t) - 1, ",a\n");
    parse(t); expect_err(2, PC_XERR_MACRO_LONG);
    parse("layer a\nkey k macro wait 2000\n"); CHECK(res.ok); CHECK_EQ_INT(cfg.layers[0].keys[0].u.steps[0].val, 2000);
    /* '#' inside a quoted text is not a comment */
    parse("layer a\nkey k text \"a#b\" # tail\n"); CHECK(res.ok); CHECK_STR(cfg.layers[0].keys[0].u.text, "a#b");
    /* file size: 8192 ok-ish (parsed), 8193 rejected as a whole */
    static char huge[PC_EXTKEYS_FILE_MAX + 2];
    memset(huge, '#', sizeof huge - 1);
    pc_extkeys_parse(huge, PC_EXTKEYS_FILE_MAX, &cfg, &res); CHECK(res.ok);
    pc_extkeys_parse(huge, PC_EXTKEYS_FILE_MAX + 1, &cfg, &res); expect_err(0, PC_XERR_TOO_BIG);
}

static void test_error_list(void)
{
    char b[1024], *p = b;
    p += sprintf(p, "layer a\nkey k consumer 1\n");
    for (int i = 0; i < 12; i++) p += sprintf(p, "bogus\n");
    parse(b);
    CHECK(!res.ok); CHECK_EQ_INT(res.nerr, 8); CHECK_EQ_INT(res.total, 12);
    CHECK_EQ_INT(res.err[0].line, 3); CHECK_EQ_INT(res.err[7].line, 10);
    char f[96];
    pc_extkeys_err_fmt(&res.err[0], f, sizeof f);
    CHECK_STR(f, "3：看不懂的開頭");
    pc_extkeys_err_t whole = { 0, PC_XERR_TOO_BIG };
    pc_extkeys_err_fmt(&whole, f, sizeof f);
    CHECK_STR(f, "檔案太大");
    CHECK(pc_extkeys_err_fmt(&res.err[0], f, 6) == 5);   /* truncates, stays terminated */
    for (int c = 1; c < PC_XERR_COUNT; c++) CHECK(strlen(pc_extkeys_err_text(c)) > 0 && strcmp(pc_extkeys_err_text(c), "?"));
}

static void test_ascii(void)
{
    uint8_t m;
    CHECK_EQ_INT(pc_extkeys_ascii_usage('a', &m), 0x04); CHECK_EQ_INT(m, 0);
    CHECK_EQ_INT(pc_extkeys_ascii_usage('A', &m), 0x04); CHECK_EQ_INT(m, 2);
    CHECK_EQ_INT(pc_extkeys_ascii_usage('0', &m), 0x27); CHECK_EQ_INT(m, 0);
    CHECK_EQ_INT(pc_extkeys_ascii_usage(')', &m), 0x27); CHECK_EQ_INT(m, 2);
    CHECK_EQ_INT(pc_extkeys_ascii_usage('!', &m), 0x1E); CHECK_EQ_INT(m, 2);
    CHECK_EQ_INT(pc_extkeys_ascii_usage('(', &m), 0x26); CHECK_EQ_INT(m, 2);
    CHECK_EQ_INT(pc_extkeys_ascii_usage('?', &m), 0x38); CHECK_EQ_INT(m, 2);
    CHECK_EQ_INT(pc_extkeys_ascii_usage('\n', &m), 0x28);
    CHECK_EQ_INT(pc_extkeys_ascii_usage(0x7F, &m), 0);
    CHECK_EQ_INT(pc_extkeys_ascii_usage(0, &m), 0);
    for (int c = 0x20; c < 0x7F; c++) CHECK(pc_extkeys_ascii_usage((char)c, &m) != 0);   /* every printable ASCII char can be typed */
}

static void test_fixround1(void)
{
    parse("\xEF\xBB\xBFlayer a\nkey k consumer 1\n");           /* BOM before line 1 */
    CHECK(res.ok); CHECK(res.from_file); CHECK_STR(cfg.layers[0].name, "a");
    parse("\xEF\xBB\xBF");                                       /* BOM only: same as an empty file */
    CHECK(res.ok); CHECK(!res.from_file);
    parse("layer a\nkey k text \"C:\\\\\" # note\n");            /* text "C:\\" # note */
    CHECK(res.ok); CHECK_STR(cfg.layers[0].keys[0].u.text, "C:\\");
    parse("layer a\nkey k text \"a\\\"#b\" # note\n");           /* escaped quote then # inside */
    CHECK(res.ok); CHECK_STR(cfg.layers[0].keys[0].u.text, "a\"#b");
    parse("layer a\nkey \xff\xfe consumer 1\n"); expect_err(2, PC_XERR_BAD_BYTE);
    parse("layer \xff\xfe\nkey k consumer 1\n"); expect_err(1, PC_XERR_BAD_BYTE);
    parse("layer a\nkey \xe6\x92 consumer 1\n"); expect_err(2, PC_XERR_BAD_BYTE);          /* truncated sequence */
    parse("layer a\nkey \xc0\x80 consumer 1\n"); expect_err(2, PC_XERR_BAD_BYTE);          /* overlong */
    parse("layer a\nkey \xed\xa0\x80 consumer 1\n"); expect_err(2, PC_XERR_BAD_BYTE);      /* surrogate */
    parse("layer 層\nkey 播放😀 consumer 1\n"); CHECK(res.ok);                          /* 3- and 4-byte sequences */
}

static unsigned rng_s = 12345;
static unsigned rnd(void) { rng_s = rng_s * 1664525u + 1013904223u; return rng_s >> 8; }

static void check_invariants(void)
{
    CHECK(cfg.nlayers >= 1 && cfg.nlayers <= PC_EXTKEYS_LAYERS);
    CHECK(res.nerr <= PC_EXTKEYS_ERRS);
    CHECK(res.ok == (res.total == 0));
    CHECK(!res.from_file || res.ok);
    for (int i = 0; i < cfg.nlayers; i++) {
        const pc_extkeys_layer_t *l = &cfg.layers[i];
        CHECK(strlen(l->name) <= PC_EXTKEYS_LAYER_NAME && l->name[0]);
        CHECK(l->nkeys >= 1 && l->nkeys <= PC_EXTKEYS_KEYS && l->knob <= PC_KNOB_NONE);
        for (int j = 0; j < l->nkeys; j++) {
            const pc_extkeys_key_t *k = &l->keys[j];
            CHECK(strlen(k->name) <= PC_EXTKEYS_KEY_NAME && k->name[0]);
            if (k->act == PC_XACT_TEXT) {
                CHECK(k->n <= PC_EXTKEYS_TEXT_MAX && strlen(k->u.text) == k->n);
                for (int c = 0; c < k->n; c++) CHECK(pc_extkeys_ascii_usage(k->u.text[c], NULL) != 0);
            } else if (k->act == PC_XACT_MACRO) {
                CHECK(k->n >= 1 && k->n <= PC_EXTKEYS_MACRO_MAX);
                for (int s = 0; s < k->n; s++) CHECK(k->u.steps[s].type != PC_XSTEP_WAIT || k->u.steps[s].val <= PC_EXTKEYS_WAIT_MAX);
            } else CHECK(k->act == PC_XACT_CONSUMER || k->act == PC_XACT_COMBO);
        }
    }
}

static void test_fuzz(void)
{
    static const char *seed =
        "layer media\nknob volume\nkey 播放 consumer 0xCD\nlayer edit\nkey c combo ctrl+c\nkey s text \"Hi\\n\"\n"
        "key m macro ctrl+a, wait 50, ctrl+c\n";
    static const char *frag[] = { "layer ", "key ", "knob ", "consumer ", "combo ", "text ", "macro ", "\"", "\\", "#", ",", "+", "wait ",
                                  "ctrl+", "0x", "\n", " ", "\r\n", "\x01", "\xff", "\xc3" };
    size_t sl = strlen(seed);
    static char buf[PC_EXTKEYS_FILE_MAX + 64];
    int accepted = 0;
    for (int it = 0; it < 6000; it++) {
        size_t n;
        if (it % 3 == 0) {            /* pure random bytes, including NUL */
            n = rnd() % 600;
            for (size_t i = 0; i < n; i++) buf[i] = (char)rnd();
        } else {                      /* the seed with random byte flips, cuts and token splices */
            memcpy(buf, seed, sl);
            n = sl;
            int edits = 1 + (int)(rnd() % 6);
            for (int e = 0; e < edits; e++) {
                unsigned op = rnd() % 4;
                size_t at = n ? rnd() % n : 0;
                if (op == 0 && n) buf[at] = (char)rnd();
                else if (op == 1) n = at;
                else if (op == 2) {
                    const char *f = frag[rnd() % (sizeof frag / sizeof *frag)];
                    size_t fl = strlen(f);
                    if (n + fl < sizeof buf) { memmove(buf + at + fl, buf + at, n - at); memcpy(buf + at, f, fl); n += fl; }
                } else if (n) { memmove(buf + at, buf + at + 1, n - at - 1); n--; }
            }
        }
        pc_extkeys_parse(buf, n, &cfg, &res);
        check_invariants();
        accepted += res.from_file;
    }
    CHECK(accepted > 0);   /* the mutations leave some files valid, so the accept path is fuzzed too */
    /* exactly at / beyond the size limit with valid content */
    memset(buf, ' ', sizeof buf);
    pc_extkeys_parse(buf, PC_EXTKEYS_FILE_MAX + 1, &cfg, &res); CHECK(!res.ok);
}

int main(void)
{
    RUN(test_missing); RUN(test_spec_example); RUN(test_combos); RUN(test_errors); RUN(test_limits); RUN(test_error_list);
    RUN(test_ascii); RUN(test_fixround1); RUN(test_fuzz);
    return 0;
}
