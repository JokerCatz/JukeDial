// SPDX-License-Identifier: LGPL-2.1-or-later
/* Clock faces (spec 2026-11-10-clock-design.md section 5): classic and minimal analog, big digital, sun dial, rings.
 * The dial is drawn once; a moving part invalidates only the small boxes it covers (pc_cf_hand_boxes), so a sweeping
 * second hand redraws a few thousand pixels, not the screen. Colours come from the theme. */
#include "clock_face.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "misc/lv_area_private.h"
#include "pc/pc_clockface.h"
#include "pc/pc_lowmem.h"
#include "pc/pc_power.h"
#include "time/clock.h"
#include "time/moon.h"
#include "time/sun.h"
#include "cfg.h"
#include "clock_svc.h"
#include "font.h"
#include "lowmem.h"
#include "power_svc.h"
#include "ui.h"
#include "ui_theme.h"
#include "ui_kit.h"
#include "ui_geom.h"
#include "tface.h"

/* The faces are designed on a 360 x 360 box. Live drawing (the hands, the seconds dots, the sun ring) is in screen coordinates, so the centre moves with the box on a bigger panel
 * (s_ox, s_oy = the box's top left); the baked picture (bake) draws in its own coordinates, origin 0. */
static EXT_RAM_BSS_ATTR int s_ox, s_oy;
#define Z UI_SCALE
#define CX (UI_CONTENT_D / 2 + s_ox)
#define CY (UI_CONTENT_D / 2 + s_oy)
enum { F_INFO, F_CLASSIC, F_MINIMAL, F_DIGITAL, F_SUN, F_RINGS, F_MOON, F_HUD };   /* F_HUD: tface.c / face_art.c (a designed face per feature: shared with the pomodoro, stopwatch and countdown) */

int clock_face_current(void) { return pc_cf_parse(cfg_str("clockface", "hud")); }
void clock_face_set(int idx) { cfg_set_str("clockface", pc_cf_name(idx)); }
const char *clock_face_label(int idx)
{
    static const char *const zh[] = { "資訊", "經典", "極簡", "大數字", "日月", "圓環", "月相" };
    static const char *const en[] = { "Info", "Classic", "Minimal", "Digital", "Sun & moon", "Rings", "Moon" };
    if (idx == F_HUD) return T(S_TF_HUD) /* HUD */;
    return idx >= 0 && idx < 7 ? T_RAW(zh[idx], en[idx]) : "";
}

/* ---- the moon: RGB565 with the limb blended into the page background, written straight into the buffer
 * (half the PSRAM of ARGB and one invalidation instead of one per pixel) ---- */
static void moon_free(lv_event_t *e) { heap_caps_free(lv_event_get_user_data(e)); }
lv_obj_t *clock_moon_create(lv_obj_t *p, int px)
{
    uint8_t *buf = heap_caps_malloc((size_t)px * px * 2, MALLOC_CAP_SPIRAM);
    if (!buf) return NULL;
    lv_obj_t *c = lv_canvas_create(p);
    lv_canvas_set_buffer(c, buf, px, px, LV_COLOR_FORMAT_RGB565);
    lv_obj_add_event_cb(c, moon_free, LV_EVENT_DELETE, buf);
    return c;
}
void clock_moon_draw(lv_obj_t *c, int64_t utc)
{
    if (!c) return;
    lv_draw_buf_t *db = lv_canvas_get_draw_buf(c);
    int px = db->header.w;                                   /* the object's size is not laid out yet */
    uint16_t *o = (uint16_t *)db->data;
    double phase = 2 * M_PI * (double)moon_age_s(utc) / (double)MOON_SYNODIC_S, k = cos(phase), r = px / 2.0 - 2.5;
    lv_color_t lit = ui_palc(UI_PAL_moon, 0), dark = ui_palc(UI_PAL_moon, 1), bg = UI_BG, rim = lv_color_mix(UI_FG, bg, 70);
    if (ui_theme_is_light()) {                               /* on a light page the moon is dark ink, its shadow side a shade darker than the page */
        lit = lv_color_mix(UI_FG, bg, 230); dark = lv_color_mix(UI_FG, bg, 25); rim = lv_color_mix(UI_FG, bg, 90);
    }
    for (int y = 0; y < px; y++)
        for (int x = 0; x < px; x++) {
            double dx = x - px / 2.0 + 0.5, dy = y - px / 2.0 + 0.5, d = sqrt(dx * dx + dy * dy) - r;
            lv_color_t col;
            if (d > 0) {                        /* a faint rim (a new moon is still a moon), soft on both edges */
                double f = d < 1 ? d : d < 2 ? 1 : d < 3 ? 3 - d : 0;
                col = lv_color_mix(rim, d < 1 ? dark : bg, (uint8_t)(255 * f));
            } else {
                double w = sqrt(r * r - dy * dy);
                bool on = phase < M_PI ? dx > w * k : dx < -w * k;    /* waxing: right side; waning: left side */
                col = on ? lit : dark;
            }
            o[y * (db->header.stride / 2) + x] = lv_color_to_u16(col);
        }
    lv_obj_invalidate(c);
}

