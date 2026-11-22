// SPDX-License-Identifier: LGPL-2.1-or-later
/* Cards (ui_cards.h). Layout: the band between the HEAD row and Back (the numbers ui_board uses; round panel: as wide as keeps the corners inside the circle),
 * piles in zones given in permille of it, faces drawn by code from a face number. Every position is computed from the state when it is needed (draw, hit test,
 * console): there is no layout cache to go stale. */
#include "ui_cards.h"
#include "ui_tokens.h"
#include <stdio.h>
#include <string.h>
#include "misc/lv_area_private.h"
#include "draw/lv_draw_private.h"
#include "game_style.h"
#include "psram_alloc.h"
#include "ui.h"
#include "ui_geom.h"
#include "ui_frame.h"
#include "ui_kit.h"

#define GLASS_EDGE 6
typedef struct { int faces, aspect; } set_info_t;       /* per face set: how many faces (a face number is 0..faces-1) and the card's long side / short side */
static const set_info_t SETS[] = { [UI_CARDS_SET_DOMINO] = { 49, 2 } };   /* the domino: v1 * 7 + v2, long side = 2 x short side */
#define GAP        3
#define LIFT_PCT   12
#define THINK_MS   250

typedef struct { uint16_t face[UI_CARDS_MAX]; uint8_t flag[UI_CARDS_MAX]; uint8_t n, ends; } pile_t;
typedef struct {
    lv_obj_t *obj;
    const ui_cards_def_t *def;
    void *user;
    const game_style_t *st;
    lv_timer_t *timer;
    int w, h;
    int faces, aspect;                        /* of the face set */
    int sel_p, sel_i;
    int think_p, phase;                       /* think_p -1: off */
    bool enabled;
    pile_t p[UI_CARDS_PILES];
} cd_t;
typedef struct { int cw, ch, step, x0, y0, per_row, lift; } geo_t;   /* object-relative; ch is the card's height as drawn (a SNAKE card lies on its side) */

static cd_t *s_live;                          /* the cards on the open page (console) */

static cd_t *of(const lv_obj_t *o) { return o ? lv_obj_get_user_data((lv_obj_t *)o) : NULL; }
static bool pile_ok(const cd_t *c, int p) { return c && p >= 0 && p < c->def->piles; }

static void geo_of(const cd_t *c, int p, geo_t *g)
{
    const ui_pile_def_t *d = &c->def->pile[p];
    const pile_t *pl = &c->p[p];
    const int zx = c->w * d->x0 / 1000, zy = c->h * d->y0 / 1000, zw = c->w * (d->x1 - d->x0) / 1000, zh = c->h * (d->y1 - d->y0) / 1000;
    memset(g, 0, sizeof *g);
    if (d->layout == UI_PILE_FAN) {
        g->lift = zh * LIFT_PCT / 100;
        const int sc = d->scale_pct > 0 ? d->scale_pct : 100;
        g->ch = (zh - g->lift) * sc / 100;
        g->cw = g->ch / c->aspect;
        const int n = pl->n, full = g->cw + GAP, avail = zw - g->cw;
        g->step = n > 1 ? LV_MIN(full, avail / (n - 1)) : 0;
        if (g->step < 1) g->step = 1;
        const int rowlen = g->cw + (n > 1 ? g->step * (n - 1) : 0);
        g->x0 = zx + (zw - rowlen) / 2;
        g->y0 = zy + g->lift + (zh - g->lift - g->ch) / 2;
    } else {
        const int cap = LV_CLAMP(1, d->cap, UI_CARDS_MAX);
        int t = 24, per = 1;
        for (; t > 6; t--) {
            per = LV_MAX(1, zw / (c->aspect * t + 2));
            if (((cap + per - 1) / per) * (t + 3) <= zh) break;
        }
        g->cw = c->aspect * t; g->ch = t;
        g->per_row = per;
        g->step = g->cw + 2;
        const int rows = (pl->n + per - 1) / per;
        g->x0 = zx + (zw - (per * g->step - 2)) / 2;
        g->y0 = zy + (zh - (rows > 0 ? rows * (t + 3) - 3 : 0)) / 2;
    }
}
/* a card's box and its orientation (0 upright, 1 lying, first half left, 2 lying, first half right), object-relative */
static void card_box(const cd_t *c, int p, int i, lv_area_t *a, int *orient)
{
    geo_t g;
    geo_of(c, p, &g);
    int x, y, o = 0;
    if (c->def->pile[p].layout == UI_PILE_FAN) {
        x = g.x0 + i * g.step;
        y = g.y0 - ((c->p[p].flag[i] & UI_CARD_SEL) ? g.lift : 0);
    } else {
        const int r = i / g.per_row, col = i % g.per_row, cc = (r & 1) ? g.per_row - 1 - col : col;
        x = g.x0 + cc * g.step;
        y = g.y0 + r * (g.ch + 3);
        o = (r & 1) ? 2 : 1;
    }
    a->x1 = x; a->y1 = y; a->x2 = x + g.cw - 1; a->y2 = y + g.ch - 1;
    if (orient) *orient = o;
}
static void abs_area(const cd_t *c, lv_area_t *a)
{
    lv_area_t o;
    lv_obj_get_coords(c->obj, &o);
    a->x1 += o.x1; a->x2 += o.x1; a->y1 += o.y1; a->y2 += o.y1;
}

