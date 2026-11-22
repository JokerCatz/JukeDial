// SPDX-License-Identifier: LGPL-2.1-or-later
/* The arena (ui_arena.h). Layout: the band between the HEAD row and Back (the numbers ui_board uses), the largest circle that fits it and the design
 * circle; the object is the circle's square box, so the touch and the boxes check see one target that never crosses the HEAD row or Back. */
#include "ui_arena.h"
#include "ui_exit.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "audio_svc.h"
#include "draw/lv_draw_private.h"
#include "font.h"
#include "psram_alloc.h"
#include "ui.h"
#include "ui_axis.h"
#include "ui_frame.h"
#include "ui_geom.h"
#include "ui_kit.h"
#include "ui_popring.h"

#define STEP_MS_X30  1000                    /* a step = 1000 / 30 ms: kept as 30 x ms so it adds up exactly */
#define MAX_CATCHUP  3
#define FX_MAX       12
#define TEXT_MAX     11
#define KEEP_EVERY   15                      /* steps between two keep-alives of the field's knob borrow (0.5 s) */
#define ENERGY_EVERY 3                       /* steps between two energy samples (10 Hz) */
#define ENERGY_N     256
#define EDGE         2                       /* px kept free inside the box (anti-aliased edges) */
#define AREAS        6                       /* areas a batch sends to LVGL at most (LVGL keeps 32 a frame, more = the whole screen): boxes merge into these (fix round 1 I1) */
#define NEAR_PX      8                       /* a box this close to a pending area joins it */
enum { K_NONE = 0, K_BALL, K_ARC, K_RECT };
enum { E_BURST = 1, E_TEXT, E_FLASH, E_WAVE };
static const char *const KNAME[] = { "-", "ball", "arc", "rect" };

typedef struct {                             /* what a slot draws: built zeroed, compared whole */
    uint8_t kind, opa, round;
    int16_t x, y, r, w, h, a0, a1;
    lv_color_t c;
} ob_t;
typedef struct { ob_t o; lv_area_t box; } slot_t;
typedef struct {
    uint8_t kind, frame, dur, n;
    int16_t x, y;
    lv_color_t c;
    char t[TEXT_MAX + 1];
} efx_t;
#define CV_Q 128                             /* canvas lines queued before one draw pass */
typedef struct { int16_t x0, y0, x1, y1; uint8_t w, opa; lv_color_t c; } cvl_t;
typedef struct {
    lv_obj_t *obj;
    lv_obj_t *cv;                             /* the canvas (ui_arena_canvas): hidden, the field draws its buffer itself, under the objects and effects */
    void *cvbuf;
    bool cv565;                               /* the canvas is RGB565 and opaque (ui_arena_canvas565): covers the whole box (the field's square), cleared with the field's fill */
    int cvn;
    cvl_t cvq[CV_Q];
    lv_timer_t *timer;
    const ui_arena_def_t *def;
    void *user;
    int R, n;                                 /* the radius px, the slots */
    bool hold, running, paused, energy_on, has_field, had_knob;   /* had_knob: the field held the borrowed knob when the shared exit paused it */
    int batch, npend;                         /* batch depth (a step, a touch, a turn); the areas it collected */
    lv_area_t pend[AREAS];
    uint32_t ticks, last_ms, acc, fps_t0, fps_n;
    int fps, energy, inv_n;
    long inv_px;
    lv_color_t fill, rim, track;
    lv_opa_t fill_opa;
    int16_t track_in, track_out;
    efx_t e[FX_MAX];
    int ne;
    float wave[ENERGY_N];
    slot_t s[];                               /* n of them */
} ar_t;

static ar_t *s_live;                          /* the open field (console, the knob's borrow) */

static ar_t *of(const lv_obj_t *o) { return o ? lv_obj_get_user_data((lv_obj_t *)o) : NULL; }
static void centre(const ar_t *a, int *cx, int *cy)
{
    lv_area_t c;
    lv_obj_get_coords(a->obj, &c);
    *cx = c.x1 + lv_area_get_width(&c) / 2;
    *cy = c.y1 + lv_area_get_height(&c) / 2;
}
static bool overlap(const lv_area_t *p, const lv_area_t *q) { return p->x1 <= q->x2 && q->x1 <= p->x2 && p->y1 <= q->y2 && q->y1 <= p->y2; }
static long area_px(const lv_area_t *b) { return (long)lv_area_get_width(b) * lv_area_get_height(b); }
static void join(lv_area_t *p, const lv_area_t *q) { p->x1 = LV_MIN(p->x1, q->x1); p->y1 = LV_MIN(p->y1, q->y1); p->x2 = LV_MAX(p->x2, q->x2); p->y2 = LV_MAX(p->y2, q->y2); }

