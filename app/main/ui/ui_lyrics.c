// SPDX-License-Identifier: LGPL-2.1-or-later
/* The lyrics view piece: see ui_lyrics.h. Raw LVGL by design (a piece). The objects: a transparent holder, three flex bands, up to five labels.
 * The holder keeps the side count in its user data. */
#include <string.h>
#include "font.h"
#include "ui_geom.h"
#include "ui_kit.h"
#include "ui_lyrics.h"
#include "ui_theme.h"

/* the holder's children: top band, middle band, bottom band; a band's children are its labels (the top band: [far,] near; the bottom: near [, far]) */
static lv_obj_t *label_at(lv_obj_t *h, int i)   /* i = t index 0..4 */
{
    if (!h || lv_obj_get_child_count(h) != 3) return NULL;
    lv_obj_t *top = lv_obj_get_child(h, 0), *mid = lv_obj_get_child(h, 1), *bot = lv_obj_get_child(h, 2);
    const int side = (int)lv_obj_get_child_count(top);
    if (i == 2) return lv_obj_get_child(mid, 0);
    if (i < 2) return side == 2 ? lv_obj_get_child(top, i) : (i == 1 ? lv_obj_get_child(top, 0) : NULL);
    return i == 3 ? lv_obj_get_child(bot, 0) : (side == 2 ? lv_obj_get_child(bot, 1) : NULL);
}

static lv_obj_t *band(lv_obj_t *parent, int w, int h, lv_flex_align_t main_place)
{
    lv_obj_t *b = lv_obj_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, w, h);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(b, main_place, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(b, 4, 0);
    return b;
}

/* a label as tall as the whole rows its band can hold (at most max_rows: with two lines each side, one row each), long text wraps and ends with dots at the last row (a long line must not run out of its band) */
static lv_obj_t *line(lv_obj_t *band_obj, int w, int band_h, int max_rows, const lv_font_t *f, lv_color_t c)
{
    lv_obj_t *l = lv_label_create(band_obj);
    const int lh = lv_font_get_line_height(f);
    int rows = band_h / lh > 0 ? band_h / lh : 1;
    if (rows > max_rows) rows = max_rows;
    lv_obj_set_size(l, w, rows * lh);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    lv_label_set_text(l, "");
    lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
    return l;
}

lv_obj_t *ui_lyrics(lv_obj_t *parent, int side)
{
    if (side < 1) side = 1;
    if (side > 2) side = 2;
    const int w = ui_by_panel(250, 400), hs = ui_by_panel(side == 1 ? 64 : 60, side == 1 ? 110 : 90), hm = ui_by_panel(112, 140);
    lv_obj_t *h = lv_obj_create(parent);
    lv_obj_remove_style_all(h);
    lv_obj_set_size(h, w, 2 * hs + hm);
    lv_obj_align(h, LV_ALIGN_CENTER, 0, 0);
    lv_obj_remove_flag(h, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(h, LV_FLEX_FLOW_COLUMN);
    lv_obj_t *top = band(h, w, hs, LV_FLEX_ALIGN_END), *mid = band(h, w, hm, LV_FLEX_ALIGN_CENTER), *bot = band(h, w, hs, LV_FLEX_ALIGN_START);
    const lv_color_t dim = UI_DIM, far = lv_color_mix(UI_DIM, UI_BG, 160);
    const lv_font_t *fs = font_main(), *fc = font_large();
    if (side == 2) line(top, w, hs, 1, fs, far);
    line(top, w, hs, side == 1 ? 2 : 1, fs, dim);
    line(mid, w, hm, 3, fc, UI_FG);
    line(bot, w, hs, side == 1 ? 2 : 1, fs, dim);
    if (side == 2) line(bot, w, hs, 1, fs, far);
    return h;
}

void ui_lyrics_set(lv_obj_t *l, const char *const t[5])
{
    if (!l || !t) return;
    for (int i = 0; i < 5; i++) {
        lv_obj_t *lab = label_at(l, i);
        if (!lab) continue;
        const char *s = t[i] ? t[i] : "";
        if (strcmp(lv_label_get_text(lab), s)) lv_label_set_text(lab, s);
        if (i == 2) lv_obj_set_style_text_color(lab, UI_FG, 0);
    }
}

void ui_lyrics_note(lv_obj_t *l, const char *text)
{
    if (!l) return;
    for (int i = 0; i < 5; i++) { lv_obj_t *lab = label_at(l, i); if (lab && i != 2) lv_label_set_text(lab, ""); }
    lv_obj_t *m = label_at(l, 2);
    if (!m) return;
    lv_label_set_text(m, text ? text : "");
    lv_obj_set_style_text_color(m, UI_DIM, 0);
}

int ui_lyrics_side(const lv_obj_t *l) { return l && lv_obj_get_child_count(l) == 3 ? (int)lv_obj_get_child_count(lv_obj_get_child(l, 0)) : 0; }
