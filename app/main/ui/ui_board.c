// SPDX-License-Identifier: LGPL-2.1-or-later
/* The board (ui_board.h). Layout: the band between the HEAD row and Back (the same numbers the bubble field uses for its bottom edge), the largest pitch
 * whose grid fits the band, the width left by the glass, and (round panel) whose four corners stay inside the design circle; the grid is centred in the band. */
#include "ui_board.h"
#include <stdio.h>
#include <string.h>
#include "misc/lv_area_private.h"
#include "draw/lv_draw_private.h"
#include "pc/pc_utf.h"
#include "psram_alloc.h"
#include "font.h"
#include "ui.h"
#include "ui_geom.h"
#include "ui_frame.h"
#include "ui_kit.h"
#include "ui_theme.h"
#include "game_style.h"

#define GLASS_EDGE  6                         /* the grid keeps this far from the circle */
#define CELL_GAP    3                         /* between two drawn cells */
#define TEXT_MAX    5
#define MAX_SIDE    9
#define SLIDE_FRAME_MS 33                     /* the slide's own timer: 30 fps while a tile moves */

typedef struct { char t[TEXT_MAX + 1]; uint8_t look, mark, hidden, tile, tint; } cell_t;   /* mark: 1 side A, 2 side B, 0 none (kept through HI / DIM); hidden: an effect draws it */
typedef struct {
    lv_obj_t *obj;
    const game_style_t *st;                   /* NULL: style 0 */
    ui_board_tap_cb cb;
    void *user;
    int cols, rows, n, pitch;
    int gx, gy;                               /* the grid's top left, relative to the object */
    bool round;
    cell_t *c;
    int cursor;                               /* the key cursor's cell, -1 none */
    lv_timer_t *tm;                           /* the slide's timer (bound to the object, made at the first slide, paused between slides) */
    int sl_to, sl_from, sl_ms;                /* a tile of cell sl_to sliding in from cell sl_from; sl_to < 0: none */
    uint32_t sl_t0;
} bd_t;

static bd_t *s_live;                          /* the board on the open page (console) */

static bool fits(int p, int cols, int rows, int top, int bottom)
{
    int w = cols * p, h = rows * p;
    if (h > bottom - top || w > UI_W - 2 * GLASS_EDGE) return false;
    if (!UI_ROUND) return true;
    long R = UI_CONTENT_D / 2 - GLASS_EDGE;
    int cy = (top + bottom) / 2;
    int xs = w / 2, ys = h / 2 + (cy < 0 ? -cy : cy);   /* the corner that is farthest from the centre of the circle */
    return (long)xs * xs + (long)ys * ys <= R * R;
}

static void cell_area(const bd_t *b, int i, lv_area_t *a)       /* the whole pitch square, screen coordinates */
{
    lv_area_t o;
    lv_obj_get_coords(b->obj, &o);
    int r = i / b->cols, c = i % b->cols;
    a->x1 = o.x1 + b->gx + c * b->pitch;
    a->y1 = o.y1 + b->gy + r * b->pitch;
    a->x2 = a->x1 + b->pitch - 1;
    a->y2 = a->y1 + b->pitch - 1;
}

static int scaled(int px, int d) { int v = px * d / 62; return v < 2 ? 2 : v; }

