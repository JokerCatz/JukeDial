// SPDX-License-Identifier: LGPL-2.1-or-later
/* Reel animation (reel.h). Motion: pc_reel (host-tested). Everything here lives in PSRAM: the view struct, the
 * strips (about (len + rows) * cell_w * cell_h * 2 bytes a reel) and the LVGL object. */
#include "reel.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "misc/lv_area_private.h"
#include "draw/lv_draw_private.h"
#include "lowmem.h"
#include "pc/pc_lowmem.h"
#include "pc/pc_reel.h"
#include "ui.h"

#define REEL_MAX 5
#define FRAME_MS 33

struct reel_view {
    reel_desc_t d;
    lv_obj_t *obj;
    lv_timer_t *tmr;
    lv_image_dsc_t img[REEL_MAX];
    pc_reel_motion_t m[REEL_MAX];
    double pos[REEL_MAX];
    int off[REEL_MAX];               /* shown pixel offset into the strip */
    long stop[REEL_MAX];             /* the whole stop each reel passed last (cb.stepped) */
    bool landed[REEL_MAX], busy;
    reel_cbs_t cb;
    uint32_t t_base, t_last;
    int frames, ms, step_sum, step_max;
};

static int pix_off(const reel_view_t *v, double pos)
{
    int len = v->d.len, ch = v->d.cell_h;
    double t = fmod(-pos, (double)len);
    if (t < 0) t += len;
    int o = (int)lround(t * ch);
    return o >= len * ch ? o - len * ch : o;
}

static void window(const reel_view_t *v, int i, lv_area_t *a)
{
    lv_area_t c;
    lv_obj_get_coords(v->obj, &c);
    int dx = v->d.vertical ? 0 : i * (v->d.cell_w + v->d.gap), dy = v->d.vertical ? i * (v->d.rows * v->d.cell_h + v->d.gap) : 0;
    a->x1 = c.x1 + dx;
    a->x2 = a->x1 + v->d.cell_w - 1;
    a->y1 = c.y1 + dy;
    a->y2 = a->y1 + v->d.rows * v->d.cell_h - 1;
}

