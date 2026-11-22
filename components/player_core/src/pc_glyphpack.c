// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_glyphpack.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "pc/pc_zip.h"
#include "pc_inflate.h"

/* pack.bin, little-endian (spec 2026-11-11-glyphpack-design.md section 3) */
enum { HDR = 64, GREC = 12, IREC = 12, NREC = 16, HDR_MAX = 1024 };
enum { B_PACK, B_G0, B_IMG = B_G0 + PC_GP_MAX_SIZES, NB };
static const char *const NAMES[NB] = { "pack.bin", "g0.bin", "g1.bin", "g2.bin", "g3.bin", "img.bin" };

typedef struct { uint32_t off, size; const uint8_t *mem; uint8_t present; } blob_t;   /* mem: inflated copy, else lazy */

struct pc_gp {
    void (*free)(void *);
    const pc_reader_t *r;
    uint32_t block_bytes;
    uint8_t kind, nsizes;
    uint16_t ellipsis;
    uint32_t ng, ni, nn, nseq;
    pc_gp_size_t sz[PC_GP_MAX_SIZES];
    const uint8_t *g, *im, *nd;       /* tables inside the block */
    const uint16_t *seq;
    blob_t blob[NB];
};

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint32_t al8(uint64_t n) { return (uint32_t)((n + 7) & ~(uint64_t)7); }
static uint32_t bpp(uint8_t fmt) { return fmt == PC_GP_FMT_A8 ? 1 : fmt == PC_GP_FMT_RGB565A8 ? 3 : 0; }

static void *def_alloc(size_t n) { return malloc(n); }
static void def_free(void *p) { free(p); }

/* one zip entry into dst (usize bytes): stored is read as is, deflate through a state buffer from the hook */
static int load_entry(const pc_reader_t *r, const pc_zip_entry_t *e, uint8_t *dst, const pc_gp_alloc_t *a, void **state)
{
    uint32_t off;
    if (pc_zip_data_offset(r, e, &off)) return -EINVAL;
    if (e->method == 0) {
        if ((uint32_t)r->read(r->u, off, dst, e->usize) != e->usize) return -EIO;
    } else {
        if (!*state && !(*state = a->alloc(pc_inflate_state_size()))) return -ENOMEM;
        uint8_t *src = a->alloc(e->csize ? e->csize : 1);
        if (!src) return -ENOMEM;
        int rc = (uint32_t)r->read(r->u, off, src, e->csize) != e->csize ? -EIO
               : pc_inflate_raw_state(src, e->csize, dst, e->usize, *state) != (long)e->usize ? -EINVAL : 0;
        a->free(src);
        if (rc) return rc;
    }
    return pc_crc32(0, dst, e->usize) == e->crc ? 0 : -EINVAL;
}

