// SPDX-License-Identifier: LGPL-2.1-or-later
/* The dial (ui_dial.h). Layout: the band between the HEAD row and Back, as ui_arena; one object, one draw event. */
#include "ui_dial.h"
#include "ui_tokens.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "draw/lv_draw_private.h"
#include "font.h"
#include "pc/pc_dial.h"
#include "psram_alloc.h"
#include "ui.h"
#include "ui_axis.h"
#include "ui_geom.h"
#include "ui_exit.h"
#include "ui_frame.h"
#include "ui_kit.h"
#include "ui_popring.h"

#define EDGE       2
#define STEP_X30   1000                      /* a timer step = 1000 / 30 ms, kept as 30 x ms */
#define MAX_CATCH  3
#define PAGE_STEPS 3                         /* timer steps per def->step (100 ms) */
#define FX_FRAMES  12
#define HUB_TEXT   8
enum { E_FLASH = 0, E_WAVE, E_N };
typedef struct { int16_t lo, hi; lv_color_t c; lv_opa_t opa; bool on; } zone_t;
typedef struct { uint8_t frame, dur; lv_color_t c; } fx_t;
typedef struct {
    lv_obj_t *obj;
    lv_timer_t *timer;
    const ui_dial_def_t *def;
    void *user;
    int R, v;
    bool meter, hold, running, paused, meter_busy, tgt_on, hub_set, had_knob;   /* had_knob: the dial held the borrowed knob when the shared exit paused it */
    int tgt, reach;
    int cue_pct, cue_lvl, cue_a10, shake, jit;   /* the cue: strength %, the level and the needle angle it was drawn at; the needle shake: amplitude and the current jitter (tenths of a degree) */
    uint32_t rng;
    pc_meter_t m;
    ui_dial_colours_t col;
    zone_t z[UI_DIAL_ZONES];
    fx_t fx[E_N];
    char hub[HUB_TEXT];
    uint32_t last_ms, acc, fps_t0, fps_n, ticks;
    int fps, acc30;
    long inv_px;
    unsigned peak_inv;                       /* times the peak marker's boxes were invalidated (console: a falling peak must redraw its own marker)  */
    lv_area_t pbox;                          /* the peak marker's box as drawn */
    lv_area_t nbox;                          /* the needle's box as drawn (screen px): what a move invalidates */
} dl_t;

static dl_t *s_live;

static dl_t *of(const lv_obj_t *o) { return o ? lv_obj_get_user_data((lv_obj_t *)o) : NULL; }
static void centre(const dl_t *a, int *cx, int *cy)
{
    lv_area_t c;
    lv_obj_get_coords(a->obj, &c);
    *cx = c.x1 + lv_area_get_width(&c) / 2;
    *cy = c.y1 + lv_area_get_height(&c) / 2;
}
static bool overlap(const lv_area_t *p, const lv_area_t *q) { return p->x1 <= q->x2 && q->x1 <= p->x2 && p->y1 <= q->y2 && q->y1 <= p->y2; }
static int hub_r(const dl_t *a) { return a->R / 4; }
static int tick_out(const dl_t *a) { return a->R - 8; }
static int needle_len(const dl_t *a) { return a->R - 20; }

