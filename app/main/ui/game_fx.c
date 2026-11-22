// SPDX-License-Identifier: LGPL-2.1-or-later
/* In-field effects of the games (game_fx.h). */
#include "game_fx.h"
#include <stdio.h>
#include <string.h>
#include "draw/lv_draw_private.h"
#include "game_style.h"
#include "psram_alloc.h"
#include "ui.h"
#include "ui_board.h"
#include "ui_kit.h"
#include "ui_theme.h"

#define FX_MS     33                          /* 30 fps */
#define FX_MAX    16
#define TEXT_MAX  5
enum { K_DROP = 1, K_FADE, K_LINE, K_GLOW, K_WARN, K_BURST };
static const char *const KNAME[] = { "-", "drop", "fade", "line", "glow", "warn", "burst" };
typedef struct {
    uint8_t kind, frame, dur, n, mark;
    int8_t rows;
    int8_t cells[4];
    char t[TEXT_MAX + 1];
} fx_t;
typedef struct {
    lv_obj_t *obj, *board;
    lv_timer_t *timer;
    fx_t e[FX_MAX];
    int n;
    bool hold, think, running;
    uint32_t frames;                          /* frames advanced since create (the think dots' clock too) */
    int inv_n;                                /* the last advance: areas invalidated and their pixels (console: a frame redraws only what moves) */
    long inv_px;
} gfx_t;

static gfx_t *s_live;                         /* the open page's (console) */

static gfx_t *of(const lv_obj_t *o) { return o ? lv_obj_get_user_data((lv_obj_t *)o) : NULL; }
static bool moving(const fx_t *f) { return f->kind && f->frame < f->dur; }
static int active(const gfx_t *g) { int k = 0; for (int i = 0; i < g->n; i++) k += moving(&g->e[i]); return k; }
static void run(gfx_t *g, bool on)
{
    if (!g->timer || g->running == on) return;
    g->running = on;
    if (on) lv_timer_resume(g->timer); else lv_timer_pause(g->timer);
}
static void retire(gfx_t *g, fx_t *f) { if (f->kind == K_DROP) ui_board_hide(g->board, f->cells[0], false); }   /* one drop per cell at a time (game_fx_drop) */

/* ---- what a frame redraws: each effect's box (the same for its whole life, so it covers where it was and where it is) and the dots' strip ---- */
static bool box(const gfx_t *g, const fx_t *f, lv_area_t *a)
{
    const int P = ui_board_pitch(g->board), pad = 4;
    int x, y;
    if (!ui_board_centre(g->board, f->cells[0], &x, &y)) return false;
    int r = f->kind == K_BURST ? P * 9 / 10 + 8 : f->kind == K_GLOW || f->kind == K_WARN ? P / 2 + P / 14 + 10 : P / 2 + pad;
    *a = (lv_area_t){ x - r, y - r, x + r, y + r };
    if (f->kind == K_DROP) a->y1 -= f->rows * P;
    for (int c = 1; c < f->n; c++) {                                      /* the line: every cell's box */
        if (!ui_board_centre(g->board, f->cells[c], &x, &y)) continue;
        a->x1 = LV_MIN(a->x1, x - r); a->y1 = LV_MIN(a->y1, y - r); a->x2 = LV_MAX(a->x2, x + r); a->y2 = LV_MAX(a->y2, y + r);
    }
    return true;
}
static int dot_d(const gfx_t *g) { return LV_MAX(4, ui_board_pitch(g->board) / 9); }
static void strip(const gfx_t *g, lv_area_t *a)                           /* the think dots: three dots under the grid */
{
    lv_area_t o;
    lv_obj_get_coords(g->obj, &o);
    int d = dot_d(g), cx = (o.x1 + o.x2) / 2;
    *a = (lv_area_t){ cx - 3 * d, o.y2 - 2 * d, cx + 3 * d, o.y2 };
}
static void inval(gfx_t *g, const lv_area_t *a)
{
    lv_area_t c = *a;
    lv_obj_invalidate_area(g->obj, &c);
    g->inv_n++;
    g->inv_px += (long)lv_area_get_width(a) * lv_area_get_height(a);
}
static void inval_fx(gfx_t *g, const fx_t *f) { lv_area_t a; if (box(g, f, &a)) inval(g, &a); }
static void inval_strip(gfx_t *g) { lv_area_t a; strip(g, &a); inval(g, &a); }