static void fill(lv_layer_t *layer, const lv_area_t *a, lv_color_t c, lv_opa_t opa, int radius, int border, lv_color_t bc)
{
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = c;
    d.bg_opa = opa;
    d.radius = (int32_t)radius;
    d.border_width = (int32_t)border;
    d.border_color = bc;
    d.border_opa = border ? LV_OPA_COVER : LV_OPA_TRANSP;
    lv_draw_rect(layer, &d, a);
}
static void ring(lv_layer_t *layer, int cx, int cy, int dia, int w, lv_color_t c, lv_opa_t opa)
{
    lv_area_t a = { cx - dia / 2, cy - dia / 2, cx - dia / 2 + dia - 1, cy - dia / 2 + dia - 1 };
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_opa = LV_OPA_TRANSP;
    d.radius = LV_RADIUS_CIRCLE;
    d.border_width = (int32_t)w;
    d.border_color = c;
    d.border_opa = opa;
    lv_draw_rect(layer, &d, &a);
}
static void disc(lv_layer_t *layer, int cx, int cy, int dia, lv_color_t c, lv_opa_t opa)
{
    lv_area_t a = { cx - dia / 2, cy - dia / 2, cx - dia / 2 + dia - 1, cy - dia / 2 + dia - 1 };
    fill(layer, &a, c, opa, LV_RADIUS_CIRCLE, 0, c);
}
static void stroke(lv_layer_t *layer, int x1, int y1, int x2, int y2, int w, lv_color_t c, lv_opa_t opa)
{
    lv_draw_line_dsc_t l;
    lv_draw_line_dsc_init(&l);
    l.p1.x = x1; l.p1.y = y1; l.p2.x = x2; l.p2.y = y2;
    l.width = w; l.color = c; l.opa = opa; l.round_start = 1; l.round_end = 1;
    lv_draw_line(layer, &l);
}

/* one mark, centred at (cx, cy) in a cell d px across (screen coordinates): the cells and game_fx draw it the same way */
/* the shine on a solid mark: white; a light theme with a pale mark (white would vanish) takes the text colour's side instead */
static lv_color_t sheen(lv_color_t mark)
{
    return ui_theme_is_light() && lv_color_luminance(mark) > 170 ? lv_color_mix(UI_FG, lv_color_white(), 100) : lv_color_white();
}

static void piece(const bd_t *b, lv_layer_t *layer, const game_palette_t *p, int mark, const char *text, int cx, int cy, int d, lv_opa_t opa)
{
    const game_style_t *st = b->st ? b->st : game_style(0);
    lv_color_t c = mark == 1 ? p->a : p->b;
    if (d < 4 || opa == LV_OPA_TRANSP) return;
    if (b->round) {
        if (st->mark == GS_MARK_STROKE) {
            int w = LV_MAX(2, d * 16 / 100);
            ring(layer, cx, cy, d, w * 2, c, (lv_opa_t)(opa / 4));                   /* the glow */
            ring(layer, cx, cy, d - w, w, c, opa);
        } else {
            disc(layer, cx, cy, d, c, opa);
            if (st->mark == GS_MARK_SOLID) disc(layer, cx - d * 18 / 100, cy - d * 18 / 100, LV_MAX(3, d * 24 / 100), sheen(c), (lv_opa_t)(opa * 40 / 100));
        }
        return;
    }
    if (st->mark == GS_MARK_STROKE) {
        int w = scaled(st->stroke, d), h = d * 26 / 100;
        if (mark == 1) {
            stroke(layer, cx - h, cy - h, cx + h, cy + h, w * 5 / 2, c, (lv_opa_t)(opa / 4));
            stroke(layer, cx - h, cy + h, cx + h, cy - h, w * 5 / 2, c, (lv_opa_t)(opa / 4));
            stroke(layer, cx - h, cy - h, cx + h, cy + h, w, c, opa);
            stroke(layer, cx - h, cy + h, cx + h, cy - h, w, c, opa);
        } else {
            ring(layer, cx, cy, d * 62 / 100 + w, w * 5 / 2, c, (lv_opa_t)(opa / 4));
            ring(layer, cx, cy, d * 62 / 100, w, c, opa);
        }
    } else if (st->mark == GS_MARK_SOLID) {
        if (mark == 1) disc(layer, cx, cy, d * 60 / 100, c, opa);
        else {
            int q = d * 54 / 100;
            lv_area_t a = { cx - q / 2, cy - q / 2, cx - q / 2 + q - 1, cy - q / 2 + q - 1 };
            fill(layer, &a, c, opa, q / 4, 0, c);
        }
    } else if (text && text[0]) {
        lv_draw_label_dsc_t t;
        lv_draw_label_dsc_init(&t);
        t.font = d >= 40 ? font_large() : font_main();
        t.align = LV_TEXT_ALIGN_CENTER;
        t.text_local = 1;
        t.color = c;
        t.opa = opa;
        t.text = text;
        int lh = lv_font_get_line_height(t.font);
        lv_area_t ta = { cx - d / 2, cy - lh / 2, cx - d / 2 + d - 1, cy - lh / 2 + lh };
        lv_draw_label(layer, &t, &ta);
    } else disc(layer, cx, cy, d * 40 / 100, c, opa);                                  /* a glyph style with no text: a dot */
}