/* the angle (tenths of a degree) the needle shows: the value, or the meter's smoothed value (1/16 units) */
static int base_angle10(const dl_t *a)
{
    const ui_dial_def_t *d = a->def;
    return pc_dial_angle10(a->v, d->lo, d->hi, d->wrap, d->a0, d->span);
}
static int angle10(const dl_t *a, bool peak)
{
    const ui_dial_def_t *d = a->def;
    if (!a->meter) return base_angle10(a) + a->jit;
    return pc_dial_angle10(peak ? a->m.peak : a->m.shown, d->lo * 16, d->hi * 16, false, d->a0, d->span);
}
static void polar(int cx, int cy, int r, int a10, int *x, int *y)
{
    const float rad = (float)a10 * 0.1f * 0.0174532925f;
    *x = cx + (int)lroundf(cosf(rad) * (float)r);
    *y = cy + (int)lroundf(sinf(rad) * (float)r);
}
static void needle_box(const dl_t *a, int a10, lv_area_t *b)
{
    int cx, cy, x, y;
    centre(a, &cx, &cy);
    polar(cx, cy, needle_len(a), a10, &x, &y);
    b->x1 = LV_MIN(cx, x) - 4; b->y1 = LV_MIN(cy, y) - 4; b->x2 = LV_MAX(cx, x) + 4; b->y2 = LV_MAX(cy, y) + 4;
}
static void inval_area(dl_t *a, const lv_area_t *b)
{
    lv_area_t o;
    lv_obj_get_coords(a->obj, &o);
    lv_area_t c = { LV_MAX(b->x1, o.x1), LV_MAX(b->y1, o.y1), LV_MIN(b->x2, o.x2), LV_MIN(b->y2, o.y2) };   /* inside the face's box */
    if (c.x1 > c.x2 || c.y1 > c.y2) return;
    a->inv_px += (long)lv_area_get_width(&c) * lv_area_get_height(&c);
    lv_obj_invalidate_area(a->obj, &c);
}
static void inval_all(dl_t *a)
{
    a->inv_px += (long)lv_obj_get_width(a->obj) * lv_obj_get_height(a->obj);
    lv_obj_invalidate(a->obj);
}
static void inval_hub(dl_t *a)
{
    int cx, cy;
    centre(a, &cx, &cy);
    lv_area_t b = { cx - hub_r(a), cy - hub_r(a), cx + hub_r(a), cy + hub_r(a) };
    inval_area(a, &b);
}
/* the peak marker: a short stroke across the tick ring at the peak's own angle */
static void peak_box(const dl_t *a, int a10, lv_area_t *b)
{
    int cx, cy, x1, y1, x2, y2;
    centre(a, &cx, &cy);
    polar(cx, cy, tick_out(a) + 1, a10, &x1, &y1);
    polar(cx, cy, tick_out(a) - 12, a10, &x2, &y2);
    b->x1 = LV_MIN(x1, x2) - 3; b->y1 = LV_MIN(y1, y2) - 3; b->x2 = LV_MAX(x1, x2) + 3; b->y2 = LV_MAX(y1, y2) + 3;
}
/* the needle moved: the old and the new box only; the peak marker has boxes of its own (it moves while the needle rests) */
static void inval_needle(dl_t *a)
{
    lv_area_t n;
    needle_box(a, angle10(a, false), &n);
    inval_area(a, &a->nbox);
    inval_area(a, &n);
    a->nbox = n;
    inval_hub(a);
    if (a->meter && a->def->peak) {
        lv_area_t p;
        peak_box(a, angle10(a, true), &p);
        if (memcmp(&p, &a->pbox, sizeof p)) { inval_area(a, &a->pbox); inval_area(a, &p); a->pbox = p; a->peak_inv++; }
    }
}

/* the cue's level from the target and the value; a change of the level or (while lit) of the needle's place redraws the face (the wedge is behind the ticks) */
static void cue_refresh(dl_t *a)
{
    if (!a->def->cue || a->meter) return;
    const ui_dial_def_t *d = a->def;
    const int lvl = a->tgt_on ? pc_dial_cue(pc_dial_dist(a->v, a->tgt, d->lo, d->hi, d->wrap), a->reach, a->cue_pct) : 0;
    const int a10 = base_angle10(a);
    if (lvl == a->cue_lvl && (!lvl || a10 == a->cue_a10)) return;
    a->cue_lvl = lvl;
    a->cue_a10 = a10;
    inval_all(a);
}