/* one frame for every effect: finished ones go (the line stays at its end), a drop's cell shows again at its end */
static void advance(gfx_t *g, int frames)
{
    g->inv_n = 0;
    g->inv_px = 0;
    for (int s = 0; s < frames; s++) {
        g->frames++;
        int k = 0;
        for (int i = 0; i < g->n; i++) {
            fx_t *f = &g->e[i];
            if (f->frame < f->dur) { f->frame++; if (s == 0) inval_fx(g, f); }   /* a line at its end that stays is not redrawn */
            if (f->frame >= f->dur && f->kind != K_LINE) { retire(g, f); continue; }
            g->e[k++] = *f;
        }
        g->n = k;
    }
    if (g->think) inval_strip(g);
}

static void tick(lv_timer_t *t)
{
    gfx_t *g = lv_timer_get_user_data(t);
    if (g->hold) return;
    if (!ui_screen_on()) {                    /* nothing animates with the screen off: every effect is at its end */
        if (active(g)) advance(g, 16);        /* longer than the longest effect */
        if (!g->think) run(g, false);         /* thinking: the timer stays (a callback that returns), so the dots move again at the wake */
        return;
    }
    advance(g, 1);
    if (!active(g) && !g->think) run(g, false);
}

/* a new effect (the caller fills its cells, then start()); NULL without a timer: nothing would end it, so the effect is not made at all (the board shows
 * the end state at once), except a line, which is made at its end (it only stays) */
static fx_t *add(gfx_t *g, int kind, int dur)
{
    if (!g || (!g->timer && kind != K_LINE)) return NULL;
    if (g->n >= FX_MAX) { inval_fx(g, &g->e[0]); retire(g, &g->e[0]); memmove(g->e, g->e + 1, sizeof g->e[0] * (FX_MAX - 1)); g->n--; }   /* full: the oldest goes */
    fx_t *f = &g->e[g->n++];
    memset(f, 0, sizeof *f);
    f->kind = (uint8_t)kind;
    f->dur = (uint8_t)dur;
    if (!g->timer) f->frame = f->dur;
    return f;
}
static void start(gfx_t *g, const fx_t *f) { inval_fx(g, f); if (f->frame < f->dur) run(g, true); }

/* the effects carrying `cell` (a drop, a fade) end now, without their own end (the cell shows again) */
static void stop_cell(gfx_t *g, int cell)
{
    int k = 0;
    for (int i = 0; i < g->n; i++) {
        fx_t *f = &g->e[i];
        if ((f->kind == K_DROP || f->kind == K_FADE) && f->cells[0] == cell) { inval_fx(g, f); retire(g, f); continue; }
        g->e[k++] = *f;
    }
    g->n = k;
}
void game_fx_stop_cell(lv_obj_t *fx, int cell) { gfx_t *g = of(fx); if (g) stop_cell(g, cell); }

void game_fx_drop(lv_obj_t *fx, int cell, int rows)
{
    gfx_t *g = of(fx);
    int mark = g ? ui_board_mark(g->board, cell) : 0;
    if (!mark) return;
    if (rows < 1) rows = 1;
    if (rows > 7) rows = 7;
    stop_cell(g, cell);                       /* one effect per cell: a drop still in flight there is replaced (its end would show the cell too early) */
    fx_t *f = add(g, K_DROP, 3 + rows + 3);   /* the fall (longer from higher), then 3 frames of bounce: at most 13 frames */
    if (!f) return;
    f->cells[0] = (int8_t)cell; f->n = 1; f->rows = (int8_t)rows; f->mark = (uint8_t)mark;
    snprintf(f->t, sizeof f->t, "%s", ui_board_text(g->board, cell));
    ui_board_hide(g->board, cell, true);
    start(g, f);
}

void game_fx_fade(lv_obj_t *fx, int cell, int mark, const char *text)
{
    gfx_t *g = of(fx);
    if (!g || mark < 1 || mark > 2) return;
    stop_cell(g, cell);
    fx_t *f = add(g, K_FADE, 9);
    if (!f) return;
    f->cells[0] = (int8_t)cell; f->n = 1; f->mark = (uint8_t)mark;
    snprintf(f->t, sizeof f->t, "%s", text ? text : "");
    start(g, f);
}

static fx_t *cells_fx(lv_obj_t *fx, int kind, int dur, const int *cells, int n)
{
    gfx_t *g = of(fx);
    if (!g || n < 1) return NULL;
    if (n > 4) n = 4;
    fx_t *f = add(g, kind, dur);
    if (!f) return NULL;
    f->n = (uint8_t)n;
    for (int i = 0; i < n; i++) f->cells[i] = (int8_t)cells[i];
    start(g, f);
    return f;
}
void game_fx_line(lv_obj_t *fx, const int *cells, int n) { if (n >= 2) cells_fx(fx, K_LINE, 9, cells, n); }
void game_fx_glow(lv_obj_t *fx, const int *cells, int n, bool warn) { cells_fx(fx, warn ? K_WARN : K_GLOW, warn ? 8 : 12, cells, n); }
void game_fx_burst(lv_obj_t *fx, int cell) { cells_fx(fx, K_BURST, 12, &cell, 1); }