static int parse(pc_gp_t *gp, const uint8_t *p, uint32_t n)
{
    if (n < HDR || memcmp(p, "JGPK", 4)) return -EINVAL;
    if (le16(p + 4) != PC_GP_VERSION) return -EPROTONOSUPPORT;
    uint32_t hs = le16(p + 6);
    if (hs < HDR || hs > HDR_MAX || hs % 4) return -EINVAL;
    gp->kind = p[8];
    gp->nsizes = p[9];
    gp->ellipsis = le16(p + 10);
    gp->ng = le32(p + 12);
    gp->ni = le32(p + 16);
    gp->nn = le32(p + 20);
    gp->nseq = le32(p + 24);
    if (gp->kind > PC_GP_KIND_VOCAB || gp->nsizes < 1 || gp->nsizes > PC_GP_MAX_SIZES) return -EINVAL;
    if (gp->ng > PC_GP_MAX_GLYPHS || gp->ni > PC_GP_MAX_IMAGES || gp->nn > PC_GP_MAX_NODES || gp->nseq > PC_GP_MAX_SEQ) return -E2BIG;
    if (!gp->nn || (gp->ellipsis != PC_GP_NONE && gp->ellipsis >= gp->ng)) return -EINVAL;
    uint64_t og = hs, oi = og + (uint64_t)GREC * gp->nsizes * gp->ng, on = oi + (uint64_t)IREC * gp->ni,
             os = on + (uint64_t)NREC * gp->nn, end = os + 2ull * gp->nseq;
    if (end != n) return -EINVAL;
    for (int s = 0; s < gp->nsizes; s++) {
        gp->sz[s].px = le16(p + 32 + 8 * s);
        gp->sz[s].line_h = le16(p + 34 + 8 * s);
        if (!gp->sz[s].px || !gp->sz[s].line_h) return -EINVAL;
    }
    gp->g = p + og;
    gp->im = p + oi;
    gp->nd = p + on;
    gp->seq = (const uint16_t *)(const void *)(p + os);   /* os is 4-aligned and the block 8-aligned */
    for (int s = 0; s < gp->nsizes; s++) {
        const blob_t *b = &gp->blob[B_G0 + s];
        for (uint32_t i = 0; i < gp->ng; i++) {
            const uint8_t *q = gp->g + ((size_t)s * gp->ng + i) * GREC;
            uint8_t fmt = q[9];
            if (fmt > PC_GP_FMT_RGB565A8) return -EINVAL;
            uint64_t bytes = (uint64_t)q[4] * q[5] * bpp(fmt);
            if (bytes && (!b->present || le32(q) + bytes > b->size)) return -EINVAL;
        }
    }
    const blob_t *ib = &gp->blob[B_IMG];
    for (uint32_t i = 0; i < gp->ni; i++) {
        const uint8_t *q = gp->im + (size_t)i * IREC;
        uint32_t w = le16(q + 4), h = le16(q + 6);
        uint8_t fmt = q[8];
        if (!w || !h || w > PC_GP_MAX_IMAGE_PX || h > PC_GP_MAX_IMAGE_PX || !bpp(fmt)) return -EINVAL;
        if (!ib->present || le32(q) + (uint64_t)w * h * bpp(fmt) > ib->size) return -EINVAL;
    }
    for (uint32_t i = 0; i < gp->nn; i++) {
        const uint8_t *q = gp->nd + (size_t)i * NREC;
        uint32_t off = le32(q), len = le16(q + 4), img = le16(q + 6), first = le32(q + 8), nc = le16(q + 12);
        if ((uint64_t)off + len > gp->nseq) return -EINVAL;
        if (img != PC_GP_NONE && img >= gp->ni) return -EINVAL;
        if (nc && (first <= i || (uint64_t)first + nc > gp->nn)) return -EINVAL;   /* children after: no cycles */
    }
    for (uint32_t i = 0; i < gp->nseq; i++)
        if (gp->seq[i] >= gp->ng && gp->seq[i] != PC_GP_NL) return -EINVAL;
    return 0;
}

int pc_gp_open(const pc_reader_t *r, const pc_gp_alloc_t *a, pc_gp_t **out)
{
    static const pc_gp_alloc_t def = { def_alloc, def_free };
    *out = NULL;
    if (!a || !a->alloc || !a->free) a = &def;
    pc_zip_entry_t e[NB];
    int rc = pc_zip_lookup(r, NAMES, NB, e);
    if (rc < 0) return rc;
    const pc_zip_entry_t *pe = &e[B_PACK];
    if (!pe->name) return -EINVAL;
    if (pe->usize > PC_GP_MAX_INDEX) return -E2BIG;
    uint64_t inflated = 0, total = al8(sizeof(pc_gp_t)) + (uint64_t)al8(pe->usize);
    for (int i = 0; i < NB; i++) {
        if (!e[i].name) continue;
        if (e[i].method != 0 && e[i].method != 8) return -EINVAL;
        if (e[i].method == 0 && e[i].csize != e[i].usize) return -EINVAL;
        if (i != B_PACK && e[i].method == 8) { inflated += e[i].usize; total += al8(e[i].usize); }
    }
    if (inflated > PC_GP_MAX_INFLATE) return -E2BIG;
    uint8_t *blk = a->alloc((size_t)total);
    if (!blk) return -ENOMEM;
    memset(blk, 0, sizeof(pc_gp_t));
    pc_gp_t *gp = (pc_gp_t *)(void *)blk;
    gp->free = a->free;
    gp->r = r;
    gp->block_bytes = (uint32_t)total;
    uint8_t *pack = blk + al8(sizeof(pc_gp_t)), *next = pack + al8(pe->usize);
    void *state = NULL;
    rc = load_entry(r, pe, pack, a, &state);
    for (int i = 1; !rc && i < NB; i++) {
        if (!e[i].name) continue;
        blob_t *b = &gp->blob[i];
        b->present = 1;
        b->size = e[i].usize;
        if (e[i].method == 0) rc = pc_zip_data_offset(r, &e[i], &b->off) ? -EINVAL : 0;
        else { rc = load_entry(r, &e[i], next, a, &state); b->mem = next; next += al8(e[i].usize); }
    }
    if (state) a->free(state);
    if (!rc) rc = parse(gp, pack, pe->usize);
    if (rc) { a->free(blk); return rc; }
    *out = gp;
    return 0;
}