/* a tile (ui_board_set_tile): a filled square in the mix of the style's two mark colours by its tint, its number or letter on it; in a lines style an outline */
static void draw_tile(const bd_t *b, lv_layer_t *layer, const game_palette_t *p, const game_style_t *st, const lv_area_t *a, const cell_t *c)
{
    const int d = (int)(a->x2 - a->x1 + 1), cx = (a->x1 + a->x2) / 2, cy = (a->y1 + a->y2) / 2;
    const bool hi = c->look == UI_CELL_HI, lines = st->grid == GS_GRID_LINES;
    lv_color_t tc = lv_color_mix(p->b, p->a, (uint8_t)(c->tint * 255 / 100));
    int r = d / 2 * (lines ? 40 : st->radius_pct) / 100;
    lv_draw_rect_dsc_t rd;
    lv_draw_rect_dsc_init(&rd);
    rd.radius = (int32_t)r;
    rd.bg_color = tc;
    rd.bg_opa = lines ? LV_OPA_10 : LV_OPA_COVER;
    rd.border_width = hi ? 3 : lines ? 2 : 0;
    rd.border_color = hi ? p->hi : tc;
    rd.border_opa = rd.border_width ? LV_OPA_COVER : LV_OPA_TRANSP;
    lv_draw_rect(layer, &rd, a);
    if (!c->t[0]) return;
    lv_draw_label_dsc_t t;
    lv_draw_label_dsc_init(&t);
    t.font = d >= 40 ? font_large() : font_main();
    t.align = LV_TEXT_ALIGN_CENTER;
    t.text_local = 1;
    t.color = lines ? tc : ui_theme_is_light() ? ui_text_on(tc) : lv_color_luminance(tc) > 140 ? lv_color_black() : lv_color_white();
    t.text = c->t;
    int lh = lv_font_get_line_height(t.font);
    lv_area_t ta = { a->x1, cy - lh / 2, a->x2, cy - lh / 2 + lh };
    lv_draw_label(layer, &t, &ta);
    (void)cx; (void)b;
}

static void draw_cell(const bd_t *b, lv_layer_t *layer, const game_palette_t *p, const game_style_t *st, const lv_area_t *cell, const cell_t *c)
{
    const bool lines = st->grid == GS_GRID_LINES;
    lv_area_t a = { cell->x1 + CELL_GAP / 2, cell->y1 + CELL_GAP / 2, cell->x2 - CELL_GAP / 2, cell->y2 - CELL_GAP / 2 };
    if (c->tile) { draw_tile(b, layer, p, st, &a, c); return; }
    const int d = (int)(a.x2 - a.x1 + 1), cx = (a.x1 + a.x2) / 2, cy = (a.y1 + a.y2) / 2;
    const bool hi = c->look == UI_CELL_HI;
    if (b->round) {
        if (lines) ring(layer, cx, cy, d, 2, p->line, LV_OPA_COVER);
        else disc(layer, cx, cy, d, p->cell, LV_OPA_COVER);
    } else {
        int r = d / 2 * st->radius_pct / 100;
        if (lines) { if (hi) fill(layer, &a, p->hi, LV_OPA_30, 8, 0, p->hi); }
        else fill(layer, &a, hi ? lv_color_mix(p->hi, p->cell, 90) : c->look == UI_CELL_DIM && !c->mark ? p->line : p->cell, LV_OPA_COVER, r, hi ? 3 : 0, p->hi);
    }
    if (c->mark && !c->hidden) piece(b, layer, p, c->mark, c->t, cx, cy, d, c->look == UI_CELL_DIM ? LV_OPA_40 : LV_OPA_COVER);
    if (b->round && hi) ring(layer, cx, cy, d + 2, 3, p->hi, LV_OPA_COVER);
}