/* ---- the timer ---- */
static uint32_t lcg(uint32_t *s) { return *s = *s * 1664525u + 1013904223u; }
static bool game_running(const dl_t *a) { return a->def->running && a->def->running(a->user); }
static bool fx_moving(const dl_t *a) { for (int i = 0; i < E_N; i++) if (a->fx[i].dur) return true; return false; }
static void run(dl_t *a, bool on)
{
    if (!a->timer || a->running == on) return;
    a->running = on;
    if (on) { a->last_ms = lv_tick_get(); a->acc = 0; lv_timer_resume(a->timer); }
    else lv_timer_pause(a->timer);
}
static bool wanted(const dl_t *a) { return !a->paused && (game_running(a) || fx_moving(a) || (a->meter && a->meter_busy) || a->shake > 0 || a->jit); }

static void advance(dl_t *a)
{
    a->inv_px = 0;
    a->ticks++;
    a->fps_n++;
    if (fx_moving(a)) {
        for (int i = 0; i < E_N; i++) {
            fx_t *f = &a->fx[i];
            if (!f->dur) continue;
            if (++f->frame >= f->dur) f->dur = 0;
        }
        inval_all(a);
    }
    if (a->shake > 0 || a->jit) {                            /* the needle's jitter: a new one each frame, its own boxes only */
        a->jit = a->shake > 0 ? (int)(lcg(&a->rng) >> 16) % (2 * a->shake + 1) - a->shake : 0;
        inval_needle(a);
    }
    if (a->meter) {
        const int s = a->m.shown, p = a->m.peak;
        a->meter_busy = pc_meter_step(&a->m);
        if (s != a->m.shown) inval_needle(a);
        else if (p != a->m.peak && a->def->peak) {          /* only the marker moved: its boxes, not the needle's */
            lv_area_t b;
            peak_box(a, angle10(a, true), &b);
            if (memcmp(&b, &a->pbox, sizeof b)) { inval_area(a, &a->pbox); inval_area(a, &b); a->pbox = b; a->peak_inv++; }
        }
    }
    if (a->def->step) {
        a->acc30 += STEP_X30;
        if (a->acc30 >= STEP_X30 * PAGE_STEPS) { a->acc30 -= STEP_X30 * PAGE_STEPS; a->def->step(100, a->user); }
    }
}
static bool covered(const dl_t *a) { return ui_visual_paused(lv_obj_get_parent(a->obj)); }
static void cover(dl_t *a)
{
    for (int i = 0; i < E_N; i++) a->fx[i].dur = 0;
    if (game_running(a) && !a->paused) {
        a->paused = true;
        if (a->def->paused) a->def->paused(true, a->user);
    }
    run(a, false);
}
static void tick(lv_timer_t *t)
{
    dl_t *a = lv_timer_get_user_data(t);
    if (a->hold) return;
    if (a->paused) { run(a, false); return; }
    if (covered(a)) { cover(a); return; }
    const uint32_t now = lv_tick_get();
    a->acc += lv_tick_diff(now, a->last_ms) * 30u;
    a->last_ms = now;
    int steps = (int)(a->acc / STEP_X30);
    a->acc %= STEP_X30;
    if (steps > MAX_CATCH) { steps = MAX_CATCH; a->acc = 0; }
    for (int s = 0; s < steps; s++) advance(a);
    if (lv_tick_diff(now, a->fps_t0) >= 1000) { a->fps = (int)a->fps_n; a->fps_n = 0; a->fps_t0 = now; }
    if (!wanted(a)) run(a, false);
}
void ui_dial_wake(lv_obj_t *dial) { dl_t *a = of(dial); if (a && !a->paused) run(a, true); }
static bool knob_step(int axis, int d);
void ui_dial_pause(lv_obj_t *dial) { dl_t *a = of(dial); if (a) { a->had_knob = ui_axis_current_of(a->obj) == 0; cover(a); } }
void ui_dial_resume(lv_obj_t *dial)
{
    dl_t *a = of(dial);
    if (!a) return;
    const bool was = a->paused;
    a->paused = false;
    if (was && a->def->paused) a->def->paused(false, a->user);
    if (a->had_knob && !a->meter) ui_axis_borrow(a->obj, 0, knob_step, NULL, false);   /* the 4 s borrow ran out during the pause */
    a->had_knob = false;
    if (was && wanted(a)) run(a, true);
}
void ui_dial_unpause(lv_obj_t *dial)
{
    dl_t *a = of(dial);
    if (!a || !a->paused) return;
    a->paused = false;
    if (wanted(a)) run(a, true);
}