/* What a batch redraws (fix round 1 I1): every changed box goes into at most AREAS pending areas: a box that touches or nearly touches one (NEAR_PX)
 * joins it, else it takes a free one, else it joins the one it grows least. A turning ring (36 bricks, old and new boxes) becomes one band, the ball its
 * own small area: LVGL never gets more than AREAS areas from the field in a frame (it has room for 32; past that it redraws the whole screen). */
static void send(ar_t *a)
{
    for (int i = 0; i < a->npend; i++) {
        lv_area_t c = a->pend[i];
        lv_obj_invalidate_area(a->obj, &c);
        a->inv_n++;
        a->inv_px += area_px(&a->pend[i]);
    }
    a->npend = 0;
}
static void inval(ar_t *a, const lv_area_t *box)
{
    lv_area_t g = { box->x1 - NEAR_PX, box->y1 - NEAR_PX, box->x2 + NEAR_PX, box->y2 + NEAR_PX };
    int at = -1;
    for (int i = 0; i < a->npend && at < 0; i++) if (overlap(&g, &a->pend[i])) at = i;
    if (at < 0 && a->npend < AREAS) { a->pend[a->npend++] = *box; at = -2; }
    if (at == -1) {                                       /* full: the one it grows least */
        long best = 0;
        for (int i = 0; i < a->npend; i++) {
            lv_area_t u = a->pend[i];
            join(&u, box);
            long grow = area_px(&u) - area_px(&a->pend[i]);
            if (at < 0 || grow < best) { at = i; best = grow; }
        }
    }
    if (at >= 0) {
        join(&a->pend[at], box);
        for (int i = 0; i < a->npend; i++)                /* a grown area may now touch another: they become one */
            if (i != at && overlap(&a->pend[i], &a->pend[at])) { join(&a->pend[at], &a->pend[i]); a->pend[i] = a->pend[--a->npend]; if (at == a->npend) at = i; i = -1; }
    }
    if (!a->batch) send(a);
}
static void batch_begin(ar_t *a) { a->batch++; }
static void cv_flush(ar_t *a);
static void batch_end(ar_t *a) { if (a->batch > 0 && --a->batch == 0) { cv_flush(a); send(a); } }
void ui_arena_begin(lv_obj_t *arena) { ar_t *a = of(arena); if (a) batch_begin(a); }
void ui_arena_end(lv_obj_t *arena) { ar_t *a = of(arena); if (a) batch_end(a); }

/* ---- the boxes (screen px) ---- */
static void grow(lv_area_t *b, int x, int y) { b->x1 = LV_MIN(b->x1, x); b->y1 = LV_MIN(b->y1, y); b->x2 = LV_MAX(b->x2, x); b->y2 = LV_MAX(b->y2, y); }
static void arc_point(lv_area_t *b, int cx, int cy, int r, int deg)
{
    int16_t d = (int16_t)(((deg % 360) + 360) % 360);
    grow(b, cx + lv_trigo_cos(d) * r / LV_TRIGO_SIN_MAX, cy + lv_trigo_sin(d) * r / LV_TRIGO_SIN_MAX);
}
static void box_of(const ar_t *a, const ob_t *o, lv_area_t *b)
{
    int cx, cy;
    centre(a, &cx, &cy);
    if (o->kind == K_BALL) { *b = (lv_area_t){ cx + o->x - o->r - 1, cy + o->y - o->r - 1, cx + o->x + o->r + 1, cy + o->y + o->r + 1 }; return; }
    if (o->kind == K_RECT) { *b = (lv_area_t){ cx + o->x - o->w / 2 - 1, cy + o->y - o->h / 2 - 1, cx + o->x + (o->w + 1) / 2 + 1, cy + o->y + (o->h + 1) / 2 + 1 }; return; }
    int ri = o->r - o->w;                     /* an arc: its two ends on both radii, and every quarter it passes on the outer one */
    *b = (lv_area_t){ INT16_MAX, INT16_MAX, INT16_MIN, INT16_MIN };
    arc_point(b, cx, cy, o->r, o->a0); arc_point(b, cx, cy, o->r, o->a1);
    arc_point(b, cx, cy, ri, o->a0); arc_point(b, cx, cy, ri, o->a1);
    for (int q = (o->a0 / 90 + 1) * 90; q < o->a1; q += 90) arc_point(b, cx, cy, o->r, q);
    int pad = 2 + (o->round ? o->w / 2 + 1 : 0);
    b->x1 -= pad; b->y1 -= pad; b->x2 += pad; b->y2 += pad;
}
static void efx_box(const ar_t *a, const efx_t *f, lv_area_t *b)
{
    int cx, cy;
    centre(a, &cx, &cy);
    if (f->kind == E_FLASH || f->kind == E_WAVE) { lv_obj_get_coords(a->obj, b); return; }
    int rx = f->kind == E_TEXT ? 40 : a->R / 5 + 6, top = f->kind == E_TEXT ? 34 : rx, bot = f->kind == E_TEXT ? 14 : rx;
    *b = (lv_area_t){ cx + f->x - rx, cy + f->y - top, cx + f->x + rx, cy + f->y + bot };
}

