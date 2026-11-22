// SPDX-License-Identifier: LGPL-2.1-or-later
#include "ui_theme.h"
#include "ui_geom.h"
#include "font.h"
#include "screens.h"
#include "audio_svc.h"
#include "sleep_timer.h"
#include "ui_kit.h"
#include "ui_frame.h"

const char *ui_word_knob(void) { return UI_HAS_KNOB ? T(S_KNOB) /* knob */ : T(S_KEYS) /* keys */; }

static void back_cb(lv_event_t *e) { (void)e; ui_pop(); }
static void do_cb(lv_event_t *e) { void (*f)(void) = (void (*)(void))lv_event_get_user_data(e); if (f) f(); }
static void hold_back_cb(lv_event_t *e) { if (lv_event_get_code(e) == LV_EVENT_PRESSED) ui_hold_start(); else ui_hold_stop(); }

/* the one Back button: UI_BACK_W x UI_BACK_H, centre y from the parent's centre; its action is `cb` (pop by default); held = the main menu (every Back button, one place) */
static lv_obj_t *back_button(lv_obj_t *parent, int y, lv_event_cb_t cb, void *user)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_size(b, UI_BACK_W, UI_BACK_H);
    lv_obj_align(b, LV_ALIGN_CENTER, 0, y);
    lv_obj_set_style_bg_color(b, UI_SYS, 0);          /* system button: its own colour */
    if (ui_theme_is_light()) lv_obj_set_style_text_color(b, UI_FG, 0);   /* LVGL's button text is light in either mode */
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);
    lv_obj_add_event_cb(b, hold_back_cb, LV_EVENT_PRESSED, NULL);        /* the escape hatch: a hold of cfg home_hold_ms (ui.c) */
    lv_obj_add_event_cb(b, hold_back_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_t *l = lv_label_create(b);
    const bool framed = ui_frame_active(parent);
    lv_label_set_text(l, framed ? T(S_LEAVE) /* Leave: inside the popup card it leaves the page; the card has its own Close */ : T(S_BACK) /* Back */);
    lv_obj_center(l);
    ui_frame_back(b);                                 /* a page with a frame (ui_frame.h): this pill is overlay chrome */
    if (!framed) lv_obj_add_flag(b, LV_OBJ_FLAG_HIDDEN);   /* GS: no Back on a page without the popup: the up flick clicks it (ui.c ui_back_set) */
    ui_back_set(b);
    return b;
}

lv_obj_t *ui_back_button(lv_obj_t *parent) { return back_button(parent, UI_BACK_Y, back_cb, NULL); }
lv_obj_t *ui_back_button_do(lv_obj_t *parent, void (*cb)(void)) { return back_button(parent, UI_BACK_Y, do_cb, (void *)cb); }
lv_obj_t *ui_back_button_hub(lv_obj_t *parent) { return back_button(parent, UI_HUB_BACK_Y, back_cb, NULL); }

/* ---- rim arcs (spec 5.7): track progress on the outermost ring, sleep timer just inside it ---- */
static lv_obj_t *rim_arc(lv_obj_t *p, int size, lv_color_t c)
{
    lv_obj_t *a = lv_arc_create(p);
    lv_obj_set_size(a, size, size);
    lv_obj_center(a);
    lv_obj_remove_style(a, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(a, LV_OBJ_FLAG_CLICKABLE);
    lv_arc_set_bg_angles(a, 0, 360);
    lv_arc_set_rotation(a, 270);                    /* starts at the top, runs clockwise */
    lv_arc_set_range(a, 0, 1000);
    lv_arc_set_value(a, 0);
    lv_obj_set_style_arc_width(a, UI_RIM_W, LV_PART_MAIN);
    lv_obj_set_style_arc_width(a, UI_RIM_W, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(a, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_color(a, c, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(a, false, LV_PART_INDICATOR);
    lv_obj_add_flag(a, LV_OBJ_FLAG_HIDDEN);
    return a;
}
static void arc_show(lv_obj_t *a, int permille)
{
    if (permille < 0) { lv_obj_add_flag(a, LV_OBJ_FLAG_HIDDEN); return; }
    lv_obj_remove_flag(a, LV_OBJ_FLAG_HIDDEN);
    if (lv_arc_get_value(a) != permille) lv_arc_set_value(a, permille);
}
static void progress_tick(lv_timer_t *t)
{
    lv_obj_t *a = lv_timer_get_user_data(t);
    if (!ui_screen_on()) return;
    aud_status_t st;
    audio_svc_status(&st);
    bool known = st.state == AUD_PLAYING && st.len_ms > 0;
    arc_show(a, known ? (int)((int64_t)st.pos_ms * 1000 / st.len_ms) : -1);   /* unknown length: no arc */
}
lv_obj_t *ui_progress_arc(lv_obj_t *p)
{
    lv_obj_t *a = rim_arc(p, UI_RIM_PROGRESS_D, UI_A1);
    lv_timer_t *t = ui_timer_bind(a, progress_tick, 1000, a);
    if (t) progress_tick(t);
    return a;
}

static void sleep_tick(lv_timer_t *t)
{
    lv_obj_t *a = lv_timer_get_user_data(t);
    int left = sleep_timer_left_s(), total = sleep_timer_minutes() * 60;
    arc_show(a, left >= 0 && total > 0 ? (int)((int64_t)left * 1000 / total) : -1);
}
lv_obj_t *ui_sleep_arc(lv_obj_t *p)
{
    lv_obj_t *a = rim_arc(p, UI_RIM_SLEEP_D, UI_A2);
    lv_timer_t *t = ui_timer_bind(a, sleep_tick, 1000, a);
    if (t) sleep_tick(t);
    return a;
}

/* ---- GS: the page-count bubble (rings, grids): small, not clickable, only when there is more than one page ---- */
lv_obj_t *ui_page_bubble(lv_obj_t *parent, lv_align_t align, int x, int y)
{
    lv_obj_t *b = lv_obj_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(b, UI_PANEL, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_border_color(b, UI_DIM, 0);
    lv_obj_set_style_pad_hor(b, 7, 0);
    lv_obj_set_style_pad_ver(b, 2, 0);
    lv_obj_set_style_text_font(b, UI_PAGE_BUBBLE_FONT, 0);
    lv_obj_set_style_text_color(b, UI_FG, 0);
    lv_obj_align(b, align, x, y);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, "");
    lv_obj_add_flag(b, LV_OBJ_FLAG_HIDDEN);
    return b;
}
int ui_page_bubble_h(void) { return lv_font_get_line_height(UI_PAGE_BUBBLE_FONT) + 2 * 2 + 2; }   /* content + padding + border */
void ui_page_bubble_set(lv_obj_t *b, int page, int pages)
{
    if (!b) return;
    if (pages <= 1) { lv_obj_add_flag(b, LV_OBJ_FLAG_HIDDEN); return; }
    lv_label_set_text_fmt(lv_obj_get_child(b, 0), "%d-%d", page + 1, pages);   /* "1-2", not "1/2" */
    lv_obj_remove_flag(b, LV_OBJ_FLAG_HIDDEN);
}
