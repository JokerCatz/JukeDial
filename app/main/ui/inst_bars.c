// SPDX-License-Identifier: LGPL-2.1-or-later
/* The instrument's voice bars (header inst_bars.h). Geometry as on the old play page: 184 x 40, bars up to 30 wide, gaps 10 (6 above 5 voices). */
#include "inst_bars.h"
#include "ui_theme.h"

#define BARS_W 184
#define BARS_H 40
#define MAXV 8

static lv_obj_t *s_box;
static int s_n, s_w, s_gap, s_x0;
static uint8_t s_h[MAXV];                     /* drawn heights; 255 = not drawn yet (drawn as the 4 px floor) */

uint32_t inst_voice_rgb(int v)
{
    const uint32_t *extra = ui_pal(UI_PAL_inst_voices);
    const pc_theme_t *t = ui_theme();
    return v == 0 ? t->a1 : v == 1 ? t->a2 : v == 2 ? t->a3 : extra[(v - 3) % 6];
}

static void draw(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t box;
    lv_obj_get_coords(s_box, &box);
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.radius = 4;
    for (int i = 0; i < s_n; i++) {
        lv_area_t a = { box.x1 + s_x0 + i * (s_w + s_gap), box.y1, 0, box.y2 };
        a.x2 = a.x1 + s_w - 1;
        lv_color_t c = lv_color_hex(inst_voice_rgb(i));
        d.bg_color = lv_color_mix(c, UI_BG, 40);
        lv_draw_rect(layer, &d, &a);
        a.y1 = a.y2 - (s_h[i] == 255 ? 4 : s_h[i]) + 1;
        d.bg_color = c;
        lv_draw_rect(layer, &d, &a);
    }
}

static void deleted(lv_event_t *e) { (void)e; s_box = NULL; }

lv_obj_t *inst_bars_create(lv_obj_t *parent, ui_slot_t s)
{
    s_box = lv_obj_create(parent);
    lv_obj_remove_style_all(s_box);
    lv_obj_set_size(s_box, BARS_W, BARS_H);
    lv_obj_align(s_box, LV_ALIGN_CENTER, 0, ui_slot_y(s));
    lv_obj_remove_flag(s_box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_box, draw, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_add_event_cb(s_box, deleted, LV_EVENT_DELETE, NULL);
    s_n = 0;
    return s_box;
}

void inst_bars_build(int n)
{
    if (n > MAXV) n = MAXV;
    if (n < 0) n = 0;
    for (int i = 0; i < MAXV; i++) s_h[i] = 255;
    s_gap = n > 5 ? 6 : 10;
    s_w = n ? (BARS_W - (n - 1) * s_gap) / n : 0;
    if (s_w > 30) s_w = 30;
    s_x0 = (BARS_W - (n * s_w + (n - 1) * s_gap)) / 2;
    s_n = n;
    if (s_box) lv_obj_invalidate(s_box);
}

void inst_bars_set(int i, uint8_t level)
{
    if (i < 0 || i >= s_n || !s_box) return;
    uint8_t h = (uint8_t)(4 + level * 36 / 255);
    if (s_h[i] != 255 && h < s_h[i]) h = (uint8_t)(s_h[i] - 4 > h ? s_h[i] - 4 : h);   /* peak hold: a square wave reads 0 half the time */
    if (h == s_h[i]) return;
    s_h[i] = h;
    lv_obj_invalidate(s_box);                 /* the same area every time: LVGL keeps one */
}
