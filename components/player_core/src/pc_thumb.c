// SPDX-License-Identifier: LGPL-2.1-or-later
#include <stdbool.h>
#include <math.h>
#include "pc/pc_thumb.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint32_t pc_thumb_hash(const char *s)
{
    uint32_t h = 2166136261u;
    while (*s) h = (h ^ (uint8_t)*s++) * 16777619u;
    return h;
}

static uint64_t fnv64(uint64_t h, const void *d, size_t n)
{
    for (size_t i = 0; i < n; i++) h = (h ^ ((const uint8_t *)d)[i]) * 1099511628211ULL;
    return h;
}
static uint64_t fnv64_u32(uint64_t h, uint32_t v)
{
    uint8_t b[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) };
    return fnv64(h, b, 4);
}

uint64_t pc_thumb_key(const char *path, uint32_t off, uint32_t len)
{
    uint64_t h = 14695981039346656037ULL;
    if (path) h = fnv64(h, path, strlen(path));
    return fnv64_u32(fnv64_u32(h, off), len);
}

uint64_t pc_thumb_image_key(const char *path, uint32_t size, int64_t mtime)
{
    uint64_t h = fnv64(14695981039346656037ULL, "\x01img:", 6);   /* a NUL-free prefix no track path starts with */
    if (path) h = fnv64(h, path, strlen(path));
    h = fnv64_u32(fnv64_u32(h, size), (uint32_t)mtime);
    return fnv64_u32(h, (uint32_t)((uint64_t)mtime >> 32));
}

static bool ieq(const char *a, const char *b, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x = (char)(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = (char)(y - 'A' + 'a');
        if (x != y) return false;
    }
    return true;
}
static const char *const EXTS[3] = { "jpg", "jpeg", "png" };
static int ext_of(const char *name, bool png)       /* index into EXTS, -1 */
{
    const char *dot = strrchr(name, '.');
    if (!dot || dot == name) return -1;
    for (int e = 0; e < (png ? 3 : 2); e++)
        if (strlen(dot + 1) == strlen(EXTS[e]) && ieq(dot + 1, EXTS[e], strlen(EXTS[e]))) return e;
    return -1;
}
bool pc_thumb_is_image(const char *name, bool png) { return name && ext_of(name, png) >= 0; }

/* index of "<base>.<EXTS[e]>" in names, -1 */
static int find_base(const char *base, size_t blen, int e, const char *const *names, int n)
{
    for (int i = 0; i < n; i++) {
        const char *dot = strrchr(names[i], '.');
        if (dot && (size_t)(dot - names[i]) == blen && ieq(names[i], base, blen) && strlen(dot + 1) == strlen(EXTS[e]) && ieq(dot + 1, EXTS[e], strlen(EXTS[e])))
            return i;
    }
    return -1;
}

int pc_thumb_pick_image(const char *track, const char *const *names, int n, bool png, bool *shared)
{
    int ne = png ? 3 : 2, k;
    const char *dot = track ? strrchr(track, '.') : NULL;
    size_t blen = dot && dot != track ? (size_t)(dot - track) : (track ? strlen(track) : 0);
    *shared = false;
    if (blen)
        for (int e = 0; e < ne; e++) if ((k = find_base(track, blen, e, names, n)) >= 0) return k;
    *shared = true;
    static const char *const DIRS[2] = { "cover", "folder" };
    for (int d = 0; d < 2; d++)
        for (int e = 0; e < ne; e++) if ((k = find_base(DIRS[d], strlen(DIRS[d]), e, names, n)) >= 0) return k;
    *shared = false;
    return -1;
}

bool pc_thumb_hires_pause(uint32_t src_rate) { return src_rate > PC_THUMB_PAUSE_ABOVE; }

uint64_t pc_thumb_content_key(const uint8_t *b, uint32_t len)
{
    uint64_t h = fnv64(14695981039346656037ULL, "\x02apic:", 6);
    if (b && len) h = fnv64(h, b, len);
    h = fnv64_u32(h, len);
    return h ? h : 1;
}

uint64_t pc_thumb_space_margin(uint64_t total)
{
    uint64_t pct = total / 20, floor = 64ull << 20;
    return pct > floor ? pct : floor;
}

bool pc_thumb_space_ok(uint64_t total, uint64_t free_bytes, uint64_t need)
{
    uint64_t m = pc_thumb_space_margin(total);
    return free_bytes >= m && free_bytes - m >= need;
}