/* ---- drawing ---- */
typedef struct { lv_color_t body, rim, pip, hi, hint, back; } look_t;
static void looks(const game_style_t *st, look_t *l)
{
    game_palette_t p;
    game_style_palette(st, &p);
    l->hi = p.hi; l->hint = p.fx;
    if (st->mark == GS_MARK_STROKE) { l->body = p.cell; l->rim = p.a; l->pip = p.b; }
    else if (st->mark == GS_MARK_SOLID) { l->body = p.cell; l->rim = p.line; l->pip = p.a; }
    else { l->body = ui_palc(UI_PAL_cards, 0); l->rim = ui_palc(UI_PAL_cards, 1); l->pip = ui_palc(UI_PAL_cards, 2); }
    l->back = st->mark == GS_MARK_GLYPH ? ui_palc(UI_PAL_cards, 3) : p.line;
}
static void rect(lv_layer_t *ly, const lv_area_t *a, lv_color_t c, lv_opa_t opa, int radius, int bw, lv_color_t bc)
{
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = c; d.bg_opa = opa; d.radius = radius;
    d.border_width = bw; d.border_color = bc; d.border_opa = bw ? opa : LV_OPA_TRANSP;
    lv_draw_rect(ly, &d, a);
}
static const uint16_t PIPS[7] = { 0, 0x010, 0x101, 0x111, 0x145, 0x155, 0x16D };   /* 3 x 3 grid bits, row-major from the top left: bit 0 TL ... bit 8 BR (2: TL+BR, 4: corners, 6: the two columns) */
static void pips(lv_layer_t *ly, const lv_area_t *h, int v, lv_color_t c, lv_opa_t opa)
{
    const int w = h->x2 - h->x1 + 1, hh = h->y2 - h->y1 + 1, s = LV_MIN(w, hh), dia = LV_MAX(2, s / 5);
    const int ix = w * 22 / 100, iy = hh * 22 / 100;
    for (int k = 0; k < 9; k++) {
        if (!(PIPS[v] >> k & 1)) continue;
        const int cx = h->x1 + ix + (w - 2 * ix - 1) * (k % 3) / 2, cy = h->y1 + iy + (hh - 2 * iy - 1) * (k / 3) / 2;
        lv_area_t a = { cx - dia / 2, cy - dia / 2, cx - dia / 2 + dia - 1, cy - dia / 2 + dia - 1 };
        rect(ly, &a, c, opa, LV_RADIUS_CIRCLE, 0, c);
    }
}
static void draw_card(lv_layer_t *ly, const look_t *l, const lv_area_t *a, int face, int orient, int flag)
{
    const int w = a->x2 - a->x1 + 1, h = a->y2 - a->y1 + 1, r = LV_MAX(2, LV_MIN(w, h) / 5);
    if (face < 0 || face >= 49) face = 0;                                    /* ui_cards_set already clamps; the set is the domino one here */
    const lv_opa_t opa = (flag & UI_CARD_DIM) ? LV_OPA_50 : LV_OPA_COVER;
    if (flag & UI_CARD_DOWN) {
        rect(ly, a, l->back, opa, r, 1, l->rim);
        lv_area_t in = { a->x1 + w / 4, a->y1 + h / 4, a->x2 - w / 4, a->y2 - h / 4 };
        rect(ly, &in, l->rim, (lv_opa_t)(opa / 3), r / 2, 0, l->rim);
    } else {
        const bool gold = flag & UI_CARD_GOLD;
        const lv_color_t body = gold ? ui_palc(UI_PAL_cards, 4) : l->body, pip = gold ? ui_palc(UI_PAL_cards, 5) : l->pip;
        rect(ly, a, body, opa, r, gold ? 2 : 1, gold ? ui_palc(UI_PAL_cards, 6) : l->rim);
        const int v1 = face / 7, v2 = face % 7;
        lv_area_t h1, h2;
        if (orient == 0) {
            h1 = (lv_area_t){ a->x1, a->y1, a->x2, a->y1 + h / 2 - 1 };
            h2 = (lv_area_t){ a->x1, a->y1 + h / 2, a->x2, a->y2 };
            lv_area_t ln = { a->x1 + w / 6, a->y1 + h / 2, a->x2 - w / 6, a->y1 + h / 2 };
            rect(ly, &ln, pip, (lv_opa_t)(opa / 2), 0, 0, pip);
        } else {
            h1 = (lv_area_t){ a->x1, a->y1, a->x1 + w / 2 - 1, a->y2 };
            h2 = (lv_area_t){ a->x1 + w / 2, a->y1, a->x2, a->y2 };
            lv_area_t ln = { a->x1 + w / 2, a->y1 + h / 6, a->x1 + w / 2, a->y2 - h / 6 };
            rect(ly, &ln, pip, (lv_opa_t)(opa / 2), 0, 0, pip);
        }
        if (orient == 2) { const int t = v1; pips(ly, &h1, v2, pip, opa); pips(ly, &h2, t, pip, opa); }
        else { pips(ly, &h1, v1, pip, opa); pips(ly, &h2, v2, pip, opa); }
    }
    if (flag & UI_CARD_SEL) { lv_area_t o = { a->x1 - 2, a->y1 - 2, a->x2 + 2, a->y2 + 2 }; rect(ly, &o, l->hi, LV_OPA_TRANSP, r + 2, 2, l->hi); }
    else if (flag & UI_CARD_HI) { lv_area_t o = { a->x1 - 1, a->y1 - 1, a->x2 + 1, a->y2 + 1 }; rect(ly, &o, l->hint, LV_OPA_TRANSP, r + 1, 1, l->hint); }
}
static void draw_cb(lv_event_t *e)
{
    const cd_t *c = lv_event_get_user_data(e);
    lv_layer_t *ly = lv_event_get_layer(e);
    const game_style_t *st = c->st ? c->st : game_style(0);
    look_t l;
    looks(st, &l);
    for (int p = 0; p < c->def->piles; p++) {
        const pile_t *pl = &c->p[p];
        for (int i = 0; i < pl->n; i++) {
            lv_area_t a, x;
            int o;
            card_box(c, p, i, &a, &o);
            abs_area(c, &a);
            if (!lv_area_intersect(&x, &ly->_clip_area, &(lv_area_t){ a.x1 - 3, a.y1 - 3, a.x2 + 3, a.y2 + 3 })) continue;
            draw_card(ly, &l, &a, pl->face[i], o, pl->flag[i]);
        }
        for (int k = 0; k < 2; k++) {                                          /* the open ends' rings */
            if (!(pl->ends >> k & 1) || !pl->n) continue;
            lv_area_t a;
            card_box(c, p, k ? pl->n - 1 : 0, &a, NULL);
            abs_area(c, &a);
            lv_area_t o = { a.x1 - 2, a.y1 - 2, a.x2 + 2, a.y2 + 2 };
            rect(ly, &o, l.hi, LV_OPA_TRANSP, 4, 2, l.hi);
        }
    }
    if (c->think_p >= 0 && c->think_p < c->def->piles) {
        const ui_pile_def_t *d = &c->def->pile[c->think_p];
        lv_area_t o;
        lv_obj_get_coords(c->obj, &o);
        const int cy = o.y1 + c->h * (d->y0 + d->y1) / 2000, cx = o.x1 + c->w * d->x1 / 1000 - 18;
        for (int k = 0; k < 3; k++) {
            const int dia = k == c->phase % 3 ? 8 : 5;
            lv_area_t a = { cx + k * 12 - dia / 2, cy - dia / 2, cx + k * 12 - dia / 2 + dia - 1, cy - dia / 2 + dia - 1 };
            rect(ly, &a, l.hi, LV_OPA_COVER, LV_RADIUS_CIRCLE, 0, l.hi);
        }
    }
}

