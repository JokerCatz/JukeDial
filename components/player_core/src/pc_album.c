// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_album.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
static bool ends_with(const char *s, const char *suf)
{
    size_t n = strlen(s), m = strlen(suf);
    if (n <= m) return false;
    for (size_t i = 0; i < m; i++) if (lower((unsigned char)s[n - m + i]) != suf[i]) return false;
    return true;
}
bool pc_album_is_photo(const char *name)
{
    if (!name || name[0] == '.') return false;
    return ends_with(name, ".jpg") || ends_with(name, ".jpeg");
}

uint64_t pc_album_key(const char *path, uint32_t size, int64_t mtime)
{
    uint64_t h = 0xcbf29ce484222325ULL;
    for (const unsigned char *p = (const unsigned char *)path; p && *p; p++) { h ^= *p; h *= 0x100000001b3ULL; }
    uint8_t b[12];
    for (int i = 0; i < 4; i++) b[i] = (uint8_t)(size >> (8 * i));
    for (int i = 0; i < 8; i++) b[4 + i] = (uint8_t)((uint64_t)mtime >> (8 * i));
    for (int i = 0; i < 12; i++) { h ^= b[i]; h *= 0x100000001b3ULL; }
    return h;
}
void pc_album_cache_name(uint64_t key, char out[PC_ALBUM_NAME_MAX])
{
    snprintf(out, PC_ALBUM_NAME_MAX, "%08lx%08lx.565", (unsigned long)(key >> 32), (unsigned long)(key & 0xffffffffUL));
}
bool pc_album_parse_name(const char *name, uint64_t *key)
{
    if (!name || strlen(name) != 20 || strcmp(name + 16, ".565")) return false;
    uint64_t k = 0;
    for (int i = 0; i < 16; i++) {
        int c = name[i], v = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
        if (v < 0) return false;
        k = k << 4 | (uint64_t)v;
    }
    *key = k;
    return true;
}
static int cmp_u64(const void *a, const void *b)
{
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return x < y ? -1 : x > y;
}
void pc_album_sort_keys(uint64_t *keys, int n) { if (n > 1) qsort(keys, (size_t)n, sizeof *keys, cmp_u64); }
bool pc_album_stale(const char *name, const uint64_t *sorted_keys, int n)
{
    uint64_t k;
    if (!pc_album_parse_name(name, &k)) return true;
    return !(n > 0 && bsearch(&k, sorted_keys, (size_t)n, sizeof k, cmp_u64));
}

/* ---- EXIF: APP1 "Exif\0\0", TIFF header, IFD0 tag 0x0112 ---- */
static unsigned rd16(const uint8_t *p, bool be) { return be ? (unsigned)(p[0] << 8 | p[1]) : (unsigned)(p[1] << 8 | p[0]); }
static uint32_t rd32(const uint8_t *p, bool be)
{
    return be ? (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]
              : (uint32_t)p[3] << 24 | (uint32_t)p[2] << 16 | (uint32_t)p[1] << 8 | p[0];
}
static int exif_tiff(const uint8_t *t, size_t n)
{
    if (n < 8) return 1;
    bool be;
    if (t[0] == 'M' && t[1] == 'M') be = true;
    else if (t[0] == 'I' && t[1] == 'I') be = false;
    else return 1;
    if (rd16(t + 2, be) != 42) return 1;
    uint32_t ifd = rd32(t + 4, be);
    if (ifd > n || n - ifd < 2) return 1;
    unsigned cnt = rd16(t + ifd, be);
    for (unsigned i = 0; i < cnt; i++) {
        size_t e = ifd + 2 + (size_t)i * 12;
        if (e + 12 > n) return 1;
        if (rd16(t + e, be) != 0x0112) continue;
        unsigned v = rd16(t + e + 8, be);                  /* SHORT, count 1: in the value field's first half */
        return v >= 1 && v <= 8 ? (int)v : 1;
    }
    return 1;
}
int pc_exif_orientation(const uint8_t *p, size_t n)
{
    if (!p || n < 4 || p[0] != 0xFF || p[1] != 0xD8) return 1;
    size_t i = 2;
    while (i + 4 <= n) {
        if (p[i] != 0xFF) return 1;
        uint8_t m = p[i + 1];
        if (m == 0xD8 || (m >= 0xD0 && m <= 0xD7) || m == 0x01 || m == 0xFF) { i += m == 0xFF ? 1 : 2; continue; }
        if (m == 0xDA || m == 0xD9) return 1;               /* image data: no EXIF before it */
        size_t len = (size_t)p[i + 2] << 8 | p[i + 3];
        if (len < 2) return 1;
        if (m == 0xE1 && len >= 8 && i + 4 + 6 <= n && !memcmp(p + i + 4, "Exif\0\0", 6)) {
            size_t t = i + 10, end = i + 2 + len < n ? i + 2 + len : n;
            return t < end ? exif_tiff(p + t, end - t) : 1;
        }
        i += 2 + len;
    }
    return 1;
}