void game_fx_think(lv_obj_t *fx, bool on)
{
    gfx_t *g = of(fx);
    if (!g || g->think == on) return;
    g->think = on;
    if (on) run(g, true);
    inval_strip(g);
}

void game_fx_clear(lv_obj_t *fx)
{
    gfx_t *g = of(fx);
    if (!g) return;
    for (int i = 0; i < g->n; i++) retire(g, &g->e[i]);
    g->n = 0;
    g->think = false;
    run(g, false);
    lv_obj_invalidate(g->obj);
}


/* ---- drawing ---- */
static int ease_out(int x, int d, int span) { int r = d - x; return span - span * r * r / (d * d); }   /* 0..span, fast first */

static void draw_cb(lv_event_t *e)
{
    gfx_t *g = lv_event_get_user_data(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    game_palette_t p;
    game_style_palette(ui_board_style(g->board), &p);
    const int pitch = ui_board_pitch(g->board);
    for (int i = 0; i < g->n; i++) {
        const fx_t *f = &g->e[i];
        int x, y;
        if (!ui_board_centre(g->board, f->cells[0], &x, &y)) continue;
        if (f->kind == K_DROP) {
            int fall = f->dur - 3, dy;
            if (f->frame < fall) { int k = f->frame + 1; dy = -(f->rows * pitch) + f->rows * pitch * k * k / (fall * fall); }   /* gravity: y grows with t^2 */
            else { static const int8_t B[3] = { 14, 10, 4 }; dy = -pitch * B[LV_MIN(f->frame - fall, 2)] / 100; }   /* the bounce, % of a pitch */
            ui_board_draw_piece(g->board, layer, f->mark, f->t, x, y + dy, 100, LV_OPA_COVER);
        } else if (f->kind == K_FADE) {
            int left = f->dur - f->frame;
            ui_board_draw_piece(g->board, layer, f->mark, f->t, x, y, 40 + 60 * left / f->dur, (lv_opa_t)(255 * left / f->dur));
        } else if (f->kind == K_LINE) {
            int w = LV_MAX(4, pitch / 9), segs = f->n - 1, upto = ease_out(LV_MIN(f->frame + 1, f->dur), f->dur, 1000);   /* 0..1000 of the whole line */
            for (int s = 0; s < segs; s++) {
                int x1, y1, x2, y2;
                ui_board_centre(g->board, f->cells[s], &x1, &y1);
                ui_board_centre(g->board, f->cells[s + 1], &x2, &y2);
                if (LV_ABS(x2 - x1) > pitch * 3 / 2 || LV_ABS(y2 - y1) > pitch * 3 / 2) continue;   /* across the wrapped edge: no stroke */
                int a = s * 1000 / segs, b = (s + 1) * 1000 / segs;
                if (upto <= a) break;
                if (upto < b) { x2 = x1 + (x2 - x1) * (upto - a) / (b - a); y2 = y1 + (y2 - y1) * (upto - a) / (b - a); }
                lv_draw_line_dsc_t l;
                lv_draw_line_dsc_init(&l);
                l.round_start = 1; l.round_end = 1;
                l.p1.x = x1; l.p1.y = y1; l.p2.x = x2; l.p2.y = y2;
                l.color = p.hi; l.opa = LV_OPA_40; l.width = w * 2;
                lv_draw_line(layer, &l);
                l.opa = LV_OPA_COVER; l.width = w;
                lv_draw_line(layer, &l);
            }
        } else if (f->kind == K_GLOW || f->kind == K_WARN) {
            int ph = f->frame % 6;                                                      /* two pulses of 6 frames (the warning: one and a bit) */
            lv_opa_t o = (lv_opa_t)(ph < 3 ? 80 + 175 * ph / 2 : 255 - 175 * (ph - 3) / 2);
            for (int c = 0; c < f->n; c++) {
                int cx, cy;
                if (!ui_board_centre(g->board, f->cells[c], &cx, &cy)) continue;
                int d = pitch + 4 + (ph < 3 ? ph : 5 - ph) * 2;
                lv_area_t a = { cx - d / 2, cy - d / 2, cx - d / 2 + d - 1, cy - d / 2 + d - 1 };
                lv_draw_rect_dsc_t r;
                lv_draw_rect_dsc_init(&r);
                r.bg_opa = LV_OPA_TRANSP;
                r.radius = ui_board_is_round(g->board) ? LV_RADIUS_CIRCLE : pitch / 5;
                r.border_width = LV_MAX(3, pitch / 14);
                r.border_color = f->kind == K_WARN ? UI_WARN : p.hi;
                r.border_opa = o;
                lv_draw_rect(layer, &r, &a);
            }
        } else if (f->kind == K_BURST) {
            for (int k = 0; k < 6; k++) {                                               /* six particles, 60 degrees apart, slowing as they go, shrinking, fading */
                int32_t ang = k * 60 + 30 + (f->cells[0] * 17) % 60;
                int dist = ease_out(f->frame + 1, f->dur, pitch * 9 / 10);
                int px = x + lv_trigo_cos((int16_t)ang) * dist / LV_TRIGO_SIN_MAX, py = y + lv_trigo_sin((int16_t)ang) * dist / LV_TRIGO_SIN_MAX;
                int dia = LV_MAX(2, 8 - 6 * f->frame / f->dur);
                lv_area_t a = { px - dia / 2, py - dia / 2, px - dia / 2 + dia - 1, py - dia / 2 + dia - 1 };
                lv_draw_rect_dsc_t r;
                lv_draw_rect_dsc_init(&r);
                r.bg_color = k & 1 ? p.fx : p.hi;
                r.bg_opa = (lv_opa_t)(255 - 230 * f->frame / f->dur);
                r.radius = LV_RADIUS_CIRCLE;
                lv_draw_rect(layer, &r, &a);
            }
        }
    }
    if (g->think) {                                                                     /* three dots under the grid, the lit one moves every 6 frames */
        lv_area_t o;
        lv_obj_get_coords(g->obj, &o);
        int lit = (int)(g->frames / 6 % 3), dia = LV_MAX(4, pitch / 9), y = o.y2 - dia;
        for (int k = 0; k < 3; k++) {
            int cx = (o.x1 + o.x2) / 2 + (k - 1) * dia * 2;
            lv_area_t a = { cx - dia / 2, y - dia / 2, cx - dia / 2 + dia - 1, y - dia / 2 + dia - 1 };
            lv_draw_rect_dsc_t r;
            lv_draw_rect_dsc_init(&r);
            r.bg_color = p.fx;
            r.bg_opa = k == lit ? LV_OPA_COVER : LV_OPA_30;
            r.radius = LV_RADIUS_CIRCLE;
            lv_draw_rect(layer, &r, &a);
        }
    }
}

static void delete_cb(lv_event_t *e)
{
    gfx_t *g = lv_event_get_user_data(e);
    if (s_live == g) s_live = NULL;
    psram_free(g);                            /* the timer is bound to the object: it goes with it */
}

lv_obj_t *game_fx_create(lv_obj_t *board)
{
    if (!board) return NULL;
    gfx_t *g = psram_calloc(1, sizeof *g);
    if (!g) return NULL;
    lv_obj_t *o = lv_obj_create(board);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, LV_PCT(100), LV_PCT(100));
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(o, LV_OBJ_FLAG_EVENT_BUBBLE);
    g->obj = o;
    g->board = board;
    lv_obj_set_user_data(o, g);
    lv_obj_add_event_cb(o, draw_cb, LV_EVENT_DRAW_MAIN, g);
    lv_obj_add_event_cb(o, delete_cb, LV_EVENT_DELETE, g);
    g->timer = ui_timer_bind(o, tick, FX_MS, g);
    if (g->timer) lv_timer_pause(g->timer);
    s_live = g;
    return o;
}