/* ---- hit test and taps ---- */
static int hit(const cd_t *c, int x, int y, int *idx)
{
    lv_area_t o;
    lv_obj_get_coords(c->obj, &o);
    const int px = x - o.x1, py = y - o.y1;
    if (idx) *idx = -1;
    for (int p = c->def->piles - 1; p >= 0; p--) {                           /* a card's own box first, the front card where they overlap */
        for (int i = c->p[p].n - 1; i >= 0; i--) {
            lv_area_t a;
            card_box(c, p, i, &a, NULL);
            if (px >= a.x1 - 1 && px <= a.x2 + 1 && py >= a.y1 - 1 && py <= a.y2 + 1) { if (idx) *idx = i; return p; }
        }
    }
    int best = -1;
    for (int p = 0; p < c->def->piles; p++) {                                /* else the zone that holds the point, with its nearest card */
        const ui_pile_def_t *d = &c->def->pile[p];
        if (px >= c->w * d->x0 / 1000 && px <= c->w * d->x1 / 1000 && py >= c->h * d->y0 / 1000 && py <= c->h * d->y1 / 1000) { best = p; break; }
    }
    if (best >= 0 && idx) {
        long bd = 0;
        for (int i = 0; i < c->p[best].n; i++) {
            lv_area_t a;
            card_box(c, best, i, &a, NULL);
            const long dx = px - (a.x1 + a.x2) / 2, dy = py - (a.y1 + a.y2) / 2, d2 = dx * dx + dy * dy;
            if (*idx < 0 || d2 < bd) { bd = d2; *idx = i; }
        }
    }
    return best;
}
static void set_sel(cd_t *c, int p, int i)
{
    if (c->sel_p >= 0 && c->sel_p < c->def->piles && c->sel_i >= 0 && c->sel_i < c->p[c->sel_p].n) c->p[c->sel_p].flag[c->sel_i] &= (uint8_t)~UI_CARD_SEL;
    c->sel_p = p; c->sel_i = i;
    if (p >= 0 && i >= 0) c->p[p].flag[i] |= UI_CARD_SEL;
    lv_obj_invalidate(c->obj);
    if (c->def->select) c->def->select(p, i, c->user);
}
void ui_cards_select(lv_obj_t *obj, int p, int i)
{
    cd_t *c = of(obj);
    if (!c) return;
    if (p < 0 || i < 0 || !pile_ok(c, p) || i >= c->p[p].n) { p = -1; i = -1; }
    if (p == c->sel_p && i == c->sel_i) return;
    set_sel(c, p, i);
}
static void touch(cd_t *c, int p, int i)
{
    if (!c || !c->enabled || !pile_ok(c, p)) return;
    const ui_pile_def_t *d = &c->def->pile[p];
    if (c->sel_p >= 0 && d->dst) {
        const int sp = c->sel_p, si = c->sel_i;
        if (c->def->move && c->def->move(sp, si, p, i, c->user)) {
            if (c->sel_p == sp && c->sel_i == si) { c->sel_p = c->sel_i = -1; }   /* the page may have rebuilt the piles already */
            lv_obj_invalidate(c->obj);
            return;
        }
        if (!d->src) return;                                                  /* refused: the selection stays */
    }
    if (d->src && i >= 0 && i < c->p[p].n) {
        ui_haptic_tick();
        if (c->sel_p == p && c->sel_i == i) set_sel(c, -1, -1); else { if (c->sel_p >= 0) set_sel(c, -1, -1); set_sel(c, p, i); }
    }
}
void ui_cards_tap(lv_obj_t *obj, int p, int i) { touch(of(obj), p, i); }
static void click_cb(lv_event_t *e)
{
    cd_t *c = lv_event_get_user_data(e);
    lv_point_t pt;
    lv_indev_get_point(lv_indev_active(), &pt);
    int i;
    const int p = hit(c, pt.x, pt.y, &i);
    if (p >= 0) touch(c, p, i);
}