/* ---- state ---- */
static int s_face;
static lv_obj_t *s_dial, *s_top, *s_msg, *s_l[6], *s_moon, *s_body, *s_halo, *s_arc[3];
static lv_timer_t *s_t;
static pc_cf_hands_t s_hd;
static clock_civil_t s_v;
static int s_ms, s_last_sec = -1, s_last_min = -1, s_sec_shown;   /* s_sec_shown: second drawn, -1 none */
static bool s_valid, s_still;
static int s_rise, s_set, s_polar, s_sun_a = -1, s_moon_day = -1;       /* sun face, recomputed each minute */
static int64_t s_moon_hour = -1;                                                   /* moon face: the UTC hour the disc shows */
static uint32_t s_ticks, s_tick_us, s_tick_max;

static lv_obj_t *face_label(lv_obj_t *p, const lv_font_t *f, lv_color_t c, int x, int y)
{
    lv_obj_t *l = lv_label_create(p);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_letter_space(l, UI_TEXT_SPACE, 0);
    lv_label_set_text(l, "");
    lv_obj_align(l, LV_ALIGN_CENTER, Z(x), Z(y));
    return l;
}
static lv_obj_t *layer_obj(lv_obj_t *p, lv_event_cb_t draw)
{
    lv_obj_t *o = lv_obj_create(p);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, UI_CONTENT_D, UI_CONTENT_D);    /* the faces' box: the panel's content box (360 round, 460 square) */
    lv_obj_center(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    if (draw) lv_obj_add_event_cb(o, draw, LV_EVENT_DRAW_MAIN, NULL);
    return o;
}
static void inv_box(lv_obj_t *o, int x1, int y1, int x2, int y2)
{
    lv_area_t a = { x1, y1, x2, y2 };
    lv_obj_invalidate_area(o, &a);
}
/* A hand moving from angle a to b (-1: not drawn there): one box per segment, old and new joined when they
 * overlap. Each area costs ~1.9 ms of flush overhead on this panel and a pixel ~0.4 us (measured 2026-11-11), so
 * few boxes beat tight ones. */
static int box_px(const pc_cf_box_t *x) { return (x->x2 - x->x1 + 1) * (x->y2 - x->y1 + 1); }
static void inv_hand(int a, int b, int r0, int r1, int hw, int nseg)
{
    pc_cf_box_t o[6] = { 0 }, n[6] = { 0 };   /* only [0..no) and [0..nn) are read; zeroed so the analyzer sees that too */
    int no = a >= 0 ? pc_cf_hand_boxes(CX, CY, a, r0, r1, hw, nseg, o, 6) : 0;
    int nn = b >= 0 ? pc_cf_hand_boxes(CX, CY, b, r0, r1, hw, nseg, n, 6) : 0;
    for (int i = 0; i < 6; i++) {
        if (i < no && i < nn) {
            pc_cf_box_t u = { LV_MIN(o[i].x1, n[i].x1), LV_MIN(o[i].y1, n[i].y1), LV_MAX(o[i].x2, n[i].x2), LV_MAX(o[i].y2, n[i].y2) };
            if (box_px(&u) * 2 < (box_px(&o[i]) + box_px(&n[i])) * 3) { inv_box(s_top, u.x1, u.y1, u.x2, u.y2); continue; }
        }
        if (i < no) inv_box(s_top, o[i].x1, o[i].y1, o[i].x2, o[i].y2);
        if (i < nn) inv_box(s_top, n[i].x1, n[i].y1, n[i].x2, n[i].y2);
    }
}
static void inv_dot(int a, int r, int rad)
{
    int x, y;
    pc_cf_point(CX, CY, a, r, &x, &y);
    inv_box(s_top, x - rad - 2, y - rad - 2, x + rad + 2, y + rad + 2);
}
static bool clip_hit(lv_layer_t *l, int x1, int y1, int x2, int y2)
{
    lv_area_t a = { x1, y1, x2, y2 }, t;
    return lv_area_intersect(&t, &l->_clip_area, &a);
}
static void line(lv_layer_t *l, int a, int r0, int r1, int w, lv_color_t c, bool round)
{
    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    int x0, y0, x1, y1;
    pc_cf_point(CX, CY, a, r0, &x0, &y0);
    pc_cf_point(CX, CY, a, r1, &x1, &y1);
    int pad = w / 2 + 2;
    if (!clip_hit(l, LV_MIN(x0, x1) - pad, LV_MIN(y0, y1) - pad, LV_MAX(x0, x1) + pad, LV_MAX(y0, y1) + pad)) return;
    d.p1.x = x0; d.p1.y = y0; d.p2.x = x1; d.p2.y = y1;
    d.width = w;
    d.color = c;
    d.round_start = d.round_end = round;
    lv_draw_line(l, &d);
}
static void dot(lv_layer_t *l, int x, int y, int r, lv_color_t c, lv_opa_t opa)
{
    if (!clip_hit(l, x - r, y - r, x + r, y + r)) return;
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.radius = LV_RADIUS_CIRCLE;
    d.bg_color = c;
    d.bg_opa = opa;
    lv_area_t a = { x - r, y - r, x + r, y + r };
    lv_draw_rect(l, &d, &a);
}
static void dot_at(lv_layer_t *l, int a, int rr, int r, lv_color_t c, lv_opa_t opa)
{
    int x, y;
    pc_cf_point(CX, CY, a, rr, &x, &y);
    dot(l, x, y, r, c, opa);
}
static void arc(lv_layer_t *l, int r, int w, int a0, int a1, lv_color_t c, lv_opa_t opa, bool round)
{   /* a0..a1 in deci-degrees from 12 o'clock, clockwise */
    if (a0 == a1) return;
    lv_draw_arc_dsc_t d;
    lv_draw_arc_dsc_init(&d);
    d.center.x = CX; d.center.y = CY;
    d.radius = (uint16_t)r;
    d.width = (uint16_t)w;
    double st = fmod(a0 / 10.0 - 90.0 + 720.0, 360.0), span = (a1 - a0) / 10.0;   /* LVGL: 0 = 3 o'clock */
    if (span >= 360) { st = 0; span = 360; }
    d.start_angle = (lv_value_precise_t)st;
    d.end_angle = (lv_value_precise_t)(span >= 360 ? 360 : fmod(st + span, 360.0));
    d.color = c;
    d.opa = opa;
    d.rounded = round;
    lv_draw_arc(l, &d);
}

