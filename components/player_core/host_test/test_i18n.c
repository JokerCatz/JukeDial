// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_i18n.h"
#include "pc_test.h"
#include <string.h>

/* i18n phase A: the language values, the choice of the three texts, the English fallback */
static void test_i18n_lang(void)
{
    CHECK_EQ_INT(pc_lang_norm(0), PC_LANG_TW);
    CHECK_EQ_INT(pc_lang_norm(2), PC_LANG_EN);
    CHECK_EQ_INT(pc_lang_norm(3), PC_LANG_TW);
    CHECK_EQ_INT(pc_lang_norm(-1), PC_LANG_TW);
    CHECK_EQ_INT(pc_lang_parse("cn"), PC_LANG_CN);
    CHECK_EQ_INT(pc_lang_parse("en"), PC_LANG_EN);
    CHECK_EQ_INT(pc_lang_parse("1"), PC_LANG_CN);
    CHECK_EQ_INT(pc_lang_parse("xx"), -1);
    CHECK_EQ_INT(pc_lang_parse(NULL), -1);
    CHECK(!strcmp(pc_lang_code(PC_LANG_CN), "cn"));
    CHECK(pc_lang_zh(PC_LANG_TW, true) && pc_lang_zh(PC_LANG_CN, true) && !pc_lang_zh(PC_LANG_EN, true) && !pc_lang_zh(PC_LANG_TW, false));
}

static void test_i18n_pick(void)
{
    CHECK(!strcmp(pc_i18n_text(PC_LANG_TW, true, "設定", "设置", "Settings"), "設定"));
    CHECK(!strcmp(pc_i18n_text(PC_LANG_CN, true, "設定", "设置", "Settings"), "设置"));
    CHECK(!strcmp(pc_i18n_text(PC_LANG_EN, true, "設定", "设置", "Settings"), "Settings"));
    CHECK(!strcmp(pc_i18n_text(PC_LANG_CN, false, "設定", "设置", "Settings"), "Settings"));   /* no CJK font: English */
    CHECK(!strcmp(pc_i18n_text(PC_LANG_TW, false, "設定", "设置", "Settings"), "Settings"));
    CHECK(!strcmp(pc_i18n_text(PC_LANG_CN, true, "設定", NULL, "Settings"), "設定"));          /* no cn text: the zh-TW one */
}

/* phase B: the data table (pc_i18n_data.c, generated from tools/gen/i18n/data.tsv): lookup by the zh-TW literal, TZ() follows pc_i18n_cur() */
static pc_lang_t s_cur = PC_LANG_TW;
pc_lang_t pc_i18n_cur(void) { return s_cur; }   /* the strong definition: the weak default of pc_i18n.c says 繁中 */

static void test_i18n_data(void)
{
    CHECK(pc_i18n_cn("這句話不在表裡") == NULL);
    CHECK(pc_i18n_cn(NULL) == NULL);
    const char *cn = pc_i18n_cn("遊戲");
    CHECK(cn && !strcmp(cn, "游戏"));                      /* a data row: the mainland name, not a character conversion */
    cn = pc_i18n_cn("藍牙喇叭");
    CHECK(cn && !strcmp(cn, "蓝牙音箱"));
    s_cur = PC_LANG_TW; CHECK(!strcmp(TZ("遊戲"), "遊戲"));
    s_cur = PC_LANG_EN; CHECK(!strcmp(TZ("遊戲"), "遊戲"));   /* English is chosen by the caller (zh ? TZ(..) : en) */
    s_cur = PC_LANG_CN; CHECK(!strcmp(TZ("遊戲"), "游戏"));
    CHECK(!strcmp(TZ("這句話不在表裡"), "這句話不在表裡"));   /* a literal without a row stays as it is */
    s_cur = PC_LANG_TW;
}

int main(void)
{
    RUN(test_i18n_data);
    RUN(test_i18n_lang);
    RUN(test_i18n_pick);
    return 0;
}