void pc_gp_close(pc_gp_t *gp) { if (gp) gp->free(gp); }
int pc_gp_kind(const pc_gp_t *gp) { return gp->kind; }
int pc_gp_nsizes(const pc_gp_t *gp) { return gp->nsizes; }
uint32_t pc_gp_node_count(const pc_gp_t *gp) { return gp->nn; }
uint32_t pc_gp_glyph_count(const pc_gp_t *gp) { return gp->ng; }
uint32_t pc_gp_image_count(const pc_gp_t *gp) { return gp->ni; }
uint16_t pc_gp_ellipsis(const pc_gp_t *gp) { return gp->ellipsis; }
uint32_t pc_gp_block_bytes(const pc_gp_t *gp) { return gp->block_bytes; }

int pc_gp_size(const pc_gp_t *gp, int size, pc_gp_size_t *s)
{
    if (size < 0 || size >= gp->nsizes) return -ERANGE;
    *s = gp->sz[size];
    return 0;
}

int pc_gp_size_for_px(const pc_gp_t *gp, int px)
{
    int best = -1, small = 0;
    for (int s = 0; s < gp->nsizes; s++) {
        if (gp->sz[s].px < gp->sz[small].px) small = s;
        if (gp->sz[s].px <= px && (best < 0 || gp->sz[s].px > gp->sz[best].px)) best = s;
    }
    return best < 0 ? small : best;
}

int pc_gp_node(const pc_gp_t *gp, uint32_t idx, pc_gp_node_t *n)
{
    if (idx >= gp->nn) return -ERANGE;
    const uint8_t *q = gp->nd + (size_t)idx * NREC;
    n->text = gp->seq + le32(q);
    n->len = le16(q + 4);
    n->image = le16(q + 6);
    n->nchild = le16(q + 12);
    n->first_child = n->nchild ? le32(q + 8) : 0;
    n->weight = q[14];
    n->flags = q[15];
    return 0;
}

static const uint8_t *grec(const pc_gp_t *gp, int size, uint32_t gid)
{
    if (size < 0 || size >= gp->nsizes || gid >= gp->ng) return NULL;
    return gp->g + ((size_t)size * gp->ng + gid) * GREC;
}

int pc_gp_glyph(const pc_gp_t *gp, int size, uint32_t gid, pc_gp_glyph_t *g)
{
    const uint8_t *q = grec(gp, size, gid);
    if (!q) return -ERANGE;
    g->w = q[4];
    g->h = q[5];
    g->x = (int8_t)q[6];
    g->y = (int8_t)q[7];
    g->adv = q[8];
    g->fmt = q[9];
    g->flags = q[10];
    g->bytes = (uint32_t)g->w * g->h * bpp(g->fmt);
    return 0;
}

static int blob_read(const pc_gp_t *gp, int b, uint32_t off, void *dst, uint32_t n)
{
    const blob_t *bl = &gp->blob[b];
    if (!n) return 0;
    if (bl->mem) { memcpy(dst, bl->mem + off, n); return (int)n; }   /* offsets were checked at open */
    if ((uint64_t)bl->off + off + n > gp->r->size) return -EIO;
    return (uint32_t)gp->r->read(gp->r->u, bl->off + off, dst, n) == n ? (int)n : -EIO;
}