bool pc_jpeg_size(const uint8_t *p, size_t n, int *w, int *h)
{
    if (!p || n < 4 || p[0] != 0xFF || p[1] != 0xD8) return false;
    size_t i = 2;
    while (i + 9 < n) {
        if (p[i] != 0xFF) return false;
        uint8_t m = p[i + 1];
        if (m == 0xFF) { i++; continue; }
        if (m == 0xDA || m == 0xD9) return false;
        size_t len = (size_t)p[i + 2] << 8 | p[i + 3];
        if (m >= 0xC0 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC) {
            *h = p[i + 5] << 8 | p[i + 6];
            *w = p[i + 7] << 8 | p[i + 8];
            return *w > 0 && *h > 0;
        }
        if (len < 2) return false;
        i += 2 + len;
    }
    return false;
}

void pc_album_oriented_size(int o, int w, int h, int *ow, int *oh)
{
    bool swap = o >= 5 && o <= 8;
    *ow = swap ? h : w;
    *oh = swap ? w : h;
}

void pc_album_decode_size(int w, int h, int out, int *dw, int *dh)
{
    int s = w < h ? w : h, ss = out;
    if (s <= 0) { *dw = *dh = 0; return; }
    if ((long)ss * 8 < s) ss = ((s + 7) / 8 + 7) / 8 * 8;
    int sw = (int)(((long)w * ss / s + 7) / 8 * 8), sh = (int)(((long)h * ss / s + 7) / 8 * 8);
    if (sw > w || sh > h) { sw = w & ~7; sh = h & ~7; }
    *dw = sw;
    *dh = sh;
}

void pc_album_cover(int w, int h, pc_album_rect_t *r)
{
    int c = w < h ? w : h;
    r->x = (w - c) / 2;
    r->y = (h - c) / 2;
    r->w = r->h = c;
}

