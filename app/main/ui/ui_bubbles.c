// SPDX-License-Identifier: LGPL-2.1-or-later
/* The bubble field (ui_bubbles.h). Layout: a hex lattice (rows 0.866 pitch apart, odd rows half a pitch across) clipped to the whole design circle
 * (radius UI_CONTENT_D / 2 less a glass margin; the page has no Back button since G4b: its exit is the shared 返回 pill, ui_exit.h); the vertical phase and
 * the first row's parity are the ones that fit the most bubbles (the loop is a few hundred steps, once at create). The object covers the whole circle's box. */
#include "ui_bubbles.h"
#include <stdio.h>
#include <string.h>
#include "misc/lv_area_private.h"
#include "draw/lv_draw_private.h"
#include "pc/pc_fortune.h"
#include "psram_alloc.h"
#include "ui_geom.h"
#include "ui_kit.h"
#include "ui_theme.h"

#define BUBBLE_D     40                       /* drawn diameter: a touch lands on the whole pitch around it, not just on the dome */
#define BUBBLE_GAP   4
#define BUBBLE_PITCH (BUBBLE_D + BUBBLE_GAP)
#define ROW_PITCH    (BUBBLE_PITCH * 866 / 1000)
#define GLASS_EDGE   6                        /* the outermost bubble keeps this far from the circle */
#define MAX_ROWS     16

typedef struct {
    lv_obj_t *obj;
    ui_bubble_pop_cb cb;
    int n, left, special, rows;
    int16_t row_first[MAX_ROWS + 1];          /* index of each row's first bubble; [rows] = n */
    int16_t *x, *y;                           /* centres, relative to the object's top-left */
    uint8_t *up;                              /* 1 raised, 0 popped */
} bf_t;

static bf_t *s_live;                          /* the field on the open page (console) */

/* one candidate lattice: rows from the top of the circle with vertical phase pv, first row's parity par. Counts the bubbles that fit and, when f is given, stores them. */
static int lay(int pv, int par, bf_t *f, int *rows_out)
{
    const int R = UI_CONTENT_D / 2, lim = R - GLASS_EDGE;
    const int bottom = lim;                                          /* from the circle's centre down: the last pixel a bubble may use */
    int n = 0, rows = 0;
    for (int r = 0;; r++) {
        int y = -lim + BUBBLE_D / 2 + pv + r * ROW_PITCH;
        if (y + BUBBLE_D / 2 > bottom) break;
        int first = n, off = ((r + par) & 1) ? BUBBLE_PITCH / 2 : 0;
        for (int k = -R / BUBBLE_PITCH - 1; k <= R / BUBBLE_PITCH + 1; k++) {
            int x = off + k * BUBBLE_PITCH;
            long d2 = (long)x * x + (long)y * y, lim2 = (long)(lim - BUBBLE_D / 2) * (lim - BUBBLE_D / 2);
#if UI_ROUND
            if (d2 > lim2) continue;
#else
            if (x > lim - BUBBLE_D / 2 || x < -(lim - BUBBLE_D / 2)) continue;     /* the square panel: the lattice fills the whole panel, not a circle */
            (void)lim2; (void)d2;
#endif
            if (f) { f->x[n] = (int16_t)(R + x); f->y[n] = (int16_t)(R + y); }
            n++;
        }
        if (n > first) {
            if (f && rows < MAX_ROWS) f->row_first[rows] = (int16_t)first;
            rows++;
        }
    }
    if (f) { f->rows = rows < MAX_ROWS ? rows : MAX_ROWS; f->row_first[f->rows] = (int16_t)n; }
    if (rows_out) *rows_out = rows;
    return rows <= MAX_ROWS ? n : 0;
}

