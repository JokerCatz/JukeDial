// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"
/* The theme's colour tokens beyond pc_theme_t (ui_tokens.inc is the table: key, defaults, what and where). A palette is n colours under one name; a
 * theme file may override any entry (ui_tok_set_key). Defaults live in flash; the overrides are a copy in PSRAM, made only when a theme file sets one.
 * LVGL task only (the copy is built at boot / theme change on that task). */
typedef enum {
#define CORE(k, zh, en)
#define TOK(id, key, n, vals, zh, en) UI_PAL_##id,
#include "ui_tokens.inc"
#undef CORE
#undef TOK
    UI_PAL_COUNT
} ui_pal_id_t;
#define UI_PAL_UNSET 0xFFFFFFFFu                        /* a default meaning "follow another token" (help, help_text) */
const uint32_t *ui_pal(ui_pal_id_t id);                 /* the palette's colours as 0xRRGGBB (the override copy when a theme file set any) */
static inline uint32_t ui_tok(ui_pal_id_t id) { return ui_pal(id)[0]; }
static inline lv_color_t ui_palc(ui_pal_id_t id, int i) { return lv_color_hex(ui_pal(id)[i]); }
/* overrides: `key` is "name" (a one-colour token) or "name.i" (entry i). 0 = applied, -1 = no such key. */
int  ui_tok_set_key(const char *key, uint32_t rgb);
void ui_tok_clear(void);                                /* every colour back to the built-in default */
bool ui_tok_touched(ui_pal_id_t id);                    /* a theme file set at least one entry of this palette (else it is the built-in default) */
int  ui_tok_overrides(void);                            /* how many entries a theme file set (sim / console) */