/* ---- the page's calls ---- */
void ui_dial_colours(lv_obj_t *dial, const ui_dial_colours_t *c)
{
    dl_t *a = of(dial);
    if (!a || !c) return;
    a->col = *c;
    if (lv_color_eq(c->cue, lv_color_black())) a->col.cue = c->peak;       /* a page that does not name the cue colour gets the peak colour */
    inval_all(a);
}
int ui_dial_value(const lv_obj_t *dial) { dl_t *a = of(dial); return a ? a->v : 0; }
void ui_dial_set(lv_obj_t *dial, int v)
{
    dl_t *a = of(dial);
    if (!a) return;
    v = pc_dial_wrap(v, a->def->lo, a->def->hi, a->def->wrap);
    if (v == a->v) return;
    a->v = v;
    if (!a->meter) { inval_needle(a); cue_refresh(a); }
}
void ui_dial_target(lv_obj_t *dial, bool on, int target, int reach)
{
    dl_t *a = of(dial);
    if (!a) return;
    a->tgt_on = on;
    a->tgt = target;
    a->reach = reach;
    cue_refresh(a);
}
void ui_dial_cue(lv_obj_t *dial, int strength)
{
    dl_t *a = of(dial);
    if (!a) return;
    a->cue_pct = LV_CLAMP(0, strength, 100);
    cue_refresh(a);
}
void ui_dial_shake(lv_obj_t *dial, int amp10)
{
    dl_t *a = of(dial);
    if (!a) return;
    a->shake = amp10 < 0 ? 0 : amp10 > 100 ? 100 : amp10;
    if (a->shake > 0) { if (!a->paused) run(a, true); }
    else if (a->jit) { a->jit = 0; inval_needle(a); }
}
void ui_dial_zone(lv_obj_t *dial, int id, int lo, int hi, lv_color_t c, lv_opa_t opa)
{
    dl_t *a = of(dial);
    if (!a || id < 0 || id >= UI_DIAL_ZONES) return;
    zone_t n = { (int16_t)lo, (int16_t)hi, c, opa, true };
    if (!memcmp(&n, &a->z[id], sizeof n)) return;
    a->z[id] = n;
    inval_all(a);
}
void ui_dial_zone_hide(lv_obj_t *dial, int id)
{
    dl_t *a = of(dial);
    if (!a || id < 0 || id >= UI_DIAL_ZONES || !a->z[id].on) return;
    a->z[id].on = false;
    inval_all(a);
}
void ui_dial_zones_clear(lv_obj_t *dial)
{
    dl_t *a = of(dial);
    if (!a) return;
    bool any = false;
    for (int i = 0; i < UI_DIAL_ZONES; i++) { any |= a->z[i].on; a->z[i].on = false; }
    if (any) inval_all(a);
}
void ui_dial_hub(lv_obj_t *dial, const char *text)
{
    dl_t *a = of(dial);
    if (!a) return;
    char b[HUB_TEXT] = { 0 };
    if (text) snprintf(b, sizeof b, "%s", text);
    if (a->hub_set == (text != NULL) && !strcmp(b, a->hub)) return;
    a->hub_set = text != NULL;
    memcpy(a->hub, b, sizeof b);
    inval_hub(a);
}
static void fx_start(dl_t *a, int kind, lv_color_t c)
{
    if (a->paused) return;
    a->fx[kind] = (fx_t){ 0, FX_FRAMES, c };
    inval_all(a);
    run(a, true);
}
void ui_dial_flash(lv_obj_t *dial, lv_color_t c) { dl_t *a = of(dial); if (a) fx_start(a, E_FLASH, c); }
void ui_dial_wave(lv_obj_t *dial, lv_color_t c) { dl_t *a = of(dial); if (a) fx_start(a, E_WAVE, c); }
void ui_dial_meter(lv_obj_t *dial, bool on)
{
    dl_t *a = of(dial);
    if (!a || a->meter == on) return;
    a->meter = on;
    if (on) { pc_meter_init(&a->m, a->v); pc_meter_config(&a->m, a->def->smooth_shift, a->def->peak); }
    else a->v = pc_dial_wrap(pc_meter_shown(&a->m), a->def->lo, a->def->hi, a->def->wrap);
    a->meter_busy = false;
    inval_all(a);
    a->nbox = (lv_area_t){ 0, 0, 0, 0 };
    a->pbox = (lv_area_t){ 0, 0, 0, 0 };
    inval_needle(a);
}
void ui_dial_feed(lv_obj_t *dial, int v)
{
    dl_t *a = of(dial);
    if (!a || !a->meter) return;
    pc_meter_feed(&a->m, LV_CLAMP(a->def->lo, v, a->def->hi));
    a->meter_busy = true;
    if (!a->paused) run(a, true);
}

