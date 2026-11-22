// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Photo album (spec 2026-11-11-album-design.md): cache keys and names, EXIF orientation, the decoder's output
 * size, the centred square crop with orientation and scaling, slideshow order, stale cache files. */
#define PC_ALBUM_NAME_MAX 24                       /* "0123456789abcdef.565" + NUL */
bool pc_album_is_photo(const char *name);          /* .jpg / .jpeg, any case; not a dot file */
uint64_t pc_album_key(const char *path, uint32_t size, int64_t mtime);   /* FNV-1a 64 */
void pc_album_cache_name(uint64_t key, char out[PC_ALBUM_NAME_MAX]);
bool pc_album_parse_name(const char *name, uint64_t *key);
void pc_album_sort_keys(uint64_t *keys, int n);
bool pc_album_stale(const char *name, const uint64_t *sorted_keys, int n);   /* not a cache name, or no photo has it */

bool pc_jpeg_size(const uint8_t *jpg, size_t n, int *w, int *h);   /* from the first SOFn marker */
int pc_exif_orientation(const uint8_t *jpg, size_t n);   /* 1..8; 1 without (valid) EXIF */
void pc_album_oriented_size(int orient, int w, int h, int *ow, int *oh);   /* 5..8 swap width and height */
/* esp_new_jpeg scaled output: short side >= out (the decoder scales down at most 8x), multiples of 8, never
 * larger than the photo */
void pc_album_decode_size(int w, int h, int out, int *dw, int *dh);
typedef struct { int x, y, w, h; } pc_album_rect_t;
void pc_album_cover(int w, int h, pc_album_rect_t *r);   /* the centred square of a w x h picture */
/* src (sw x sh RGB565 as stored) -> dst (out x out): orientation applied, centred square, bilinear */
void pc_album_render(const uint16_t *src, int sw, int sh, int orient, uint16_t *dst, int out);

/* slideshow order: in order, or shuffled rounds (every photo once a round, never the same twice in a row) */
typedef struct { uint16_t *order; int n, pos, last; uint32_t rng; bool shuffle; } pc_album_seq_t;
void pc_album_seq_init(pc_album_seq_t *s, uint16_t *buf, int n, bool shuffle, uint32_t seed);   /* buf holds n */
int pc_album_seq_next(pc_album_seq_t *s);           /* -1 when empty */
#ifdef __cplusplus
}
#endif