/* ---- slots ---- */
static void set(lv_obj_t *arena, int id, const ob_t *o)
{
    ar_t *a = of(arena);
    if (!a || id < 0 || id >= a->n) return;
    slot_t *s = &a->s[id];
    if (!memcmp(&s->o, o, sizeof *o)) return;
    if (s->o.kind) inval(a, &s->box);
    s->o = *o;
    if (!o->kind) return;
    box_of(a, o, &s->box);
    inval(a, &s->box);
}
void ui_arena_ball(lv_obj_t *arena, int id, int x, int y, int r, lv_color_t c, lv_opa_t opa)
{
    ob_t o; memset(&o, 0, sizeof o);
    o.kind = K_BALL; o.x = (int16_t)x; o.y = (int16_t)y; o.r = (int16_t)LV_MAX(1, r); o.c = c; o.opa = opa;
    set(arena, id, &o);
}
void ui_arena_arc(lv_obj_t *arena, int id, int r_in, int r_out, int a0, int a1, bool round_ends, lv_color_t c, lv_opa_t opa)
{
    ob_t o; memset(&o, 0, sizeof o);
    int span = LV_CLAMP(1, a1 - a0, 360);
    a0 = ((a0 % 360) + 360) % 360;
    o.kind = K_ARC; o.r = (int16_t)r_out; o.w = (int16_t)LV_MAX(1, r_out - r_in); o.a0 = (int16_t)a0; o.a1 = (int16_t)(a0 + span); o.round = round_ends; o.c = c; o.opa = opa;
    set(arena, id, &o);
}
void ui_arena_rect(lv_obj_t *arena, int id, int x, int y, int w, int h, int radius, lv_color_t c, lv_opa_t opa)
{
    ob_t o; memset(&o, 0, sizeof o);
    o.kind = K_RECT; o.x = (int16_t)x; o.y = (int16_t)y; o.w = (int16_t)LV_MAX(1, w); o.h = (int16_t)LV_MAX(1, h); o.r = (int16_t)radius; o.c = c; o.opa = opa;
    set(arena, id, &o);
}
void ui_arena_hide(lv_obj_t *arena, int id) { ob_t o; memset(&o, 0, sizeof o); set(arena, id, &o); }
void ui_arena_clear(lv_obj_t *arena)
{
    ar_t *a = of(arena);
    if (!a) return;
    memset(a->s, 0, sizeof a->s[0] * (size_t)a->n);
    a->ne = 0;
    lv_obj_invalidate(a->obj);
}
int ui_arena_radius(const lv_obj_t *arena) { ar_t *a = of(arena); return a ? a->R : 0; }

void ui_arena_field(lv_obj_t *arena, lv_color_t fill, lv_opa_t opa, lv_color_t rim, int track_in, int track_out, lv_color_t track)
{
    ar_t *a = of(arena);
    if (!a) return;
    a->has_field = true;
    a->fill = fill; a->fill_opa = opa; a->rim = rim; a->track = track;
    a->track_in = (int16_t)track_in; a->track_out = (int16_t)track_out;
    lv_obj_invalidate(a->obj);
}

/* ---- the timer ---- */
static bool game_running(const ar_t *a) { return !a->def->running || a->def->running(a->user); }
static int fx_moving(const ar_t *a) { return a->ne; }
static void run(ar_t *a, bool on)
{
    if (!a->timer || a->running == on) return;
    a->running = on;
    if (on) { a->last_ms = lv_tick_get(); a->acc = 0; lv_timer_resume(a->timer); }
    else lv_timer_pause(a->timer);
}
static bool wanted(const ar_t *a) { return !a->paused && (game_running(a) || fx_moving(a) || a->energy_on); }

static void fx_advance(ar_t *a, int frames)
{
    int k = 0;
    for (int i = 0; i < a->ne; i++) {
        efx_t *f = &a->e[i];
        lv_area_t b;
        efx_box(a, f, &b);
        inval(a, &b);
        f->frame = (uint8_t)LV_MIN(f->dur, f->frame + frames);
        if (f->frame < f->dur) a->e[k++] = *f;
    }
    a->ne = k;
}

static void sample_energy(ar_t *a)
{
    int n = audio_svc_wave(a->wave, ENERGY_N);
    float s = 0;
    for (int i = 0; i < n; i++) s += a->wave[i] * a->wave[i];
    int e = n > 0 ? (int)(sqrtf(s / (float)n) * 4.0f * 255.0f) : 0;   /* -12 dBFS RMS = full */
    e = LV_CLAMP(0, e, 255);
    a->energy = e > a->energy ? e : a->energy - (a->energy - e) / 3;    /* fast up, slow down */
}

