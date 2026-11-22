// SPDX-License-Identifier: LGPL-2.1-or-later
/* The sectors (ui_sectors.h). Layout: the band between the HEAD row and Back (ui_board's numbers) or the design circle; the disc is the largest that fits
 * the band and the glass, centred in the band. Each sector is an arc of the disc drawn with lv_draw_arc (no masks: nothing to keep in memory). */
#include "ui_sectors.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "misc/lv_area_private.h"
#include "draw/lv_draw_private.h"
#include "pc/pc_utf.h"
#include "psram_alloc.h"
#include "font.h"
#include "game_style.h"
#include "sfx.h"
#include "ui.h"
#include "ui_geom.h"
#include "ui_kit.h"
#include "ui_frame.h"
#include "ui_theme.h"

#define GLASS_EDGE  6                         /* the disc keeps this far from the circle (band layout) */
#define FULL_EDGE   (UI_ROUND ? 10 : 2)       /* the full layout: the content circle (UI_CONTENT_D) less this (the square bezel has a black margin of its own: SQ2) */
#define STATUS_MAX  32                        /* the status line (ui_sectors_set_status) */
#define GAP_DEG     4.5f                      /* between two sectors (the ring's rounded segments use the same gap) */
#define HUB_GAP     4                         /* between the hub and the sectors */
#define PROG_W      6                         /* the progress ring's width, and its gap to the sectors */
#define PROG_GAP_DEG 3.0f
#define CURSOR_W    4
#define TEXT_MAX    15

typedef struct { char t[TEXT_MAX + 1]; uint8_t look, paint, lit; uint32_t t0, dur; lv_area_t box; } sec_t;   /* box: relative to the object */
typedef struct {
    lv_obj_t *obj;
    const game_style_t *st;                   /* NULL: the theme */
    ui_sectors_tap_cb cb;
    ui_sectors_hub_cb hub;
    void *user;
    const char *const *tones;
    int n, tap_ms, prog_n, prog_max, cursor;
    float rot;
    int cx, cy, ro, ri, rp;                   /* relative to the object: centre, the sectors' radii, the progress ring's outer radius (0 none) */
    bool en, keys;
    lv_timer_t *timer;                        /* the flash's end: paused while nothing is lit */
    char hub_t[TEXT_MAX + 1];
    char status_t[STATUS_MAX + 1];            /* the status the user needs, inside the content (a page with a frame has no title line) */
    sec_t s[UI_SECTORS_MAX];
} sc_t;

static sc_t *s_live;                          /* the sectors on the open page (console) */

static sc_t *of(const lv_obj_t *o) { return o ? lv_obj_get_user_data((lv_obj_t *)o) : NULL; }
static bool ok(const sc_t *c, int i) { return c && i >= 0 && i < c->n; }
static float centre_deg(const sc_t *c, int i) { return c->rot + 360.0f * (float)i / (float)c->n; }   /* clockwise from the top */

static void colours(const sc_t *c, const sec_t *x, lv_color_t *fill, lv_color_t *text)
{
    ui_sec_look_t lk = x->lit ? UI_SEC_LIT : (ui_sec_look_t)x->look;
    if (!c->st) {
        *fill = lk == UI_SEC_LIT ? UI_A1 : lk == UI_SEC_DIM ? ui_cell_empty() : ui_cell_used();
        *text = lk == UI_SEC_LIT ? ui_text_on(*fill) : lk == UI_SEC_DIM ? UI_DIM : UI_FG;
        return;
    }
    lv_color_t k = game_style_sector(c->st, x->paint);
    *fill = lk == UI_SEC_LIT ? lv_color_mix(ui_theme_is_light() ? UI_FG : lv_color_white(), k, 40) : lk == UI_SEC_DIM ? lv_color_mix(k, UI_BG, 48) : lv_color_mix(k, UI_BG, 120);
    *text = lk == UI_SEC_LIT ? ui_text_on(*fill) : UI_FG;
}

static void arc(lv_layer_t *layer, int cx, int cy, int r, int w, float a0, float a1, lv_color_t col)   /* a0..a1 clockwise from the top */
{
    lv_draw_arc_dsc_t d;
    lv_draw_arc_dsc_init(&d);
    d.center.x = cx;
    d.center.y = cy;
    d.radius = (uint16_t)r;
    d.width = w;
    float s = fmodf(a0 - 90.0f + 720.0f, 360.0f), e = fmodf(a1 - 90.0f + 720.0f, 360.0f);   /* LVGL: from 3 o'clock */
    d.start_angle = s;
    d.end_angle = e;
    d.color = col;
    d.opa = LV_OPA_COVER;
    lv_draw_arc(layer, &d);
}

