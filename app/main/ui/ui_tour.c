// SPDX-License-Identifier: LGPL-2.1-or-later
#include <stdio.h>
#include "esp_attr.h"
#include "cfg.h"
#include "font.h"
#include "screens.h"
#include "ui_geom.h"
#include "ui_kit.h"
#include "ui_qs.h"
#include "ui_theme.h"
#include "ui_tour.h"

enum { ST_UP, ST_DOWN, ST_SIDE, ST_HOLD, ST_N };
EXT_RAM_BSS_ATTR static int s_step;
EXT_RAM_BSS_ATTR static bool s_qs_seen;                 /* step 1: the quick settings were opened by the down flick and a flick up is closing them */
EXT_RAM_BSS_ATTR static bool s_done;
EXT_RAM_BSS_ATTR static lv_obj_t *s_art, *s_art_l, *s_text, *s_dots;

bool tour_needed(void) { return g_sd_ok && !cfg_has("tour_done"); }
void tour_dump(void) { printf("APP tour step=%d done=%d needed=%d\n", ui_top() == &scr_tour ? s_step : -1, cfg_has("tour_done"), tour_needed()); }

static void step_text(int st, char *b, size_t n)
{
    switch (st) {
    case ST_UP: snprintf(b, n, "%s", T(S_TOUR_UP)); break;
    case ST_DOWN: snprintf(b, n, "%s", T(S_TOUR_DOWN)); break;
    case ST_SIDE: snprintf(b, n, "%s", T(S_TOUR_SIDE)); break;
    default: snprintf(b, n, T(S_TOUR_HOLD), cfg_int("home_hold_ms", 3000) / 1000); break;
    }
}
static void show_step(void)
{
    if (!s_art) return;
    static const char *const art[ST_N] = { LV_SYMBOL_UP, LV_SYMBOL_DOWN, LV_SYMBOL_LEFT "   " LV_SYMBOL_RIGHT, "" };
    char b[96];
    lv_label_set_text(s_art_l, art[s_step]);
    lv_obj_set_style_border_width(s_art, s_step == ST_HOLD ? 4 : 0, 0);          /* the hold step: a disc to press and keep pressed */
    step_text(s_step, b, sizeof b);
    lv_label_set_text(s_text, b);
    ui_dots_set(s_dots, ST_N, s_step, UI_A1);
}
static void finish(void)
{
    if (s_done) return;
    s_done = true;
    cfg_set_int("tour_done", 1);
    ui_gesture_hook(NULL);
    printf("APP tour done\n");
    ui_pop();
}
static void advance(void)
{
    if (++s_step >= ST_N) { finish(); return; }
    s_qs_seen = false;
    show_step();
}
/* the gesture comes first to the page (ui.c): true = taken, nothing else happens. Step 1 lets the down flick and the up flick run (the real quick settings open and close) and moves on
 * when the page is back on top (on_resume). */
static bool gesture(ui_gesture_t g)
{
    if (ui_top() != &scr_tour) {                                  /* the quick settings are on top: their closing flick is the step's end */
        if (s_step == ST_DOWN && ui_qs_is(ui_top()) && g == UI_G_UP) s_qs_seen = true;
        if (g == UI_G_HOLD && !s_done) { s_done = true; cfg_set_int("tour_done", 1); ui_gesture_hook(NULL); printf("APP tour done\n"); }   /* M6: the hold over the open quick settings goes home through both: the tour is still finished */
        return false;
    }
    if (g == UI_G_HOLD) { finish(); return true; }
    switch (s_step) {
    case ST_UP: if (g == UI_G_UP) advance(); break;
    case ST_DOWN: if (g == UI_G_DOWN) return false; break;       /* opens the quick settings for real */
    case ST_SIDE: if (g == UI_G_LEFT || g == UI_G_RIGHT) advance(); break;
    default: break;
    }
    return true;                                                  /* every other gesture is ignored here: nothing leaves the tutorial by accident */
}
static void skip_cb(lv_event_t *e) { (void)e; finish(); }

static void create(lv_obj_t *p, void *arg)
{
    (void)arg;
    s_done = false;
    s_step = ST_UP;
    s_qs_seen = false;
    s_dots = ui_dots(p, UI_SLOT_TITLE, ST_N);
    s_art = lv_obj_create(p);
    lv_obj_remove_style_all(s_art);
    lv_obj_set_size(s_art, ui_scale(96), ui_scale(96));
    lv_obj_align(s_art, LV_ALIGN_CENTER, 0, ui_scale(-44));
    lv_obj_remove_flag(s_art, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(s_art, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_color(s_art, UI_A1, 0);
    s_art_l = lv_label_create(s_art);
    lv_obj_set_style_text_font(s_art_l, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(s_art_l, UI_A1, 0);
    lv_obj_center(s_art_l);
    s_text = ui_text(p, UI_SLOT_LOWER, NULL);
    lv_obj_set_style_text_align(s_text, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_t *l;
    lv_obj_t *b = ui_button(p, 100, UI_TAP_MIN, 0, UI_ROUND ? 128 : 190, skip_cb, NULL, &l);
    lv_label_set_text(l, T(S_TOUR_SKIP) /* Skip */);
    lv_obj_set_style_bg_color(b, UI_PANEL2, 0);
    ui_gesture_hook(gesture);
    show_step();
}
static void destroy(void)
{
    ui_gesture_hook(NULL);                                        /* leaving by any way: the recogniser is the system's again */
    s_art = s_art_l = s_text = s_dots = NULL;
}
static void resume(void) { if (s_qs_seen && s_step == ST_DOWN) advance(); }

const ui_screen_t scr_tour = { .name = "tour", .create = create, .destroy = destroy, .on_resume = resume };