/* one fixed step: the game, then the effects; the knob's borrow kept while the game runs */
static void advance(ar_t *a)
{
    a->inv_n = 0;
    a->inv_px = 0;
    a->ticks++;
    a->fps_n++;
    batch_begin(a);
    if (a->energy_on && a->ticks % ENERGY_EVERY == 0) sample_energy(a);
    a->def->step(a->user);
    fx_advance(a, 1);
    batch_end(a);
    if (a->ticks % KEEP_EVERY == 0 && game_running(a) && !a->def->toy && ui_axis_current_of(a->obj) == 0) ui_axis_touch();
}

static bool covered(const ar_t *a) { return ui_visual_paused(lv_obj_get_parent(a->obj)); }

/* covered: a running game pauses, the effects end and the timer STOPS (fix round 1 m3: no polling while the screen is off or the page is covered); the
 * next tap on the field resumes it (a game that was not running starts it again with its own input: the serve, ui_arena_wake). */
static void cover(ar_t *a)
{
    if (a->ne) fx_advance(a, 64);
    if (game_running(a) && !a->paused && !a->def->toy) {   /* a toy is never 'paused': it only stops its timer and restarts itself (ui_arena_kick) */
        a->paused = true;
        if (a->def->paused) a->def->paused(true, a->user);
    }
    run(a, false);
}

static void tick(lv_timer_t *t)
{
    ar_t *a = lv_timer_get_user_data(t);
    if (a->hold) return;
    if (a->paused) { run(a, false); return; }            /* fix round 1 m2: no step of a paused game, whoever restarted the timer */
    if (covered(a)) { cover(a); return; }
    uint32_t now = lv_tick_get();
    a->acc += lv_tick_diff(now, a->last_ms) * 30u;
    a->last_ms = now;
    int steps = (int)(a->acc / STEP_MS_X30);
    a->acc %= STEP_MS_X30;
    if (steps > MAX_CATCHUP) { steps = MAX_CATCHUP; a->acc = 0; }
    for (int s = 0; s < steps; s++) advance(a);
    if (lv_tick_diff(now, a->fps_t0) >= 1000) { a->fps = (int)a->fps_n; a->fps_n = 0; a->fps_t0 = now; }
    if (!wanted(a)) run(a, false);
}

/* a toy's timer stopped because the page was covered (screen off, a ring, another page): it runs again when nothing covers it (called from the screen waking,
 * the ring closing and the page's on_resume); no-op for a game, a running timer or a held one */
void ui_arena_kick(void)
{
    ar_t *a = s_live;
    if (!a || !a->def->toy || a->paused || a->running || covered(a) || !wanted(a)) return;
    run(a, true);
}
void ui_arena_wake(lv_obj_t *arena) { ar_t *a = of(arena); if (a && !a->paused) run(a, true); }
void ui_arena_energy_on(lv_obj_t *arena, bool on)
{
    ar_t *a = of(arena);
    if (!a) return;
    a->energy_on = on;
    if (!on) a->energy = 0;
    if (on && !a->paused) run(a, true);
}
static bool knob_step(int axis, int d);
void ui_arena_pause(lv_obj_t *arena) { ar_t *a = of(arena); if (a) { a->had_knob = ui_axis_current_of(a->obj) == 0; cover(a); } }
void ui_arena_resume(lv_obj_t *arena)
{
    ar_t *a = of(arena);
    if (!a) return;
    const bool was = a->paused;
    a->paused = false;
    if (was && a->def->paused) a->def->paused(false, a->user);
    if (a->had_knob) ui_axis_borrow(a->obj, 0, knob_step, NULL, false);   /* the 4 s borrow ran out during the pause: the knob is the field's again, as before it */
    a->had_knob = false;
    if (was && wanted(a)) run(a, true);
}
void ui_arena_unpause(lv_obj_t *arena)
{
    ar_t *a = of(arena);
    if (!a || !a->paused) return;
    a->paused = false;                        /* the page's own state is new (a new game): it says so itself, no paused(false) */
    if (wanted(a)) run(a, true);
}
int ui_arena_energy(const lv_obj_t *arena) { ar_t *a = of(arena); return a && a->energy_on ? a->energy : 0; }