bool pc_thumb_bg_allowed(bool playing, bool paused, uint32_t paused_ms)
{
    return !playing && (!paused || paused_ms >= PC_THUMB_IDLE_PAUSE_MS);
}

bool pc_thumb_paused_like(int st)
{
    return st == PC_THUMB_AUD_PAUSED || st == PC_THUMB_AUD_ENDED || st == PC_THUMB_AUD_FADED || st == PC_THUMB_AUD_ERROR;
}

bool pc_thumb_opening(uint32_t live, uint32_t pub) { return live != 0 && live != pub; }

const char *pc_thumb_write_gate(bool hires, bool opening, bool background, bool idle, bool space_known)
{
    return hires ? "hires" : opening ? "opening" : background && !idle ? "playing" : !space_known ? "space_unknown" : NULL;
}

void pc_thumb_crop_scale_rgb888(const uint8_t *src, int w, int h, uint16_t *dst, int n)
{
    int side = w < h ? w : h;
    int x0 = (w - side) / 2, y0 = (h - side) / 2;
    for (int y = 0; y < n; y++) {
        const uint8_t *row = src + ((size_t)(y0 + (int)((int64_t)y * side / n)) * w + x0) * 3;
        for (int x = 0; x < n; x++) {
            const uint8_t *p = row + (size_t)((int64_t)x * side / n) * 3;
            dst[y * n + x] = (uint16_t)((p[0] >> 3) << 11 | (p[1] >> 2) << 5 | p[2] >> 3);
        }
    }
}

void pc_thumb_crop_scale(const uint16_t *src, int w, int h, uint16_t *dst, int n)
{
    int side = w < h ? w : h;
    int x0 = (w - side) / 2, y0 = (h - side) / 2;
    for (int y = 0; y < n; y++) {
        const uint16_t *row = src + (size_t)(y0 + (int)((int64_t)y * side / n)) * w + x0;
        for (int x = 0; x < n; x++) dst[y * n + x] = row[(int64_t)x * side / n];
    }
}


/* RGB565 spread into 0x07E0F81F so one multiply blends all three channels at 5-bit weights */
static inline uint32_t spread(uint16_t c) { return (c | (uint32_t)c << 16) & 0x07E0F81Fu; }
static inline uint16_t pack565(uint32_t v) { v &= 0x07E0F81Fu; return (uint16_t)(v | v >> 16); }
static inline uint32_t lerp5(uint32_t a, uint32_t b, uint32_t w) { return (a * (32 - w) + b * w) >> 5; }   /* w 0..32 */

void pc_thumb_rotate_smooth(const uint16_t *src, uint16_t *dst, int size, int angle_deci, uint16_t bg)
{
    float a = (float)angle_deci * 3.14159265f / 1800.0f, c = size / 2.0f, r = c, ca = cosf(a), sa = sinf(a);
    int32_t cs = (int32_t)lrintf(ca * 65536.0f), sn = (int32_t)lrintf(sa * 65536.0f);
    uint32_t bgs = spread(bg);
    int last = size - 1;
    for (int y = 0; y < size; y++) {
        uint16_t *o = dst + y * size;
        float dy = y + 0.5f - c, h2 = r * r - dy * dy;
        int xa = size, xb = size;                        /* pixels touched by the disc, rim included */
        if (h2 > -2 * r) {
            float h = h2 > 0 ? sqrtf(h2) : 0;
            xa = (int)floorf(c - h - 1.0f);
            xb = (int)ceilf(c + h + 1.0f);
            if (xa < 0) xa = 0;
            if (xb > size) xb = size;
        }
        if (xa > xb) xa = xb;
        for (int x = 0; x < xa; x++) o[x] = bg;
        for (int x = xb; x < size; x++) o[x] = bg;
        if (xa >= xb) continue;
        /* sample point (pixel centres): u = c + dx cos + dy sin - 0.5, v = c - dx sin + dy cos - 0.5 */
        float dx0 = xa + 0.5f - c;
        int32_t u = (int32_t)lrintf((c + dx0 * ca + dy * sa - 0.5f) * 65536.0f);
        int32_t v = (int32_t)lrintf((c - dx0 * sa + dy * ca - 0.5f) * 65536.0f);
        for (int x = xa; x < xb; x++, u += cs, v -= sn) {
            int ix = u >> 16, iy = v >> 16;
            uint32_t wx = (uint32_t)(u & 0xFFFF) >> 11, wy = (uint32_t)(v & 0xFFFF) >> 11;   /* 0..31 */
            int x0 = ix < 0 ? 0 : ix > last ? last : ix, x1 = ix + 1 < 0 ? 0 : ix + 1 > last ? last : ix + 1;
            int y0 = iy < 0 ? 0 : iy > last ? last : iy, y1 = iy + 1 < 0 ? 0 : iy + 1 > last ? last : iy + 1;
            uint32_t top = lerp5(spread(src[y0 * size + x0]), spread(src[y0 * size + x1]), wx);
            uint32_t bot = lerp5(spread(src[y1 * size + x0]), spread(src[y1 * size + x1]), wx);
            uint32_t px = lerp5(top & 0x07E0F81Fu, bot & 0x07E0F81Fu, wy);
            float dx = x + 0.5f - c, d2 = dx * dx + dy * dy;
            if (d2 > (r - 1) * (r - 1)) {                  /* the rim: coverage of this pixel by the disc */
                float cov = r - sqrtf(d2) + 0.5f;
                uint32_t w = cov <= 0 ? 0 : cov >= 1 ? 32 : (uint32_t)(cov * 32.0f);
                px = lerp5(bgs, px & 0x07E0F81Fu, w);
            }
            o[x] = pack565(px);
        }
    }
}