/* ---- the state ---- */
void ui_cards_set(lv_obj_t *obj, int p, const uint16_t *faces, const uint8_t *flags, int n)
{
    cd_t *c = of(obj);
    if (!pile_ok(c, p) || n < 0 || (n > 0 && !faces)) return;
    n = LV_MIN(n, UI_CARDS_MAX);
    if (c->def->pile[p].layout == UI_PILE_SNAKE) n = LV_MIN(n, LV_CLAMP(1, c->def->pile[p].cap, UI_CARDS_MAX));   /* a snake never holds more than its zone was sized for */
    pile_t *pl = &c->p[p];
    pl->n = (uint8_t)n;
    for (int i = 0; i < n; i++) {
        pl->face[i] = faces[i] < c->faces ? faces[i] : 0;                    /* a face number outside the set shows the first face */
        pl->flag[i] = flags ? (uint8_t)(flags[i] & ~UI_CARD_SEL) : 0;        /* the selection is set by ui_cards_select only */
    }
    if (c->sel_p == p) { c->sel_p = c->sel_i = -1; }                          /* a rebuilt pile drops its selection (the page re-selects if it wants) */
    lv_obj_invalidate(c->obj);
}
void ui_cards_flag(lv_obj_t *obj, int p, int i, int flag, bool on)
{
    cd_t *c = of(obj);
    if (!pile_ok(c, p) || i < 0 || i >= c->p[p].n) return;
    if (flag & UI_CARD_SEL) {                                                 /* the selection has one owner: it stays in step with sel_p / sel_i */
        if (on) ui_cards_select(obj, p, i); else if (c->sel_p == p && c->sel_i == i) ui_cards_select(obj, -1, -1);
        flag &= ~UI_CARD_SEL;
        if (!flag) return;
    }
    uint8_t *f = &c->p[p].flag[i];
    const uint8_t nv = on ? (uint8_t)(*f | flag) : (uint8_t)(*f & ~flag);
    if (nv != *f) { *f = nv; lv_obj_invalidate(c->obj); }
}
void ui_cards_ends(lv_obj_t *obj, int p, int mask)
{
    cd_t *c = of(obj);
    if (!pile_ok(c, p) || c->p[p].ends == (uint8_t)mask) return;
    c->p[p].ends = (uint8_t)mask;
    lv_obj_invalidate(c->obj);
}
bool ui_cards_selected(const lv_obj_t *obj, int *p, int *i)
{
    const cd_t *c = of(obj);
    if (!c || c->sel_p < 0) return false;
    if (p) *p = c->sel_p;
    if (i) *i = c->sel_i;
    return true;
}
void ui_cards_enabled(lv_obj_t *obj, bool on) { cd_t *c = of(obj); if (c) c->enabled = on; }
void ui_cards_set_style(lv_obj_t *obj, const game_style_t *st) { cd_t *c = of(obj); if (c) { c->st = st; lv_obj_invalidate(c->obj); } }
int ui_cards_hit(const lv_obj_t *obj, int x, int y, int *idx) { const cd_t *c = of(obj); int d; if (!c) return -1; lv_obj_update_layout(c->obj); return hit(c, x, y, idx ? idx : &d); }
bool ui_cards_xy(const lv_obj_t *obj, int p, int i, int *x, int *y)
{
    const cd_t *c = of(obj);
    if (!pile_ok(c, p) || i < 0 || i >= c->p[p].n) return false;
    lv_obj_update_layout(c->obj);
    lv_area_t a;
    card_box(c, p, i, &a, NULL);
    abs_area(c, &a);
    *x = (a.x1 + a.x2) / 2; *y = (a.y1 + a.y2) / 2;
    return true;
}

