// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include "lvgl.h"
#include "i18n.h"
/* ui-tc.ttf (+ ui-sc, ui-ext) through FreeType, from the card or the flash pack (spec 2026-11-11-flash-font-design.md);
 * Montserrat when neither has it. */
void font_init(void);                 /* call with the LVGL lock held, after the display started */
void font_init_flash(void);           /* the same from the flash pack only, never /sdcard (USB mode: the card is the computer's); no pack = English */
const lv_font_t *font_main(void);     /* 20 px (24 on the square panel, UI_FONT_MAIN_PX) */
const lv_font_t *font_large(void);    /* 28 px (32 on the square panel, UI_FONT_LARGE_PX) */
const lv_font_t *font_clock(void);    /* 72 px for the clock digits (loaded on first use; 28 px without the card font) */
const lv_font_t *font_huge(void);     /* 150 px digits for the big digital clock face (first use; else font_clock) */
bool font_has_cjk(void);
const lv_font_t *font_unscii8_xn(int n);   /* lv_font_unscii_8 with every pixel drawn n x n (1..4), hard edges: the page bubble (2), the quick-settings numbers (3) */
const lv_font_t *font_icon(int px);   /* ui-icon.ttf at px (icons.def; NULL without the file); inside text the icons come from the normal fonts' chain, see icons_gen.h */
bool font_has_icons(void);
/* the language of cfg "lang" (i18n.h): T(S_xxx) = zh-TW, 簡中 or en from the generated arrays; en when no CJK font loaded */
static inline const char *T(i18n_id_t id) { return i18n_str(id); }
static inline const char *T_RAW(const char *zh, const char *en) { return i18n_raw(zh, en); }   /* not a plain literal pair (tools/gen/i18n_gen.py counts them) */
void font_info(void);                 /* console: which copy each font file uses */
void font_bench(int px);              /* console: cold glyph cost, card vs flash (LVGL context) */
/* glyphs no font has (the box LVGL draws): console `fontmiss` prints "APP fontmiss total=N list=U+XXXX,... more=M" (list=- when empty), `fontmiss clear`
 * empties it; each new code point is also printed once as "APP font miss U+XXXX". The first 16 distinct ones since boot / clear are kept (never evicted); further distinct ones only count in more=. */
void font_miss_print(void);
void font_miss_clear(void);
/* the same list for the CTL op font_miss: copies up to max code points, returns how many; *total / *more as printed */
int  font_miss_get(uint32_t *cp, int max, uint32_t *total, uint32_t *more);