/* ---- effects ---- */
static efx_t *fx_add(ar_t *a, int kind, int dur)
{
    if (!a || !a->timer || !ui_screen_on()) return NULL;   /* nothing would move it: not made */
    if (a->ne >= FX_MAX) { lv_area_t b; efx_box(a, &a->e[0], &b); inval(a, &b); memmove(a->e, a->e + 1, sizeof a->e[0] * (FX_MAX - 1)); a->ne--; }
    efx_t *f = &a->e[a->ne++];
    memset(f, 0, sizeof *f);
    f->kind = (uint8_t)kind;
    f->dur = (uint8_t)dur;
    return f;
}
static void fx_start(ar_t *a, const efx_t *f) { lv_area_t b; efx_box(a, f, &b); inval(a, &b); if (!a->paused) run(a, true); }
void ui_arena_burst(lv_obj_t *arena, int x, int y, lv_color_t c, int n)
{
    ar_t *a = of(arena);
    efx_t *f = fx_add(a, E_BURST, 12);
    if (!f) return;
    f->x = (int16_t)x; f->y = (int16_t)y; f->c = c; f->n = (uint8_t)LV_CLAMP(3, n, 8);
    fx_start(a, f);
}
void ui_arena_text(lv_obj_t *arena, int x, int y, const char *text, lv_color_t c)
{
    ar_t *a = of(arena);
    efx_t *f = fx_add(a, E_TEXT, 12);
    if (!f) return;
    f->x = (int16_t)x; f->y = (int16_t)y; f->c = c;
    snprintf(f->t, sizeof f->t, "%s", text ? text : "");
    fx_start(a, f);
}
void ui_arena_flash(lv_obj_t *arena, lv_color_t c) { ar_t *a = of(arena); efx_t *f = fx_add(a, E_FLASH, 9); if (f) { f->c = c; fx_start(a, f); } }
void ui_arena_wave(lv_obj_t *arena, lv_color_t c) { ar_t *a = of(arena); efx_t *f = fx_add(a, E_WAVE, 12); if (f) { f->c = c; fx_start(a, f); } }

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
static void arc(lv_layer_t *layer, int cx, int cy, int r_out, int w, int a0, int a1, bool round, lv_color_t c, lv_opa_t opa)
{
    lv_draw_arc_dsc_t d;
    lv_draw_arc_dsc_init(&d);
    d.center.x = cx; d.center.y = cy;
    d.radius = (uint16_t)r_out;
    d.width = w;
    d.start_angle = a0;
    d.end_angle = a1 - a0 >= 360 ? a0 + 360 : a1 % 360;
    d.rounded = round;
    d.color = c;
    d.opa = opa;
    lv_draw_arc(layer, &d);
}
static int ease_out(int x, int d, int span) { int r = d - x; return span - span * r * r / (d * d); }

