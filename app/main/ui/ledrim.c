// SPDX-License-Identifier: LGPL-2.1-or-later
/* Rim LED ring (ledrim.h). The segment geometry (RING_R 173, RING_W 7, 22.5 degrees a segment) lives here and is shared with inst_ui.c. */
#include "ledrim.h"
#include "ui_geom.h"
#include <math.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "misc/lv_area_private.h"
#include "draw/lv_draw_private.h"
#include "pc/pc_ledfx.h"
#include "ui.h"
#include "ui_theme.h"
#include "ui_kit.h"

#define RING_R 173
#define RING_W 7
#define RING_HALF 8.0f
#define TICK_MS 33

static lv_obj_t *s_obj;
static lv_timer_t *s_tmr;
static lv_area_t s_area[LEDRIM_SEGS];
static lv_image_dsc_t s_img[LEDRIM_SEGS];
static uint32_t s_col[LEDRIM_SEGS], s_shown[LEDRIM_SEGS];
static float s_lvl[LEDRIM_SEGS], s_rate[LEDRIM_SEGS];   /* level 0..1, fall per ms */
static float s_comet;                                     /* chase: head position in segments, < 0 = none */
static int s_comet_end;
static uint32_t s_comet_col;

float ledrim_seg_deg(int v) { return -123.75f + 22.5f * (float)v; }
static uint32_t unlit(void)   /* dark: the panel2 tone toward the page; a light theme: the page toward the text colour, a faint tone that shows */
{
    return pc_theme_is_light(ui_theme()) ? pc_fx_mix(ui_theme()->bg, ui_theme()->fg, 0.14f) : pc_fx_mix(ui_theme()->panel2, ui_theme()->bg, 0.25f);
}

#if UI_ROUND
void ledrim_seg_area(int v, lv_area_t *a)
{
    int x1 = 360, y1 = 360, x2 = 0, y2 = 0;
    for (int i = 0; i <= 8; i++) {
        float d = (ledrim_seg_deg(v) - RING_HALF + 2 * RING_HALF * (float)i / 8.0f) * 3.1415927f / 180.0f;
        for (int r = RING_R - RING_W; r <= RING_R; r += RING_W) {
            int x = UI_CX + (int)lroundf((float)r * sinf(d)), y = UI_CY - (int)lroundf((float)r * cosf(d));
            x1 = LV_MIN(x1, x); y1 = LV_MIN(y1, y); x2 = LV_MAX(x2, x); y2 = LV_MAX(y2, y);
        }
    }
    a->x1 = x1 - 1; a->y1 = y1 - 1; a->x2 = x2 + 1; a->y2 = y2 + 1;
}

bool ledrim_seg_mask(const lv_area_t *a, int v, lv_image_dsc_t *im)
{
    int w = lv_area_get_width(a), h = lv_area_get_height(a);
    uint8_t *m = heap_caps_calloc(1, (size_t)(w * h), MALLOC_CAP_SPIRAM);
    if (!m) return false;
    const float rc = RING_R - RING_W / 2.0f, hw = RING_W / 2.0f, deg = 3.1415927f / 180.0f;
    const float core = RING_HALF - hw / rc / deg;
    const float c0 = ledrim_seg_deg(v) - core, c1 = ledrim_seg_deg(v) + core;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            float dx = (float)(a->x1 + x) + 0.5f - (float)UI_CX, dy = (float)(a->y1 + y) + 0.5f - (float)UI_CY;
            float r = sqrtf(dx * dx + dy * dy), t = atan2f(dx, -dy) / deg, d;
            if (t >= c0 && t <= c1) d = fabsf(r - rc) - hw;
            else {
                float e = (t < c0 ? c0 : c1) * deg, ex = rc * sinf(e) - dx, ey = -rc * cosf(e) - dy;
                d = sqrtf(ex * ex + ey * ey) - hw;
            }
            float cov = 0.5f - d;
            m[y * w + x] = (uint8_t)(cov <= 0 ? 0 : cov >= 1 ? 255 : (int)(cov * 255.0f));
        }
    memset(im, 0, sizeof *im);
    im->header.magic = LV_IMAGE_HEADER_MAGIC;
    im->header.cf = LV_COLOR_FORMAT_A8;
    im->header.w = (uint32_t)w;
    im->header.h = (uint32_t)h;
    im->header.stride = (uint32_t)w;
    im->data = m;
    im->data_size = (uint32_t)(w * h);
    return true;
}
#else
/* The square panel: the same 12 segments (same angular slots as the round ring: 22.5 degrees of the loop a segment) run along the panel's edge as a
 * rounded-square band. The loop's length is split by the same fractions of 360, so segment v sits where the round ring's segment v is, projected on
 * the perimeter (u = arc length from the top centre, clockwise). The centre line is a rounded square: half size SQ_HC, corner radius SQ_RR. */