static void text_at2(lv_layer_t *layer, const char *t, int x, int y, int w, lv_color_t col, bool small)
{
    if (!t[0]) return;
    lv_draw_label_dsc_t d;
    lv_draw_label_dsc_init(&d);
    lv_point_t sz;
    d.font = small ? font_main() : font_large();
    lv_text_get_size(&sz, t, d.font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    if (sz.x > w) d.font = font_main();
    d.align = LV_TEXT_ALIGN_CENTER;
    d.text_local = 1;
    d.color = col;
    d.text = t;
    int lh = lv_font_get_line_height(d.font);
    lv_area_t a = { x - w / 2, y - lh / 2, x - w / 2 + w - 1, y - lh / 2 + lh };
    lv_draw_label(layer, &d, &a);
}
static void text_at(lv_layer_t *layer, const char *t, int x, int y, int w, lv_color_t col) { text_at2(layer, t, x, y, w, col, false); }

static int status_y(const sc_t *c) { return c->hub ? c->ri + 18 : 0; }   /* from the centre: under the hub, or the middle when there is none */

static void draw_cb(lv_event_t *e)
{
    const sc_t *c = lv_event_get_user_data(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t o, clip = layer->_clip_area, x;
    lv_obj_get_coords(c->obj, &o);
    const int cx = o.x1 + c->cx, cy = o.y1 + c->cy;
    game_palette_t p;
    game_style_palette(c->st, &p);
    const float half = 180.0f / (float)c->n - GAP_DEG / 2.0f, deg = 3.1415927f / 180.0f;
    for (int i = 0; i < c->n; i++) {
        const sec_t *s = &c->s[i];
        lv_area_t b = s->box;
        lv_area_move(&b, o.x1, o.y1);
        if (!lv_area_intersect(&x, &clip, &b)) continue;
        float m = centre_deg(c, i);
        lv_color_t fill, tc;
        colours(c, s, &fill, &tc);
        arc(layer, cx, cy, c->ro, c->ro - c->ri, m - half, m + half, fill);
        if (c->keys && c->cursor == i) {                                   /* the key cursor: the sector's outer and inner edges in the highlight */
            arc(layer, cx, cy, c->ro, CURSOR_W, m - half, m + half, p.hi);
            arc(layer, cx, cy, c->ri + CURSOR_W, CURSOR_W, m - half, m + half, p.hi);
        }
        int rm = (c->ri + c->ro) / 2;
        text_at(layer, s->t, cx + (int)lroundf((float)rm * sinf(m * deg)), cy - (int)lroundf((float)rm * cosf(m * deg)), (c->ro - c->ri) * 3 / 2, tc);
    }
    if (c->ri > 2 * HUB_GAP + 8 && c->hub) {                               /* the hub */
        int r = c->ri - HUB_GAP;
        lv_area_t h = { cx - r, cy - r, cx + r - 1, cy + r - 1 };
        if (lv_area_intersect(&x, &clip, &h)) {
            lv_draw_rect_dsc_t d;
            lv_draw_rect_dsc_init(&d);
            d.bg_color = p.cell;
            d.bg_opa = LV_OPA_COVER;
            d.radius = LV_RADIUS_CIRCLE;
            lv_draw_rect(layer, &d, &h);
            text_at(layer, c->hub_t, cx, cy, 2 * r - 8, ui_text_on(p.cell));
        }
    }
    if (c->status_t[0]) {                                                  /* the status line: a dim plate and the text */
        lv_point_t sz;
        lv_text_get_size(&sz, c->status_t, font_main(), 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        int w = LV_MIN(sz.x + 20, c->ro), h = lv_font_get_line_height(font_main()) + 8, y = cy + status_y(c);
        lv_area_t pl = { cx - w / 2, y - h / 2, cx + w / 2 - 1, y + h / 2 - 1 };
        if (lv_area_intersect(&x, &clip, &pl)) {
            lv_draw_rect_dsc_t d;
            lv_draw_rect_dsc_init(&d);
            d.bg_color = UI_BG;
            d.bg_opa = LV_OPA_70;
            d.radius = LV_RADIUS_CIRCLE;
            lv_draw_rect(layer, &d, &pl);
            text_at2(layer, c->status_t, cx, y, w - 8, UI_FG, true);
        }
    }
    if (c->rp && c->prog_max > 0) {                                        /* the progress ring: max segments from the top, n filled */
        float step = 360.0f / (float)c->prog_max, g = c->prog_max > 1 ? PROG_GAP_DEG / 2.0f : 0.0f;
        for (int k = 0; k < c->prog_max; k++)
            arc(layer, cx, cy, c->rp, PROG_W, step * (float)k + g, step * (float)(k + 1) - g, k < c->prog_n ? p.hi : p.line);
    }
}

static void inval_sector(const sc_t *c, int i)
{
    lv_area_t o, a = c->s[i].box;
    lv_obj_get_coords(c->obj, &o);
    lv_area_move(&a, o.x1, o.y1);
    lv_obj_invalidate_area(c->obj, &a);
}

static void inval_hub(const sc_t *c)
{
    lv_area_t o;
    lv_obj_get_coords(c->obj, &o);
    lv_area_t a = { o.x1 + c->cx - c->ri, o.y1 + c->cy - c->ri, o.x1 + c->cx + c->ri, o.y1 + c->cy + c->ri };
    lv_obj_invalidate_area(c->obj, &a);
}

/* the bounding box of sector i (with the cursor and a pixel of margin), relative to the object */
static void sector_box(sc_t *c, int i)
{
    const float half = 180.0f / (float)c->n - GAP_DEG / 2.0f, deg = 3.1415927f / 180.0f, m = centre_deg(c, i);
    int x1 = c->cx, y1 = c->cy, x2 = c->cx, y2 = c->cy;
    for (int k = 0; k <= 16; k++) {
        float t = (m - half + 2.0f * half * (float)k / 16.0f) * deg;
        for (int r = c->ri; r <= c->ro; r += c->ro - c->ri) {
            int x = c->cx + (int)lroundf((float)r * sinf(t)), y = c->cy - (int)lroundf((float)r * cosf(t));
            x1 = LV_MIN(x1, x); y1 = LV_MIN(y1, y); x2 = LV_MAX(x2, x); y2 = LV_MAX(y2, y);
            if (c->ro == c->ri) break;
        }
    }
    if (c->n == 2) { x1 = LV_MIN(x1, c->cx - c->ri); x2 = LV_MAX(x2, c->cx + c->ri); y1 = LV_MIN(y1, c->cy - c->ri); y2 = LV_MAX(y2, c->cy + c->ri); }
    lv_area_t *b = &c->s[i].box;
    b->x1 = x1 - 2; b->y1 = y1 - 2; b->x2 = x2 + 2; b->y2 = y2 + 2;
}

static void arm_timer(sc_t *c)                     /* the timer's period = the first flash to end; paused when none is lit */
{
    if (!c->timer) return;
    uint32_t rem = UINT32_MAX;
    for (int i = 0; i < c->n; i++)
        if (c->s[i].lit) { uint32_t el = lv_tick_elaps(c->s[i].t0); uint32_t r = el >= c->s[i].dur ? 1 : c->s[i].dur - el; if (r < rem) rem = r; }
    if (rem == UINT32_MAX) { lv_timer_pause(c->timer); return; }
    lv_timer_set_period(c->timer, rem);
    lv_timer_reset(c->timer);
    lv_timer_resume(c->timer);
}

static void tick_cb(lv_timer_t *t)
{
    sc_t *c = lv_timer_get_user_data(t);
    for (int i = 0; i < c->n; i++)
        if (c->s[i].lit && lv_tick_elaps(c->s[i].t0) >= c->s[i].dur) { c->s[i].lit = 0; inval_sector(c, i); }
    arm_timer(c);
}

static void tone(const sc_t *c, int i) { if (c->tones && c->tones[i]) sfx_play(c->tones[i]); }

void ui_sectors_flash(lv_obj_t *obj, int i, int ms, bool snd)
{
    sc_t *c = of(obj);
    if (!ok(c, i)) return;
    if (snd) tone(c, i);
    if (ms <= 0 || !c->timer) return;              /* no timer (no memory): no light that could not end */
    sec_t *s = &c->s[i];
    s->t0 = lv_tick_get();
    s->dur = (uint32_t)ms;
    if (!s->lit) { s->lit = 1; inval_sector(c, i); }
    arm_timer(c);
}

void ui_sectors_unlight(lv_obj_t *obj)
{
    sc_t *c = of(obj);
    if (!c) return;
    for (int i = 0; i < c->n; i++) if (c->s[i].lit) { c->s[i].lit = 0; inval_sector(c, i); }
    arm_timer(c);
}

static void press(sc_t *c, int i)                  /* a taken touch on sector i (touch, key, console) */
{
    if (!c->en) return;
    ui_haptic_tap();
    if (c->keys && c->cursor != i) { int old = c->cursor; c->cursor = i; if (ok(c, old)) inval_sector(c, old); inval_sector(c, i); }
    if (c->tap_ms > 0) ui_sectors_flash(c->obj, i, c->tap_ms, true);
    else tone(c, i);
    if (c->cb) c->cb(i, c->user);
}

static void hub_press(sc_t *c)
{
    if (c->hub && c->hub(c->user)) ui_haptic_tap();   /* a hub touch that does nothing gives no feedback */
}

int ui_sectors_hit(const lv_obj_t *obj, int sx, int sy)
{
    const sc_t *c = of(obj);
    if (!c) return -1;
    lv_area_t o;
    lv_obj_get_coords(c->obj, &o);
    float dx = (float)(sx - (o.x1 + c->cx)), dy = (float)(sy - (o.y1 + c->cy)), r = sqrtf(dx * dx + dy * dy);
    int outer = (c->rp ? c->rp : c->ro) + c->ro / 5;                      /* a margin past the edge still takes the nearest sector */
    if (r > (float)outer) return -1;
    if (c->hub && r < (float)c->ri) return -2;
    if (r < (float)c->ri / 2) return -1;                                   /* no hub: the very middle belongs to no sector */
    float a = atan2f(dx, -dy) * 180.0f / 3.1415927f - c->rot + 180.0f / (float)c->n;   /* clockwise from sector 0's start */
    a = fmodf(a + 720.0f, 360.0f);
    int i = (int)(a * (float)c->n / 360.0f);
    return i >= c->n ? c->n - 1 : i;
}

static void press_cb(lv_event_t *e)
{
    sc_t *c = lv_event_get_user_data(e);
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    int i = ui_sectors_hit(c->obj, p.x, p.y);
    if (i == -2) hub_press(c);
    else if (i >= 0) press(c, i);
}

static void delete_cb(lv_event_t *e)
{
    sc_t *c = lv_event_get_user_data(e);
    if (s_live == c) s_live = NULL;
    psram_free(c);                                 /* the bound timer goes with the object (ui_timer_bind) */
}

lv_obj_t *ui_sectors_create(lv_obj_t *parent, const ui_sectors_def_t *def, ui_sectors_tap_cb cb, ui_sectors_hub_cb hub, void *user)
{
    if (!def || (def->n != 2 && def->n != 4 && def->n != 8)) return NULL;
    int R, cyo, mx;
    if (def->field && ui_frame_active(parent)) { ui_frame_field(&R, &cyo); mx = 0; }                       /* under the page's status line, down to the margin */
    else if (def->full || (def->frame && ui_frame_active(parent))) { R = UI_CONTENT_D / 2 - FULL_EDGE; cyo = 0; mx = 0; }
    else {
        const int top = UI_SLOT_HEAD_Y + UI_TAP_MIN / 2 + UI_TAP_GAP, bottom = UI_BAND_BOTTOM;   /* centre offsets (ui_board's band) */
        cyo = (top + bottom) / 2;
        R = (bottom - top) / 2;
        int side = UI_W / 2 - GLASS_EDGE;
        if (R > side) R = side;
        if (UI_ROUND) { int g = UI_CONTENT_D / 2 - GLASS_EDGE - (cyo < 0 ? -cyo : cyo); if (R > g) R = g; }
        mx = LV_MIN(R / 5, UI_W / 2 - R);                                  /* the touch margin beside the disc (never above or below: the HEAD row, Back) */
    }
    sc_t *c = psram_calloc(1, sizeof *c);
    if (!c) return NULL;
    c->n = def->n;
    c->rot = (float)def->rot;
    c->tap_ms = def->tap_flash_ms;
    c->tones = def->tones;
    c->cb = cb;
    c->hub = hub;
    c->user = user;
    c->en = true;
    c->keys = !UI_HAS_KNOB;
    c->cursor = c->keys ? 0 : -1;
    c->rp = def->progress ? R - 1 : 0;
    c->ro = def->progress ? R - 2 * PROG_W - 1 : R;
    c->ri = def->hub_pct > 0 ? c->ro * def->hub_pct / 100 : c->ro / 12;
    c->cx = mx + R;
    c->cy = R;
    for (int i = 0; i < c->n; i++) { c->s[i].paint = (uint8_t)(i % 4); sector_box(c, i); }
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, 2 * R + 2 * mx, 2 * R);
    lv_obj_align(o, LV_ALIGN_CENTER, 0, cyo);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
    c->obj = o;
    lv_obj_set_user_data(o, c);
    lv_obj_add_event_cb(o, draw_cb, LV_EVENT_DRAW_MAIN, c);
    lv_obj_add_event_cb(o, press_cb, LV_EVENT_PRESSED, c);
    lv_obj_add_event_cb(o, delete_cb, LV_EVENT_DELETE, c);
    c->timer = ui_timer_bind(o, tick_cb, 1000, c);
    if (c->timer) lv_timer_pause(c->timer);
    s_live = c;
    return o;
}

void ui_sectors_set_style(lv_obj_t *obj, const game_style_t *st)
{
    sc_t *c = of(obj);
    if (!c || c->st == st) return;
    c->st = st;
    lv_obj_invalidate(c->obj);
}

void ui_sectors_set_paint(lv_obj_t *obj, int i, int k)
{
    sc_t *c = of(obj);
    if (!ok(c, i) || c->s[i].paint == (uint8_t)(k & 3)) return;
    c->s[i].paint = (uint8_t)(k & 3);
    inval_sector(c, i);
}

void ui_sectors_set_look(lv_obj_t *obj, int i, ui_sec_look_t look)
{
    sc_t *c = of(obj);
    if (!ok(c, i) || look >= UI_SEC_LOOKS || c->s[i].look == (uint8_t)look) return;
    c->s[i].look = (uint8_t)look;
    inval_sector(c, i);
}
void ui_sectors_set_looks(lv_obj_t *obj, ui_sec_look_t look) { for (int i = 0; i < ui_sectors_count(obj); i++) ui_sectors_set_look(obj, i, look); }

void ui_sectors_set_text(lv_obj_t *obj, int i, const char *text)
{
    sc_t *c = of(obj);
    if (!ok(c, i)) return;
    char t[TEXT_MAX + 1] = { 0 };
    if (text) pc_utf8_copy(t, sizeof t, text);
    if (!strcmp(t, c->s[i].t)) return;
    memcpy(c->s[i].t, t, sizeof t);
    inval_sector(c, i);
}

void ui_sectors_set_hub(lv_obj_t *obj, const char *text)
{
    sc_t *c = of(obj);
    if (!c) return;
    char t[TEXT_MAX + 1] = { 0 };
    if (text) pc_utf8_copy(t, sizeof t, text);
    if (!strcmp(t, c->hub_t)) return;
    memcpy(c->hub_t, t, sizeof t);
    inval_hub(c);
}

void ui_sectors_set_status(lv_obj_t *obj, const char *text)
{
    sc_t *c = of(obj);
    if (!c) return;
    char t[STATUS_MAX + 1] = { 0 };
    if (text) pc_utf8_copy(t, sizeof t, text);
    if (!strcmp(t, c->status_t)) return;
    lv_area_t o, a;
    lv_obj_get_coords(c->obj, &o);
    int y = o.y1 + c->cy + status_y(c), hh = lv_font_get_line_height(font_main()) / 2 + 8;
    a = (lv_area_t){ o.x1 + c->cx - c->ro, y - hh, o.x1 + c->cx + c->ro, y + hh };
    lv_obj_invalidate_area(c->obj, &a);                                    /* the old plate */
    memcpy(c->status_t, t, sizeof t);
    lv_obj_invalidate_area(c->obj, &a);
}

void ui_sectors_set_enabled(lv_obj_t *obj, bool on) { sc_t *c = of(obj); if (c) c->en = on; }

void ui_sectors_set_progress(lv_obj_t *obj, int n, int max)
{
    sc_t *c = of(obj);
    if (!c || !c->rp) return;
    if (max < 0) max = 0;
    if (max > 60) max = 60;
    n = n < 0 ? 0 : n > max ? max : n;
    if (c->prog_n == n && c->prog_max == max) return;
    c->prog_n = n;
    c->prog_max = max;
    lv_obj_invalidate(c->obj);                     /* the ring goes round the whole disc */
}

void ui_sectors_set_keys(lv_obj_t *obj, bool on)
{
    sc_t *c = of(obj);
    if (!c || c->keys == on) return;
    c->keys = on;
    if (on && c->cursor < 0) c->cursor = 0;
    if (ok(c, c->cursor)) inval_sector(c, c->cursor);
}

bool ui_sectors_knob(lv_obj_t *obj, int d)
{
    sc_t *c = of(obj);
    if (!c || !c->keys || !d) return false;
    if (!ok(c, c->cursor)) c->cursor = 0;
    if (d > 0) {
        int old = c->cursor;
        c->cursor = (c->cursor + 1) % c->n;
        inval_sector(c, old);
        inval_sector(c, c->cursor);
    } else press(c, c->cursor);
    return true;
}

void ui_sectors_tap(lv_obj_t *obj, int i) { sc_t *c = of(obj); if (ok(c, i)) press(c, i); }

int ui_sectors_count(const lv_obj_t *obj) { const sc_t *c = of(obj); return c ? c->n : 0; }

bool ui_sectors_centre(const lv_obj_t *obj, int i, int *x, int *y)
{
    const sc_t *c = of(obj);
    if (!ok(c, i)) return false;
    lv_obj_update_layout(c->obj);
    lv_area_t o;
    lv_obj_get_coords(c->obj, &o);
    float m = centre_deg(c, i) * 3.1415927f / 180.0f, rm = (float)(c->ri + c->ro) / 2.0f;
    *x = o.x1 + c->cx + (int)lroundf(rm * sinf(m));
    *y = o.y1 + c->cy - (int)lroundf(rm * cosf(m));
    return true;
}

static void dump(const sc_t *c)
{
    static const char LK[UI_SEC_LOOKS] = { 'n', 'l', 'd' };
    char looks[2 * UI_SECTORS_MAX + 1], paint[2 * UI_SECTORS_MAX + 1];
    unsigned lit = 0;
    int k = 0;
    for (int i = 0; i < c->n; i++) {
        looks[k] = LK[c->s[i].look < UI_SEC_LOOKS ? c->s[i].look : 0];
        paint[k] = (char)('0' + c->s[i].paint);
        if (i < c->n - 1) { looks[k + 1] = ','; paint[k + 1] = ','; }
        k += 2;
        if (c->s[i].lit) lit |= 1u << i;
    }
    looks[k - 1] = paint[k - 1] = 0;
    lv_obj_update_layout(c->obj);
    lv_area_t o;
    lv_obj_get_coords(c->obj, &o);
    printf("APP sectors n=%d cx=%d cy=%d ro=%d ri=%d en=%d keys=%d cursor=%d lit=%u looks=%s paint=%s hub=%s progress=%d/%d\n", c->n, (int)o.x1 + c->cx,
           (int)o.y1 + c->cy, c->ro, c->ri, c->en, c->keys, c->cursor, lit, looks, paint, c->hub_t[0] ? c->hub_t : "-", c->prog_n, c->prog_max);
    for (int i = 0; i < c->n; i++) if (c->s[i].t[0]) printf("APP sectors text %d %s\n", i, c->s[i].t);
    if (c->status_t[0]) printf("APP sectors status %s\n", c->status_t);
}

void ui_sectors_cmd(const char *arg)
{
    sc_t *c = s_live;
    int i, x, y;
    if (!c) { printf("APP sectors none\n"); return; }
    if (!arg[0]) dump(c);
    else if (sscanf(arg, "tap %d", &i) == 1) { if (ok(c, i)) press(c, i); if (s_live) dump(s_live); }
    else if (!strcmp(arg, "hub")) { hub_press(c); if (s_live) dump(s_live); }
    else if (sscanf(arg, "at %d", &i) == 1 && ui_sectors_centre(c->obj, i, &x, &y)) printf("APP sectors at %d x=%d,y=%d\n", i, x, y);
    else if (sscanf(arg, "hit %d %d", &x, &y) == 2) { lv_obj_update_layout(c->obj); printf("APP sectors hit %d,%d = %d\n", x, y, ui_sectors_hit(c->obj, x, y)); }
    else if (sscanf(arg, "key %d", &i) == 1) { bool took = ui_sectors_knob(c->obj, i); printf("APP sectors key %d took=%d\n", i, took); if (s_live) dump(s_live); }
    else if (sscanf(arg, "keys %d", &i) == 1) { ui_sectors_set_keys(c->obj, i != 0); dump(c); }
    else printf("APP sectors unknown %s\n", arg);
}