static int best_lay(int *pv, int *par)
{
    int best = 0;
    for (int p = 0; p < ROW_PITCH; p++)
        for (int q = 0; q < 2; q++) {
            int rows = 0, n = lay(p, q, NULL, &rows);
#if !UI_ROUND
            if (q != 0 || !(rows & 1)) continue;          /* the square panel's first and last rows are the SHORT ones (9, 10, 9 ... 9), so the field reads as rounded corners */
#endif
            if (n > best) { best = n; *pv = p; *par = q; }
        }
    #if !UI_ROUND
    if (best) {                                          /* SQ3: the unused room under the last row is shared with the top: the field sits in the middle of the panel */
        int rows = 0;
        lay(*pv, *par, NULL, &rows);
        const int lim = UI_CONTENT_D / 2 - GLASS_EDGE, last = -lim + BUBBLE_D / 2 + *pv + (rows - 1) * ROW_PITCH + BUBBLE_D / 2;
        if (rows > 0 && lim > last) *pv += (lim - last) / 2;
    }
#endif
    return best;
}

static void bubble_area(const bf_t *f, int i, lv_area_t *a)
{
    lv_area_t o;
    lv_obj_get_coords(f->obj, &o);
    a->x1 = o.x1 + f->x[i] - BUBBLE_D / 2;
    a->y1 = o.y1 + f->y[i] - BUBBLE_D / 2;
    a->x2 = a->x1 + BUBBLE_D - 1;
    a->y2 = a->y1 + BUBBLE_D - 1;
}

static void draw_cb(lv_event_t *e)
{
    const bf_t *f = lv_event_get_user_data(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    const lv_area_t clip = layer->_clip_area;
    const lv_color_t dome = lv_color_mix(UI_A1, UI_PANEL, 90), rim = lv_color_mix(UI_FG, dome, 90), flat = UI_PANEL, flat_rim = UI_PANEL2;
    lv_draw_rect_dsc_t d, hl;
    lv_draw_rect_dsc_init(&d);
    d.radius = LV_RADIUS_CIRCLE;
    d.bg_opa = LV_OPA_COVER;
    d.border_opa = LV_OPA_COVER;
    lv_draw_rect_dsc_init(&hl);                           /* the highlight of a raised dome: a small light disc up and left */
    hl.radius = LV_RADIUS_CIRCLE;
    hl.bg_opa = LV_OPA_50;
    hl.bg_color = ui_theme_is_light() ? lv_color_mix(UI_FG, UI_A1, 128) : lv_color_white();   /* a light dome is pale: the sheen is a deeper spot of the accent */
    for (int i = 0; i < f->n; i++) {
        lv_area_t a, x;
        bubble_area(f, i, &a);
        if (!lv_area_intersect(&x, &clip, &a)) continue;
        if (f->up[i]) {
            const bool sp = i == f->special;                  /* the lucky bubble: a faint tint of the gold and a second small sheen dot (a surprise, not a marker) */
            d.bg_color = sp ? lv_color_mix(UI_GOLD, dome, 40) : dome; d.border_color = sp ? lv_color_mix(UI_GOLD, rim, 40) : rim; d.border_width = 3;
            lv_draw_rect(layer, &d, &a);
            if (sp) {
                lv_area_t h2 = { a.x2 - BUBBLE_D / 4 - BUBBLE_D / 10, a.y2 - BUBBLE_D / 4 - BUBBLE_D / 10, a.x2 - BUBBLE_D / 4, a.y2 - BUBBLE_D / 4 };
                lv_draw_rect(layer, &hl, &h2);
            }
            lv_area_t h = { a.x1 + BUBBLE_D / 4, a.y1 + BUBBLE_D / 5, a.x1 + BUBBLE_D / 4 + BUBBLE_D / 6, a.y1 + BUBBLE_D / 5 + BUBBLE_D / 6 };
            lv_draw_rect(layer, &hl, &h);
        } else {
            d.bg_color = flat; d.border_color = flat_rim; d.border_width = 1;
            lv_draw_rect(layer, &d, &a);
        }
    }
}

static int hit(const bf_t *f, int sx, int sy)
{
    lv_area_t o;
    lv_obj_get_coords(f->obj, &o);
    int best = -1;
    long bd = (long)(BUBBLE_PITCH / 2 + 1) * (BUBBLE_PITCH / 2 + 1);
    for (int i = 0; i < f->n; i++) {
        long dx = sx - (o.x1 + f->x[i]), dy = sy - (o.y1 + f->y[i]), d2 = dx * dx + dy * dy;
        if (d2 <= bd) { bd = d2; best = i; }
    }
    return best;
}

static bool pop(bf_t *f, int idx)
{
    if (idx < 0 || idx >= f->n || !f->up[idx]) return false;
    f->up[idx] = 0;
    f->left--;
    lv_area_t a;
    bubble_area(f, idx, &a);
    lv_obj_invalidate_area(f->obj, &a);
    if (f->cb) f->cb(idx, idx == f->special, f->left);
    return true;
}

static void press_cb(lv_event_t *e)
{
    bf_t *f = lv_event_get_user_data(e);
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    pop(f, hit(f, p.x, p.y));
}

static void delete_cb(lv_event_t *e)
{
    bf_t *f = lv_event_get_user_data(e);
    if (s_live == f) s_live = NULL;
    psram_free(f);
}

lv_obj_t *ui_bubble_field_create(lv_obj_t *parent, ui_bubble_pop_cb cb)
{
    int pv = 0, par = 0, n = best_lay(&pv, &par);
    if (n <= 0) return NULL;
    size_t sz = sizeof(bf_t) + (size_t)n * (2 * sizeof(int16_t) + 1);
    bf_t *f = psram_calloc(1, sz);
    if (!f) return NULL;
    f->x = (int16_t *)(f + 1);
    f->y = f->x + n;
    f->up = (uint8_t *)(f->y + n);
    f->n = n;
    f->cb = cb;
    lay(pv, par, f, NULL);
    const int R = UI_CONTENT_D / 2, bottom = R;
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, UI_CONTENT_D, R + bottom);
    lv_obj_align(o, LV_ALIGN_CENTER, 0, (R + bottom) / 2 - R);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
    f->obj = o;
    lv_obj_add_event_cb(o, draw_cb, LV_EVENT_DRAW_MAIN, f);
    lv_obj_add_event_cb(o, press_cb, LV_EVENT_PRESSED, f);
    lv_obj_add_event_cb(o, delete_cb, LV_EVENT_DELETE, f);
    lv_obj_set_user_data(o, f);
    f->special = -1;
    ui_bubble_field_reset(o);
    s_live = f;
    return o;
}