static void tick(lv_timer_t *t)
{
    cd_t *c = lv_timer_get_user_data(t);
    if (!ui_screen_on()) return;                                              /* the screen is off: the dots rest */
    c->phase++;
    lv_obj_invalidate(c->obj);
}
void ui_cards_think(lv_obj_t *obj, int p, bool on)
{
    cd_t *c = of(obj);
    if (!c) return;
    c->think_p = on && pile_ok(c, p) ? p : -1;
    c->phase = 0;
    if (c->timer) { if (c->think_p >= 0) lv_timer_resume(c->timer); else lv_timer_pause(c->timer); }
    lv_obj_invalidate(c->obj);
}

static void delete_cb(lv_event_t *e)
{
    cd_t *c = lv_event_get_user_data(e);
    if (s_live == c) s_live = NULL;
    psram_free(c);                                                            /* the timer is bound to the object: it goes with it */
}
lv_obj_t *ui_cards_create(lv_obj_t *parent, const ui_cards_def_t *def, void *user)
{
    if (!parent || !def || def->set != UI_CARDS_SET_DOMINO || def->piles < 1 || def->piles > UI_CARDS_PILES) return NULL;
    cd_t *c = psram_calloc(1, sizeof *c);
    if (!c) return NULL;
    int top, bottom;                                                          /* centre offsets, as ui_board */
    ui_frame_band(parent, &top, &bottom);
    c->h = bottom - top;
    c->w = UI_W - 2 * GLASS_EDGE;
    if (UI_ROUND) {                                                           /* the widest rectangle whose four corners stay inside the circle */
        const long R = UI_CONTENT_D / 2 - GLASS_EDGE;
        const int ym = LV_MAX(LV_ABS(top), LV_ABS(bottom));
        long hw2 = R * R - (long)ym * ym, hw = 0;
        while ((hw + 1) * (hw + 1) <= hw2) hw++;
        c->w = LV_MIN(c->w, (int)(2 * hw));
    }
    c->def = def; c->user = user;
    c->faces = SETS[def->set].faces; c->aspect = SETS[def->set].aspect;
    c->sel_p = c->sel_i = -1; c->think_p = -1; c->enabled = true;
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, c->w, c->h);
    lv_obj_align(o, LV_ALIGN_CENTER, 0, (top + bottom) / 2);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
    c->obj = o;
    lv_obj_set_user_data(o, c);
    lv_obj_add_event_cb(o, draw_cb, LV_EVENT_DRAW_MAIN, c);
    lv_obj_add_event_cb(o, click_cb, LV_EVENT_CLICKED, c);
    lv_obj_add_event_cb(o, delete_cb, LV_EVENT_DELETE, c);
    c->timer = ui_timer_bind(o, tick, THINK_MS, c);
    if (c->timer) lv_timer_pause(c->timer);
    lv_obj_update_layout(o);
    s_live = c;
    return o;
}

