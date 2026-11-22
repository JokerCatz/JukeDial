// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "pc/pc_i18n.h"
#include "i18n_strings.h"
/* The UI language (i18n phase A): cfg "lang" 0 Traditional Chinese (the default) / 1 Simplified Chinese / 2 English, switched at run time (every screen is built again, no reboot).
 * A text is a string id: T(S_xxx) (font.h) indexes the three generated arrays (i18n_strings.c, from tools/gen/i18n/strings.tsv). T_RAW(zh_tw, en) is the
 * escape hatch for a text that is no plain literal pair (no Simplified Chinese text: zh-TW shows); tools/gen/i18n_gen.py counts them.
 * The language lives in one byte (PSRAM) that i18n_set_lang / i18n_load write: T() may be read from any task (a byte read is atomic); the setters run on the LVGL task. */
pc_lang_t i18n_lang(void);
bool i18n_zh(void);                          /* Chinese text wanted (a CJK font is loaded and the language is not English): the bool the pc_* label functions take as `zh` */
const char *i18n_str(i18n_id_t id);          /* T(id) */
const char *i18n_raw(const char *zh_tw, const char *en);   /* T_RAW(zh_tw, en) */
void i18n_load(void);                        /* the language byte from cfg "lang": cfg_load calls it, and a raw cfg write of the key */
void i18n_set_lang(pc_lang_t l);             /* stores it (byte + cfg) and rebuilds every screen (the theme way) */
void i18n_choose_lang(pc_lang_t l);          /* the first-boot picker: stores the choice (byte + cfg key, also when it is the default) and nothing else; the caller rebuilds */
const char *i18n_lang_name(pc_lang_t l);     /* a language's own name (繁體中文 / 简体中文 / English; TC / SC without a CJK font) */
const char *i18n_lang_label(void);           /* the language's own name, the same text in every language */