void pc_thumb_default(uint16_t *dst, int size, uint16_t disc, uint16_t groove, uint16_t sheen, uint16_t label, uint16_t hole)
{
    float c = size / 2.0f, rl = size / 6.0f, rh = size / 40.0f;
    for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++) {
            float dx = x + 0.5f - c, dy = y + 0.5f - c, r = sqrtf(dx * dx + dy * dy);
            uint16_t v;
            if (r < rh) v = hole;
            else if (r < rl) v = label;
            else {
                float a = atan2f(dy, dx);                        /* sheen: two opposite 20-degree wedges */
                bool lit = fabsf(a) < 0.17f || fabsf(a) > 3.14159265f - 0.17f;
                v = lit ? sheen : ((int)(r / 3.0f) % 2 ? groove : disc);
            }
            dst[y * size + x] = v;
        }
}

void pc_thumb_rotate(const uint16_t *src, uint16_t *dst, int size, int angle_deci, uint16_t bg)
{
    /* nearest neighbour in 16.16 fixed point over each row's span inside the disc; only the rim (a pixel or two
     * a row) is blended into bg, so the edge is soft for almost nothing (~6 ms a step on the board) */
    float a = (float)angle_deci * 3.14159265f / 1800.0f, c = size / 2.0f, r = c, ca = cosf(a), sa = sinf(a);
    int32_t cs = (int32_t)lrintf(ca * 65536.0f), sn = (int32_t)lrintf(sa * 65536.0f);
    uint32_t bgs = spread(bg);
    for (int y = 0; y < size; y++) {
        uint16_t *o = dst + y * size;
        float dy = y + 0.5f - c, h2 = r * r - dy * dy;
        int xa = size, xb = size;                        /* pixels touched by the disc, rim included */
        if (h2 > -2 * r) {
            float h = h2 > 0 ? sqrtf(h2) : 0;
            xa = (int)floorf(c - h - 1.0f);
            xb = (int)ceilf(c + h + 1.0f);
            if (xa < 0) xa = 0;
            if (xb > size) xb = size;
        }
        if (xa > xb) xa = xb;
        for (int x = 0; x < xa; x++) o[x] = bg;
        for (int x = xb; x < size; x++) o[x] = bg;
        if (xa >= xb) continue;
        /* inverse map: dst (dx, dy) comes from src (c + dx cos + dy sin, c - dx sin + dy cos) */
        float dx0 = xa + 0.5f - c;
        int32_t sx = (int32_t)lrintf((c + dx0 * ca + dy * sa) * 65536.0f);
        int32_t sy = (int32_t)lrintf((c - dx0 * sa + dy * ca) * 65536.0f);
        float in2 = (r - 1) * (r - 1), hi2 = in2 - dy * dy;
        int xi0 = xb, xi1 = xb;                          /* fully inside: no rim maths there */
        if (hi2 > 0) { float hi = sqrtf(hi2); xi0 = (int)ceilf(c - hi - 0.5f) + 1; xi1 = (int)floorf(c + hi - 0.5f) - 1; }
        for (int x = xa; x < xb; x++, sx += cs, sy -= sn) {
            int ix = sx >> 16, iy = sy >> 16;             /* arithmetic shift = floor */
            uint16_t v = (unsigned)ix < (unsigned)size && (unsigned)iy < (unsigned)size ? src[iy * size + ix] : bg;
            if (x >= xi0 && x < xi1) { o[x] = v; continue; }
            float dx = x + 0.5f - c, d2 = dx * dx + dy * dy;
            if (d2 > in2) {                                /* the rim: coverage of this pixel by the disc */
                float cov = r - sqrtf(d2) + 0.5f;
                uint32_t w = cov <= 0 ? 0 : cov >= 1 ? 32 : (uint32_t)(cov * 32.0f);
                v = pack565(lerp5(bgs, spread(v), w));
            }
            o[x] = v;
        }
    }
}