/* ---- console ---- */
static void dump(const cd_t *c)
{
    geo_t g;
    geo_of(c, 0, &g);
    printf("APP cards set=domino piles=%d sel=%d:%d en=%d think=%d w=%d h=%d card=%dx%d\n", c->def->piles, c->sel_p, c->sel_i, c->enabled, c->think_p >= 0, c->w, c->h, g.cw, g.ch);
    for (int p = 0; p < c->def->piles; p++) {
        const pile_t *pl = &c->p[p];
        char f[UI_CARDS_MAX * 4 + 2] = "", fl[UI_CARDS_MAX * 3 + 2] = "";
        int a = 0, b = 0;
        for (int i = 0; i < pl->n; i++) {
            a += snprintf(f + a, sizeof f - (size_t)a, i ? ",%d" : "%d", pl->face[i]);
            b += snprintf(fl + b, sizeof fl - (size_t)b, i ? ",%x" : "%x", pl->flag[i]);
        }
        geo_of(c, p, &g);
        printf("APP cards pile %d n=%d faces=%s flags=%s ends=%d step=%d card=%dx%d\n", p, pl->n, pl->n ? f : "-", pl->n ? fl : "-", pl->ends, g.step, g.cw, g.ch);
    }
}
void ui_cards_cmd(const char *arg)
{
    cd_t *c = s_live;
    int p, i, x, y;
    if (!c) { printf("APP cards none\n"); return; }
    if (sscanf(arg, "tap %d %d", &p, &i) == 2) ui_cards_tap(c->obj, p, i);
    else if (sscanf(arg, "at %d %d", &p, &i) == 2) {
        if (ui_cards_xy(c->obj, p, i, &x, &y)) printf("APP cards at %d %d x=%d,y=%d\n", p, i, x, y); else printf("APP cards at %d %d none\n", p, i);
        return;
    }
    else if (sscanf(arg, "hit %d %d", &x, &y) == 2) { p = ui_cards_hit(c->obj, x, y, &i); printf("APP cards hit %d %d\n", p, i); return; }
    else if (arg[0]) { printf("APP cards unknown %s\n", arg); return; }
    dump(c);
}