static void dump(const gfx_t *g)
{
    printf("APP game_fx n=%d think=%d hold=%d run=%d frames=%u inv_n=%d inv_px=%ld board_px=%ld\n", g->n, g->think, g->hold, g->running, (unsigned)g->frames,
           g->inv_n, g->inv_px, (long)lv_obj_get_width(g->obj) * lv_obj_get_height(g->obj));
    for (int i = 0; i < g->n; i++) {
        const fx_t *f = &g->e[i];
        char cells[24] = "";
        int k = 0;
        for (int c = 0; c < f->n; c++) k += snprintf(cells + k, sizeof cells - (size_t)k, c ? ",%d" : "%d", f->cells[c]);
        printf("APP game_fx e%d kind=%s frame=%d/%d cells=%s\n", i, KNAME[f->kind < 7 ? f->kind : 0], f->frame, f->dur, cells);
    }
}

void game_fx_cmd(const char *arg)
{
    gfx_t *g = s_live;
    int n;
    if (!g) { printf("APP game_fx none\n"); return; }
    if (sscanf(arg, "step %d", &n) == 1) { if (n > 0) advance(g, n > 100 ? 100 : n); if (!active(g) && !g->think) run(g, false); }
    else if (sscanf(arg, "hold %d", &n) == 1) g->hold = n != 0;
    else if (arg[0]) { printf("APP game_fx unknown %s\n", arg); return; }
    dump(g);
}