#define SQ_HC   (UI_W / 2.0f - (float)UI_RIM_SQ_INSET - RING_W / 2.0f)
#define SQ_RR   ((float)UI_RIM_SQ_RR)
#define SQ_HI   (SQ_HC - SQ_RR)
#define SQ_ARC  (3.1415927f * SQ_RR / 2.0f)
#define SQ_LQ   (2 * SQ_HI + SQ_ARC)      /* one quadrant: half a side, a corner, half the next side */
#define SQ_L    (4 * SQ_LQ)

static void sq_point(float u, float *px, float *py)         /* the centre line at arc length u (wrapped), clockwise from the top centre */
{
    u = fmodf(u, SQ_L);
    if (u < 0) u += SQ_L;
    int q = (int)(u / SQ_LQ);
    if (q > 3) q = 3;
    float uq = u - q * SQ_LQ, X, Y;                                     /* the top-right quadrant's frame: X right, Y down (negative at the top) */
    if (uq < SQ_HI) { X = uq; Y = -SQ_HC; }
    else if (uq < SQ_HI + SQ_ARC) { float f = (uq - SQ_HI) / SQ_RR; X = SQ_HI + SQ_RR * sinf(f); Y = -SQ_HI - SQ_RR * cosf(f); }
    else { X = SQ_HC; Y = -SQ_HI + (uq - SQ_HI - SQ_ARC); }
    switch (q) {                                                         /* back to the panel: BR X = dy, Y = -dx ... */
    case 0: *px = X; *py = Y; break;
    case 1: *px = -Y; *py = X; break;
    case 2: *px = -X; *py = -Y; break;
    default: *px = Y; *py = -X; break;
    }
}
static void sq_project(float dx, float dy, float *u, float *dist)   /* the nearest centre-line point's arc length and the distance to the line */
{
    int q = dx >= 0 ? (dy < 0 ? 0 : 1) : (dy >= 0 ? 2 : 3);
    float X, Y;
    switch (q) { case 0: X = dx; Y = dy; break; case 1: X = dy; Y = -dx; break; case 2: X = -dx; Y = -dy; break; default: X = -dy; Y = dx; break; }
    float ax = X, ay = -Y, uq, d;
    if (ax > SQ_HI && ay > SQ_HI) {
        float qx = ax - SQ_HI, qy = ay - SQ_HI, r = sqrtf(qx * qx + qy * qy);
        d = fabsf(r - SQ_RR);
        uq = SQ_HI + SQ_RR * atan2f(qx, qy);
    } else if (ay >= ax) {                                   /* the top edge (deep inside too) */
        d = fabsf(ay - SQ_HC);
        uq = ax < SQ_HI ? ax : SQ_HI;
    } else {                                                 /* the right edge */
        d = fabsf(ax - SQ_HC);
        uq = SQ_HI + SQ_ARC + (SQ_HI - (ay < SQ_HI ? ay : SQ_HI));
    }
    *u = q * SQ_LQ + uq;
    *dist = d;
}
static float sq_centre(int v) { return SQ_L * ledrim_seg_deg(v) / 360.0f; }
static float sq_half(void) { return SQ_L * RING_HALF / 360.0f - RING_W / 2.0f; }   /* the straight core's half length; the round caps add RING_W / 2 */

