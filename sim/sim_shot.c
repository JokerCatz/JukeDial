// SPDX-License-Identifier: LGPL-2.1-or-later
/* Screenshot: the same snapshot the board's console "shot" takes (RGB565), converted like tools/simtools/shot.py, as PNG. */
#include <png.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lvgl.h"
#include "sim.h"
#include "ui.h"
#include "splash.h"
#include "pc/pc_rotate.h"

bool sim_shot_png(const char *file)
{
    lv_draw_buf_t *snap = lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_RGB565);
    if (!snap) return false;
    uint32_t w = snap->header.w, h = snap->header.h, stride = snap->header.stride;
    uint8_t *rgb = malloc((size_t)w * h * 3);
    FILE *f = fopen(file, "wb");
    png_structp png = f && rgb ? png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL) : NULL;
    png_infop info = png ? png_create_info_struct(png) : NULL;
    bool ok = false;
    if (info && !setjmp(png_jmpbuf(png))) {
        for (uint32_t y = 0; y < h; y++) {
            const uint8_t *row = (const uint8_t *)snap->data + (size_t)y * stride;
            uint8_t *o = rgb + (size_t)y * w * 3;
            for (uint32_t x = 0; x < w; x++, o += 3) {
                uint32_t v = row[2 * x] | row[2 * x + 1] << 8;   /* little-endian RGB565, as in console.c */
                o[0] = (uint8_t)((v >> 11 & 31) * 255 / 31);
                o[1] = (uint8_t)((v >> 5 & 63) * 255 / 63);
                o[2] = (uint8_t)((v & 31) * 255 / 31);
            }
        }
        lv_obj_t *layers[2] = { lv_layer_top(), lv_layer_sys() };   /* the top layer holds the square panel's status strip, the system layer the power-on picture (splash.c): the active-screen snapshot leaves both out: blended over the picture here */
        for (int li = 0; li < 2; li++) {
          if (li == 0 ? (ui_strip_shown() || getenv("SIM_SHOT_TOP")) : splash_shown()) {   /* SIM_SHOT_TOP=1: always blend the top layer (the volume overlay is on it; the baselines do not set it) */
            lv_draw_buf_t *top = lv_snapshot_take(layers[li], LV_COLOR_FORMAT_ARGB8888);
            if (top && top->header.w == w && top->header.h == h)
                for (uint32_t y = 0; y < h; y++) {
                    const uint8_t *tr = (const uint8_t *)top->data + (size_t)y * top->header.stride;
                    uint8_t *o = rgb + (size_t)y * w * 3;
                    for (uint32_t x = 0; x < w; x++, o += 3, tr += 4) {   /* ARGB8888 little-endian: B G R A */
                        unsigned a = tr[3];
                        if (!a) continue;
                        for (int c = 0; c < 3; c++) o[c] = (uint8_t)((o[c] * (255 - a) + tr[2 - c] * a) / 255);
                    }
                }
            if (top) lv_draw_buf_destroy(top);
          }
        }
        if (sim_rotation() && w == h) {   /* the screen as the user sees it: the UI's picture turned 180 degrees (pc_rotate.h) */
            uint8_t *rot = malloc((size_t)w * h * 3);
            if (!rot) goto done;
            for (uint32_t y = 0; y < h; y++)
                for (uint32_t x = 0; x < w; x++) {
                    int ox, oy;
                    pc_rotate_screen(sim_rotation(), (int)w, (int)x, (int)y, &ox, &oy);
                    memcpy(rot + ((size_t)oy * w + (size_t)ox) * 3, rgb + ((size_t)y * w + x) * 3, 3);
                }
            memcpy(rgb, rot, (size_t)w * h * 3);
            free(rot);
        }
        png_init_io(png, f);
        png_set_IHDR(png, info, w, h, 8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
        png_write_info(png, info);
        for (uint32_t y = 0; y < h; y++) png_write_row(png, rgb + (size_t)y * w * 3);
        png_write_end(png, NULL);
        ok = true;
    }
done:
    if (png) png_destroy_write_struct(&png, info ? &info : NULL);
    if (f && fclose(f)) ok = false;
    free(rgb);
    if (!ok && f) remove(file);   /* no partial PNG left behind */
    lv_draw_buf_destroy(snap);
    return ok;
}