static void draw_cb(lv_event_t *e)
{
    ar_t *a = lv_event_get_user_data(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    const lv_area_t *clip = &layer->_clip_area;
    int cx, cy;
    centre(a, &cx, &cy);
    if (a->has_field) {
        disc(layer, cx, cy, a->R, a->fill, a->fill_opa, 1, a->rim);
        if (a->track_out > a->track_in) arc(layer, cx, cy, a->track_out, a->track_out - a->track_in, 0, 360, false, a->track, LV_OPA_40);
    }
    if (a->cv) {                                  /* the picture (spirograph): over the field, under the objects and effects */
        lv_draw_image_dsc_t id;
        lv_draw_image_dsc_init(&id);
        id.src = lv_canvas_get_draw_buf(a->cv);
        lv_area_t b;
        lv_obj_get_coords(a->obj, &b);
        lv_draw_image(layer, &id, &b);
    }
    for (int i = 0; i < a->n; i++) {
        const slot_t *s = &a->s[i];
        if (!s->o.kind || !overlap(&s->box, clip)) continue;   /* only what this area holds */
        const ob_t *o = &s->o;
        if (o->kind == K_BALL) disc(layer, cx + o->x, cy + o->y, o->r, o->c, o->opa, 0, o->c);
        else if (o->kind == K_ARC) arc(layer, cx, cy, o->r, o->w, o->a0, o->a1, o->round, o->c, o->opa);
        else {
            lv_draw_rect_dsc_t d;
            lv_draw_rect_dsc_init(&d);
            d.radius = o->r;
            d.bg_color = o->c;
            d.bg_opa = o->opa;
            lv_area_t b = { cx + o->x - o->w / 2, cy + o->y - o->h / 2, cx + o->x - o->w / 2 + o->w - 1, cy + o->y - o->h / 2 + o->h - 1 };
            lv_draw_rect(layer, &d, &b);
        }
    }
    for (int i = 0; i < a->ne; i++) {
        const efx_t *f = &a->e[i];
        const int left = f->dur - f->frame;
        if (f->kind == E_BURST) {
            for (int k = 0; k < f->n; k++) {                           /* n particles, evenly apart, slowing, shrinking, fading */
                int16_t ang = (int16_t)((k * 360 / f->n + (f->x * 7 + f->y * 13) % 45 + 360) % 360);
                int dist = ease_out(f->frame + 1, f->dur, a->R / 5);
                int px = cx + f->x + lv_trigo_cos(ang) * dist / LV_TRIGO_SIN_MAX, py = cy + f->y + lv_trigo_sin(ang) * dist / LV_TRIGO_SIN_MAX;
                disc(layer, px, py, LV_MAX(1, 4 - 3 * f->frame / f->dur), f->c, (lv_opa_t)(255 * left / f->dur), 0, f->c);
            }
        } else if (f->kind == E_TEXT) {
            lv_draw_label_dsc_t d;
            lv_draw_label_dsc_init(&d);
            d.font = font_main();
            d.color = f->c;
            d.opa = (lv_opa_t)(left * 255 / f->dur);
            d.text = f->t;
            d.align = LV_TEXT_ALIGN_CENTER;
            int up = ease_out(f->frame + 1, f->dur, 18), h = lv_font_get_line_height(d.font);
            lv_area_t b = { cx + f->x - 40, cy + f->y - up - h / 2, cx + f->x + 40, cy + f->y - up + h / 2 };
            lv_draw_label(layer, &d, &b);
        } else if (f->kind == E_FLASH) {
            int w = 3 + 3 * left / f->dur;
            arc(layer, cx, cy, a->R, w, 0, 360, false, f->c, (lv_opa_t)(60 + 195 * left / f->dur));
        } else if (f->kind == E_WAVE) {
            int r = ease_out(f->frame + 1, f->dur, a->R - 4) + 4;
            arc(layer, cx, cy, r, 3, 0, 360, false, f->c, (lv_opa_t)(255 * left / f->dur));
        }
    }
}

/* ---- input ---- */
static bool knob_step(int axis, int d)
{
    (void)axis;
    ar_t *a = s_live;
    if (!a || a->paused) return true;         /* paused: the turn is the field's, but nothing moves */
    batch_begin(a);
    a->def->input(UI_ARENA_STEP, d, 0, 0, a->user);
    batch_end(a);
    return true;
}
static void touch(ar_t *a, int x, int y)
{
    if (ui_exit_tap()) return;                /* CF2: a page that takes no taps (the toys): the touch shows the popup; a knob-only game (mode B): it pauses */
    ui_axis_borrow(a->obj, 0, knob_step, NULL, false);   /* every tap: the knob is the field's now (4 s after the last turn: volume again); the resuming tap too (fix round 1 I3) */
    if (a->paused) {                          /* the tap after a pause only resumes */
        a->paused = false;
        if (a->def->paused) a->def->paused(false, a->user);
        ui_haptic_tap();
        if (wanted(a)) run(a, true);
        return;
    }
    batch_begin(a);
    a->def->input(UI_ARENA_TAP, 0, x, y, a->user);
    batch_end(a);
}
static void press_cb(lv_event_t *e)
{
    ar_t *a = lv_event_get_user_data(e);
    lv_indev_t *in = lv_indev_active();
    if (!in) return;
    lv_point_t p;
    lv_indev_get_point(in, &p);
    int cx, cy;
    centre(a, &cx, &cy);
    touch(a, p.x - cx, p.y - cy);
}
bool ui_arena_round(void) { return UI_ROUND; }
bool ui_arena_has_knob(void) { return UI_HAS_KNOB; }
/* A build without a knob (the FakePod): its two keys ARE the game's input from the first press (nothing lends them first: no tap borrows a knob that does not exist,
 * and a turn that fell to the volume would be of no use in a game): the field gets UI_ARENA_STEP, and a game that wants a tap from a key reads it from there. The
 * exit is a key turn between rounds (volume + popup, ui.c keys_idle) / the pause overlay's volume (ui.c: under the overlay the keys never come here). With a knob: only a borrowed knob is the field's. */
bool ui_arena_knob(int detents)
{
    if (ui_axis_knob(detents)) return true;
    if (UI_HAS_KNOB || !s_live || !detents) return false;
    return knob_step(0, detents);
}

/* ---- the canvas (spirograph): a PSRAM picture of the field's box, ARGB8888 (transparent where nothing is drawn: the field's disc shows through) ---- */
lv_obj_t *ui_arena_canvas(lv_obj_t *arena)
{
    ar_t *a = of(arena);
    if (!a) return NULL;
    if (a->cv) return a->cv;
    const int w = lv_obj_get_width(a->obj), h = lv_obj_get_height(a->obj);
    void *buf = psram_calloc((size_t)w * (size_t)h, 4);
    if (!buf) return NULL;
    lv_obj_t *c = lv_canvas_create(a->obj);
    if (!c) { psram_free(buf); return NULL; }
    lv_canvas_set_buffer(c, buf, w, h, LV_COLOR_FORMAT_ARGB8888);
    lv_canvas_fill_bg(c, lv_color_black(), LV_OPA_TRANSP);
    lv_obj_add_flag(c, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_CLICKABLE);
    a->cv = c;
    a->cvbuf = buf;
    return c;
}
/* the opaque variant (Game of Life): RGB565, 2 bytes a px (half of the ARGB8888 picture), cleared with the field's FILL (set the field first: ui_arena_field), drawn over the
 * WHOLE box (the square panel: no rim, the round glass hides the corners). The game writes pre-blended opaque colours itself. */
lv_obj_t *ui_arena_canvas565(lv_obj_t *arena)
{
    ar_t *a = of(arena);
    if (!a) return NULL;
    if (a->cv) return a->cv565 ? a->cv : NULL;
    const int w = lv_obj_get_width(a->obj), h = lv_obj_get_height(a->obj);
    void *buf = psram_calloc((size_t)w * (size_t)h, 2);
    if (!buf) return NULL;
    lv_obj_t *c = lv_canvas_create(a->obj);
    if (!c) { psram_free(buf); return NULL; }
    lv_canvas_set_buffer(c, buf, w, h, LV_COLOR_FORMAT_RGB565);
    lv_canvas_fill_bg(c, a->fill, LV_OPA_COVER);
    lv_obj_add_flag(c, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_CLICKABLE);
    a->cv = c;
    a->cvbuf = buf;
    a->cv565 = true;
    return c;
}
uint16_t *ui_arena_canvas_pixels16(lv_obj_t *arena, int *stride)
{
    ar_t *a = of(arena);
    if (!a || !a->cv || !a->cv565) return NULL;
    const lv_draw_buf_t *b = lv_canvas_get_draw_buf(a->cv);
    if (!b) return NULL;
    if (stride) *stride = (int)(b->header.stride / 2);
    return (uint16_t *)(void *)b->data;
}
/* the queued lines are drawn into the picture in ONE pass (init layer, N lines, finish) and their boxes invalidated; a line is anti-aliased, round at both ends */
static void cv_flush(ar_t *a)
{
    if (!a->cv || a->cvn <= 0) { a->cvn = 0; return; }
    lv_layer_t layer;
    lv_canvas_init_layer(a->cv, &layer);
    const int ox = a->R + EDGE, oy = a->R + EDGE;
    int cx, cy;
    centre(a, &cx, &cy);
    for (int i = 0; i < a->cvn; i++) {
        const cvl_t *q = &a->cvq[i];
        lv_draw_line_dsc_t d;
        lv_draw_line_dsc_init(&d);
        d.p1.x = (float)(ox + q->x0); d.p1.y = (float)(oy + q->y0);
        d.p2.x = (float)(ox + q->x1); d.p2.y = (float)(oy + q->y1);
        d.width = q->w; d.color = q->c; d.opa = q->opa; d.round_start = d.round_end = 1;
        lv_draw_line(&layer, &d);
        lv_area_t b = { cx + LV_MIN(q->x0, q->x1) - q->w - 1, cy + LV_MIN(q->y0, q->y1) - q->w - 1, cx + LV_MAX(q->x0, q->x1) + q->w + 1, cy + LV_MAX(q->y0, q->y1) + q->w + 1 };
        inval(a, &b);
    }
    lv_canvas_finish_layer(a->cv, &layer);
    a->cvn = 0;
}
void ui_arena_canvas_line(lv_obj_t *arena, int x0, int y0, int x1, int y1, int w, lv_color_t c, lv_opa_t opa)
{
    ar_t *a = of(arena);
    if (!a || !a->cv || a->cv565) return;
    if (a->cvn >= CV_Q) cv_flush(a);
    a->cvq[a->cvn++] = (cvl_t){ (int16_t)x0, (int16_t)y0, (int16_t)x1, (int16_t)y1, (uint8_t)LV_CLAMP(1, w, 40), (uint8_t)opa, c };
    if (!a->batch) { cv_flush(a); send(a); }
}
void ui_arena_canvas_dirty(lv_obj_t *arena, int x0, int y0, int x1, int y1)
{
    ar_t *a = of(arena);
    if (!a || !a->cv) return;
    const int w = lv_obj_get_width(a->obj), h = lv_obj_get_height(a->obj);
    x0 = LV_CLAMP(0, x0, w - 1); x1 = LV_CLAMP(0, x1, w - 1); y0 = LV_CLAMP(0, y0, h - 1); y1 = LV_CLAMP(0, y1, h - 1);
    if (x1 < x0 || y1 < y0) return;
    lv_area_t c, b;
    lv_obj_get_coords(a->obj, &c);
    b = (lv_area_t){ c.x1 + x0, c.y1 + y0, c.x1 + x1, c.y1 + y1 };
    inval(a, &b);
}
void ui_arena_canvas_clear(lv_obj_t *arena)
{
    ar_t *a = of(arena);
    if (!a || !a->cv) return;
    a->cvn = 0;
    if (a->cv565) lv_canvas_fill_bg(a->cv, a->fill, LV_OPA_COVER);
    else lv_canvas_fill_bg(a->cv, lv_color_black(), LV_OPA_TRANSP);
    lv_obj_invalidate(a->obj);
}

static void delete_cb(lv_event_t *e)
{
    ar_t *a = lv_event_get_user_data(e);
    if (s_live == a) s_live = NULL;
    psram_free(a->cvbuf);                     /* the canvas object (a child) goes after this, and only compares the pointer of its buffer */
    psram_free(a);                            /* the timer is bound to the object: it goes with it; the knob's borrow ends with its owner */
}

lv_obj_t *ui_arena_create(lv_obj_t *parent, const ui_arena_def_t *def, void *user)
{
    if (!parent || !def || !def->step || !def->input) return NULL;
    int n = LV_CLAMP(1, def->objs, UI_ARENA_OBJS_MAX);
    ar_t *a = psram_calloc(1, sizeof *a + sizeof a->s[0] * (size_t)n);
    if (!a) return NULL;
    int top, bottom;                                                          /* centre offsets, as ui_board */
    ui_frame_band(parent, &top, &bottom);
    const bool full = def->full || (def->full_sq && !UI_ROUND);
    const bool fr = ui_frame_active(parent) && !full;                 /* a frame page: a round field centred on the panel (F1a2) */
    const int d = bottom - top;
    int fR = 0, fmid = 0;
    if (fr) { ui_frame_field(&fR, &fmid); if (def->centred) { fR = ui_frame_field_centred(); fmid = 0; } }
    const int mid = full ? 0 : fr ? fmid : (top + bottom) / 2;        /* G6: the whole design circle, under the HEAD row and the exit; a frame page: under the status line (CF2) */
    a->R = (full ? LV_MIN(UI_W, UI_H) / 2 :   /* the whole panel (the round glass: the design circle, the same number) */
             fr ? fR :
             LV_MIN(d / 2, UI_CONTENT_D / 2 - LV_ABS(mid))) - EDGE;
    a->n = n;
    a->def = def;
    a->user = user;
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, 2 * (a->R + EDGE), 2 * (a->R + EDGE));
    lv_obj_align(o, LV_ALIGN_CENTER, 0, mid);
    lv_obj_update_layout(o);                  /* the slots' boxes are screen px from the first set on: the coordinates must be real now */
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_user_data(o, a);
    a->obj = o;
    lv_obj_add_event_cb(o, draw_cb, LV_EVENT_DRAW_MAIN, a);
    lv_obj_add_event_cb(o, press_cb, LV_EVENT_PRESSED, a);
    lv_obj_add_event_cb(o, delete_cb, LV_EVENT_DELETE, a);
    a->timer = ui_timer_bind(o, tick, (def->fps == 15 ? 66 : 1000 / UI_ARENA_FPS), a);
    if (a->timer) lv_timer_pause(a->timer);
    a->fps_t0 = lv_tick_get();
    s_live = a;
    return o;
}