/* The still part of a face, drawn once into a 360x360 RGB565 picture in PSRAM (253 KB): redrawing a small area
 * under a moving hand is then a copy, not the dial drawn again (measured 2026-11-11: classic 36 -> see the log).
 * Without the memory: drawn live. */
static void (*s_live)(lv_layer_t *);
static void live_draw(lv_event_t *e) { if (s_live) s_live(lv_event_get_layer(e)); }
static void bake_free(lv_event_t *e) { heap_caps_free(lv_event_get_user_data(e)); }
static lv_obj_t *bake(lv_obj_t *p, void (*draw)(lv_layer_t *))
{
    uint16_t *buf = heap_caps_malloc(UI_CONTENT_D * UI_CONTENT_D * 2, MALLOC_CAP_SPIRAM);
    if (!buf) { s_live = draw; return layer_obj(p, live_draw); }
    lv_obj_t *c = lv_canvas_create(p);
    lv_canvas_set_buffer(c, buf, UI_CONTENT_D, UI_CONTENT_D, LV_COLOR_FORMAT_RGB565);
    lv_obj_center(c);
    lv_obj_add_event_cb(c, bake_free, LV_EVENT_DELETE, buf);
    lv_canvas_fill_bg(c, UI_BG, LV_OPA_COVER);
    lv_layer_t l;
    lv_canvas_init_layer(c, &l);
    const int ox = s_ox, oy = s_oy;
    s_ox = s_oy = 0;                                   /* the picture's own coordinates */
    draw(&l);
    s_ox = ox; s_oy = oy;
    lv_canvas_finish_layer(c, &l);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_CLICKABLE);
    return c;
}

/* ---- classic: dial with 60 ticks and numerals, three hands, sweeping seconds ---- */
static void classic_dial(lv_layer_t *l)
{
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.radius = LV_RADIUS_CIRCLE;
    d.bg_color = UI_PANEL;
    d.border_color = UI_PANEL2;
    d.border_width = Z(3);
    lv_area_t a = { CX - Z(172), CY - Z(172), CX + Z(172), CY + Z(172) };
    lv_draw_rect(l, &d, &a);
    for (int i = 0; i < 60; i++) {
        if (i % 5) line(l, i * 60, Z(154), Z(164), Z(2), UI_DIM, false);
        else line(l, i * 60, Z(138), Z(164), Z(i ? 6 : 8), i ? UI_FG : UI_A1, false);
    }
    lv_draw_label_dsc_t t;
    lv_draw_label_dsc_init(&t);
    t.font = font_large();
    t.color = UI_FG;
    t.align = LV_TEXT_ALIGN_CENTER;
    char n[4];
    t.text = n;
    t.text_local = 1;
    int lh = lv_font_get_line_height(t.font);
    for (int h = 1; h <= 12; h++) {
        int x, y;
        pc_cf_point(CX, CY, h * 300, Z(114), &x, &y);
        snprintf(n, sizeof n, "%d", h);
        lv_area_t a = { x - Z(24), y - lh / 2, x + Z(24), y + lh / 2 };
        lv_draw_label(l, &t, &a);
    }
}
static void classic_hands(lv_event_t *e)
{
    lv_layer_t *l = lv_event_get_layer(e);
    if (!s_valid) return;
    line(l, s_hd.hour, Z(-16), Z(86), Z(10), UI_FG, true);
    line(l, s_hd.hour, Z(22), Z(76), Z(3), UI_PANEL, true);               /* the slot down the middle */
    line(l, s_hd.minute, Z(-20), Z(136), Z(7), UI_FG, true);
    line(l, s_hd.minute, Z(24), Z(126), Z(2), UI_PANEL, true);
    if (s_sec_shown >= 0) {
        line(l, s_hd.second, Z(-34), Z(150), Z(2), UI_A1, true);
        line(l, s_hd.second, Z(-34), Z(-14), Z(6), UI_A1, true);           /* counterweight */
    }
    dot(l, CX, CY, Z(8), s_sec_shown >= 0 ? UI_A1 : UI_FG, LV_OPA_COVER);
    dot(l, CX, CY, Z(3), UI_PANEL, LV_OPA_COVER);
}
static void classic_create(lv_obj_t *p)
{
    s_dial = bake(p, classic_dial);
    s_l[0] = face_label(p, font_main(), UI_DIM, 0, -58);        /* time zone */
    s_l[1] = face_label(p, font_main(), UI_DIM, 0, 60);         /* date */
    s_top = layer_obj(p, classic_hands);
}

