// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "pc/pc_io.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Glyph packs (spec an internal note): one zip per list. The web editor renders
 * every glyph (grapheme cluster) once per size; the device only blits. Texts are glyph-ID sequences; the device
 * never interprets UTF-8. Zip entries: pack.bin (index), g0.bin..g3.bin (glyph bitmaps per size), img.bin (whole
 * images), source.json (editor only). The whole index lives in ONE block from the allocator hook (PSRAM on the
 * device); bitmaps are read lazily through the reader. Little-endian hosts only (host tests, ESP32). */

#define PC_GP_VERSION      1
#define PC_GP_MAX_SIZES    4
#define PC_GP_MAX_GLYPHS   0xFFF0u
#define PC_GP_MAX_IMAGES   1024u
#define PC_GP_MAX_NODES    65535u
#define PC_GP_MAX_SEQ      (1u << 20)       /* glyph IDs in all texts together */
#define PC_GP_MAX_INDEX    (4u << 20)       /* pack.bin */
#define PC_GP_MAX_INFLATE  (2u << 20)       /* deflated bitmap blobs are inflated into the block at open */
#define PC_GP_MAX_IMAGE_PX 360u
#define PC_GP_NL           0xFFFEu          /* forced line break inside a text */
#define PC_GP_NONE         0xFFFFu          /* no image / no ellipsis glyph */

enum { PC_GP_KIND_PLAIN = 0, PC_GP_KIND_LUNCH = 1, PC_GP_KIND_PEOPLE = 2, PC_GP_KIND_COLUMNS = 3, PC_GP_KIND_VOCAB = 4 };   /* 4: pc_vocab.h */
enum { PC_GP_FMT_EMPTY = 0, PC_GP_FMT_A8 = 1, PC_GP_FMT_RGB565A8 = 2 };   /* RGB565A8: w*h RGB565 LE, then w*h alpha */
enum { PC_GP_GF_SPACE = 1, PC_GP_GF_WORD = 2 };   /* SPACE: break here, dropped at line ends; WORD: no break between two */

typedef struct { void *(*alloc)(size_t n); void (*free)(void *p); } pc_gp_alloc_t;   /* NULL: malloc/free */
typedef struct pc_gp pc_gp_t;

typedef struct {
    const uint16_t *text;      /* glyph IDs (or PC_GP_NL), inside the pack's block */
    uint16_t len;
    uint16_t image;            /* image index or PC_GP_NONE */
    uint32_t first_child;      /* children are nodes first_child .. first_child+nchild-1 (always after this node) */
    uint16_t nchild;
    uint8_t weight;            /* 0 = left out of picks; default 1 */
    uint8_t flags;             /* app-defined, 0 by default */
} pc_gp_node_t;

typedef struct { uint8_t fmt, w, h, adv, flags; int8_t x, y; uint32_t bytes; } pc_gp_glyph_t;   /* x,y: from pen / line top */
typedef struct { uint8_t fmt; uint16_t w, h; uint32_t bytes; } pc_gp_image_t;
typedef struct { uint16_t px, line_h; } pc_gp_size_t;
typedef struct { uint32_t start, len; int32_t width; uint8_t ellipsis; } pc_gp_line_t;   /* width includes the ellipsis */

/* r must stay valid (and its file open) until pc_gp_close. Errors: -EINVAL not a pack / corrupt, -EPROTONOSUPPORT
 * other version, -E2BIG over a limit, -ENOMEM, -EIO. */
int  pc_gp_open(const pc_reader_t *r, const pc_gp_alloc_t *a, pc_gp_t **out);
void pc_gp_close(pc_gp_t *gp);
int  pc_gp_kind(const pc_gp_t *gp);
int  pc_gp_nsizes(const pc_gp_t *gp);
int  pc_gp_size(const pc_gp_t *gp, int size, pc_gp_size_t *s);   /* -ERANGE */
int  pc_gp_size_for_px(const pc_gp_t *gp, int px);               /* the largest size not above px, else the smallest */
uint32_t pc_gp_node_count(const pc_gp_t *gp);
uint32_t pc_gp_glyph_count(const pc_gp_t *gp);
uint32_t pc_gp_image_count(const pc_gp_t *gp);
uint16_t pc_gp_ellipsis(const pc_gp_t *gp);                      /* glyph ID of "…" or PC_GP_NONE */
uint32_t pc_gp_block_bytes(const pc_gp_t *gp);                   /* size of the one allocation */
int  pc_gp_node(const pc_gp_t *gp, uint32_t idx, pc_gp_node_t *n); /* node 0 = the list title; -ERANGE */
int  pc_gp_glyph(const pc_gp_t *gp, int size, uint32_t gid, pc_gp_glyph_t *g);   /* -ERANGE */
int  pc_gp_glyph_read(const pc_gp_t *gp, int size, uint32_t gid, void *dst, uint32_t cap);   /* bytes, -ENOSPC, -EIO */
int  pc_gp_image(const pc_gp_t *gp, uint32_t idx, pc_gp_image_t *im);
int  pc_gp_image_read(const pc_gp_t *gp, uint32_t idx, void *dst, uint32_t cap);
int32_t pc_gp_text_width(const pc_gp_t *gp, int size, const uint16_t *ids, uint32_t n);
/* Wraps ids into lines of at most max_w px (CJK anywhere; not inside WORD runs when a better break exists; PC_GP_NL
 * forces one), max_lines at most; the last line gets an ellipsis when text is left over. Unknown IDs have zero
 * width. Returns the number of lines (0 for an empty text), -EINVAL on bad arguments. */
int  pc_gp_layout(const pc_gp_t *gp, int size, const uint16_t *ids, uint32_t n, int32_t max_w, int max_lines, pc_gp_line_t *lines);

/* the largest glyph bitmap of a size (scratch for pc_gp_draw565); 0 for a bad size */
uint32_t pc_gp_max_glyph_bytes(const pc_gp_t *gp, int size);
#define PC_GP_DRAW_MAX_LINES 8
/* Software blit into an RGB565 buffer (w x h pixels, stride pixels a row, the background already in it): laid out
 * like pc_gp_layout into w (at most max_lines, max PC_GP_DRAW_MAX_LINES), each line centred, the block centred
 * vertically; A8 glyphs blended in fg, RGB565A8 glyphs with their own colours and alpha; clipped to w x h.
 * scratch holds one glyph bitmap at a time (pc_gp_max_glyph_bytes). Returns the lines, -EINVAL, -ENOSPC, -EIO. */
int  pc_gp_draw565(const pc_gp_t *gp, int size, const uint16_t *ids, uint32_t n, int max_lines, uint16_t fg,
                   uint16_t *dst, int stride, int w, int h, void *scratch, uint32_t cap);

#ifdef __cplusplus
}
#endif
