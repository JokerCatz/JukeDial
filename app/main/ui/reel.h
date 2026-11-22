// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"
/* Reel animation (tools: slot machine; later the list tools): each reel is a vertical strip of cells pre-rendered
 * once into PSRAM (opaque RGB565, the window's rows repeated at the end so any offset is one blit), moved by
 * pc_reel (speed-up, cruise, ease-out to a small overshoot, settle back; staggered stops). One LVGL object draws all
 * reels; each frame invalidates one area (the union of the reels still moving). 33 ms frames. lowmem_level() >= 2:
 * no animation, the result is shown at once. LVGL task only; any number of instances. */
typedef struct reel_view reel_view_t;
/* draw symbol sym into an opaque w x h RGB565 cell (stride in pixels) whose background is already filled */
typedef void (*reel_cell_fn)(void *user, int sym, uint16_t *dst, int stride, int w, int h);
typedef struct {
    int nreels, rows, cell_w, cell_h, gap;   /* gap: pixels between reels */
    bool vertical;                           /* reels stacked top to bottom (list phrases) instead of side by side */
    int len;                                 /* stops per strip */
    const uint8_t *const *strips;            /* nreels strips of len symbol numbers (kept by the caller) */
    reel_cell_fn draw_cell;
    void *user;
    uint32_t bg;                             /* cell background 0xRRGGBB */
} reel_desc_t;
typedef struct {
    void (*reel_landed)(void *u, int reel);  /* a reel reached its stop (before settling): a haptic tick */
    void (*done)(void *u);                   /* every reel still */
    void (*stepped)(void *u);                /* optional: one or more reels moved on by a whole stop this frame (at most once a frame): a click sound */
    void *u;
} reel_cbs_t;

reel_view_t *reel_create(lv_obj_t *parent, const reel_desc_t *d);   /* NULL: no PSRAM for the strips */
lv_obj_t *reel_obj(reel_view_t *v);          /* size nreels*cell_w + gaps by rows*cell_h; align it as you like */
void reel_destroy(reel_view_t *v);           /* frees strips and timer; the object goes with its parent */
void reel_spin(reel_view_t *v, const int *stops, const reel_cbs_t *cb);   /* top-row stop of each reel */
void reel_stop_early(reel_view_t *v);        /* knob / tap while spinning: stop soon, same result */
void reel_show(reel_view_t *v, const int *stops);   /* jump there, no animation */
int  reel_top(const reel_view_t *v, int reel);   /* the stop shown in the top row now */
/* draw one stop of a reel's strip again (what its cell callback draws changed: a large list put another item
 * there); not while spinning */
void reel_redraw_cell(reel_view_t *v, int reel, int stop);
/* last spin: timer frames, ms from start to still, average / max frame step (ms) */
void reel_stat(const reel_view_t *v, int *frames, int *ms, int *step_avg, int *step_max);
/* ready-made cell drawer: user = a reel_zoom_t (an array of LV_COLOR_FORMAT_RGB565A8 lv_image_dsc_t, one per symbol; the symbol enlarged by nearest samples, centred) */
typedef struct { const lv_image_dsc_t *im; int zoom; } reel_zoom_t;   /* zoom in 1/256 */
void reel_cell_image_zoom(void *user, int sym, uint16_t *dst, int stride, int w, int h);