/* ---- minimal: twelve dots, thin hands, the seconds as a dot running round the rim ---- */
static void minimal_dial(lv_layer_t *l)
{
    for (int i = 0; i < 12; i++) {
        if (i == 0) line(l, 0, Z(146), Z(166), Z(5), UI_A1, true);
        else if (i % 3 == 0) dot_at(l, i * 300, Z(158), Z(5), UI_FG, LV_OPA_COVER);
        else dot_at(l, i * 300, Z(158), Z(2), UI_DIM, LV_OPA_COVER);
    }
}
static void minimal_hands(lv_event_t *e)
{
    lv_layer_t *l = lv_event_get_layer(e);
    if (!s_valid) return;
    line(l, s_hd.hour, Z(0), Z(92), Z(7), UI_FG, true);
    line(l, s_hd.minute, Z(0), Z(140), Z(3), UI_FG, true);
    dot(l, CX, CY, Z(6), UI_FG, LV_OPA_COVER);
    dot(l, CX, CY, Z(2), UI_BG, LV_OPA_COVER);
    if (s_sec_shown >= 0) dot_at(l, s_sec_shown * 60, Z(172), Z(4), UI_A1, LV_OPA_COVER);
}
static void minimal_create(lv_obj_t *p)
{
    s_dial = bake(p, minimal_dial);
    s_top = layer_obj(p, minimal_hands);
}

/* ---- digital: hours over minutes in big digits, the seconds lighting up a ring of 60 dots ---- */
static void digital_dots(lv_event_t *e)
{
    lv_layer_t *l = lv_event_get_layer(e);
    if (!s_valid) return;
    for (int i = 0; i < 60; i++) {
        bool on = s_sec_shown >= 0 && i <= s_sec_shown;
        if (on) dot_at(l, i * 60, Z(168), Z(i % 5 ? 3 : 4), UI_A1, LV_OPA_COVER);
        else dot_at(l, i * 60, Z(168), Z(i % 5 ? 2 : 3), i % 5 ? UI_PANEL2 : UI_DIM, LV_OPA_COVER);
    }
}
static void digital_create(lv_obj_t *p)
{
    s_top = layer_obj(p, digital_dots);
    s_l[0] = face_label(p, font_huge(), UI_FG, 0, -62);
    s_l[1] = face_label(p, font_huge(), UI_A1, 0, 66);
    lv_obj_set_style_text_line_space(s_l[0], 0, 0);
    s_l[2] = face_label(p, font_main(), UI_DIM, -126, 2);         /* weekday */
    s_l[3] = face_label(p, font_main(), UI_DIM, 126, 2);          /* date */
}

