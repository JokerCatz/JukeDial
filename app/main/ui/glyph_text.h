// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"
#include "pc/pc_glyphpack.h"
/* Text from a glyph pack (spec 2026-11-11-glyphpack-design.md section 6): the web editor rendered every glyph; this
 * only blits them. Text glyphs are A8 masks tinted with the widget colour (default: theme foreground); emoji and
 * pictures are RGB565A8 and keep their colours. Index and bitmaps live in PSRAM; internal RAM ~0 (one FILE, unbuffered).
 * LVGL task only.
 *
 *   gt_pack_t *p = gt_pack_open(PC_LISTS_DIR "/lunch.zip");
 *   lv_obj_t *t = glyph_text_create(parent);
 *   lv_obj_set_width(t, 280);                       // lines wrap to the content width
 *   glyph_text_set_node(t, p, 0, 40, 2);            // node 0 (the title) at the 40 px size, at most 2 lines
 *   ...
 *   lv_obj_delete(t); gt_pack_close(p);             // widgets first: they point into the pack
 */
typedef struct gt_pack gt_pack_t;

gt_pack_t *gt_pack_open(const char *path);       /* NULL when missing or not a valid pack (logged with the reason) */
gt_pack_t *gt_pack_open_mem(const char *name, const void *data, uint32_t len);   /* a pack in flash/RAM (built-in examples); data must stay */
void gt_pack_close(gt_pack_t *p);
const pc_gp_t *gt_pack_gp(const gt_pack_t *p);
int gt_pack_open_count(void);                    /* packs open right now (the SD card allows 10 open files); console gtfiles */
bool gt_pack_in_use(const char *path);           /* open right now (an upload must not replace it) */

lv_obj_t *glyph_text_create(lv_obj_t *parent);
/* Lays out ids (copied) at the pack size nearest below px into the widget's content width, centred lines, ellipsis
 * after max_lines; sets the widget height to the lines. Reads the bitmaps now (one PSRAM block per widget).
 * 0, or <0 (bad arguments, out of memory, read error: the widget is then empty). */
int  glyph_text_set(lv_obj_t *obj, gt_pack_t *pack, const uint16_t *ids, uint32_t n, int px, int max_lines);
int  glyph_text_set_node(lv_obj_t *obj, gt_pack_t *pack, uint32_t node, int px, int max_lines);
void glyph_text_set_color(lv_obj_t *obj, lv_color_t c);
void glyph_text_clear(lv_obj_t *obj);

