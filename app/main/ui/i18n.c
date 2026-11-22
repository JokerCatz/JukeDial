// SPDX-License-Identifier: LGPL-2.1-or-later
#include "i18n.h"
#include <stdio.h>
#include "cfg.h"
#include "esp_attr.h"
#include "font.h"
#include "ui.h"

EXT_RAM_BSS_ATTR static volatile uint8_t s_lang;   /* pc_lang_t; 0 = Traditional Chinese until i18n_load; one byte in PSRAM, no internal RAM */

pc_lang_t i18n_lang(void) { return pc_lang_norm(s_lang); }
bool i18n_zh(void) { return pc_lang_zh(i18n_lang(), font_has_cjk()); }

const char *i18n_str(i18n_id_t id)
{
    if ((unsigned)id >= (unsigned)S__COUNT) return "";
    return pc_i18n_text(i18n_lang(), font_has_cjk(), I18N_TW[id], I18N_CN[id], I18N_EN[id]);
}

const char *i18n_raw(const char *zh_tw, const char *en) { return pc_i18n_text(i18n_lang(), font_has_cjk(), zh_tw, i18n_lang() == PC_LANG_CN ? pc_i18n_cn(zh_tw) : NULL, en); }

pc_lang_t pc_i18n_cur(void) { return i18n_lang(); }   /* overrides the weak default of pc_i18n.c: TZ() in the pc_* components follows the UI language */

void i18n_load(void) { s_lang = (uint8_t)pc_lang_norm(cfg_int("lang", PC_LANG_TW)); }

void i18n_set_lang(pc_lang_t l)
{
    if (l == i18n_lang()) return;
    s_lang = (uint8_t)l;
    cfg_set_int("lang", (int)l);
    ui_rebuild_all();
}

const char *i18n_lang_name(pc_lang_t l)
{
    static const char *const nat[] = { "繁體中文", "简体中文", "English" }, *const ascii[] = { "TC", "SC", "English" };
    return (font_has_cjk() ? nat : ascii)[pc_lang_norm(l)];
}

const char *i18n_lang_label(void) { return i18n_lang_name(i18n_lang()); }

void i18n_choose_lang(pc_lang_t l)
{
    s_lang = (uint8_t)pc_lang_norm(l);
    cfg_set_int("lang", (int)s_lang);
    if (cfg_save_now() != 0) printf("APP lang cfg write failed: the picker comes again at the next boot\n");   /* written at once: a first-boot choice must not wait 3 s for a power cut */
}