static void draw_cb(lv_event_t *e)
{
    reel_view_t *v = lv_event_get_user_data(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    const lv_area_t clip = layer->_clip_area;
    for (int i = 0; i < v->d.nreels; i++) {
        lv_area_t w, x;
        window(v, i, &w);
        if (!v->img[i].data || !lv_area_intersect(&x, &clip, &w)) continue;
        layer->_clip_area = x;
        lv_area_t im = { w.x1, w.y1 - v->off[i], w.x2, w.y1 - v->off[i] + (int32_t)v->img[i].header.h - 1 };
        lv_draw_image_dsc_t d;
        lv_draw_image_dsc_init(&d);
        d.src = &v->img[i];
        lv_draw_image(layer, &d, &im);
    }
    layer->_clip_area = clip;
}

static bool render_strip(reel_view_t *v, int i)
{
    const reel_desc_t *d = &v->d;
    int cells = d->len + d->rows, w = d->cell_w, h = d->cell_h;
    size_t n = (size_t)w * h * cells;
    uint16_t *px = heap_caps_malloc(n * 2, MALLOC_CAP_SPIRAM);
    if (!px) return false;
    lv_color_t bg = lv_color_hex(d->bg);
    uint16_t b = lv_color_to_u16(bg);
    for (size_t k = 0; k < n; k++) px[k] = b;
    for (int c = 0; c < cells; c++) d->draw_cell(d->user, d->strips[i][c % d->len], px + (size_t)c * w * h, w, w, h);
    lv_image_dsc_t *im = &v->img[i];
    memset(im, 0, sizeof *im);
    im->header.magic = LV_IMAGE_HEADER_MAGIC;
    im->header.cf = LV_COLOR_FORMAT_RGB565;
    im->header.w = (uint32_t)w;
    im->header.h = (uint32_t)(h * cells);
    im->header.stride = (uint32_t)(w * 2);
    im->data = (const uint8_t *)px;
    im->data_size = (uint32_t)(n * 2);
    return true;
}

reel_view_t *reel_create(lv_obj_t *parent, const reel_desc_t *d)
{
    if (d->nreels < 1 || d->nreels > REEL_MAX || d->len < 1) return NULL;
    reel_view_t *v = heap_caps_calloc(1, sizeof *v, MALLOC_CAP_SPIRAM);
    if (!v) return NULL;
    v->d = *d;
    for (int i = 0; i < d->nreels; i++)
        if (!render_strip(v, i)) { reel_destroy(v); return NULL; }
    v->obj = lv_obj_create(parent);
    lv_obj_remove_style_all(v->obj);
    if (d->vertical) lv_obj_set_size(v->obj, d->cell_w, d->nreels * d->rows * d->cell_h + (d->nreels - 1) * d->gap);
    else lv_obj_set_size(v->obj, d->nreels * d->cell_w + (d->nreels - 1) * d->gap, d->rows * d->cell_h);
    lv_obj_remove_flag(v->obj, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(v->obj, draw_cb, LV_EVENT_DRAW_MAIN, v);
    return v;
}

lv_obj_t *reel_obj(reel_view_t *v) { return v ? v->obj : NULL; }

void reel_destroy(reel_view_t *v)
{
    if (!v) return;
    if (v->tmr) lv_timer_delete(v->tmr);
    for (int i = 0; i < REEL_MAX; i++) heap_caps_free((void *)v->img[i].data);
    heap_caps_free(v);
}

static void invalidate(reel_view_t *v, int i0, int i1)   /* reels i0..i1 as one area */
{
    if (i0 > i1 || !ui_screen_on()) return;
    lv_area_t a, b;
    window(v, i0, &a);
    window(v, i1, &b);
    a.x2 = b.x2;
    a.y2 = b.y2;
    lv_obj_invalidate_area(v->obj, &a);
}

static void finish(reel_view_t *v)
{
    if (v->tmr) { lv_timer_delete(v->tmr); v->tmr = NULL; }
    v->busy = false;
    if (v->cb.done) v->cb.done(v->cb.u);
}

static void tick_cb(lv_timer_t *t)
{
    reel_view_t *v = lv_timer_get_user_data(t);
    uint32_t now = lv_tick_get();
    int32_t rel = (int32_t)(now - v->t_base);
    int step = (int)(now - v->t_last);
    v->t_last = now;
    v->frames++;
    v->step_sum += step;
    if (step > v->step_max) v->step_max = step;
    int lo = REEL_MAX, hi = -1;
    bool all = true, stepped = false;
    for (int i = 0; i < v->d.nreels; i++) {
        v->pos[i] = pc_reel_pos(&v->m[i], rel);
        long st = lround(v->pos[i]);
        if (st != v->stop[i]) { v->stop[i] = st; stepped = true; }
        int o = pix_off(v, v->pos[i]);
        if (o != v->off[i]) { v->off[i] = o; if (i < lo) lo = i; if (i > hi) hi = i; }
        if (!v->landed[i] && pc_reel_landed(&v->m[i], rel)) { v->landed[i] = true; if (v->cb.reel_landed) v->cb.reel_landed(v->cb.u, i); }
        if (!pc_reel_done(&v->m[i], rel)) all = false;
    }
    invalidate(v, lo, hi);
    if (stepped && v->cb.stepped) v->cb.stepped(v->cb.u);
    if (all) { v->ms = rel; finish(v); }
}

void reel_show(reel_view_t *v, const int *stops)
{
    if (!v) return;
    if (v->tmr) { lv_timer_delete(v->tmr); v->tmr = NULL; }
    v->busy = false;
    for (int i = 0; i < v->d.nreels; i++) {
        v->pos[i] = (double)((v->d.len - stops[i] % v->d.len) % v->d.len);
        v->off[i] = pix_off(v, v->pos[i]);
    }
    invalidate(v, 0, v->d.nreels - 1);
}

void reel_spin(reel_view_t *v, const int *stops, const reel_cbs_t *cb)
{
    if (!v || v->busy) return;
    v->cb = *cb;
    v->frames = v->step_sum = v->step_max = v->ms = 0;
    if (lowmem_level() >= PC_LOWMEM_STILL) {                 /* no animation: the result at once */
        reel_show(v, stops);
        for (int i = 0; i < v->d.nreels; i++) if (cb->reel_landed) cb->reel_landed(cb->u, i);
        printf("APP reel still (lowmem)\n");
        if (cb->done) cb->done(cb->u);
        return;
    }
    v->t_base = v->t_last = lv_tick_get();
    for (int i = 0; i < v->d.nreels; i++) {
        double p0 = floor(v->pos[i] + 0.5);
        int sp = (v->d.len - stops[i] % v->d.len) % v->d.len;
        pc_reel_plan(&v->m[i], p0, sp, v->d.len, 0, i, &pc_reel_default);
        v->stop[i] = lround(p0);
        v->landed[i] = false;
    }
    v->busy = true;
    v->tmr = lv_timer_create(tick_cb, FRAME_MS, v);
}

void reel_stop_early(reel_view_t *v)
{
    if (!v || !v->busy) return;
    int32_t rel = (int32_t)(lv_tick_get() - v->t_base);
    int k = 0;
    for (int i = 0; i < v->d.nreels; i++)
        if (rel < v->m[i].t_dec && pc_reel_stop_early(&v->m[i], v->d.len, rel, k, &pc_reel_default)) k++;
}


int reel_top(const reel_view_t *v, int reel)
{
    if (!v || reel < 0 || reel >= v->d.nreels) return 0;
    return (v->off[reel] + v->d.cell_h / 2) / v->d.cell_h % v->d.len;
}

static void draw_one(reel_view_t *v, int i, int c)
{
    const reel_desc_t *d = &v->d;
    uint16_t *px = (uint16_t *)v->img[i].data + (size_t)c * d->cell_w * d->cell_h;
    uint16_t b = lv_color_to_u16(lv_color_hex(d->bg));
    for (size_t k = 0, n = (size_t)d->cell_w * d->cell_h; k < n; k++) px[k] = b;
    d->draw_cell(d->user, d->strips[i][c % d->len], px, d->cell_w, d->cell_w, d->cell_h);
}

void reel_redraw_cell(reel_view_t *v, int reel, int stop)
{
    if (!v || v->busy || reel < 0 || reel >= v->d.nreels || stop < 0 || stop >= v->d.len || !v->img[reel].data) return;
    draw_one(v, reel, stop);
    if (stop < v->d.rows) draw_one(v, reel, v->d.len + stop);     /* the window's copy at the strip's end */
    invalidate(v, reel, reel);
}

void reel_stat(const reel_view_t *v, int *frames, int *ms, int *step_avg, int *step_max)
{
    *frames = v ? v->frames : 0;
    *ms = v ? v->ms : 0;
    *step_avg = v && v->frames ? v->step_sum / v->frames : 0;
    *step_max = v ? v->step_max : 0;
}


/* the same, the symbol enlarged by z->zoom (1/256) with nearest samples, centred (the square panel's larger slot cells) */
void reel_cell_image_zoom(void *user, int sym, uint16_t *dst, int stride, int w, int h)
{
    const reel_zoom_t *z = user;
    const lv_image_dsc_t *im = &z->im[sym];
    int sw = (int)im->header.w, sh = (int)im->header.h;
    int dw = sw * z->zoom / 256, dh = sh * z->zoom / 256;
    const uint16_t *c = (const uint16_t *)im->data;
    const uint8_t *al = im->data + (size_t)sw * sh * 2;
    int x0 = (w - dw) / 2, y0 = (h - dh) / 2;
    for (int y = 0; y < dh; y++) {
        if (y + y0 < 0 || y + y0 >= h) continue;
        int sy = y * 256 / z->zoom;
        for (int x = 0; x < dw; x++) {
            if (x + x0 < 0 || x + x0 >= w) continue;
            int sx = x * 256 / z->zoom;
            unsigned a = al[sy * sw + sx];
            if (!a) continue;
            uint16_t *p = &dst[(y + y0) * stride + x + x0];
            uint16_t sc = c[sy * sw + sx], b = *p;
            if (a == 255) { *p = sc; continue; }
            unsigned r = ((sc >> 11) * a + (b >> 11) * (255 - a)) / 255;
            unsigned g = ((sc >> 5 & 63) * a + (b >> 5 & 63) * (255 - a)) / 255;
            unsigned bl = ((sc & 31) * a + (b & 31) * (255 - a)) / 255;
            *p = (uint16_t)(r << 11 | g << 5 | bl);
        }
    }
}