#define SUN_R (UI_ROUND ? 150 : Z(164))   /* the sun ring: the square panel's reaches nearly to the edge */
/* ---- sun: day on the top half of the dial, night on the bottom; the sun (or the moon) where it is now ---- */
static void sun_ring(lv_event_t *e)
{
    lv_layer_t *l = lv_event_get_layer(e);
    lv_color_t day = UI_A3, night = UI_A2;
    if (s_polar > 0) arc(l, SUN_R, Z(4), 0, 3600, day, LV_OPA_50, false);
    else if (s_polar < 0) arc(l, SUN_R, Z(4), 0, 3600, night, LV_OPA_40, false);
    else {
        arc(l, SUN_R, Z(4), 2700, 4500, day, LV_OPA_50, false);
        arc(l, SUN_R, Z(4), 900, 2700, night, LV_OPA_30, false);
        line(l, 2700, SUN_R - Z(12), SUN_R + Z(12), Z(2), UI_DIM, false);              /* horizon marks */
        line(l, 900, SUN_R - Z(12), SUN_R + Z(12), Z(2), UI_DIM, false);
    }
    if (!s_valid || s_sun_a < 0) return;
    bool up = pc_cf_is_day(s_v.hour * 60 + s_v.minute, s_rise, s_set, s_polar);
    int from = s_polar ? 0 : up ? 2700 : 900;                    /* elapsed part of this half, brighter */
    int to = s_sun_a < from ? s_sun_a + 3600 : s_sun_a;
    if (!s_polar) arc(l, SUN_R, Z(6), from, to, up ? day : night, LV_OPA_COVER, true);
}
static void sun_place(void)
{
    int x, y;
    pc_cf_point(0, 0, s_sun_a, SUN_R, &x, &y);
    bool up = pc_cf_is_day(s_v.hour * 60 + s_v.minute, s_rise, s_set, s_polar);
    if (up) {
        lv_obj_align(s_body, LV_ALIGN_CENTER, x, y);
        lv_obj_align(s_halo, LV_ALIGN_CENTER, x, y);
        lv_obj_remove_flag(s_body, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_halo, LV_OBJ_FLAG_HIDDEN);
        if (s_moon) lv_obj_add_flag(s_moon, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_body, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_halo, LV_OBJ_FLAG_HIDDEN);
        if (s_moon) { lv_obj_align(s_moon, LV_ALIGN_CENTER, x, y); lv_obj_remove_flag(s_moon, LV_OBJ_FLAG_HIDDEN); }
    }
}
static lv_obj_t *disc(lv_obj_t *p, int d, lv_color_t c, lv_opa_t opa)
{
    lv_obj_t *o = lv_obj_create(p);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, d, d);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(o, c, 0);
    lv_obj_set_style_bg_opa(o, opa, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    return o;
}
static void sun_create(lv_obj_t *p)
{
    s_top = layer_obj(p, sun_ring);
    s_halo = disc(p, Z(40), UI_A3, LV_OPA_30);
    s_body = disc(p, Z(24), UI_A3, LV_OPA_COVER);
    s_moon = clock_moon_create(p, Z(34));
    if (s_moon) lv_obj_add_flag(s_moon, LV_OBJ_FLAG_HIDDEN);
    s_l[0] = face_label(p, font_clock(), UI_FG, 0, -24);          /* time */
    s_l[1] = face_label(p, font_main(), UI_DIM, 0, 34);           /* date */
    s_l[2] = face_label(p, font_main(), UI_A3, 0, 64);            /* day length / moon phase */
    s_l[3] = face_label(p, font_main(), UI_DIM, -112, 26);        /* sunrise */
    s_l[4] = face_label(p, font_main(), UI_DIM, 112, 26);         /* sunset */
    s_l[5] = face_label(p, font_main(), UI_DIM, 0, -100);         /* time zone */
}