void ledrim_seg_area(int v, lv_area_t *a)
{
    int x1 = UI_W, y1 = UI_H, x2 = 0, y2 = 0;
    const float c = sq_centre(v), h = sq_half() + RING_W / 2.0f, pad = RING_W / 2.0f + 2;
    for (int i = 0; i <= 12; i++) {
        float px, py;
        sq_point(c - h + 2 * h * (float)i / 12.0f, &px, &py);
        x1 = LV_MIN(x1, (int)floorf(UI_CX + px - pad)); y1 = LV_MIN(y1, (int)floorf(UI_CY + py - pad));
        x2 = LV_MAX(x2, (int)ceilf(UI_CX + px + pad)); y2 = LV_MAX(y2, (int)ceilf(UI_CY + py + pad));
    }
    a->x1 = LV_MAX(x1, 0); a->y1 = LV_MAX(y1, 0); a->x2 = LV_MIN(x2, UI_W - 1); a->y2 = LV_MIN(y2, UI_H - 1);
}

bool ledrim_seg_mask(const lv_area_t *a, int v, lv_image_dsc_t *im)
{
    int w = lv_area_get_width(a), h = lv_area_get_height(a);
    uint8_t *m = heap_caps_calloc(1, (size_t)(w * h), MALLOC_CAP_SPIRAM);
    if (!m) return false;
    const float hw = RING_W / 2.0f, c = sq_centre(v), half = sq_half();
    float e0x, e0y, e1x, e1y;
    sq_point(c - half, &e0x, &e0y);
    sq_point(c + half, &e1x, &e1y);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            float dx = (float)(a->x1 + x) + 0.5f - (float)UI_CX, dy = (float)(a->y1 + y) + 0.5f - (float)UI_CY, u, dist, d;
            sq_project(dx, dy, &u, &dist);
            float t = fmodf(u - c + SQ_L * 1.5f, SQ_L) - SQ_L / 2.0f;        /* arc length from the segment's centre, wrapped to +-L/2 */
            if (t >= -half && t <= half) d = dist - hw;
            else {
                float ex = (t < 0 ? e0x : e1x) - dx, ey = (t < 0 ? e0y : e1y) - dy;
                d = sqrtf(ex * ex + ey * ey) - hw;
            }
            float cov = 0.5f - d;
            m[y * w + x] = (uint8_t)(cov <= 0 ? 0 : cov >= 1 ? 255 : (int)(cov * 255.0f));
        }
    memset(im, 0, sizeof *im);
    im->header.magic = LV_IMAGE_HEADER_MAGIC;
    im->header.cf = LV_COLOR_FORMAT_A8;
    im->header.w = (uint32_t)w;
    im->header.h = (uint32_t)h;
    im->header.stride = (uint32_t)w;
    im->data = m;
    im->data_size = (uint32_t)(w * h);
    return true;
}
#endif

static void draw_cb(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t tmp;
    lv_draw_image_dsc_t d;
    for (int v = 0; v < LEDRIM_SEGS; v++) {
        if (!s_img[v].data || !lv_area_intersect(&tmp, &layer->_clip_area, &s_area[v])) continue;
        lv_draw_image_dsc_init(&d);
        d.src = &s_img[v];
        d.recolor = lv_color_hex(s_shown[v]);
        d.recolor_opa = LV_OPA_COVER;
        lv_draw_image(layer, &d, &s_area[v]);
    }
}