static bf_t *of(const lv_obj_t *o) { return o ? lv_obj_get_user_data((lv_obj_t *)o) : NULL; }

void ui_bubble_field_reset(lv_obj_t *field)
{
    bf_t *f = of(field);
    if (!f) return;
    memset(f->up, 1, (size_t)f->n);
    f->left = f->n;
    int prev = f->special, s = (int)pc_rng_below(ui_rng(), (uint32_t)(f->n > 1 && prev >= 0 ? f->n - 1 : f->n));
    if (prev >= 0 && f->n > 1 && s >= prev) s++;     /* the new special is never the old one */
    f->special = s;
    lv_obj_invalidate(f->obj);
}


static void dump(const bf_t *f)
{
    printf("APP bubbles n=%d left=%d special=%d\n", f->n, f->left, f->special);
    for (int r = 0; r < f->rows; r++) {
        char b[48];
        int k = 0;
        for (int i = f->row_first[r]; i < f->row_first[r + 1] && k < (int)sizeof b - 1; i++) b[k++] = f->up[i] ? '1' : '0';
        b[k] = 0;
        printf("APP bubbles row %d %s\n", r, b);
    }
}

void ui_bubbles_cmd(const char *arg)
{
    bf_t *f = s_live;
    int i;
    if (!f) { printf("APP bubbles none\n"); return; }
    if (!arg[0]) dump(f);
    else if (sscanf(arg, "tap %d", &i) == 1) { pop(f, i); dump(f); }
    else if (!strcmp(arg, "pop all")) { for (i = 0; i < f->n; i++) pop(f, i); }
    else if (sscanf(arg, "pop %d", &i) == 1) pop(f, i);
    else if (sscanf(arg, "at %d", &i) == 1 && i >= 0 && i < f->n) {
        lv_area_t o;
        lv_obj_update_layout(f->obj);
        lv_obj_get_coords(f->obj, &o);
        printf("APP bubbles at %d x=%d,y=%d\n", i, (int)(o.x1 + f->x[i]), (int)(o.y1 + f->y[i]));
    }
    else printf("APP bubbles unknown %s\n", arg);
}
