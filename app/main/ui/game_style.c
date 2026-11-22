// SPDX-License-Identifier: LGPL-2.1-or-later
/* The game styles table (game_style.h). */
#include "game_style.h"
#include "font.h"
#include "ui_theme.h"
#include "ui_tokens.h"

/* The shapes live here (flash); the colours are theme tokens (ui_tokens.inc: game_classic / game_neon / game_candy / game_gold / game_field), read through col() so a
 * theme file may change them; the gold copy (game_style_gold) carries its base field in its own cell and line. */
static const game_style_t STYLES[GAME_STYLES] = {
    { .zh = "經典", .en = "Classic", .theme = true, .mark = GS_MARK_GLYPH, .grid = GS_GRID_TILES, .stroke = 6, .radius_pct = 32 },
    { .zh = "霓虹", .en = "Neon", .mark = GS_MARK_STROKE, .grid = GS_GRID_LINES, .stroke = 6, .radius_pct = 0, .pulse = true },
    { .zh = "糖果", .en = "Candy", .mark = GS_MARK_SOLID, .grid = GS_GRID_TILES, .stroke = 6, .radius_pct = 60 },
};
static const ui_pal_id_t STYLE_PAL[GAME_STYLES] = { UI_PAL_game_classic, UI_PAL_game_neon, UI_PAL_game_candy };
enum { C_CELL, C_LINE, C_A, C_B, C_HI, C_FX, C_SEC };    /* the entry order of game_neon / game_candy / game_gold; C_SEC + k = sector k */

int game_style_count(void) { return GAME_STYLES; }
const game_style_t *game_style(int i) { return &STYLES[((i % GAME_STYLES) + GAME_STYLES) % GAME_STYLES]; }
const char *game_style_name(int i) { const game_style_t *s = game_style(i); return T_RAW(s->zh, s->en); }

/* A light theme (a light board with dark content, not a dark island on a light page): Neon / Candy / Gold take the page's tones for the field (cell a
 * tone of UI_FG over UI_BG, lines a darker one) and their marks / sectors are darkened toward UI_FG until they read on what they sit on (cells: the marks; the page: the
 * sectors). A dark theme: the tokens as they are. */
static uint32_t rgb_of(lv_color_t m) { return ((uint32_t)m.red << 16) | ((uint32_t)m.green << 8) | m.blue; }
static uint32_t field_of(int c, uint32_t v)
{
    if (!ui_theme_is_light()) return v;
    const lv_color_t cell = lv_color_mix(UI_FG, UI_BG, 26);
    if (c == C_CELL) return rgb_of(cell);
    if (c == C_LINE) return rgb_of(lv_color_mix(UI_FG, UI_BG, 72));
    return rgb_of(ui_hue_on(lv_color_hex(v), c >= C_SEC ? UI_BG : cell));
}

/* colour c (C_*) of style s: a STYLES entry reads its token palette; the gold copy reads game_gold (it is not in STYLES). The classic palette only has the four sectors. */
static uint32_t col(const game_style_t *s, int c)
{
    if (s->gold) return c == C_CELL ? s->cell : c == C_LINE ? s->line : field_of(c, ui_pal(UI_PAL_game_gold)[c]);   /* the gold copy keeps its base's field (derived at its creation) */
    const int i = (int)(s - STYLES);
    if (i == 0) return c >= C_SEC ? rgb_of(ui_hue_on(lv_color_hex(ui_pal(UI_PAL_game_classic)[c - C_SEC]), UI_BG)) : 0;
    return field_of(c, ui_pal(STYLE_PAL[i])[c]);
}

void game_style_gold(const game_style_t *base, game_style_t *out)
{
    *out = *base;
    out->gold = true;
    if (base->theme) { out->theme = false; out->cell = field_of(C_CELL, ui_pal(UI_PAL_game_gold)[C_CELL]); out->line = field_of(C_LINE, ui_pal(UI_PAL_game_gold)[C_LINE]); }   /* the classic one gets a neutral field for the gold */
    else { out->cell = col(base, C_CELL); out->line = col(base, C_LINE); }
}

lv_color_t game_style_sector(const game_style_t *s, int k) { if (!s) s = &STYLES[0]; return lv_color_hex(col(s, C_SEC + (k & 3))); }   /* a light theme: already darkened by col() */

void game_style_palette(const game_style_t *s, game_palette_t *p)
{
    if (!s) s = &STYLES[0];
    p->bg = UI_BG;
    if (s->theme) {
        const uint32_t *gb = ui_pal(UI_PAL_game_board);                 /* a theme file may give the classic board its own colours (light themes); unset = the theme's */
#define GB(i, d) (gb[i] == UI_PAL_UNSET ? (d) : lv_color_hex(gb[i]))
        p->cell = GB(0, UI_PANEL); p->line = GB(1, UI_PANEL2); p->a = GB(2, UI_A1); p->b = GB(3, UI_ON); p->hi = GB(4, UI_A3); p->fx = GB(5, UI_A1);
#undef GB
        return;
    }
    p->cell = lv_color_hex(col(s, C_CELL)); p->line = lv_color_hex(col(s, C_LINE)); p->a = lv_color_hex(col(s, C_A)); p->b = lv_color_hex(col(s, C_B));
    p->hi = lv_color_hex(col(s, C_HI)); p->fx = lv_color_hex(col(s, C_FX));
}

void game_style_field(const game_style_t *s, game_field_palette_t *f)
{
    game_palette_t p;
    const uint32_t *fp = ui_pal(UI_PAL_game_field);
    game_style_palette(s, &p);
    f->fill = p.cell; f->rim = p.line; f->track = p.line;
    f->piece = p.a; f->tough = p.b; f->power = p.hi; f->fx = p.fx;
    f->boom = ui_hue_on(lv_color_hex(fp[0]), p.cell);   /* warm: a brick that bursts (a light theme: darkened until it reads on the field) */
    f->gold = ui_hue_on(lv_color_hex(fp[1]), p.cell);
    f->warn = UI_WARN;                                /* a lost ball */
    f->player = s && !s->theme ? p.hi : ui_text_on(p.cell);        /* the paddle */
    f->ball = s && !s->theme ? ui_hue_on(lv_color_hex(fp[2]), p.cell) : ui_text_on(p.cell);
    if (s && s->gold) { f->ball = p.hi; f->gold = ui_hue_on(lv_color_hex(fp[2]), p.cell); }   /* the golden game: a gold field; its golden brick turns white */
}

lv_color_t game_style_hue(int deg) { return lv_color_hsv_to_rgb((uint16_t)(((deg % 360) + 360) % 360), 75, 100); }