static bool update(void)   /* recolour; one area per quarter; true while anything is lit */
{
    uint32_t base = unlit();
    bool lit = false;
    for (int g = 0; g < LEDRIM_SEGS; g += 3) {
        lv_area_t u;
        bool any = false;
        for (int v = g; v < g + 3; v++) {
            float l = s_lvl[v];
            uint32_t c = s_col[v];
            if (s_comet >= 0) {                                  /* the comet: head plus a short tail */
                float d = s_comet - (float)v;
                while (d < 0) d += LEDRIM_SEGS;
                d = fmodf(d, LEDRIM_SEGS);
                float k = d < 3.0f ? 1.0f - d / 3.0f : 0.0f;
                if (k > l) { l = k; c = s_comet_col; }
            }
            if (l > 0.004f) lit = true;
            uint32_t out = l > 0.004f ? pc_fx_mix(base, c, l > 1 ? 1 : l) : base;
            if (ledrim_rgb565(out) == ledrim_rgb565(s_shown[v])) continue;
            s_shown[v] = out;
            if (!any) u = s_area[v];
            else { u.x1 = LV_MIN(u.x1, s_area[v].x1); u.y1 = LV_MIN(u.y1, s_area[v].y1); u.x2 = LV_MAX(u.x2, s_area[v].x2); u.y2 = LV_MAX(u.y2, s_area[v].y2); }
            any = true;
        }
        if (any && ui_screen_on()) lv_obj_invalidate_area(s_obj, &u);
    }
    return lit || s_comet >= 0;
}

static void tick_cb(lv_timer_t *t)
{
    (void)t;
    for (int v = 0; v < LEDRIM_SEGS; v++) { s_lvl[v] -= s_rate[v] * TICK_MS; if (s_lvl[v] < 0) s_lvl[v] = 0; }
    if (s_comet >= 0) { s_comet += 0.6f; if (s_comet > (float)s_comet_end) s_comet = -1; }
    if (!update()) { update(); lv_timer_delete(s_tmr); s_tmr = NULL; }
}

static void run(void)
{
    if (!s_obj) return;
    update();
    if (!s_tmr) s_tmr = lv_timer_create(tick_cb, TICK_MS, NULL);
}

lv_obj_t *ledrim_create(lv_obj_t *parent)
{
    ledrim_destroy();
    for (int v = 0; v < LEDRIM_SEGS; v++) {
        ledrim_seg_area(v, &s_area[v]);
        if (!ledrim_seg_mask(&s_area[v], v, &s_img[v])) { ledrim_destroy(); return NULL; }
        s_lvl[v] = 0;
        s_shown[v] = s_col[v] = unlit();
    }
    s_comet = -1;
    s_obj = lv_obj_create(parent);
    lv_obj_remove_style_all(s_obj);
    lv_obj_set_size(s_obj, UI_W, UI_H);
    lv_obj_align(s_obj, LV_ALIGN_CENTER, 0, 0);      /* the whole panel */
    lv_obj_remove_flag(s_obj, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_obj, draw_cb, LV_EVENT_DRAW_MAIN, NULL);
    ui_timer_guard(s_obj, &s_tmr);                       /* lists and vocab clean their root before ledrim_destroy */
    return s_obj;
}

void ledrim_destroy(void)   /* the object goes with its screen */
{
    if (s_tmr) { lv_timer_delete(s_tmr); s_tmr = NULL; }
    for (int v = 0; v < LEDRIM_SEGS; v++) { heap_caps_free((void *)s_img[v].data); memset(&s_img[v], 0, sizeof s_img[v]); }
    s_obj = NULL;
}

void ledrim_seg(int v, uint32_t rgb, int fade_ms)
{
    if (v < 0 || v >= LEDRIM_SEGS) return;
    s_lvl[v] = 1.0f;
    s_col[v] = pc_fx_ink(ui_theme(), rgb);     /* a light page: dark enough to read */
    s_rate[v] = 1.0f / (float)(fade_ms > 1 ? fade_ms : 1);
    run();
}

void ledrim_flash(uint32_t rgb, int fade_ms) { for (int v = 0; v < LEDRIM_SEGS; v++) ledrim_seg(v, rgb, fade_ms); }

void ledrim_chase(uint32_t rgb, int laps)
{
    s_comet = 0;
    s_comet_end = LEDRIM_SEGS * (laps > 0 ? laps : 1) + 3;
    s_comet_col = pc_fx_ink(ui_theme(), rgb);
    run();
}

