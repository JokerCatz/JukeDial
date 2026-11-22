// SPDX-License-Identifier: LGPL-2.1-or-later
#include <errno.h>
#include "pc/pc_glyphpack.h"
#include "pc/pc_io.h"
#include "pc/pc_zip.h"
#include "pc_test.h"

/* ---------- helpers ---------- */
static uint8_t *load(const char *name, uint32_t *len)
{
    *len = 0;
    char p[256];
    snprintf(p, sizeof p, "%s/%s", FIXTURES, name);
    FILE *f = fopen(p, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    *len = (uint32_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc(*len + 1);
    CHECK(fread(b, 1, *len, f) == *len);
    fclose(f);
    return b;
}

static void put16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { put16(p, v); put16(p + 2, v >> 16); }

typedef struct { const char *name; const uint8_t *d; uint32_t n; } zent_t;
static uint8_t *mkzip(const zent_t *e, int ne, uint32_t *len)   /* stored, valid CRCs */
{
    uint32_t cap = 22;
    for (int i = 0; i < ne; i++) cap += 76 + 2 * (uint32_t)strlen(e[i].name) + e[i].n;
    uint8_t *z = calloc(1, cap), *cd = calloc(1, cap);
    uint32_t p = 0, c = 0;
    for (int i = 0; i < ne; i++) {
        uint32_t nl = (uint32_t)strlen(e[i].name), crc = pc_crc32(0, e[i].d, e[i].n);
        put32(cd + c, 0x02014b50); put32(cd + c + 16, crc); put32(cd + c + 20, e[i].n); put32(cd + c + 24, e[i].n);
        put16(cd + c + 28, nl); put32(cd + c + 42, p); memcpy(cd + c + 46, e[i].name, nl); c += 46 + nl;
        put32(z + p, 0x04034b50); put32(z + p + 14, crc); put32(z + p + 18, e[i].n); put32(z + p + 22, e[i].n);
        put16(z + p + 26, nl); memcpy(z + p + 30, e[i].name, nl); p += 30 + nl;
        if (e[i].n) memcpy(z + p, e[i].d, e[i].n);
        p += e[i].n;
    }
    memcpy(z + p, cd, c);
    uint8_t *eo = z + p + c;
    put32(eo, 0x06054b50); put16(eo + 8, (uint32_t)ne); put16(eo + 10, (uint32_t)ne); put32(eo + 12, c); put32(eo + 16, p);
    *len = p + c + 22;
    free(cd);
    return z;
}

/* counting allocator: the pack must hold exactly one block; fail_at makes the n-th allocation fail */
static int live, allocs, fail_at;
static size_t live_bytes;
static void *t_alloc(size_t n)
{
    if (fail_at && ++allocs == fail_at) return NULL;
    size_t *p = malloc(n + 16);
    p[0] = n;
    live++;
    live_bytes += n;
    return (uint8_t *)p + 16;
}
static void t_free(void *p)
{
    if (!p) return;
    size_t *q = (size_t *)(void *)((uint8_t *)p - 16);
    live--;
    live_bytes -= q[0];
    free(q);
}
static const pc_gp_alloc_t A = { t_alloc, t_free };

/* the synthetic content of fixtures/make_glyphpacks.py */
enum { NG = 12, ELL = 11 };
static void syn_glyph(int g, int s, pc_gp_glyph_t *m)
{
    memset(m, 0, sizeof *m);
    if (g == 0) { m->adv = (uint8_t)(5 + 5 * s); m->flags = PC_GP_GF_SPACE; return; }
    m->fmt = g % 5 == 4 ? PC_GP_FMT_RGB565A8 : PC_GP_FMT_A8;
    m->w = (uint8_t)(4 + (g * 3 + s * 7) % 9);
    m->h = (uint8_t)(6 + (g + s * 5) % 10);
    m->x = (int8_t)(g % 3 - 1);
    m->y = (int8_t)(2 + s);
    m->adv = (uint8_t)(m->w + 2);
    m->flags = g % 2 ? PC_GP_GF_WORD : 0;
    m->bytes = (uint32_t)m->w * m->h * (m->fmt == PC_GP_FMT_A8 ? 1 : 3);
}

static void check_node(const pc_gp_t *gp, uint32_t i, const uint16_t *text, int len, int image, uint32_t first, int nchild, int weight, int flags)
{
    pc_gp_node_t n;
    CHECK_EQ_INT(pc_gp_node(gp, i, &n), 0);
    CHECK_EQ_INT(n.len, len);
    for (int k = 0; k < len; k++) CHECK_EQ_INT(n.text[k], text[k]);
    CHECK_EQ_INT(n.image, image);
    CHECK_EQ_INT(n.nchild, nchild);
    if (nchild) CHECK_EQ_INT(n.first_child, first);
    CHECK_EQ_INT(n.weight, weight);
    CHECK_EQ_INT(n.flags, flags);
}

static void check_basic(const pc_gp_t *gp)
{
    CHECK_EQ_INT(pc_gp_kind(gp), PC_GP_KIND_LUNCH);
    CHECK_EQ_INT(pc_gp_nsizes(gp), 2);
    pc_gp_size_t sz;
    CHECK_EQ_INT(pc_gp_size(gp, 0, &sz), 0);
    CHECK_EQ_INT(sz.px, 24); CHECK_EQ_INT(sz.line_h, 30);
    CHECK_EQ_INT(pc_gp_size(gp, 1, &sz), 0);
    CHECK_EQ_INT(sz.px, 40); CHECK_EQ_INT(sz.line_h, 50);
    CHECK_EQ_INT(pc_gp_size(gp, 2, &sz), -ERANGE);
    CHECK_EQ_INT(pc_gp_size_for_px(gp, 10), 0);
    CHECK_EQ_INT(pc_gp_size_for_px(gp, 30), 0);
    CHECK_EQ_INT(pc_gp_size_for_px(gp, 40), 1);
    CHECK_EQ_INT(pc_gp_size_for_px(gp, 400), 1);
    CHECK_EQ_INT(pc_gp_node_count(gp), 6);
    CHECK_EQ_INT(pc_gp_glyph_count(gp), NG);
    CHECK_EQ_INT(pc_gp_image_count(gp), 1);
    CHECK_EQ_INT(pc_gp_ellipsis(gp), ELL);
    /* breadth first: root, the two shops, then the dishes */
    check_node(gp, 0, (const uint16_t[]){ 1, 2, 3 }, 3, PC_GP_NONE, 1, 2, 1, 0);
    check_node(gp, 1, (const uint16_t[]){ 4, 5 }, 2, PC_GP_NONE, 3, 2, 1, 0);
    check_node(gp, 2, (const uint16_t[]){ 10, PC_GP_NL, 2 }, 3, PC_GP_NONE, 5, 1, 1, 0);
    check_node(gp, 3, (const uint16_t[]){ 6, 7, 0, 8 }, 4, PC_GP_NONE, 0, 0, 3, 0);
    check_node(gp, 4, (const uint16_t[]){ 9 }, 1, 0, 0, 0, 1, 1);
    check_node(gp, 5, (const uint16_t[]){ 3, 3, 3 }, 3, PC_GP_NONE, 0, 0, 0, 0);
    pc_gp_node_t n;
    CHECK_EQ_INT(pc_gp_node(gp, 6, &n), -ERANGE);
    uint8_t buf[2048];
    for (int s = 0; s < 2; s++)
        for (int g = 0; g < NG; g++) {
            pc_gp_glyph_t want, got;
            syn_glyph(g, s, &want);
            CHECK_EQ_INT(pc_gp_glyph(gp, s, (uint32_t)g, &got), 0);
            CHECK_EQ_INT(got.fmt, want.fmt); CHECK_EQ_INT(got.w, want.w); CHECK_EQ_INT(got.h, want.h);
            CHECK_EQ_INT(got.x, want.x); CHECK_EQ_INT(got.y, want.y); CHECK_EQ_INT(got.adv, want.adv);
            CHECK_EQ_INT(got.flags, want.flags); CHECK_EQ_INT(got.bytes, want.bytes);
            CHECK_EQ_INT(pc_gp_glyph_read(gp, s, (uint32_t)g, buf, sizeof buf), (int)want.bytes);
            for (uint32_t i = 0; i < want.bytes; i++) CHECK_EQ_INT(buf[i], (g * 37 + s * 11 + (int)i * 13) & 255);
            if (want.bytes) CHECK_EQ_INT(pc_gp_glyph_read(gp, s, (uint32_t)g, buf, want.bytes - 1), -ENOSPC);
        }
    pc_gp_glyph_t g;
    CHECK_EQ_INT(pc_gp_glyph(gp, 0, NG, &g), -ERANGE);
    CHECK_EQ_INT(pc_gp_glyph(gp, 2, 1, &g), -ERANGE);
    CHECK_EQ_INT(pc_gp_glyph(gp, -1, 1, &g), -ERANGE);
    CHECK_EQ_INT(pc_gp_glyph(gp, 0, PC_GP_NL, &g), -ERANGE);
    CHECK_EQ_INT(pc_gp_glyph_read(gp, 0, 99999, buf, sizeof buf), -ERANGE);
    pc_gp_image_t im;
    CHECK_EQ_INT(pc_gp_image(gp, 0, &im), 0);
    CHECK_EQ_INT(im.w, 10); CHECK_EQ_INT(im.h, 8); CHECK_EQ_INT(im.fmt, PC_GP_FMT_RGB565A8); CHECK_EQ_INT(im.bytes, 240);
    CHECK_EQ_INT(pc_gp_image_read(gp, 0, buf, sizeof buf), 240);
    for (int i = 0; i < 240; i++) CHECK_EQ_INT(buf[i], (i * 7 + 3) & 255);
    CHECK_EQ_INT(pc_gp_image(gp, 1, &im), -ERANGE);
    CHECK_EQ_INT(pc_gp_image_read(gp, 0, buf, 10), -ENOSPC);
}

static void open_file_check(const char *name)
{
    uint32_t len;
    uint8_t *z = load(name, &len);
    CHECK(z != NULL);
    pc_reader_t r;
    pc_reader_mem(&r, z, len);
    pc_gp_t *gp;
    live = allocs = fail_at = 0;
    CHECK_EQ_INT(pc_gp_open(&r, &A, &gp), 0);
    CHECK_EQ_INT(live, 1);                              /* the whole index is one block */
    CHECK_EQ_INT(pc_gp_block_bytes(gp), live_bytes);
    check_basic(gp);
    pc_gp_close(gp);
    CHECK_EQ_INT(live, 0);
    free(z);
}

static void test_basic(void) { open_file_check("gp_basic.zip"); }
static void test_deflate(void) { open_file_check("gp_deflate.zip"); }

static void test_js_writer(void)   /* tools/web/gp_fixture.mjs: the web page's writer, same content */
{
    open_file_check("gp_js.zip");
}

static void test_default_alloc_and_file(void)
{
    char p[256];
    snprintf(p, sizeof p, "%s/gp_basic.zip", FIXTURES);
    FILE *f = fopen(p, "rb");
    CHECK(f != NULL);
    pc_reader_t r;
    CHECK_EQ_INT(pc_reader_file(&r, f), 0);
    pc_gp_t *gp;
    CHECK_EQ_INT(pc_gp_open(&r, NULL, &gp), 0);
    check_basic(gp);
    pc_gp_close(gp);
    pc_gp_close(NULL);
    fclose(f);
}

static void test_alloc_failures(void)
{
    const char *names[] = { "gp_basic.zip", "gp_deflate.zip" };
    for (int k = 0; k < 2; k++) {
        uint32_t len;
        uint8_t *z = load(names[k], &len);
        pc_reader_t r;
        pc_reader_mem(&r, z, len);
        for (int at = 1; at < 12; at++) {
            live = allocs = 0;
            fail_at = at;
            pc_gp_t *gp = (pc_gp_t *)1;
            int rc = pc_gp_open(&r, &A, &gp);
            if (rc == 0) { CHECK(allocs < at); pc_gp_close(gp); }
            else { CHECK_EQ_INT(rc, -ENOMEM); CHECK(gp == NULL); }
            CHECK_EQ_INT(live, 0);
        }
        fail_at = 0;
        free(z);
    }
}

/* ---------- a pack built here: metrics only, for layout and corruption tests ---------- */
typedef struct { uint8_t w, h, adv, fmt, flags; } tg_t;
typedef struct { uint32_t off; uint16_t len, image; uint32_t first; uint16_t nchild; uint8_t weight, flags; } tn_t;

static uint8_t *mkpack(uint32_t *plen, int ns, const tg_t *g, uint32_t ng, uint16_t ell, const tn_t *nd, uint32_t nn, const uint16_t *seq, uint32_t nseq, uint32_t nimg)
{
    uint32_t n = 64 + 12u * ns * ng + 12u * nimg + 16u * nn + 2u * nseq;
    uint8_t *p = calloc(1, n), *q = p + 64;
    memcpy(p, "JGPK", 4); put16(p + 4, 1); put16(p + 6, 64); p[8] = 0; p[9] = (uint8_t)ns; put16(p + 10, ell);
    put32(p + 12, ng); put32(p + 16, nimg); put32(p + 20, nn); put32(p + 24, nseq);
    for (int s = 0; s < ns; s++) { put16(p + 32 + 8 * s, (uint32_t)(20 + 10 * s)); put16(p + 34 + 8 * s, (uint32_t)(24 + 10 * s)); }
    for (int s = 0; s < ns; s++)
        for (uint32_t i = 0; i < ng; i++, q += 12) {
            uint32_t off = 0;                           /* bitmaps one after the other, each w*h (A8) */
            for (uint32_t j = 0; j < i; j++) off += (uint32_t)g[j].w * g[j].h * (g[j].fmt == 2 ? 3 : g[j].fmt == 1);
            put32(q, off); q[4] = g[i].w; q[5] = g[i].h; q[8] = g[i].adv; q[9] = g[i].fmt; q[10] = g[i].flags;
        }
    for (uint32_t i = 0; i < nimg; i++, q += 12) { put16(q + 4, 4); put16(q + 6, 4); q[8] = 1; put32(q, 16 * i); }
    for (uint32_t i = 0; i < nn; i++, q += 16) {
        put32(q, nd[i].off); put16(q + 4, nd[i].len); put16(q + 6, nd[i].image); put32(q + 8, nd[i].first);
        put16(q + 12, nd[i].nchild); q[14] = nd[i].weight; q[15] = nd[i].flags;
    }
    for (uint32_t i = 0; i < nseq; i++, q += 2) put16(q, seq[i]);
    *plen = n;
    return p;
}

static pc_gp_t *open_pack(const uint8_t *pack, uint32_t plen, const uint8_t *g0, uint32_t g0n, uint8_t **zip, pc_reader_t *r, int *rc)
{
    static const uint8_t img[64];
    zent_t e[] = { { "pack.bin", pack, plen }, { "g0.bin", g0, g0n }, { "img.bin", img, sizeof img } };
    uint32_t len;
    *zip = mkzip(e, 3, &len);
    pc_reader_mem(r, *zip, len);
    pc_gp_t *gp = NULL;
    *rc = pc_gp_open(r, &A, &gp);
    return gp;
}

/* glyphs for layout: 0 space (adv 5), 1 W (Latin, adv 10), 2 C (CJK, adv 20), 3 ellipsis (adv 8) */
static const tg_t LG[] = { { 0, 0, 5, 0, PC_GP_GF_SPACE }, { 2, 2, 10, 1, PC_GP_GF_WORD }, { 2, 2, 20, 1, 0 }, { 2, 2, 8, 1, 0 } };
enum { SP = 0, W = 1, C = 2 };

static pc_gp_t *layout_pack(uint8_t **zip, pc_reader_t *r, uint16_t ell)
{
    tn_t root = { 0, 1, PC_GP_NONE, 0, 0, 1, 0 };
    uint16_t seq[] = { C };
    uint32_t plen;
    uint8_t *pack = mkpack(&plen, 1, LG, 4, ell, &root, 1, seq, 1, 0);
    static uint8_t g0[16];
    int rc;
    pc_gp_t *gp = open_pack(pack, plen, g0, sizeof g0, zip, r, &rc);
    CHECK_EQ_INT(rc, 0);
    free(pack);
    return gp;
}

#define LAYOUT(gp, ids, maxw, maxl) pc_gp_layout(gp, 0, ids, sizeof ids / sizeof ids[0], maxw, maxl, L)
static void line_is(const pc_gp_line_t *l, uint32_t start, uint32_t len, int32_t width, int ell)
{
    CHECK_EQ_INT(l->start, start);
    CHECK_EQ_INT(l->len, len);
    CHECK_EQ_INT(l->width, width);
    CHECK_EQ_INT(l->ellipsis, ell);
}

static void test_layout(void)
{
    uint8_t *zip;
    pc_reader_t r;
    pc_gp_t *gp = layout_pack(&zip, &r, 3);
    pc_gp_line_t L[8];
    const uint16_t cjk5[] = { C, C, C, C, C };
    CHECK_EQ_INT(LAYOUT(gp, cjk5, 50, 8), 3);                  /* CJK breaks anywhere */
    line_is(&L[0], 0, 2, 40, 0); line_is(&L[1], 2, 2, 40, 0); line_is(&L[2], 4, 1, 20, 0);
    CHECK_EQ_INT(pc_gp_text_width(gp, 0, cjk5, 5), 100);
    const uint16_t words[] = { W, W, SP, W, W };
    CHECK_EQ_INT(LAYOUT(gp, words, 30, 8), 2);                 /* at the space, not inside a word */
    line_is(&L[0], 0, 2, 20, 0); line_is(&L[1], 3, 2, 20, 0);
    CHECK_EQ_INT(LAYOUT(gp, words, 45, 8), 1);
    line_is(&L[0], 0, 5, 45, 0);
    const uint16_t word5[] = { W, W, W, W, W };
    CHECK_EQ_INT(LAYOUT(gp, word5, 30, 8), 2);                 /* one long word: cut where it must */
    line_is(&L[0], 0, 3, 30, 0); line_is(&L[1], 3, 2, 20, 0);
    const uint16_t wwc[] = { W, W, C };
    CHECK_EQ_INT(LAYOUT(gp, wwc, 25, 8), 2);                   /* Latin then CJK: a break between them */
    line_is(&L[0], 0, 2, 20, 0); line_is(&L[1], 2, 1, 20, 0);
    const uint16_t wword[] = { W, SP, W, W, W };
    CHECK_EQ_INT(LAYOUT(gp, wword, 35, 8), 2);                 /* the word moves to the next line whole */
    line_is(&L[0], 0, 1, 10, 0); line_is(&L[1], 2, 3, 30, 0);
    const uint16_t nl[] = { C, PC_GP_NL, C };
    CHECK_EQ_INT(LAYOUT(gp, nl, 300, 8), 2);
    line_is(&L[0], 0, 1, 20, 0); line_is(&L[1], 2, 1, 20, 0);
    const uint16_t nl2[] = { C, PC_GP_NL, PC_GP_NL, C };
    CHECK_EQ_INT(LAYOUT(gp, nl2, 300, 8), 3);                  /* an empty line in between */
    line_is(&L[1], 2, 0, 0, 0);
    const uint16_t cjk10[] = { C, C, C, C, C, C, C, C, C, C };
    CHECK_EQ_INT(LAYOUT(gp, cjk10, 50, 2), 2);                 /* too long: the last line ends in "…" */
    line_is(&L[0], 0, 2, 40, 0); line_is(&L[1], 2, 2, 48, 1);
    CHECK_EQ_INT(LAYOUT(gp, cjk10, 50, 1), 1);
    line_is(&L[0], 0, 2, 48, 1);
    CHECK_EQ_INT(LAYOUT(gp, cjk10, 25, 1), 1);                 /* even one glyph + "…" overflows: only the ellipsis */
    line_is(&L[0], 0, 0, 8, 1);
    const uint16_t cjk4[] = { C, C, C, C };
    CHECK_EQ_INT(LAYOUT(gp, cjk4, 50, 2), 2);                  /* fits exactly: no ellipsis */
    line_is(&L[1], 2, 2, 40, 0);
    const uint16_t spaces[] = { SP, SP, W, SP, SP, SP, SP, SP, SP, W, SP };
    CHECK_EQ_INT(LAYOUT(gp, spaces, 20, 8), 2);                /* spaces at line ends and starts vanish */
    line_is(&L[0], 2, 1, 10, 0); line_is(&L[1], 9, 1, 10, 0);
    const uint16_t wide[] = { C, C };
    CHECK_EQ_INT(LAYOUT(gp, wide, 10, 8), 2);                  /* wider than the line: one glyph per line */
    line_is(&L[0], 0, 1, 20, 0); line_is(&L[1], 1, 1, 20, 0);
    const uint16_t unknown[] = { 999, C, 4 };
    CHECK_EQ_INT(LAYOUT(gp, unknown, 50, 8), 1);
    line_is(&L[0], 0, 3, 20, 0);
    const uint16_t only_sp[] = { SP, SP };
    CHECK_EQ_INT(LAYOUT(gp, only_sp, 50, 8), 0);
    CHECK_EQ_INT(pc_gp_layout(gp, 0, cjk5, 0, 50, 8, L), 0);
    CHECK_EQ_INT(pc_gp_layout(gp, 0, cjk5, 5, 50, 0, L), -EINVAL);
    CHECK_EQ_INT(pc_gp_layout(gp, 0, cjk5, 5, 50, 8, NULL), -EINVAL);
    CHECK_EQ_INT(pc_gp_layout(gp, 1, cjk5, 5, 50, 8, L), -EINVAL);
    CHECK_EQ_INT(pc_gp_layout(gp, 0, NULL, 5, 50, 8, L), -EINVAL);
    CHECK_EQ_INT(pc_gp_layout(gp, 0, cjk5, 5, 0, 8, L), 5);   /* zero width: one glyph per line */
    pc_gp_close(gp);
    free(zip);
    gp = layout_pack(&zip, &r, PC_GP_NONE);                    /* no "…" glyph: just cut */
    CHECK_EQ_INT(LAYOUT(gp, cjk10, 50, 1), 1);
    line_is(&L[0], 0, 2, 40, 1);
    pc_gp_close(gp);
    free(zip);
    CHECK_EQ_INT(live, 0);
}

/* ---------- corruption ---------- */
static uint8_t *basic_part(const char *name, uint32_t *n)   /* one entry of gp_basic.zip */
{
    uint32_t len;
    uint8_t *z = load("gp_basic.zip", &len), *out;
    pc_reader_t r;
    pc_reader_mem(&r, z, len);
    pc_zip_entry_t *e;
    int ne;
    CHECK_EQ_INT(pc_zip_open(&r, &e, &ne), 0);
    CHECK_EQ_INT(pc_zip_extract(&r, pc_zip_find(e, ne, name), &out, n), 0);
    pc_zip_free(e, ne);
    free(z);
    return out;
}

static int open_parts(const uint8_t *pack, uint32_t plen, int drop)   /* drop: 1 g1.bin, 2 pack.bin */
{
    uint32_t n0, n1, ni, len;
    uint8_t *g0 = basic_part("g0.bin", &n0), *g1 = basic_part("g1.bin", &n1), *im = basic_part("img.bin", &ni);
    zent_t e[4] = { { "g0.bin", g0, n0 }, { "img.bin", im, ni } };
    int ne = 2;
    if (drop != 1) e[ne++] = (zent_t){ "g1.bin", g1, n1 };
    if (drop != 2) e[ne++] = (zent_t){ "pack.bin", pack, plen };
    uint8_t *z = mkzip(e, ne, &len);
    pc_reader_t r;
    pc_reader_mem(&r, z, len);
    pc_gp_t *gp = NULL;
    live = 0;
    int rc = pc_gp_open(&r, &A, &gp);
    if (!rc) pc_gp_close(gp);
    else CHECK(gp == NULL);
    CHECK_EQ_INT(live, 0);
    free(z); free(g0); free(g1); free(im);
    return rc;
}

/* basic pack.bin layout: 64 header, 2*12 glyph records of 12 B, 1 image of 12 B, 6 nodes of 16 B, sequence */
enum { OG = 64, OI = 64 + 24 * 12, ON = OI + 12, OS = ON + 6 * 16 };

static int with32(uint32_t at, uint32_t v)
{
    uint32_t n;
    uint8_t *p = basic_part("pack.bin", &n);
    put32(p + at, v);
    int rc = open_parts(p, n, 0);
    free(p);
    return rc;
}
static int with16(uint32_t at, uint32_t v)
{
    uint32_t n;
    uint8_t *p = basic_part("pack.bin", &n);
    put16(p + at, v);
    int rc = open_parts(p, n, 0);
    free(p);
    return rc;
}
static int with8(uint32_t at, uint8_t v)
{
    uint32_t n;
    uint8_t *p = basic_part("pack.bin", &n);
    p[at] = v;
    int rc = open_parts(p, n, 0);
    free(p);
    return rc;
}

static void test_bad_header(void)
{
    uint32_t n;
    uint8_t *p = basic_part("pack.bin", &n);
    CHECK_EQ_INT(n, OS + 2 * 16);
    CHECK_EQ_INT(open_parts(p, n, 0), 0);                    /* rebuilt by mkzip: still fine */
    CHECK_EQ_INT(open_parts(p, n, 1), -EINVAL);              /* glyphs of size 1 have nowhere to live */
    CHECK_EQ_INT(open_parts(p, n, 2), -EINVAL);              /* no index */
    for (uint32_t cut = 0; cut < n; cut++) CHECK(open_parts(p, cut, 0) < 0);
    free(p);
    CHECK_EQ_INT(with8(0, 'X'), -EINVAL);                    /* magic */
    CHECK_EQ_INT(with16(4, 2), -EPROTONOSUPPORT);            /* a newer version */
    CHECK_EQ_INT(with16(4, 0), -EPROTONOSUPPORT);
    CHECK_EQ_INT(with16(6, 32), -EINVAL);                    /* header shorter than v1 */
    CHECK_EQ_INT(with16(6, 0xFFFF), -EINVAL);
    CHECK_EQ_INT(with8(8, 9), -EINVAL);                      /* unknown kind */
    CHECK_EQ_INT(with8(8, 5), -EINVAL);
    CHECK_EQ_INT(with8(8, PC_GP_KIND_VOCAB), 0);             /* kind 4: vocabulary decks (pc_vocab.h) */
    CHECK_EQ_INT(with8(9, 0), -EINVAL);                      /* no sizes */
    CHECK_EQ_INT(with8(9, 5), -EINVAL);
    CHECK_EQ_INT(with8(9, 1), -EINVAL);                      /* the tables no longer line up */
    CHECK_EQ_INT(with16(10, 200), -EINVAL);                  /* ellipsis out of range */
    CHECK_EQ_INT(with32(12, 0xFFFFFFFFu), -E2BIG);           /* huge counts */
    CHECK_EQ_INT(with32(16, 0xFFFFFFFFu), -E2BIG);
    CHECK_EQ_INT(with32(20, 0xFFFFFFFFu), -E2BIG);
    CHECK_EQ_INT(with32(24, 0xFFFFFFFFu), -E2BIG);
    CHECK_EQ_INT(with32(12, 0xFFF0), -EINVAL);               /* in the limits but past the end of pack.bin */
    CHECK_EQ_INT(with32(20, 0), -EINVAL);                    /* a list needs its title node */
    CHECK_EQ_INT(with32(12, 13), -EINVAL);
    CHECK_EQ_INT(with16(32, 0), -EINVAL);                    /* size 0 px */
    CHECK_EQ_INT(with16(34, 0), -EINVAL);                    /* line height 0 */
}

static void test_bad_tables(void)
{
    CHECK_EQ_INT(with32(OG + 12 * 5, 5000), -EINVAL);        /* bitmap past the end of g0.bin */
    CHECK_EQ_INT(with32(OG + 12 * 5, 0xFFFFFFF0u), -EINVAL); /* ... with a wrapping offset */
    CHECK_EQ_INT(with8(OG + 12 * 5 + 4, 255), -EINVAL);      /* width too large for the blob */
    CHECK_EQ_INT(with8(OG + 12 * 5 + 9, 7), -EINVAL);        /* unknown bitmap format */
    CHECK_EQ_INT(with8(OG + 12 * 5 + 9, 0), 0);              /* empty glyph: fine */
    CHECK_EQ_INT(with16(OI + 4, 0), -EINVAL);                /* image 0 wide */
    CHECK_EQ_INT(with16(OI + 4, 361), -EINVAL);              /* wider than the screen */
    CHECK_EQ_INT(with32(OI, 100), -EINVAL);                  /* image data past img.bin */
    CHECK_EQ_INT(with8(OI + 8, 0), -EINVAL);                 /* image format */
    CHECK_EQ_INT(with32(ON + 0, 30), -EINVAL);               /* root text past the sequence */
    CHECK_EQ_INT(with16(ON + 4, 40), -EINVAL);
    CHECK_EQ_INT(with16(ON + 6, 5), -EINVAL);                /* image 5 of 1 */
    CHECK_EQ_INT(with32(ON + 8, 0), -EINVAL);                /* the root its own child: a cycle */
    CHECK_EQ_INT(with32(ON + 16 + 8, 1), -EINVAL);           /* node 1 its own child */
    CHECK_EQ_INT(with32(ON + 16 + 8, 0), -EINVAL);           /* node 1 points back at the root */
    CHECK_EQ_INT(with16(ON + 12, 9), -EINVAL);               /* children past the last node */
    CHECK_EQ_INT(with32(ON + 8, 0xFFFFFFFFu), -EINVAL);
    CHECK_EQ_INT(with16(OS, 500), -EINVAL);                  /* glyph ID out of range in a text */
    CHECK_EQ_INT(with16(OS, PC_GP_NONE), -EINVAL);
    CHECK_EQ_INT(with16(OS, PC_GP_NL), 0);                   /* a line break is fine */
}

/* every accessor over the whole pack: must stay in bounds whatever open accepted */
static void exercise(const pc_gp_t *gp)
{
    static uint8_t buf[256 * 256 * 3];
    pc_gp_line_t L[4];
    for (uint32_t i = 0; i < pc_gp_node_count(gp); i++) {
        pc_gp_node_t n;
        CHECK_EQ_INT(pc_gp_node(gp, i, &n), 0);
        CHECK(!n.nchild || (n.first_child > i && n.first_child + n.nchild <= pc_gp_node_count(gp)));
        for (int s = 0; s < pc_gp_nsizes(gp); s++)
            for (int w = 0; w < 200; w += 37) {
                int nl = pc_gp_layout(gp, s, n.text, n.len, w, 4, L);
                CHECK(nl >= 0 && nl <= 4);
                for (int k = 0; k < nl; k++) CHECK(L[k].start + L[k].len <= n.len);
            }
    }
    for (int s = 0; s < pc_gp_nsizes(gp); s++)
        for (uint32_t g = 0; g < pc_gp_glyph_count(gp); g++) {
            pc_gp_glyph_t m;
            CHECK_EQ_INT(pc_gp_glyph(gp, s, g, &m), 0);
            int rc = pc_gp_glyph_read(gp, s, g, buf, sizeof buf);
            CHECK(rc == (int)m.bytes || rc == -EIO);
        }
    for (uint32_t i = 0; i < pc_gp_image_count(gp); i++) {
        pc_gp_image_t m;
        CHECK_EQ_INT(pc_gp_image(gp, i, &m), 0);
        int rc = pc_gp_image_read(gp, i, buf, sizeof buf);
        CHECK(rc == (int)m.bytes || rc == -EIO);
    }
}

static int try_open(const uint8_t *z, uint32_t len)
{
    pc_reader_t r;
    pc_reader_mem(&r, z, len);
    pc_gp_t *gp = NULL;
    live = 0;
    int rc = pc_gp_open(&r, &A, &gp);
    if (!rc) { exercise(gp); pc_gp_close(gp); }
    else CHECK(gp == NULL && rc < 0);
    CHECK_EQ_INT(live, 0);
    return rc;
}

static void test_truncated_and_flipped(void)
{
    const char *names[] = { "gp_basic.zip", "gp_deflate.zip" };
    for (int k = 0; k < 2; k++) {
        uint32_t len;
        uint8_t *z = load(names[k], &len);
        for (uint32_t cut = 0; cut < len; cut++) {           /* exact-size copies so ASan sees any over-read */
            uint8_t *c = malloc(cut ? cut : 1);
            memcpy(c, z, cut);
            CHECK(try_open(c, cut) < 0);
            free(c);
        }
        int opened = 0;
        for (uint32_t i = 0; i < len; i++) {
            z[i] ^= 0xFF;
            opened += try_open(z, len) == 0;
            z[i] ^= 0xA5;
            opened += try_open(z, len) == 0;
            z[i] ^= 0x5A;
        }
        CHECK(opened > 0);                                    /* flips in names/dates/source.json do not matter */
        free(z);
    }
}

static uint32_t rnd_state = 12345;
static uint32_t rnd(void) { rnd_state ^= rnd_state << 13; rnd_state ^= rnd_state >> 17; rnd_state ^= rnd_state << 5; return rnd_state; }

static void test_fuzz_index(void)   /* random damage to pack.bin with valid CRCs, so it reaches the parser */
{
    uint32_t n, n0, n1, ni;
    uint8_t *p = basic_part("pack.bin", &n), *g0 = basic_part("g0.bin", &n0), *g1 = basic_part("g1.bin", &n1), *im = basic_part("img.bin", &ni);
    uint8_t *m = malloc(n);
    static const uint32_t vals[] = { 0, 1, 2, 0x7F, 0x80, 0xFF, 0xFFFF, 0xFFFE, 0x10000, 0x7FFFFFFF, 0x80000000u, 0xFFFFFFFFu };
    int ok = 0;
    for (int it = 0; it < 4000; it++) {
        memcpy(m, p, n);
        int k = 1 + (int)(rnd() % 4);
        for (int j = 0; j < k; j++) {
            uint32_t at = rnd() % n;
            switch (rnd() % 3) {
            case 0: m[at] = (uint8_t)rnd(); break;
            case 1: if (at + 2 <= n) put16(m + at, vals[rnd() % 12]); break;
            default: if (at + 4 <= n) put32(m + at, vals[rnd() % 12]); break;
            }
        }
        uint32_t plen = rnd() % 8 ? n : rnd() % (n + 1);
        zent_t e[] = { { "pack.bin", m, plen }, { "g0.bin", g0, n0 }, { "g1.bin", g1, n1 }, { "img.bin", im, ni } };
        uint32_t zl;
        uint8_t *z = mkzip(e, 4, &zl);
        ok += try_open(z, zl) == 0;
        free(z);
    }
    CHECK(ok > 0 && ok < 4000);
    free(m); free(p); free(g0); free(g1); free(im);
}

static void test_fuzz_random(void)
{
    for (int it = 0; it < 3000; it++) {
        uint32_t len = rnd() % 3000;
        uint8_t *b = malloc(len ? len : 1);
        for (uint32_t i = 0; i < len; i++) b[i] = (uint8_t)rnd();
        if (len > 30 && it % 2) {                             /* look like a zip, at least at the ends */
            memcpy(b, "PK\3\4", 4);
            memcpy(b + len - 22, "PK\5\6", 4);
        }
        CHECK(try_open(b, len) < 0);
        free(b);
        uint8_t junk[200];                                    /* a well-formed zip with a random index */
        uint32_t jl = 1 + rnd() % sizeof junk;
        for (uint32_t i = 0; i < jl; i++) junk[i] = (uint8_t)rnd();
        if (it % 3 == 0 && jl > 8) { memcpy(junk, "JGPK\1\0\100\0", 8); }
        zent_t e[] = { { "pack.bin", junk, jl } };
        uint32_t zl;
        uint8_t *z = mkzip(e, 1, &zl);
        CHECK(try_open(z, zl) < 0);
        free(z);
    }
}

/* a reader that starts failing after open: bitmap reads report -EIO */
static int fail_reads;
static pc_reader_t inner;
static int flaky_read(void *u, uint32_t off, void *buf, uint32_t n) { (void)u; return fail_reads ? -1 : inner.read(inner.u, off, buf, n); }

static void test_read_errors(void)
{
    uint32_t len;
    uint8_t *z = load("gp_basic.zip", &len);
    pc_reader_mem(&inner, z, len);
    pc_reader_t r = { .read = flaky_read, .size = len };
    pc_gp_t *gp;
    fail_reads = 0;
    CHECK_EQ_INT(pc_gp_open(&r, &A, &gp), 0);
    fail_reads = 1;
    uint8_t buf[512];
    CHECK_EQ_INT(pc_gp_glyph_read(gp, 0, 1, buf, sizeof buf), -EIO);
    CHECK_EQ_INT(pc_gp_image_read(gp, 0, buf, sizeof buf), -EIO);
    CHECK_EQ_INT(pc_gp_glyph_read(gp, 0, 0, buf, sizeof buf), 0);   /* empty glyph: nothing to read */
    pc_gp_close(gp);
    CHECK(pc_gp_open(&r, &A, &gp) < 0);
    CHECK(gp == NULL);
    free(z);
}

/* ---------- pc_gp_draw565: the list reels' cells ---------- */
static uint16_t blend(uint16_t s, uint16_t d, unsigned a)
{
    unsigned r = ((s >> 11) * a + (d >> 11) * (255 - a)) / 255, g = ((s >> 5 & 63) * a + (d >> 5 & 63) * (255 - a)) / 255,
             b = ((s & 31) * a + (d & 31) * (255 - a)) / 255;
    return (uint16_t)(r << 11 | g << 5 | b);
}
static uint8_t gbyte(int g, int s, int i) { return (uint8_t)((g * 37 + s * 11 + i * 13) & 255); }

static void test_draw565(void)
{
    uint32_t len;
    uint8_t *z = load("gp_basic.zip", &len);
    pc_reader_t r;
    pc_reader_mem(&r, z, len);
    pc_gp_t *gp;
    CHECK_EQ_INT(pc_gp_open(&r, NULL, &gp), 0);
    uint32_t mx[2] = { 0, 0 };
    for (int s = 0; s < 2; s++) for (int g = 0; g < NG; g++) { pc_gp_glyph_t m; syn_glyph(g, s, &m); if (m.bytes > mx[s]) mx[s] = m.bytes; }
    CHECK_EQ_INT(pc_gp_max_glyph_bytes(gp, 0), mx[0]);
    CHECK_EQ_INT(pc_gp_max_glyph_bytes(gp, 1), mx[1]);
    CHECK_EQ_INT(pc_gp_max_glyph_bytes(gp, 5), 0);
    uint8_t scratch[1024];
    enum { W = 40, H = 40, BG = 0x1234 };
    static uint16_t px[W * H];
    for (int i = 0; i < W * H; i++) px[i] = BG;
    /* one A8 glyph (1: 7x7 at x 0, y 2, adv 9): pen (40-9)/2 = 15, line top (40-30)/2 = 5 */
    CHECK_EQ_INT(pc_gp_draw565(gp, 0, (const uint16_t[]){ 1 }, 1, 1, 0xF81F, px, W, W, H, scratch, sizeof scratch), 1);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            int gx = x - 15, gy = y - 7;
            uint16_t want = BG;
            if (gx >= 0 && gx < 7 && gy >= 0 && gy < 7) { unsigned a = gbyte(1, 0, gy * 7 + gx); want = a ? blend(0xF81F, BG, a) : BG; }
            CHECK_EQ_INT(px[y * W + x], want);
        }
    /* RGB565A8 (4: 7x10 at x 0, y 2): colour plane then alpha */
    for (int i = 0; i < W * H; i++) px[i] = BG;
    CHECK_EQ_INT(pc_gp_draw565(gp, 0, (const uint16_t[]){ 4 }, 1, 1, 0xFFFF, px, W, W, H, scratch, sizeof scratch), 1);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            int gx = x - 15, gy = y - 7;
            uint16_t want = BG;
            if (gx >= 0 && gx < 7 && gy >= 0 && gy < 10) {
                int k = gy * 7 + gx;
                uint16_t c = (uint16_t)(gbyte(4, 0, 2 * k) | gbyte(4, 0, 2 * k + 1) << 8);
                unsigned a = gbyte(4, 0, 70 * 2 + k);
                want = a ? blend(c, BG, a) : BG;
            }
            CHECK_EQ_INT(px[y * W + x], want);
        }
    /* two lines, centred as a block: 2 x 30 in 100 -> top 20, the second line at 50 */
    static uint16_t tall[W * 100];
    memset(tall, 0, sizeof tall);
    CHECK_EQ_INT(pc_gp_draw565(gp, 0, (const uint16_t[]){ 1, PC_GP_NL, 1 }, 3, 2, 0xFFFF, tall, W, W, 100, scratch, sizeof scratch), 2);
    int first = -1, last = -1;
    for (int y = 0; y < 100; y++) for (int x = 0; x < W; x++) if (tall[y * W + x]) { if (first < 0) first = y; last = y; }
    int top1 = -1;
    for (int gy = 0; gy < 7 && top1 < 0; gy++) for (int gx = 0; gx < 7; gx++) if (gbyte(1, 0, gy * 7 + gx) >= 5) { top1 = gy; break; }
    CHECK_EQ_INT(first, 22 + top1);
    CHECK(last >= 52 && last < 59);
    /* empty text, bad arguments, no room for a glyph */
    CHECK_EQ_INT(pc_gp_draw565(gp, 0, NULL, 0, 1, 0xFFFF, px, W, W, H, scratch, sizeof scratch), 0);
    CHECK_EQ_INT(pc_gp_draw565(gp, 0, (const uint16_t[]){ 1 }, 1, 1, 0xFFFF, NULL, W, W, H, scratch, sizeof scratch), -EINVAL);
    CHECK_EQ_INT(pc_gp_draw565(gp, 0, (const uint16_t[]){ 1 }, 1, 1, 0xFFFF, px, W, 0, H, scratch, sizeof scratch), -EINVAL);
    CHECK_EQ_INT(pc_gp_draw565(gp, 0, (const uint16_t[]){ 1 }, 1, 1, 0xFFFF, px, 10, 20, H, scratch, sizeof scratch), -EINVAL);   /* stride < w */
    CHECK_EQ_INT(pc_gp_draw565(gp, 0, (const uint16_t[]){ 1 }, 1, 1, 0xFFFF, px, W, W, H, scratch, 10), -ENOSPC);
    CHECK_EQ_INT(pc_gp_draw565(gp, 0, (const uint16_t[]){ 1 }, 1, 1, 0xFFFF, px, W, W, H, NULL, 0), -ENOSPC);
    CHECK_EQ_INT(pc_gp_draw565(gp, 7, (const uint16_t[]){ 1 }, 1, 1, 0xFFFF, px, W, W, H, scratch, sizeof scratch), -EINVAL);
    /* random texts into small exactly-sized buffers (ASan watches the edges); columns past w stay untouched */
    uint32_t seed = 12345;
    for (int it = 0; it < 3000; it++) {
        seed = seed * 1103515245u + 12345u;
        int w = 1 + (int)(seed >> 8) % 60, h = 1 + (int)(seed >> 16) % 70, stride = w + (int)(seed >> 3) % 3, n = (int)(seed >> 20) % 12;
        uint16_t ids[12];
        for (int k = 0; k < n; k++) { seed = seed * 1103515245u + 12345u; uint32_t v = (seed >> 10) % 16; ids[k] = v == 14 ? PC_GP_NL : v == 15 ? 40000 : (uint16_t)(v % NG); }
        uint16_t *buf = malloc((size_t)stride * h * 2);
        for (int i = 0; i < stride * h; i++) buf[i] = 0xABCD;
        int rc = pc_gp_draw565(gp, (int)(seed >> 30) & 1, ids, (uint32_t)n, 1 + (int)(seed >> 5) % 4, 0x07E0, buf, stride, w, h, scratch, sizeof scratch);
        CHECK(rc >= 0);
        for (int y = 0; y < h; y++) for (int x = w; x < stride; x++) CHECK_EQ_INT(buf[y * stride + x], 0xABCD);
        free(buf);
    }
    pc_gp_close(gp);
    free(z);
}

int main(void)
{
    RUN(test_basic);
    RUN(test_deflate);
    RUN(test_js_writer);
    RUN(test_default_alloc_and_file);
    RUN(test_alloc_failures);
    RUN(test_layout);
    RUN(test_bad_header);
    RUN(test_bad_tables);
    RUN(test_truncated_and_flipped);
    RUN(test_fuzz_index);
    RUN(test_fuzz_random);
    RUN(test_read_errors);
    RUN(test_draw565);
    return 0;
}