/* displayed (u, v) -> stored (x, y), all in 16.16 fixed point */
static void orient_map(int o, int32_t u, int32_t v, int32_t wm1, int32_t hm1, int32_t *x, int32_t *y)
{
    switch (o) {
    case 2: *x = wm1 - u; *y = v; break;
    case 3: *x = wm1 - u; *y = hm1 - v; break;
    case 4: *x = u; *y = hm1 - v; break;
    case 5: *x = v; *y = u; break;
    case 6: *x = v; *y = hm1 - u; break;
    case 7: *x = wm1 - v; *y = hm1 - u; break;
    case 8: *x = wm1 - v; *y = u; break;
    default: *x = u; *y = v; break;
    }
}
static uint16_t lerp565(uint16_t a, uint16_t b, uint16_t c, uint16_t d, uint32_t fx, uint32_t fy)
{   /* fx, fy in 0..256 */
    uint32_t w00 = (256 - fx) * (256 - fy), w10 = fx * (256 - fy), w01 = (256 - fx) * fy, w11 = fx * fy;
    uint32_t r = ((a >> 11) * w00 + (b >> 11) * w10 + (c >> 11) * w01 + (d >> 11) * w11 + 32768) >> 16;
    uint32_t g = (((a >> 5) & 63) * w00 + ((b >> 5) & 63) * w10 + ((c >> 5) & 63) * w01 + ((d >> 5) & 63) * w11 + 32768) >> 16;
    uint32_t bl = ((a & 31) * w00 + (b & 31) * w10 + (c & 31) * w01 + (d & 31) * w11 + 32768) >> 16;
    return (uint16_t)(r << 11 | g << 5 | bl);
}
void pc_album_render(const uint16_t *src, int sw, int sh, int orient, uint16_t *dst, int out)
{
    int ow, oh;
    pc_album_oriented_size(orient, sw, sh, &ow, &oh);
    pc_album_rect_t c;
    pc_album_cover(ow, oh, &c);
    int32_t step = (int32_t)(((int64_t)c.w << 16) / out), wm1 = (sw - 1) << 16, hm1 = (sh - 1) << 16;
    for (int oy = 0; oy < out; oy++) {
        int32_t v = (c.y << 16) + (int32_t)(((int64_t)(2 * oy + 1) * step) / 2) - 32768;
        for (int ox = 0; ox < out; ox++) {
            int32_t u = (c.x << 16) + (int32_t)(((int64_t)(2 * ox + 1) * step) / 2) - 32768, x, y;
            orient_map(orient, u, v, wm1, hm1, &x, &y);
            if (x < 0) x = 0;
            if (y < 0) y = 0;
            if (x > wm1) x = wm1;
            if (y > hm1) y = hm1;
            int x0 = x >> 16, y0 = y >> 16, x1 = x0 + 1 < sw ? x0 + 1 : x0, y1 = y0 + 1 < sh ? y0 + 1 : y0;
            uint32_t fx = (uint32_t)(x & 0xffff) >> 8, fy = (uint32_t)(y & 0xffff) >> 8;
            const uint16_t *r0 = src + (size_t)y0 * sw, *r1 = src + (size_t)y1 * sw;
            dst[(size_t)oy * out + ox] = (fx | fy) ? lerp565(r0[x0], r0[x1], r1[x0], r1[x1], fx, fy) : r0[x0];
        }
    }
}

static uint32_t xs(uint32_t *s)
{
    uint32_t x = *s ? *s : 0x9e3779b9u;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return *s = x;
}
static void shuffle(pc_album_seq_t *s)
{
    for (int i = 0; i < s->n; i++) s->order[i] = (uint16_t)i;
    for (int i = s->n - 1; i > 0; i--) {
        int j = (int)(xs(&s->rng) % (uint32_t)(i + 1));
        uint16_t t = s->order[i]; s->order[i] = s->order[j]; s->order[j] = t;
    }
    if (s->n > 1 && s->order[0] == s->last) {               /* no repeat across rounds */
        int j = 1 + (int)(xs(&s->rng) % (uint32_t)(s->n - 1));
        uint16_t t = s->order[0]; s->order[0] = s->order[j]; s->order[j] = t;
    }
}
void pc_album_seq_init(pc_album_seq_t *s, uint16_t *buf, int n, bool sh, uint32_t seed)
{
    s->order = buf;
    s->n = n < 0 ? 0 : n;
    s->pos = 0;
    s->last = -1;
    s->rng = seed;
    s->shuffle = sh;
    if (sh) shuffle(s);
    else for (int i = 0; i < s->n; i++) buf[i] = (uint16_t)i;
}
int pc_album_seq_next(pc_album_seq_t *s)
{
    if (s->n <= 0) return -1;
    if (s->pos >= s->n) {
        s->pos = 0;
        if (s->shuffle) shuffle(s);
    }
    s->last = s->order[s->pos++];
    return s->last;
}