int pc_gp_glyph_read(const pc_gp_t *gp, int size, uint32_t gid, void *dst, uint32_t cap)
{
    pc_gp_glyph_t g;
    if (pc_gp_glyph(gp, size, gid, &g)) return -ERANGE;
    if (g.bytes > cap) return -ENOSPC;
    return blob_read(gp, B_G0 + size, le32(grec(gp, size, gid)), dst, g.bytes);
}

int pc_gp_image(const pc_gp_t *gp, uint32_t idx, pc_gp_image_t *im)
{
    if (idx >= gp->ni) return -ERANGE;
    const uint8_t *q = gp->im + (size_t)idx * IREC;
    im->w = le16(q + 4);
    im->h = le16(q + 6);
    im->fmt = q[8];
    im->bytes = (uint32_t)im->w * im->h * bpp(im->fmt);
    return 0;
}

int pc_gp_image_read(const pc_gp_t *gp, uint32_t idx, void *dst, uint32_t cap)
{
    pc_gp_image_t im;
    if (pc_gp_image(gp, idx, &im)) return -ERANGE;
    if (im.bytes > cap) return -ENOSPC;
    return blob_read(gp, B_IMG, le32(gp->im + (size_t)idx * IREC), dst, im.bytes);
}

/* ---------- layout ---------- */
static int32_t adv(const pc_gp_t *gp, int size, uint16_t id) { const uint8_t *q = grec(gp, size, id); return q ? q[8] : 0; }
static uint8_t gfl(const pc_gp_t *gp, int size, uint16_t id) { const uint8_t *q = grec(gp, size, id); return q ? q[10] : 0; }
static int is_space(const pc_gp_t *gp, int size, uint16_t id) { return id != PC_GP_NL && (gfl(gp, size, id) & PC_GP_GF_SPACE); }

int32_t pc_gp_text_width(const pc_gp_t *gp, int size, const uint16_t *ids, uint32_t n)
{
    int32_t w = 0;
    for (uint32_t i = 0; ids && i < n; i++) w += adv(gp, size, ids[i]);
    return w;
}

/* a break is allowed before ids[k] (0 < k < n): at a space, or anywhere but inside a run of WORD glyphs */
static int can_break(const pc_gp_t *gp, int size, const uint16_t *ids, uint32_t k)
{
    uint8_t a = gfl(gp, size, ids[k - 1]), b = gfl(gp, size, ids[k]);
    return (a | b) & PC_GP_GF_SPACE || !(a & b & PC_GP_GF_WORD);
}

static void trim(const pc_gp_t *gp, int size, const uint16_t *ids, pc_gp_line_t *l)
{
    while (l->len && is_space(gp, size, ids[l->start + l->len - 1])) { l->len--; l->width -= adv(gp, size, ids[l->start + l->len]); }
}

int pc_gp_layout(const pc_gp_t *gp, int size, const uint16_t *ids, uint32_t n, int32_t max_w, int max_lines, pc_gp_line_t *lines)
{
    if (!lines || max_lines < 1 || size < 0 || size >= gp->nsizes || (n && !ids)) return -EINVAL;
    int nl = 0;
    uint32_t i = 0;
    while (nl < max_lines) {
        while (i < n && is_space(gp, size, ids[i])) i++;    /* spaces at a line start vanish */
        if (i >= n) break;
        pc_gp_line_t *l = &lines[nl++];
        uint32_t start = i, j = i, next, end;
        int32_t w = 0;
        for (;;) {
            if (j >= n) { next = end = n; break; }
            if (ids[j] == PC_GP_NL) { end = j; next = j + 1; break; }
            int32_t a = adv(gp, size, ids[j]);
            if (w + a > max_w && j > start) {
                uint32_t k = j;                              /* the last allowed break in (start, j] */
                while (k > start && !can_break(gp, size, ids, k)) k--;
                next = end = k > start ? k : j;
                break;
            }
            w += a;
            j++;
        }
        *l = (pc_gp_line_t){ .start = start, .len = end - start, .width = pc_gp_text_width(gp, size, ids + start, end - start) };
        trim(gp, size, ids, l);
        i = next;
        if (nl == max_lines) {                               /* the last line: anything left over? */
            uint32_t k = i;
            while (k < n && (ids[k] == PC_GP_NL || is_space(gp, size, ids[k]))) k++;
            if (k < n) {
                int32_t ell = gp->ellipsis != PC_GP_NONE ? adv(gp, size, gp->ellipsis) : 0;
                uint32_t m = start;
                w = 0;
                while (m < n && ids[m] != PC_GP_NL && w + adv(gp, size, ids[m]) + ell <= max_w) w += adv(gp, size, ids[m++]);
                *l = (pc_gp_line_t){ .start = start, .len = m - start, .width = w };
                trim(gp, size, ids, l);
                l->width += ell;
                l->ellipsis = 1;
            }
        }
    }
    return nl;
}