/* ---- drawing ---- */
static void disc(lv_layer_t *layer, int cx, int cy, int r, lv_color_t c, lv_opa_t opa, int border, lv_color_t bc)
{
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.radius = LV_RADIUS_CIRCLE;
    d.bg_color = c;
    d.bg_opa = opa;
    d.border_width = border;
    d.border_color = bc;
    d.border_opa = border ? LV_OPA_COVER : LV_OPA_TRANSP;
    lv_area_t b = { cx - r, cy - r, cx + r - 1, cy + r - 1 };
    lv_draw_rect(layer, &d, &b);
}
static void ring(lv_layer_t *layer, int cx, int cy, int r_out, int w, int a0, int a1, lv_color_t c, lv_opa_t opa)
{
    lv_draw_arc_dsc_t d;
    lv_draw_arc_dsc_init(&d);
    d.center.x = cx; d.center.y = cy;
    d.radius = (uint16_t)r_out;
    d.width = w;
    d.start_angle = ((a0 % 360) + 360) % 360;
    d.end_angle = a1 - a0 >= 360 ? d.start_angle + 360 : ((a1 % 360) + 360) % 360;
    d.color = c;
    d.opa = opa;
    lv_draw_arc(layer, &d);
}
static void line(lv_layer_t *layer, int x1, int y1, int x2, int y2, int w, lv_color_t c, lv_opa_t opa, bool round)
{
    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.p1.x = x1; d.p1.y = y1; d.p2.x = x2; d.p2.y = y2;
    d.width = w;
    d.color = c;
    d.opa = opa;
    d.round_start = d.round_end = round;
    lv_draw_line(layer, &d);
}
static void label(lv_layer_t *layer, const lv_font_t *f, int x, int y, const char *text, lv_color_t c, lv_opa_t opa)
{
    lv_draw_label_dsc_t d;
    lv_draw_label_dsc_init(&d);
    d.font = f;
    d.color = c;
    d.opa = opa;
    d.text = text;
    d.align = LV_TEXT_ALIGN_CENTER;
    const int h = lv_font_get_line_height(f);
    lv_area_t b = { x - 40, y - h / 2, x + 40, y + h / 2 };
    lv_draw_label(layer, &d, &b);
}
static int ease_out(int x, int d, int span) { int r = d - x; return span - span * r * r / (d * d); }