/* ---- rings: hours outside, minutes, seconds inside ---- */
static lv_obj_t *ring(lv_obj_t *p, int size, int w, lv_color_t c)
{
    lv_obj_t *a = lv_arc_create(p);
    lv_obj_set_size(a, size, size);
    lv_obj_center(a);
    lv_obj_remove_style(a, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(a, LV_OBJ_FLAG_CLICKABLE);
    lv_arc_set_bg_angles(a, 0, 360);
    lv_arc_set_rotation(a, 270);
    lv_arc_set_range(a, 0, 1000);
    lv_arc_set_value(a, 0);
    lv_obj_set_style_arc_width(a, w, LV_PART_MAIN);
    lv_obj_set_style_arc_width(a, w, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(a, UI_PANEL2, LV_PART_MAIN);
    lv_obj_set_style_arc_color(a, c, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(a, true, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(a, true, LV_PART_MAIN);
    return a;
}
static void rings_create(lv_obj_t *p)
{
    s_arc[0] = ring(p, Z(340), Z(14), UI_A1);
    s_arc[1] = ring(p, Z(304), Z(14), UI_A2);
    s_arc[2] = ring(p, Z(268), Z(10), UI_A3);
    s_l[0] = face_label(p, font_clock(), UI_FG, 0, -14);
    s_l[1] = face_label(p, font_main(), UI_DIM, 0, 42);
}

/* ---- updates ---- */
static const char *const WEEK[7] = { "日", "一", "二", "三", "四", "五", "六" };
static const char *const WEEK_EN[7] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
static void date_text(char *b, size_t n, bool weekday)
{
    if (i18n_zh()) {
        if (weekday) snprintf(b, n, TZ("%d月%d日 週%s"), s_v.month, s_v.day, WEEK[s_v.weekday]);
        else snprintf(b, n, "%d/%d", s_v.month, s_v.day);
    } else {
        static const char *const MON[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
        if (weekday) snprintf(b, n, "%s %d %s", WEEK_EN[s_v.weekday], s_v.day, MON[(s_v.month + 11) % 12]);
        else snprintf(b, n, "%d/%d", s_v.month, s_v.day);
    }
}
static void set_text(lv_obj_t *l, const char *t) { if (l && strcmp(lv_label_get_text(l), t)) lv_label_set_text(l, t); }

/* ---- moon: the disc (redrawn when the hour changes), the time, the phase and age, the lit part and the next full/new ---- */
static void moon_face_create(lv_obj_t *p)
{
    s_moon = clock_moon_create(p, Z(150));
    if (s_moon) lv_obj_align(s_moon, LV_ALIGN_CENTER, 0, Z(-72));
    s_l[0] = face_label(p, font_clock(), UI_FG, 0, 28);            /* time */
    s_l[1] = face_label(p, font_main(), UI_A3, 0, 72);             /* phase, age */
    s_l[2] = face_label(p, font_main(), UI_DIM, 0, 100);           /* lit, days to the next full / new moon */
}
typedef struct { int phase, tenths, lit, days, full; } moon_info_t;
static moon_info_t moon_info(int64_t now)
{
    moon_info_t m;
    m.phase = moon_phase8(now);
    m.tenths = (int)((moon_age_s(now) + 4320) / 8640);                           /* age in days, one decimal, rounded */
    m.lit = moon_lit_pct(now);
    m.days = (int)((moon_next_event_s(now, &m.full) + 43200) / 86400);
    return m;
}
void clock_moon_report(int64_t now)      /* console: clockface moon? */
{
    moon_info_t m = moon_info(now);
    printf("APP clockface moon phase=%s age=%d.%d lit=%d next=%s in=%d\n", moon_phase_name(m.phase), m.tenths / 10, m.tenths % 10, m.lit, m.full ? "full" : "new", m.days);
}
static void moon_face_update(int64_t now)
{
    char b[64];
    snprintf(b, sizeof b, "%02d:%02d", s_v.hour, s_v.minute); set_text(s_l[0], b);
    moon_info_t m = moon_info(now);
    const char *name = T_RAW(moon_phase_name_zh(m.phase), moon_phase_name(m.phase));
    snprintf(b, sizeof b, T(S_S_DAY_D_D) /* %s・moon age %d.%d days */, name, m.tenths / 10, m.tenths % 10);
    set_text(s_l[1], b);
    snprintf(b, sizeof b, m.full ? T(S_D_LIT_FULL_MOON_IN_D_D) /* lit %d%%・full moon in %d days */ : T(S_D_LIT_NEW_MOON_IN_D_D) /* lit %d%%・new moon in %d days */, m.lit, m.days);
    set_text(s_l[2], b);
    int64_t hour = now / 3600;
    if (s_moon && s_moon_hour != hour) {
        clock_moon_draw(s_moon, now);
        s_moon_hour = hour;
        printf("APP clockface moon draw hour=%lld\n", (long long)hour);
    }
}

static void on_minute(void)          /* texts and the slow parts: once a minute (and when the face opens) */
{
    char b[64];
    char zone[24];
    clock_zone_label(zone, sizeof zone);
    switch (s_face) {
    case F_CLASSIC:
        set_text(s_l[0], zone);
        date_text(b, sizeof b, true);
        set_text(s_l[1], b);
        break;
    case F_DIGITAL:
        snprintf(b, sizeof b, "%02d", s_v.hour); set_text(s_l[0], b);
        snprintf(b, sizeof b, "%02d", s_v.minute); set_text(s_l[1], b);
        set_text(s_l[2], i18n_zh() ? (snprintf(b, sizeof b, TZ("週%s"), WEEK[s_v.weekday]), b) : WEEK_EN[s_v.weekday]);
        date_text(b, sizeof b, false);
        set_text(s_l[3], b);
        break;
    case F_SUN: {
        snprintf(b, sizeof b, "%02d:%02d", s_v.hour, s_v.minute); set_text(s_l[0], b);
        date_text(b, sizeof b, true); set_text(s_l[1], b);
        set_text(s_l[5], zone);
        int rise = 0, set = 0, rc = clock_sun_has()
            ? sun_rise_set(s_v.year, s_v.month, s_v.day, clock_tz_day_min(s_v.year, s_v.month, s_v.day), clock_sun_lat_e4(), clock_sun_lon_e4(), &rise, &set) : SUN_POLAR_NIGHT;   /* no location: like a polar night, no times */
        s_rise = rise; s_set = set; s_polar = rc == SUN_OK ? 0 : rc;
        int now_min = s_v.hour * 60 + s_v.minute, a = pc_cf_sun_deg(now_min, rise, set, s_polar);
        if (s_sun_a >= 0) inv_dot(s_sun_a, SUN_R, Z(6));              /* the bright arc's end moves */
        s_sun_a = a;
        inv_dot(a, SUN_R, Z(6));
        if (s_polar) { set_text(s_l[3], ""); set_text(s_l[4], ""); }
        else {
            snprintf(b, sizeof b, "%02d:%02d", rise / 60, rise % 60); set_text(s_l[3], b);
            snprintf(b, sizeof b, "%02d:%02d", set / 60, set % 60); set_text(s_l[4], b);
        }
        int64_t now = clock_now_utc();
        if (pc_cf_is_day(now_min, rise, set, s_polar)) {
            int len = s_polar ? 1440 : ((set - rise) % 1440 + 1440) % 1440;
            snprintf(b, sizeof b, "%s %d:%02d", T(S_DAYLIGHT) /* Daylight */, len / 60, len % 60);
        } else snprintf(b, sizeof b, "%s", T_RAW(moon_phase_name_zh(moon_phase8(now)), moon_phase_name(moon_phase8(now))));
        set_text(s_l[2], b);
        if (s_moon && s_moon_day != s_v.day) { clock_moon_draw(s_moon, now); s_moon_day = s_v.day; }
        sun_place();
        break;
    }
    case F_RINGS:
        snprintf(b, sizeof b, "%02d:%02d", s_v.hour, s_v.minute); set_text(s_l[0], b);
        date_text(b, sizeof b, true); set_text(s_l[1], b);
        break;
    case F_MOON: moon_face_update(clock_now_utc()); break;
    default: break;
    }
}

static void hands_update(bool smooth)   /* analog faces */
{
    pc_cf_hands_t h;
    pc_cf_hands(s_v.hour, s_v.minute, s_v.second, smooth ? s_ms : 0, &h);
    h.minute -= h.minute % 10;                 /* minute hand in 1 degree steps (10 s), hour hand in 0.5 (1 min): */
    h.hour -= h.hour % 5;                      /* the tip moves ~2 px a step, and not every second */
    int sec = s_still ? -1 : s_v.second;
    bool classic = s_face == F_CLASSIC;
    if (h.minute != s_hd.minute || h.hour != s_hd.hour) {
        if (classic) { inv_hand(s_hd.minute, h.minute, Z(-20), Z(136), Z(4), 3); inv_hand(s_hd.hour, h.hour, Z(-16), Z(86), Z(5), 2); }
        else { inv_hand(s_hd.minute, h.minute, Z(0), Z(140), Z(2), 3); inv_hand(s_hd.hour, h.hour, Z(0), Z(92), Z(4), 2); }
    }
    if (classic && (h.second != s_hd.second || sec != s_sec_shown)) {
        inv_hand(s_sec_shown >= 0 ? s_hd.second : -1, sec >= 0 ? h.second : -1, Z(-34), Z(150), Z(3), 2);
    }
    if (!classic && sec != s_sec_shown) {
        if (s_sec_shown >= 0) inv_dot(s_sec_shown * 60, Z(172), Z(4));
        if (sec >= 0) inv_dot(sec * 60, Z(172), Z(4));
    }
    s_hd = h;
    s_sec_shown = sec;
}

static void show_invalid(bool invalid)
{
    if (invalid == !s_valid && s_msg) return;
    s_valid = !invalid;
    if (!s_msg) {
        s_msg = face_label(lv_obj_get_parent(s_top ? s_top : s_dial), font_main(), UI_FG, 0, 0);
        lv_obj_set_style_bg_color(s_msg, UI_BG, 0);
        lv_obj_set_style_bg_opa(s_msg, LV_OPA_80, 0);
        lv_obj_set_style_pad_all(s_msg, 6, 0);
        lv_label_set_text(s_msg, T(S_NOT_SYNCED_TAP_SYNC_NOW) /* Time not set yet; tap → set it now */);
    }
    if (invalid) lv_obj_remove_flag(s_msg, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(s_msg, LV_OBJ_FLAG_HIDDEN);
    for (int i = 0; i < 6; i++) if (s_l[i] && (s_face == F_DIGITAL || s_face == F_RINGS || s_face == F_MOON || (s_face == F_SUN && i < 3))) {
        if (invalid) lv_obj_add_flag(s_l[i], LV_OBJ_FLAG_HIDDEN); else lv_obj_remove_flag(s_l[i], LV_OBJ_FLAG_HIDDEN);
    }
    if (s_top) lv_obj_invalidate(s_top);
}

/* the HUD face: the designed clock face (tface.c / face_art.c) gets its slot values once a second (and when the face opens). Fixed formats only. */
#define AV(f, ...) snprintf(d.art.v[f], sizeof d.art.v[0], __VA_ARGS__)
static void hud_tick(void)
{
    tface_data_t d;
    memset(&d, 0, sizeof d);
    struct timeval tv;
    gettimeofday(&tv, NULL);
    bool valid = clock_time_valid();
    if (valid) {
        clock_civil_from_utc((int64_t)tv.tv_sec, clock_tz_min(), &s_v);
        if (s_v.second == s_last_sec) return;                    /* the same second: nothing changed */
        s_last_sec = s_v.second;
        snprintf(d.big, sizeof d.big, "%02d:%02d:%02d", s_v.hour, s_v.minute, s_v.second);
        AV(FAV_TIME_HM, "%02d:%02d", s_v.hour % 100, s_v.minute % 100);
        AV(FAV_TIME_S, "%02d", s_v.second % 100);
        AV(FAV_DATE, "%04d-%02d-%02d", s_v.year % 10000, s_v.month % 100, s_v.day % 100);
        AV(FAV_WDAY, "%d", (s_v.weekday + 6) % 7);             /* the sheet starts on Monday */
    } else {
        s_last_sec = -2;                                         /* not synced: set every tick (cheap when nothing changed), the warning follows the status at once */
        snprintf(d.big, sizeof d.big, "--:--:--");
        AV(FAV_TIME_HM, "--:--"); AV(FAV_TIME_S, "--"); AV(FAV_DATE, "----------"); AV(FAV_WDAY, "7");
    }
    bool ringing = clock_ringing();
    const pc_alarm_t *al = clock_alarm();
    AV(FAV_ALARM, "%d", al->on ? 1 : 0);
    if (al->on) AV(FAV_ALARM_HM, "%02d:%02d", al->min / 60 % 100, al->min % 60); else AV(FAV_ALARM_HM, "--:--");
    int w = tface_common(&d.art);
    AV(FAV_STATE, "%d", ringing ? 2 : w ? 1 : 0);                /* the status plate: normal / warning / alarm */
    d.art.warn = w || ringing;                                    /* time not synced, FakePod battery low, the alarm ringing: the warning layer */
    tface_set(s_top, &d);
}
#undef AV

static void tick(lv_timer_t *t)
{
    (void)t;
    if (!ui_screen_on() && s_last_min >= 0) return;               /* dark: nothing to draw (but fill a new face once) */
    if (s_face == F_HUD) { s_last_min = 0; hud_tick(); return; }
    int64_t t0 = esp_timer_get_time();
    bool still = lowmem_level() >= PC_LOWMEM_STILL, smooth = !still && pc_power_allows_heavy(power_svc_state());
    if (!clock_time_valid()) { show_invalid(true); return; }
    struct timeval tv;
    gettimeofday(&tv, NULL);
    clock_civil_from_utc((int64_t)tv.tv_sec, clock_tz_min(), &s_v);
    s_ms = (int)(tv.tv_usec / 1000);
    if (!s_valid) { show_invalid(false); s_last_min = -1; }
    if (still != s_still) { s_still = still; s_last_sec = -1; }
    int mk = s_v.hour * 60 + s_v.minute;
    bool new_min = mk != s_last_min, new_sec = s_v.second != s_last_sec;
    if (new_min) { s_last_min = mk; on_minute(); }
    switch (s_face) {
    case F_CLASSIC: case F_MINIMAL:
        if (new_sec || (s_face == F_CLASSIC && smooth)) hands_update(s_face == F_CLASSIC && smooth);
        break;
    case F_DIGITAL:
        if (new_sec) {
            int sec = still ? -1 : s_v.second;
            if (sec != s_sec_shown) {
                if (sec < s_sec_shown || sec < 0 || s_sec_shown < 0) lv_obj_invalidate(s_top);   /* ring restarts / goes */
                else for (int i = s_sec_shown + 1; i <= sec; i++) inv_dot(i * 60, Z(168), Z(4));
                s_sec_shown = sec;
            }
        }
        break;
    case F_RINGS: {
        pc_cf_rings_t r;
        pc_cf_rings(s_v.hour, s_v.minute, s_v.second, smooth ? s_ms : 0, &r);
        if (lv_arc_get_value(s_arc[0]) != r.hour) lv_arc_set_value(s_arc[0], r.hour);
        if (lv_arc_get_value(s_arc[1]) != r.minute) lv_arc_set_value(s_arc[1], r.minute);
        int sv = still ? 0 : r.second;
        if (lv_arc_get_value(s_arc[2]) != sv) lv_arc_set_value(s_arc[2], sv);
        break;
    }
    default: break;
    }
    s_last_sec = s_v.second;
    uint32_t us = (uint32_t)(esp_timer_get_time() - t0);
    s_tick_us += us;
    if (us > s_tick_max) s_tick_max = us;
    if (++s_ticks >= 600) {
        printf("APP clockface face=%s ticks=%lu tick_avg_us=%lu tick_max_us=%lu still=%d smooth=%d\n", pc_cf_name(s_face),
               (unsigned long)s_ticks, (unsigned long)(s_tick_us / s_ticks), (unsigned long)s_tick_max, s_still, smooth);
        s_ticks = s_tick_us = s_tick_max = 0;
    }
}

void clock_face_create(lv_obj_t *p, int idx)
{
    s_face = idx;
    s_ox = (UI_W - UI_CONTENT_D) / 2; s_oy = (UI_H - UI_CONTENT_D) / 2;   /* the box sits in the middle of the panel */
    s_dial = s_top = s_msg = s_moon = s_body = s_halo = NULL;
    memset(s_l, 0, sizeof s_l);
    memset(s_arc, 0, sizeof s_arc);
    memset(&s_hd, 0, sizeof s_hd);
    s_last_sec = s_last_min = -1;
    s_sec_shown = -1;
    s_sun_a = s_moon_day = -1;
    s_moon_hour = -1;
    s_valid = true;
    s_still = lowmem_level() >= PC_LOWMEM_STILL;
    switch (idx) {
    case F_CLASSIC: classic_create(p); break;
    case F_MINIMAL: minimal_create(p); break;
    case F_DIGITAL: digital_create(p); break;
    case F_SUN: sun_create(p); break;
    case F_MOON: moon_face_create(p); break;
    case F_HUD: s_top = tface_create(p, PC_TF_HUD, TF_CLOCK); break;
    default: rings_create(p); break;
    }
    if (!s_top) s_top = layer_obj(p, NULL);
    /* the classic face sweeps its second hand at 10 Hz; the others look for a new second 4 times a second */
    s_t = ui_timer_bind(s_top, tick, idx == F_CLASSIC ? 100 : idx == F_RINGS ? 200 : 250, NULL);   /* goes with the face */
    if (s_t) tick(s_t);
}

void clock_face_dump(void) { tface_dump(s_face == F_HUD ? s_top : NULL); }

void clock_face_destroy(void)
{
    s_t = NULL;                                        /* bound to s_top: deleted with the face's objects */
    s_dial = s_top = s_msg = s_moon = NULL;
}