/* ---------- software blit (list reel cells) ---------- */
uint32_t pc_gp_max_glyph_bytes(const pc_gp_t *gp, int size)
{
    uint32_t mx = 0;
    if (size < 0 || size >= gp->nsizes) return 0;
    for (uint32_t i = 0; i < gp->ng; i++) {
        const uint8_t *q = grec(gp, size, i);
        uint32_t b = (uint32_t)q[4] * q[5] * bpp(q[9]);
        if (b > mx) mx = b;
    }
    return mx;
}

static uint16_t mix565(uint16_t s, uint16_t d, unsigned a)
{
    unsigned r = ((s >> 11) * a + (d >> 11) * (255 - a)) / 255, g = ((s >> 5 & 63) * a + (d >> 5 & 63) * (255 - a)) / 255,
             b = ((s & 31) * a + (d & 31) * (255 - a)) / 255;
    return (uint16_t)(r << 11 | g << 5 | b);
}

int pc_gp_draw565(const pc_gp_t *gp, int size, const uint16_t *ids, uint32_t n, int max_lines, uint16_t fg,
                  uint16_t *dst, int stride, int w, int h, void *scratch, uint32_t cap)
{
    if (!gp || !dst || w < 1 || h < 1 || stride < w || size < 0 || size >= gp->nsizes || (n && !ids)) return -EINVAL;
    if (max_lines > PC_GP_DRAW_MAX_LINES) max_lines = PC_GP_DRAW_MAX_LINES;
    pc_gp_line_t L[PC_GP_DRAW_MAX_LINES];
    int nl = pc_gp_layout(gp, size, ids, n, w, max_lines, L);
    if (nl <= 0) return nl;
    int32_t lh = gp->sz[size].line_h, top = (h - nl * lh) / 2;
    uint8_t *buf = scratch;
    for (int k = 0; k < nl; k++) {
        int32_t pen = (w - L[k].width) / 2, ly = top + k * lh;
        for (uint32_t i = 0; i <= L[k].len; i++) {
            uint16_t id = i < L[k].len ? ids[L[k].start + i] : (L[k].ellipsis ? gp->ellipsis : PC_GP_NONE);
            pc_gp_glyph_t g;
            if (pc_gp_glyph(gp, size, id, &g)) continue;
            if (g.bytes) {
                int32_t gx = pen + g.x, gy = ly + g.y;
                if (gx < w && gy < h && gx + g.w > 0 && gy + g.h > 0) {     /* visible at all: read it */
                    if (g.bytes > cap || !buf) return -ENOSPC;
                    int got = pc_gp_glyph_read(gp, size, id, buf, cap);
                    if (got != (int)g.bytes) return got < 0 ? got : -EIO;
                    const uint8_t *al = g.fmt == PC_GP_FMT_A8 ? buf : buf + (size_t)g.w * g.h * 2;
                    for (int y = 0; y < g.h; y++) {
                        int32_t py = gy + y;
                        if (py < 0 || py >= h) continue;
                        uint16_t *row = dst + (size_t)py * stride;
                        for (int x = 0; x < g.w; x++) {
                            int32_t qx = gx + x;
                            unsigned a = al[y * g.w + x];
                            if (qx < 0 || qx >= w || !a) continue;
                            uint16_t c = fg;
                            if (g.fmt != PC_GP_FMT_A8) { const uint8_t *cp = buf + 2 * ((size_t)y * g.w + x); c = (uint16_t)(cp[0] | cp[1] << 8); }
                            row[qx] = a == 255 ? c : mix565(c, row[qx], a);
                        }
                    }
                }
            }
            pen += g.adv;
        }
    }
    return nl;
}