/* the same rotation straight into a bigger picture: src is ssize x ssize, dst dsize x dsize (the disc fills dst), each dst pixel samples src at ssize/dsize of its distance from the
 * centre. One pass and no second scaling (the square panel's 1.5x cover); smooth = bilinear. */
void pc_thumb_rotate_scaled(const uint16_t *src, int ssize, uint16_t *dst, int dsize, int angle_deci, uint16_t bg, bool smooth)
{
    float a = (float)angle_deci * 3.14159265f / 1800.0f, k = (float)ssize / (float)dsize, c = dsize / 2.0f, cs_ = ssize / 2.0f, r = c, ca = cosf(a) * k, sa = sinf(a) * k;
    int32_t cs = (int32_t)lrintf(ca * 65536.0f), sn = (int32_t)lrintf(sa * 65536.0f);
    uint32_t bgs = spread(bg);
    const int last = ssize - 1;
    const float off = smooth ? 0.5f : 0.0f;
    for (int y = 0; y < dsize; y++) {
        uint16_t *o = dst + y * dsize;
        float dy = y + 0.5f - c, h2 = r * r - dy * dy;
        int xa = dsize, xb = dsize;
        if (h2 > -2 * r) {
            float h = h2 > 0 ? sqrtf(h2) : 0;
            xa = (int)floorf(c - h - 1.0f);
            xb = (int)ceilf(c + h + 1.0f);
            if (xa < 0) xa = 0;
            if (xb > dsize) xb = dsize;
        }
        if (xa > xb) xa = xb;
        for (int x = 0; x < xa; x++) o[x] = bg;
        for (int x = xb; x < dsize; x++) o[x] = bg;
        if (xa >= xb) continue;
        float dx0 = xa + 0.5f - c;
        int32_t u = (int32_t)lrintf((cs_ + dx0 * ca + dy * sa - off) * 65536.0f);
        int32_t v = (int32_t)lrintf((cs_ - dx0 * sa + dy * ca - off) * 65536.0f);
        for (int x = xa; x < xb; x++, u += cs, v -= sn) {
            int ix = u >> 16, iy = v >> 16;
            uint32_t px;
            if (smooth) {
                uint32_t wx = (uint32_t)(u & 0xFFFF) >> 11, wy = (uint32_t)(v & 0xFFFF) >> 11;
                int x0 = ix < 0 ? 0 : ix > last ? last : ix, x1 = ix + 1 < 0 ? 0 : ix + 1 > last ? last : ix + 1;
                int y0 = iy < 0 ? 0 : iy > last ? last : iy, y1 = iy + 1 < 0 ? 0 : iy + 1 > last ? last : iy + 1;
                uint32_t top = lerp5(spread(src[y0 * ssize + x0]), spread(src[y0 * ssize + x1]), wx);
                uint32_t bot = lerp5(spread(src[y1 * ssize + x0]), spread(src[y1 * ssize + x1]), wx);
                px = lerp5(top & 0x07E0F81Fu, bot & 0x07E0F81Fu, wy);
            } else {
                uint16_t sv = (unsigned)ix < (unsigned)ssize && (unsigned)iy < (unsigned)ssize ? src[iy * ssize + ix] : bg;
                px = spread(sv);
            }
            float dx = x + 0.5f - c, d2 = dx * dx + dy * dy;
            if (d2 > (r - 1) * (r - 1)) {
                float cov = r - sqrtf(d2) + 0.5f;
                uint32_t w = cov <= 0 ? 0 : cov >= 1 ? 32 : (uint32_t)(cov * 32.0f);
                px = lerp5(bgs, px & 0x07E0F81Fu, w);
            }
            o[x] = pack565(px);
        }
    }
}