/* where the sliding tile is now: its own cell moved back toward the cell it came from by what is left of the slide (ease out) */
static void slide_offset(const bd_t *b, int *dx, int *dy)
{
    int pct = b->sl_ms > 0 ? (int)(lv_tick_elaps(b->sl_t0) * 100 / (uint32_t)b->sl_ms) : 100;
    if (pct > 100) pct = 100;
    int left = 100 - pct * (200 - pct) / 100;
    *dx = ((b->sl_from % b->cols) - (b->sl_to % b->cols)) * b->pitch * left / 100;
    *dy = ((b->sl_from / b->cols) - (b->sl_to / b->cols)) * b->pitch * left / 100;
}

static void draw_cb(lv_event_t *e)
{
    const bd_t *b = lv_event_get_user_data(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    const lv_area_t clip = layer->_clip_area;
    const game_style_t *st = b->st ? b->st : game_style(0);
    game_palette_t p;
    game_style_palette(st, &p);
    const bool lines = st->grid == GS_GRID_LINES;
    static const cell_t HOLE = { .look = UI_CELL_DIM };                         /* under a sliding tile: the empty cell it left */
    for (int i = 0; i < b->n; i++) {
        lv_area_t cell, x;
        cell_area(b, i, &cell);
        if (!lv_area_intersect(&x, &clip, &cell)) continue;
        draw_cell(b, layer, &p, st, &cell, i == b->sl_to ? &HOLE : &b->c[i]);
    }
    if (b->sl_to >= 0) {                                                        /* the sliding tile on top, between its two cells */
        lv_area_t cell;
        int dx, dy;
        cell_area(b, b->sl_to, &cell);
        slide_offset(b, &dx, &dy);
        cell.x1 += dx; cell.x2 += dx; cell.y1 += dy; cell.y2 += dy;
        draw_cell(b, layer, &p, st, &cell, &b->c[b->sl_to]);
    }
    if (b->cursor >= 0 && b->cursor < b->n) {                                   /* the key cursor: a ring round the cell */
        lv_area_t cell;
        cell_area(b, b->cursor, &cell);
        fill(layer, &cell, p.hi, LV_OPA_TRANSP, st->radius_pct * (b->pitch / 2) / 100, 3, p.hi);
    }
    if (lines && !b->round) {                                                          /* the grid: lines between the cells, not round the outside */
        lv_area_t o;
        lv_obj_get_coords(b->obj, &o);
        int x0 = o.x1 + b->gx, y0 = o.y1 + b->gy, w = scaled(st->stroke, b->pitch) * 2 / 3, pad = b->pitch / 8;
        for (int k = 1; k < b->cols; k++) stroke(layer, x0 + k * b->pitch, y0 + pad, x0 + k * b->pitch, y0 + b->rows * b->pitch - pad, w, p.line, LV_OPA_COVER);
        for (int k = 1; k < b->rows; k++) stroke(layer, x0 + pad, y0 + k * b->pitch, x0 + b->cols * b->pitch - pad, y0 + k * b->pitch, w, p.line, LV_OPA_COVER);
    }
}

void ui_board_draw_piece(const lv_obj_t *board, lv_layer_t *layer, int mark, const char *text, int cx, int cy, int pct, lv_opa_t opa)
{
    const bd_t *b = board ? lv_obj_get_user_data((lv_obj_t *)board) : NULL;
    if (!b || mark < 1 || mark > 2) return;
    game_palette_t p;
    game_style_palette(b->st, &p);
    piece(b, layer, &p, mark, text, cx, cy, (b->pitch - CELL_GAP) * pct / 100, opa);
}

int ui_board_hit(const lv_obj_t *board, int sx, int sy)
{
    const bd_t *b = board ? lv_obj_get_user_data((lv_obj_t *)board) : NULL;
    if (!b) return -1;
    lv_area_t o;
    lv_obj_get_coords(b->obj, &o);
    int px = sx - (o.x1 + b->gx), py = sy - (o.y1 + b->gy), p = b->pitch;
    int c = (px >= 0 ? px / p : -1 - (-px - 1) / p), r = (py >= 0 ? py / p : -1 - (-py - 1) / p);
    c = c < 0 ? 0 : c >= b->cols ? b->cols - 1 : c;       /* the nearest cell centre of a square grid is in the clamped pitch square */
    r = r < 0 ? 0 : r >= b->rows ? b->rows - 1 : r;
    long dx = px - (c * p + p / 2), dy = py - (r * p + p / 2), lim = (long)p * 3 / 4;
    return dx * dx + dy * dy <= lim * lim ? r * b->cols + c : -1;
}

static void inval(const bd_t *b, int i)
{
    lv_area_t a;
    cell_area(b, i, &a);
    lv_obj_invalidate_area(b->obj, &a);
}

static bd_t *of(const lv_obj_t *o) { return o ? lv_obj_get_user_data((lv_obj_t *)o) : NULL; }

bool ui_board_set(lv_obj_t *board, int idx, const char *text, ui_cell_look_t look)
{
    bd_t *b = of(board);
    if (!b || idx < 0 || idx >= b->n || look >= UI_CELL_LOOKS) return false;
    cell_t *c = &b->c[idx];
    char t[TEXT_MAX + 1] = { 0 };
    if (text) pc_utf8_copy(t, sizeof t, text);                  /* cut at a character boundary: never half a character */
    uint8_t mark = look == UI_CELL_A ? 1 : look == UI_CELL_B ? 2 : look == UI_CELL_NORMAL ? 0 : c->mark;   /* HI and DIM keep the cell's mark */
    if (!strcmp(t, c->t) && c->look == (uint8_t)look && c->mark == mark && !c->tile) return true;
    memcpy(c->t, t, sizeof t);
    c->tile = 0;
    c->look = (uint8_t)look;
    c->mark = mark;
    inval(b, idx);
    return true;
}

bool ui_board_set_look(lv_obj_t *board, int idx, ui_cell_look_t look)
{
    bd_t *b = of(board);
    if (!b || idx < 0 || idx >= b->n || look >= UI_CELL_LOOKS) return false;
    if (b->c[idx].tile) {                                          /* a tile keeps its tile; only its look changes */
        if (b->c[idx].look != (uint8_t)look) { b->c[idx].look = (uint8_t)look; inval(b, idx); }
        return true;
    }
    return ui_board_set(board, idx, b->c[idx].t, look);
}

bool ui_board_set_tile(lv_obj_t *board, int idx, const char *text, int tint, ui_cell_look_t look)
{
    bd_t *b = of(board);
    if (!b || idx < 0 || idx >= b->n || look >= UI_CELL_LOOKS) return false;
    cell_t *c = &b->c[idx];
    char t[TEXT_MAX + 1] = { 0 };
    if (text) pc_utf8_copy(t, sizeof t, text);
    uint8_t ti = (uint8_t)(tint < 0 ? 0 : tint > 100 ? 100 : tint);
    if (c->tile && !strcmp(t, c->t) && c->look == (uint8_t)look && c->tint == ti) return true;
    memcpy(c->t, t, sizeof t);
    c->look = (uint8_t)look;
    c->mark = 0;
    c->tile = 1;
    c->tint = ti;
    inval(b, idx);
    return true;
}

void ui_board_clear(lv_obj_t *board)
{
    bd_t *b = of(board);
    if (!b) return;
    memset(b->c, 0, (size_t)b->n * sizeof(cell_t));
    b->sl_to = -1;
    lv_obj_invalidate(b->obj);
}

void ui_board_set_style(lv_obj_t *board, const game_style_t *st)
{
    bd_t *b = of(board);
    if (!b || b->st == st) return;
    b->st = st;
    lv_obj_invalidate(b->obj);
}
const game_style_t *ui_board_style(const lv_obj_t *board) { bd_t *b = of(board); return b && b->st ? b->st : game_style(0); }
int ui_board_pitch(const lv_obj_t *board) { bd_t *b = of(board); return b ? b->pitch : 0; }
bool ui_board_is_round(const lv_obj_t *board) { bd_t *b = of(board); return b && b->round; }
int ui_board_mark(const lv_obj_t *board, int idx) { bd_t *b = of(board); return b && idx >= 0 && idx < b->n ? b->c[idx].mark : 0; }
void ui_board_hide(lv_obj_t *board, int idx, bool hidden)
{
    bd_t *b = of(board);
    if (!b || idx < 0 || idx >= b->n || b->c[idx].hidden == (uint8_t)hidden) return;
    b->c[idx].hidden = (uint8_t)hidden;
    inval(b, idx);
}
bool ui_board_centre(const lv_obj_t *board, int idx, int *x, int *y)
{
    bd_t *b = of(board);
    if (!b || idx < 0 || idx >= b->n) return false;
    lv_area_t a;
    cell_area(b, idx, &a);
    *x = (a.x1 + a.x2) / 2;
    *y = (a.y1 + a.y2) / 2;
    return true;
}

int ui_board_cols(const lv_obj_t *board) { bd_t *b = of(board); return b ? b->cols : 0; }
int ui_board_rows(const lv_obj_t *board) { bd_t *b = of(board); return b ? b->rows : 0; }
const char *ui_board_text(const lv_obj_t *board, int idx) { bd_t *b = of(board); return b && idx >= 0 && idx < b->n ? b->c[idx].t : ""; }

bool ui_board_cell_xy(const lv_obj_t *board, int idx, int *x, int *y)
{
    bd_t *b = of(board);
    if (!b || idx < 0 || idx >= b->n) return false;
    lv_obj_update_layout(b->obj);
    lv_area_t a;
    cell_area(b, idx, &a);
    *x = (a.x1 + a.x2) / 2;
    *y = (a.y1 + a.y2) / 2;
    return true;
}

void ui_board_set_cursor(lv_obj_t *board, int idx)
{
    bd_t *b = of(board);
    if (!b) return;
    if (idx < 0 || idx >= b->n) idx = -1;
    if (b->cursor == idx) return;
    if (b->cursor >= 0) inval(b, b->cursor);
    b->cursor = idx;
    if (idx >= 0) inval(b, idx);
}
int ui_board_cursor(const lv_obj_t *board) { bd_t *b = of(board); return b ? b->cursor : -1; }
bool ui_board_keys(const lv_obj_t *board) { (void)board; return !UI_HAS_KNOB; }

static void inval_slide(const bd_t *b)
{
    lv_area_t a, c;
    cell_area(b, b->sl_from, &a);
    cell_area(b, b->sl_to, &c);
    lv_area_join(&a, &a, &c);
    lv_obj_invalidate_area(b->obj, &a);
}
static void slide_end(bd_t *b)
{
    if (b->sl_to < 0) return;
    inval_slide(b);
    b->sl_to = -1;
    if (b->tm) lv_timer_pause(b->tm);
}
static void slide_tick(lv_timer_t *t)
{
    bd_t *b = lv_timer_get_user_data(t);
    if (b->sl_to < 0) { lv_timer_pause(t); return; }
    if (!ui_screen_on() || lv_tick_elaps(b->sl_t0) >= (uint32_t)b->sl_ms) { slide_end(b); return; }
    inval_slide(b);
}
bool ui_board_slide(lv_obj_t *board, int to, int from, int ms)
{
    bd_t *b = of(board);
    if (!b || to < 0 || from < 0 || to >= b->n || from >= b->n || to == from || ms <= 0) return false;
    if (b->sl_to >= 0) slide_end(b);                               /* one slide at a time: a new one ends the one in flight */
    if (!ui_screen_on()) return false;                             /* nothing animates with the screen off */
    if (!b->tm) { b->tm = ui_timer_bind(b->obj, slide_tick, SLIDE_FRAME_MS, b); if (!b->tm) return false; }
    b->sl_to = to; b->sl_from = from; b->sl_ms = ms; b->sl_t0 = lv_tick_get();
    lv_timer_resume(b->tm);
    inval_slide(b);
    return true;
}

static void click_cb(lv_event_t *e)
{
    bd_t *b = lv_event_get_user_data(e);
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    int i = ui_board_hit(b->obj, p.x, p.y);
    if (i >= 0 && b->cb) b->cb(i, b->user);
}

static void delete_cb(lv_event_t *e)
{
    bd_t *b = lv_event_get_user_data(e);
    if (s_live == b) s_live = NULL;
    psram_free(b);
}

lv_obj_t *ui_board_create(lv_obj_t *parent, const ui_board_def_t *def, ui_board_tap_cb cb, void *user)
{
    if (!def || def->layout != UI_BOARD_SQUARE || def->cols < 1 || def->rows < 1 || def->cols > MAX_SIDE || def->rows > MAX_SIDE) return NULL;
    int top, bottom;                                                          /* centre offsets */
    ui_frame_band(parent, &top, &bottom);
    int p = ui_frame_active(parent) ? 200 : UI_ROUND ? 80 : 140;   /* no frame: the square panel starts larger (the band decides); a frame page fills the panel */
    while (p > 8 && !fits(p, def->cols, def->rows, top, bottom)) p--;
    int n = def->cols * def->rows;
    bd_t *b = psram_calloc(1, sizeof(bd_t) + (size_t)n * sizeof(cell_t));
    if (!b) return NULL;
    b->c = (cell_t *)(b + 1);
    b->cols = def->cols; b->rows = def->rows; b->n = n; b->pitch = p; b->round = def->round;
    b->cb = cb; b->user = user;
    b->cursor = -1; b->sl_to = -1;
    int gw = def->cols * p, gh = def->rows * p;
    int mx = p / 4, my = LV_MIN(p / 4, (bottom - top - gh) / 2);     /* the touch margin: never into the HEAD row or Back's gap */
    if (my < 0) my = 0;
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, gw + 2 * mx, gh + 2 * my);
    lv_obj_align(o, LV_ALIGN_CENTER, 0, (top + bottom) / 2);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
    b->gx = mx; b->gy = my;
    b->obj = o;
    lv_obj_add_event_cb(o, draw_cb, LV_EVENT_DRAW_MAIN, b);
    lv_obj_add_event_cb(o, click_cb, LV_EVENT_CLICKED, b);
    lv_obj_add_event_cb(o, delete_cb, LV_EVENT_DELETE, b);
    lv_obj_set_user_data(o, b);
    s_live = b;
    return o;
}