/* ---- the instrument's effect ring (moved from inst_ui.c ring_build / ring_draw / ring_update) ---- */
static lv_obj_t *s_fx_obj;
static pc_ledfx_t *s_fx;
static lv_area_t s_fx_area[LEDRIM_SEGS];
static lv_image_dsc_t s_fx_img[LEDRIM_SEGS];
static uint32_t s_fx_col[LEDRIM_SEGS];
static uint32_t s_fx_n, s_fx_us, s_fx_max;

static void fx_draw(lv_event_t *e)
{
    int64_t t0 = esp_timer_get_time();
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t tmp;
    lv_draw_image_dsc_t d;
    for (int v = 0; v < LEDRIM_SEGS; v++) {
        if (!s_fx_img[v].data || !lv_area_intersect(&tmp, &layer->_clip_area, &s_fx_area[v])) continue;
        lv_draw_image_dsc_init(&d);
        d.src = &s_fx_img[v];
        d.recolor = lv_color_hex(s_fx_col[v]);
        d.recolor_opa = LV_OPA_COVER;
        lv_draw_image(layer, &d, &s_fx_area[v]);
    }
    uint32_t us = (uint32_t)(esp_timer_get_time() - t0);
    s_fx_n++;
    s_fx_us += us;
    if (us > s_fx_max) s_fx_max = us;
}

static void fx_deleted(lv_event_t *e) { (void)e; s_fx_obj = NULL; }

lv_obj_t *ledrim_fx_create(lv_obj_t *scr, pc_ledfx_t *fx)
{
    for (int v = 0; v < LEDRIM_SEGS; v++) {
        ledrim_seg_area(v, &s_fx_area[v]);
        if (!s_fx_img[v].data && !ledrim_seg_mask(&s_fx_area[v], v, &s_fx_img[v])) return NULL;   /* the masks stay for the mode's life */
        s_fx_col[v] = unlit();
    }
    s_fx = fx;
    s_fx_obj = lv_obj_create(scr);
    lv_obj_remove_style_all(s_fx_obj);
    lv_obj_set_size(s_fx_obj, UI_W, UI_H);
    lv_obj_align(s_fx_obj, LV_ALIGN_CENTER, 0, 0);
    lv_obj_remove_flag(s_fx_obj, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_fx_obj, fx_draw, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_add_event_cb(s_fx_obj, fx_deleted, LV_EVENT_DELETE, NULL);
    return s_fx_obj;
}

/* changed segments, one area per `group` of them: each area is a render pass plus a QSPI window of its own, and that per-area cost
 * outweighs the few extra pixels of a union */
void ledrim_fx_update(int group)
{
    if (!s_fx_obj || !s_fx) return;
    if (group < 1) group = 1;
    uint32_t out[PC_FX_SEGS];
    pc_fx_render(s_fx, unlit(), ui_viz_ramp(), ui_theme(), out);
    for (int g = 0; g < LEDRIM_SEGS; g += group) {
        lv_area_t u;
        bool any = false;
        for (int v = g; v < g + group && v < LEDRIM_SEGS; v++) {
            if (ledrim_rgb565(out[v]) == ledrim_rgb565(s_fx_col[v])) continue;   /* the panel shows 16 bits */
            s_fx_col[v] = out[v];
            if (!any) u = s_fx_area[v];
            else { u.x1 = LV_MIN(u.x1, s_fx_area[v].x1); u.y1 = LV_MIN(u.y1, s_fx_area[v].y1); u.x2 = LV_MAX(u.x2, s_fx_area[v].x2); u.y2 = LV_MAX(u.y2, s_fx_area[v].y2); }
            any = true;
        }
        if (any) lv_obj_invalidate_area(s_fx_obj, &u);
    }
}

void ledrim_fx_stat(ledrim_fx_stat_t *out)
{
    *out = (ledrim_fx_stat_t){ s_fx_n, s_fx_n ? s_fx_us / s_fx_n : 0, s_fx_max };
    s_fx_n = s_fx_us = s_fx_max = 0;
}