static void draw_cb(lv_event_t *e)
{
    dl_t *a = lv_event_get_user_data(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    const lv_area_t *clip = &layer->_clip_area;
    const ui_dial_def_t *d = a->def;
    int cx, cy;
    centre(a, &cx, &cy);
    disc(layer, cx, cy, a->R, a->col.fill, LV_OPA_COVER, 1, a->col.rim);
    /* zones: arcs on the rim, from lo's angle to hi's (a wrapped dial: the end of the last unit) */
    for (int i = 0; i < UI_DIAL_ZONES; i++) {
        const zone_t *z = &a->z[i];
        if (!z->on) continue;
        const int a0 = pc_dial_angle10(z->lo, d->lo, d->hi, d->wrap, d->a0, d->span) / 10;
        const int a1 = pc_dial_angle10(z->hi, d->lo, d->hi, d->wrap, d->a0, d->span) / 10 + LV_MAX(1, d->span / (d->hi - d->lo + 1));
        ring(layer, cx, cy, a->R - 2, 5, a0, a1, z->c, z->opa);
    }
    /* the proximity cue: a soft wedge behind the ticks, centred on the needle, wider and brighter the nearer the target */
    if (a->cue_lvl > 0) {
        const int half = 8 + 16 * a->cue_lvl / 255, c10 = a->cue_a10 / 10;
        ring(layer, cx, cy, a->R - 3, 34, c10 - half, c10 + half, a->col.cue, (lv_opa_t)(40 + 140 * a->cue_lvl / 255));
    }
    /* ticks and labels */
    const lv_font_t *font = font_main();
    const int step = d->minor > 0 ? d->minor : d->major;
    if (step > 0) {
        for (int v = d->lo; v <= d->hi; v += step) {
            const bool major = d->major > 0 && (v - d->lo) % d->major == 0;
            int x1, y1, x2, y2;
            const int a10 = pc_dial_angle10(v, d->lo, d->hi, d->wrap, d->a0, d->span);
            polar(cx, cy, tick_out(a), a10, &x1, &y1);
            polar(cx, cy, tick_out(a) - (major ? 10 : 5), a10, &x2, &y2);
            lv_area_t b = { LV_MIN(x1, x2) - 2, LV_MIN(y1, y2) - 2, LV_MAX(x1, x2) + 2, LV_MAX(y1, y2) + 2 };
            if (overlap(&b, clip)) line(layer, x1, y1, x2, y2, major ? 2 : 1, a->col.tick, major ? LV_OPA_COVER : LV_OPA_60, false);
            if (major) {
                int lx, ly;
                polar(cx, cy, a->R - 31, a10, &lx, &ly);
                lv_area_t lb = { lx - 16, ly - 12, lx + 16, ly + 12 };
                if (overlap(&lb, clip)) {
                    char t[8];
                    if (d->label) d->label(v, t, (int)sizeof t); else snprintf(t, sizeof t, "%d", v);
                    label(layer, font, lx, ly, t, a->col.label, LV_OPA_COVER);
                }
            }
        }
    }
    /* the effects under the needle */
    for (int i = 0; i < E_N; i++) {
        const fx_t *f = &a->fx[i];
        if (!f->dur) continue;
        const int left = f->dur - f->frame;
        if (i == E_FLASH) ring(layer, cx, cy, a->R, 3 + 3 * left / f->dur, 0, 360, f->c, (lv_opa_t)(60 + 195 * left / f->dur));
        else ring(layer, cx, cy, ease_out(f->frame + 1, f->dur, a->R - 8) + 4, 3, 0, 360, f->c, (lv_opa_t)(255 * left / f->dur));
    }
    /* the meter's peak marker, the needle, the hub */
    if (a->meter && a->def->peak) {
        int px, py, qx, qy;
        const int pa = angle10(a, true);
        polar(cx, cy, tick_out(a) + 1, pa, &px, &py);
        polar(cx, cy, tick_out(a) - 12, pa, &qx, &qy);
        line(layer, px, py, qx, qy, 3, a->col.peak, LV_OPA_COVER, true);
    }
    int nx, ny;
    polar(cx, cy, needle_len(a), angle10(a, false), &nx, &ny);
    line(layer, cx, cy, nx, ny, 3, a->col.needle, LV_OPA_COVER, true);
    disc(layer, cx, cy, hub_r(a), a->col.hub, LV_OPA_COVER, 2, a->cue_lvl > 0 ? lv_color_mix(a->col.cue, a->col.needle, (uint8_t)a->cue_lvl) : a->col.needle);
    char t[HUB_TEXT];
    if (a->hub_set) snprintf(t, sizeof t, "%s", a->hub);
    else snprintf(t, sizeof t, "%d", a->meter ? pc_meter_shown(&a->m) : a->v);
    if (strlen(t) > 4) t[4] = 0;                                   /* the hub holds four characters at most (ui_dial.h) */
    label(layer, strlen(t) <= 2 ? font_large() : font_main(), cx, cy, t, a->col.needle, LV_OPA_COVER);
}

/* ---- input ---- */
static void apply(dl_t *a, int delta, bool knob)
{
    if (a->meter || !delta) return;
    const ui_dial_def_t *d = a->def;
    const int v = pc_dial_step(a->v, delta, d->lo, d->hi, d->wrap);
    if (v == a->v) return;
    a->v = v;
    inval_needle(a);
    if (a->tgt_on) {                                         /* the closer, the stronger */
        const int g = pc_dial_grade(pc_dial_dist(v, a->tgt, d->lo, d->hi, d->wrap), a->reach);
        if (g == 3) ui_haptic_strong(); else if (g == 2) ui_haptic_tap(); else if (g == 1) ui_haptic_tick();
    }
    if (d->changed) d->changed(v, delta, knob, a->user);
    cue_refresh(a);                                          /* after the page's retarget (a radio's nearest station) */
}
static bool knob_step(int axis, int d)
{
    (void)axis;
    dl_t *a = s_live;
    if (a && !a->paused) apply(a, d, true);
    return true;
}
static void touch(dl_t *a, int x, int y)
{
    if (a->meter) { ui_exit_tap(); return; }              /* a meter takes no touch: on a page with the shared exit in mode A it is the tap that shows the popup (the tuner) */
    ui_axis_borrow(a->obj, 0, knob_step, NULL, false);
    if (a->paused) {
        a->paused = false;
        if (a->def->paused) a->def->paused(false, a->user);
        ui_haptic_tap();
        if (wanted(a)) run(a, true);
        return;
    }
    const int r = hub_r(a);
    if (x * x + y * y <= r * r) {
        ui_haptic_tap();
        if (a->def->confirm) a->def->confirm(a->v, a->user);
    } else apply(a, x < 0 ? -1 : 1, false);
}
static void press_cb(lv_event_t *e)
{
    dl_t *a = lv_event_get_user_data(e);
    lv_indev_t *in = lv_indev_active();
    if (!in) return;
    lv_point_t p;
    lv_indev_get_point(in, &p);
    int cx, cy;
    centre(a, &cx, &cy);
    touch(a, p.x - cx, p.y - cy);
}
/* The page's on_knob. A borrowed knob steps the dial; on a device without a knob (FakePod: two keys through ui_knob_inject) a key counts as a tap on the dial:
 * it borrows the knob and steps, so the keys work from the first press. With a knob and nothing borrowed the turn is the volume (false). */
bool ui_dial_knob(int detents)
{
    if (ui_axis_knob(detents)) return true;
    dl_t *a = s_live;
    if (UI_HAS_KNOB || !a || a->meter || !detents) return false;
    ui_axis_borrow(a->obj, 0, knob_step, NULL, false);
    return ui_axis_knob(detents);
}

static void delete_cb(lv_event_t *e)
{
    dl_t *a = lv_event_get_user_data(e);
    if (s_live == a) s_live = NULL;
    psram_free(a);
}

lv_obj_t *ui_dial_create(lv_obj_t *parent, const ui_dial_def_t *def, void *user)
{
    if (!parent || !def || def->hi <= def->lo) return NULL;
    dl_t *a = psram_calloc(1, sizeof *a);
    if (!a) return NULL;
    int top, bottom;
    ui_frame_band(parent, &top, &bottom);
    const bool fr = ui_frame_active(parent);                                   /* a frame page: the dial is a round field centred on the panel */
    const int dd = bottom - top;
    int fR = 0, fmid = 0;
    if (fr) ui_frame_field(&fR, &fmid);
    const int mid = fr ? fmid : (top + bottom) / 2;
    a->R = (fr ? fR : LV_MIN(dd / 2, UI_CONTENT_D / 2 - LV_ABS(mid))) - EDGE;
    a->def = def;
    a->user = user;
    a->v = def->lo;
    a->meter = def->meter;
    pc_meter_init(&a->m, def->lo);
    pc_meter_config(&a->m, def->smooth_shift, def->peak);
    a->col = (ui_dial_colours_t){ ui_palc(UI_PAL_dial, 0), ui_palc(UI_PAL_dial, 1), ui_palc(UI_PAL_dial, 2), ui_palc(UI_PAL_dial, 3), ui_palc(UI_PAL_dial, 4),
                                  ui_palc(UI_PAL_dial, 5), ui_palc(UI_PAL_dial, 6), ui_palc(UI_PAL_dial, 7) };
    a->cue_pct = def->cue ? 100 : 0;
    a->rng = 0x2545F491u;
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, 2 * (a->R + EDGE), 2 * (a->R + EDGE));
    lv_obj_align(o, LV_ALIGN_CENTER, 0, mid);
    lv_obj_update_layout(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_user_data(o, a);
    a->obj = o;
    lv_obj_add_event_cb(o, draw_cb, LV_EVENT_DRAW_MAIN, a);
    lv_obj_add_event_cb(o, press_cb, LV_EVENT_PRESSED, a);
    lv_obj_add_event_cb(o, delete_cb, LV_EVENT_DELETE, a);
    a->timer = ui_timer_bind(o, tick, 1000 / 30, a);
    if (a->timer) lv_timer_pause(a->timer);
    a->fps_t0 = lv_tick_get();
    needle_box(a, angle10(a, false), &a->nbox);
    peak_box(a, angle10(a, true), &a->pbox);
    s_live = a;
    return o;
}

/* ---- console ---- */
static void dump(const dl_t *a)
{
    int zones = 0;
    for (int i = 0; i < UI_DIAL_ZONES; i++) zones += a->z[i].on;
    printf("APP dial name=%s mode=%s v=%d lo=%d hi=%d target=%d reach=%d zones=%d shown=%d peak=%d fx=%d run=%d hold=%d paused=%d knob=%d ticks=%u fps=%d inv_px=%ld face_px=%ld peak_inv=%u cue=%d cuepct=%d shake=%d\n",
           a->def->name ? a->def->name : "-", a->meter ? "meter" : "dial", a->v, a->def->lo, a->def->hi, a->tgt_on ? a->tgt : -1, a->reach, zones, pc_meter_shown(&a->m),
           pc_meter_peak(&a->m), fx_moving(a), a->running, a->hold, a->paused, ui_axis_current_of(a->obj) == 0, (unsigned)a->ticks, a->fps, a->inv_px,
           (long)lv_obj_get_width(a->obj) * lv_obj_get_height(a->obj), a->peak_inv, a->cue_lvl, a->cue_pct, a->shake);
    if (a->def->dump) a->def->dump(a->user);
}
void ui_dial_cmd(const char *arg)
{
    dl_t *a = s_live;
    int n, x, y;
    if (!a) { printf("APP dial none\n"); return; }
    a->inv_px = 0;                                           /* the dump says what this command invalidated */
    if (sscanf(arg, "turn %d", &n) == 1) knob_step(0, n);
    else if (sscanf(arg, "tap %d %d", &x, &y) == 2) touch(a, x, y);
    else if (sscanf(arg, "step %d", &n) == 1) { for (int s = 0; s < LV_CLAMP(0, n, 3000); s++) advance(a); if (!wanted(a)) run(a, false); }
    else if (sscanf(arg, "hold %d", &n) == 1) a->hold = n != 0;
    else if (sscanf(arg, "meter %d", &n) == 1) ui_dial_meter(a->obj, n != 0);
    else if (sscanf(arg, "feed %d", &n) == 1) ui_dial_feed(a->obj, n);
    else if (!strncmp(arg, "game", 4) && (!arg[4] || arg[4] == ' ')) {
        if (!a->def->cmd) { printf("APP dial game none\n"); return; }
        a->def->cmd(arg[4] ? arg + 5 : "", a->user);
    }
    else if (arg[0]) { printf("APP dial unknown %s\n", arg); return; }
    dump(a);
}