static void dump(const bd_t *b)
{
    static const char LK[UI_CELL_LOOKS] = { 'n', 'a', 'b', 'h', 'd' };
    printf("APP board w=%d h=%d\n", b->cols, b->rows);
    for (int r = 0; r < b->rows; r++) {
        char line[MAX_SIDE * (TEXT_MAX + 1) + 1], lk[2 * MAX_SIDE + 1];
        int k = 0, l = 0;
        for (int c = 0; c < b->cols; c++) {
            const cell_t *x = &b->c[r * b->cols + c];
            k += snprintf(line + k, sizeof line - (size_t)k, c ? " %s" : "%s", x->t[0] ? x->t : "-");
            lk[l++] = LK[x->look < UI_CELL_LOOKS ? x->look : 0];
            if (c < b->cols - 1) lk[l++] = ' ';
        }
        lk[l] = 0;
        printf("APP board row %d %s\n", r, line);
        printf("APP board look %d %s\n", r, lk);
    }
}

void ui_board_cmd(const char *arg)
{
    bd_t *b = s_live;
    int i, x, y;
    if (!b) { printf("APP board none\n"); return; }
    if (!arg[0]) dump(b);
    else if (sscanf(arg, "tap %d", &i) == 1) {
        if (i >= 0 && i < b->n && b->cb) b->cb(i, b->user);
        dump(b);
    }
    else if (!strcmp(arg, "slide")) printf("APP board slide on=%d to=%d from=%d cursor=%d keys=%d\n", b->sl_to >= 0, b->sl_to, b->sl_from, b->cursor, !UI_HAS_KNOB);
    else if (sscanf(arg, "at %d", &i) == 1 && ui_board_cell_xy(b->obj, i, &x, &y)) printf("APP board at %d x=%d,y=%d\n", i, x, y);
    else if (sscanf(arg, "hit %d %d", &x, &y) == 2) { lv_obj_update_layout(b->obj); printf("APP board hit %d,%d = %d\n", x, y, ui_board_hit(b->obj, x, y)); }
    else printf("APP board unknown %s\n", arg);
}