/* ---- console ---- */
static void dump(const ar_t *a)
{
    int live = 0;
    for (int i = 0; i < a->n; i++) live += a->s[i].o.kind != K_NONE;
    printf("APP arena name=%s objs=%d/%d ticks=%u fps=%d run=%d hold=%d paused=%d knob=%d energy=%d fx=%d inv_n=%d inv_px=%ld field_px=%ld\n", a->def->name ? a->def->name : "-",
           live, a->n, (unsigned)a->ticks, a->fps, a->running, a->hold, a->paused, ui_axis_current_of(a->obj) == 0, ui_arena_energy(a->obj), a->ne, a->inv_n, a->inv_px,
           (long)lv_obj_get_width(a->obj) * lv_obj_get_height(a->obj));
    if (a->def->dump) a->def->dump(a->user);
}

void ui_arena_cmd(const char *arg)
{
    ar_t *a = s_live;
    int n, x, y;
    if (!a) { printf("APP arena none\n"); return; }
    if (sscanf(arg, "step %d", &n) == 1) { for (int s = 0; s < LV_CLAMP(0, n, 3000); s++) advance(a); if (!wanted(a)) run(a, false); }
    else if (sscanf(arg, "hold %d", &n) == 1) a->hold = n != 0;
    else if (sscanf(arg, "rate %d", &n) == 1) { if (a->timer) lv_timer_set_period(a->timer, n == 15 ? 66 : 1000 / UI_ARENA_FPS); }   /* 15 | 30 fps of the timer (steps stay 1/30 s: two per tick), for the board measurement */
    else if (sscanf(arg, "tap %d %d", &x, &y) == 2) touch(a, x, y);
    else if (!strncmp(arg, "game", 4) && (!arg[4] || arg[4] == ' ')) {   /* the game's own console (def->cmd): tests and pictures set up a case */
        if (!a->def->cmd) { printf("APP arena game none\n"); return; }
        batch_begin(a);
        a->def->cmd(arg[4] ? arg + 5 : "", a->user);
        batch_end(a);
    }
    else if (sscanf(arg, "at %d", &n) == 1) {
        if (n < 0 || n >= a->n) { printf("APP arena at %d none\n", n); return; }
        const slot_t *s = &a->s[n];
        printf("APP arena at %d kind=%s x=%d y=%d r=%d a0=%d a1=%d box=%d,%d,%d,%d\n", n, KNAME[s->o.kind & 3], s->o.x, s->o.y, s->o.r, s->o.a0, s->o.a1,
               (int)s->box.x1, (int)s->box.y1, (int)s->box.x2, (int)s->box.y2);
        return;
    }
    else if (arg[0]) { printf("APP arena unknown %s\n", arg); return; }
    dump(a);
}

