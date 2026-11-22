// SPDX-License-Identifier: LGPL-2.1-or-later
/* hires task 8d: which decoder takes a JPEG (pc_jpeg_probe) and our own baseline decoder (pc_jpeg_decode), against Pillow's decode of
 * pictures made at build time (tools/gen/jpeg_fixtures.py -> JPEG_FIXTURES, never committed): every sampling layout the board meets -
 * 4:4:4 (1x1 and ffmpeg's 1x2), 4:2:2 (2x1 and ffmpeg's 2x2 + 1x2 chroma), 4:2:0, 4:4:0, 4:1:1, all 2x1, gray (1x1 and 2x2 factors),
 * restart markers - plus progressive (refused with its own reason), crafted headers, truncated and damaged files, failed allocations. */
#include <stdint.h>
#include <time.h>
#include "pc/pc_jpeg.h"
#include "pc_test.h"

typedef struct { uint8_t *p; size_t n; } blob_t;
static blob_t load(const char *name, const char *ext)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s%s", JPEG_FIXTURES, name, ext);
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "missing fixture %s (tools/gen/jpeg_fixtures.py)\n", path); exit(1); }
    fseek(f, 0, SEEK_END);
    blob_t b = { NULL, (size_t)ftell(f) };
    fseek(f, 0, SEEK_SET);
    b.p = malloc(b.n ? b.n : 1);
    CHECK(fread(b.p, 1, b.n, f) == b.n);
    fclose(f);
    return b;
}

typedef struct { char name[64], route[16]; int w, h; } item_t;
static item_t s_items[64];
static int s_n;
static void read_manifest(void)
{
    char path[512];
    snprintf(path, sizeof path, "%s/manifest.txt", JPEG_FIXTURES);
    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "missing %s (tools/gen/jpeg_fixtures.py)\n", path); exit(1); }
    while (s_n < 64 && fscanf(f, "%63s %15s %d %d", s_items[s_n].name, s_items[s_n].route, &s_items[s_n].w, &s_items[s_n].h) == 4) s_n++;
    fclose(f);
    CHECK(s_n >= 12);
}
static bool has(const char *name)
{
    for (int i = 0; i < s_n; i++) if (!strcmp(s_items[i].name, name)) return true;
    return false;
}

/* mean absolute error (0..255 per channel) of an RGB565 picture against the reference RGB888, each output pixel against the mean of its
 * 2^shift square of the reference */
static double mae(const uint16_t *px, int ow, int oh, const uint8_t *ref, int w, int shift)
{
    double sum = 0;
    int k = 1 << shift;
    for (int y = 0; y < oh; y++)
        for (int x = 0; x < ow; x++) {
            int acc[3] = { 0, 0, 0 };
            for (int dy = 0; dy < k; dy++)
                for (int dx = 0; dx < k; dx++)
                    for (int c = 0; c < 3; c++) acc[c] += ref[(((size_t)(y * k + dy)) * w + x * k + dx) * 3 + c];
            uint16_t v = px[y * ow + x];
            int r = (v >> 11) << 3 | (v >> 13), g = ((v >> 5) & 63) << 2 | ((v >> 9) & 3), b = (v & 31) << 3 | ((v >> 2) & 7);
            int kk = k * k;
            sum += abs(r - acc[0] / kk) + abs(g - acc[1] / kk) + abs(b - acc[2] / kk);
        }
    return sum / (3.0 * ow * oh);
}

static void test_routes(void)
{
    for (int i = 0; i < s_n; i++) {
        blob_t b = load(s_items[i].name, ".jpg");
        pc_jpeg_info_t in;
        int r = pc_jpeg_probe(b.p, b.n, &in);
        if (strcmp(pc_jpeg_route_name(r), s_items[i].route)) { fprintf(stderr, "%s: route %s, expected %s\n", s_items[i].name, pc_jpeg_route_name(r), s_items[i].route); exit(1); }
        CHECK_EQ_INT(in.w, s_items[i].w);
        CHECK_EQ_INT(in.h, s_items[i].h);
        free(b.p);
    }
}

/* the board's Batch C2 cover (ffmpeg yuvj444p: 1x2 for all three components) and ffmpeg's 4:2:2 are ours, not the chip's */
static void test_board_layouts(void)
{
    if (!has("ff_444")) { printf("SKIP ffmpeg pictures (no ffmpeg at build time)\n"); return; }
    blob_t b = load("ff_444", ".jpg");
    pc_jpeg_info_t in;
    CHECK_EQ_INT(pc_jpeg_probe(b.p, b.n, &in), PC_JPEG_OWN);
    CHECK_EQ_INT(in.ncomp, 3);
    CHECK_EQ_INT(in.hs[0], 1); CHECK_EQ_INT(in.vs[0], 2); CHECK_EQ_INT(in.hs[1], 1); CHECK_EQ_INT(in.vs[1], 2);
    free(b.p);
    b = load("ff_422", ".jpg");
    CHECK_EQ_INT(pc_jpeg_probe(b.p, b.n, &in), PC_JPEG_OWN);
    CHECK_EQ_INT(in.hs[0], 2); CHECK_EQ_INT(in.vs[0], 2); CHECK_EQ_INT(in.vs[1], 2);
    free(b.p);
}

static void test_decode_all(void)
{
    for (int i = 0; i < s_n; i++) {
        if (!strcmp(s_items[i].route, "progressive")) continue;
        blob_t b = load(s_items[i].name, ".jpg"), ref = load(s_items[i].name, ".rgb");
        int w = s_items[i].w, h = s_items[i].h;
        CHECK_EQ_INT(ref.n, (size_t)w * h * 3);
        for (int shift = 0; shift <= 3; shift++) {
            uint16_t *px = NULL;
            int ow = 0, oh = 0;
            int rc = pc_jpeg_decode(b.p, b.n, shift, NULL, &px, &ow, &oh);
            if (rc) { fprintf(stderr, "%s shift %d: rc %d\n", s_items[i].name, shift, rc); exit(1); }
            CHECK_EQ_INT(ow, w >> shift);
            CHECK_EQ_INT(oh, h >> shift);
            double e = mae(px, ow, oh, ref.p, w, shift);
            printf("  %-12s shift %d mae %.2f\n", s_items[i].name, shift, e);
            if (e > 4.0) { fprintf(stderr, "%s shift %d: mae %.2f > 4\n", s_items[i].name, shift, e); exit(1); }
            free(px);
        }
        free(b.p);
        free(ref.p);
    }
}

static void test_progressive_refused(void)
{
    blob_t b = load("pil_prog", ".jpg");
    pc_jpeg_info_t in;
    CHECK_EQ_INT(pc_jpeg_probe(b.p, b.n, &in), PC_JPEG_PROGRESSIVE);
    CHECK_EQ_INT(in.sof, 0xC2);
    CHECK_STR(pc_jpeg_route_name(PC_JPEG_PROGRESSIVE), "progressive");
    uint16_t *px = (uint16_t *)1;
    int ow, oh;
    CHECK_EQ_INT(pc_jpeg_decode(b.p, b.n, 0, NULL, &px, &ow, &oh), PC_JPEG_E_PROGRESSIVE);
    CHECK(px == NULL);
    free(b.p);
}

/* the SOF marker byte of a file, patched */
static size_t sof_at(const blob_t *b)
{
    for (size_t i = 2; i + 3 < b->n;) {
        CHECK(b->p[i] == 0xFF);
        uint8_t m = b->p[i + 1];
        if (m >= 0xC0 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC) return i;
        i += 2 + ((size_t)b->p[i + 2] << 8 | b->p[i + 3]);
    }
    CHECK(0);
    return 0;
}

static void test_crafted_headers(void)
{
    blob_t b = load("enc_440", ".jpg");
    size_t s = sof_at(&b);
    pc_jpeg_info_t in;
    uint16_t *a = NULL, *c = NULL;
    int aw, ah, cw, ch;
    CHECK_EQ_INT(pc_jpeg_decode(b.p, b.n, 0, NULL, &a, &aw, &ah), PC_JPEG_OK);
    b.p[s + 1] = 0xC1;                                       /* extended sequential, Huffman, 8 bit: the same data */
    CHECK_EQ_INT(pc_jpeg_probe(b.p, b.n, &in), PC_JPEG_OWN);
    CHECK_EQ_INT(pc_jpeg_decode(b.p, b.n, 0, NULL, &c, &cw, &ch), PC_JPEG_OK);
    CHECK(aw == cw && ah == ch && !memcmp(a, c, (size_t)aw * ah * 2));
    free(a); free(c);
    static const uint8_t other[] = { 0xC3, 0xC5, 0xC9, 0xCA, 0xCB, 0xCD, 0xCE, 0xCF };
    for (size_t k = 0; k < sizeof other; k++) {
        b.p[s + 1] = other[k];
        CHECK_EQ_INT(pc_jpeg_probe(b.p, b.n, &in), PC_JPEG_UNSUPPORTED);
        CHECK_EQ_INT(pc_jpeg_decode(b.p, b.n, 0, NULL, &a, &aw, &ah), PC_JPEG_E_UNSUPPORTED);
    }
    b.p[s + 1] = 0xC0;
    b.p[s + 4] = 12;                                         /* 12-bit samples */
    CHECK_EQ_INT(pc_jpeg_probe(b.p, b.n, &in), PC_JPEG_UNSUPPORTED);
    b.p[s + 4] = 8;
    b.p[s + 11] = 0x31;                                      /* luma 3x1 with 1x1 chroma: allowed (3 divides 3) */
    CHECK_EQ_INT(pc_jpeg_probe(b.p, b.n, &in), PC_JPEG_OWN);
    b.p[s + 14] = 0x21;                                      /* chroma 2x1 against luma 3: no integer ratio */
    CHECK_EQ_INT(pc_jpeg_probe(b.p, b.n, &in), PC_JPEG_UNSUPPORTED);
    b.p[s + 11] = 0x00;                                      /* a zero factor */
    CHECK_EQ_INT(pc_jpeg_probe(b.p, b.n, &in), PC_JPEG_BAD);
    b.p[s + 11] = 0x12; b.p[s + 14] = 0x11;
    CHECK_EQ_INT(pc_jpeg_probe(b.p, b.n, &in), PC_JPEG_OWN);
    CHECK_EQ_INT(pc_jpeg_probe(b.p, s + 6, &in), PC_JPEG_BAD);   /* cut inside the frame header */
    CHECK_EQ_INT(pc_jpeg_probe(b.p + 2, b.n - 2, &in), PC_JPEG_BAD);   /* no SOI */
    free(b.p);
    static const uint8_t junk[] = { 0xFF, 0xD8, 0xFF, 0xD9 }, tiny[] = { 0xFF };
    CHECK_EQ_INT(pc_jpeg_probe(junk, sizeof junk, &in), PC_JPEG_BAD);
    CHECK_EQ_INT(pc_jpeg_probe(tiny, sizeof tiny, &in), PC_JPEG_BAD);
    CHECK_EQ_INT(pc_jpeg_probe(NULL, 0, &in), PC_JPEG_BAD);
    CHECK_EQ_INT(pc_jpeg_decode(junk, sizeof junk, 0, NULL, &a, &aw, &ah), PC_JPEG_E_FORMAT);
    /* a CMYK frame (4 components), crafted: refused with a reason */
    static const uint8_t cmyk[] = { 0xFF, 0xD8, 0xFF, 0xC0, 0x00, 0x14, 8, 0, 16, 0, 16, 4, 1, 0x11, 0, 2, 0x11, 0, 3, 0x11, 0, 4, 0x11, 0, 0xFF, 0xD9 };
    CHECK_EQ_INT(pc_jpeg_probe(cmyk, sizeof cmyk, &in), PC_JPEG_UNSUPPORTED);
    CHECK_EQ_INT(in.ncomp, 4);
    blob_t g = load("pil_gray", ".jpg");
    s = sof_at(&g);
    CHECK_EQ_INT(pc_jpeg_probe(g.p, g.n, &in), PC_JPEG_NATIVE);
    g.p[s + 9] = 3;                                          /* three components in a header long enough for one */
    CHECK_EQ_INT(pc_jpeg_probe(g.p, g.n, &in), PC_JPEG_BAD);
    free(g.p);
}

/* a sequential file in several scans (one per component): refused with a reason, not decoded wrong */
static void test_multi_scan_refused(void)
{
    blob_t b = load("enc_444", ".jpg");
    for (size_t i = 2; i + 4 < b.n;) {
        uint8_t m = b.p[i + 1];
        size_t len = (size_t)b.p[i + 2] << 8 | b.p[i + 3];
        if (m == 0xDA) { b.p[i + 4] = 1; break; }           /* the scan now names one component */
        i += 2 + len;
    }
    pc_jpeg_info_t in;
    CHECK_EQ_INT(pc_jpeg_probe(b.p, b.n, &in), PC_JPEG_UNSUPPORTED);
    CHECK_EQ_INT(in.scan_comps, 1);
    free(b.p);
}

static void test_shift_for(void)
{
    CHECK_EQ_INT(pc_jpeg_shift_for(203, 141, 0), 3);
    CHECK_EQ_INT(pc_jpeg_shift_for(500, 500, 240), 1);
    CHECK_EQ_INT(pc_jpeg_shift_for(479, 1000, 240), 0);
    CHECK_EQ_INT(pc_jpeg_shift_for(1920, 1080, 240), 2);
    CHECK_EQ_INT(pc_jpeg_shift_for(4000, 3000, 240), 3);
    CHECK_EQ_INT(pc_jpeg_shift_for(100, 100, 240), 0);
}

/* truncated and damaged files: never a crash or a read outside the buffer (ASan), always a result or an error */
static void test_damaged(void)
{
    uint32_t seed = 12345;
    for (int i = 0; i < s_n; i++) {
        if (!strcmp(s_items[i].route, "progressive")) continue;
        blob_t b = load(s_items[i].name, ".jpg");
        for (int cut = 1; cut <= 12; cut++) {
            size_t n = b.n * (size_t)cut / 13;
            uint8_t *t = malloc(n ? n : 1);
            memcpy(t, b.p, n);
            uint16_t *px = NULL;
            int ow, oh, rc = pc_jpeg_decode(t, n, 1, NULL, &px, &ow, &oh);
            CHECK(rc == PC_JPEG_OK ? px != NULL : px == NULL);
            free(px);
            free(t);
        }
        for (int k = 0; k < 60; k++) {
            uint8_t *t = malloc(b.n);
            memcpy(t, b.p, b.n);
            for (int j = 0; j < 4; j++) { seed = seed * 1103515245u + 12345u; t[(seed >> 8) % b.n] ^= (uint8_t)(seed >> 3 | 1); }
            pc_jpeg_info_t in;
            (void)pc_jpeg_probe(t, b.n, &in);
            uint16_t *px = NULL;
            int ow, oh, rc = pc_jpeg_decode(t, b.n, k & 3, NULL, &px, &ow, &oh);
            CHECK(rc == PC_JPEG_OK ? px != NULL : px == NULL);
            free(px);
            free(t);
        }
        free(b.p);
    }
}

/* every allocation goes through mem; any one failing is E_NOMEM with nothing left allocated */
static int s_live, s_calls, s_fail_at;
static void *t_alloc(size_t n) { if (++s_calls == s_fail_at) return NULL; s_live++; return malloc(n); }
static void t_free(void *p) { if (p) s_live--; free(p); }
/* night3 B: the poll hook runs after every MCU and every converted output row, and changes nothing in the picture */
static int s_polls;
static void t_poll(void) { s_polls++; }
static void test_poll_hook(void)
{
    blob_t b = load("enc_440", ".jpg");
    pc_jpeg_mem_t none = { t_alloc, t_free, NULL }, hook = { t_alloc, t_free, t_poll };
    uint16_t *a = NULL, *c = NULL;
    int ow, oh, ow2, oh2;
    s_calls = 0; s_fail_at = -1; s_live = 0; s_polls = 0;
    CHECK_EQ_INT(pc_jpeg_decode(b.p, b.n, 0, &none, &a, &ow, &oh), PC_JPEG_OK);
    CHECK_EQ_INT(s_polls, 0);
    CHECK_EQ_INT(pc_jpeg_decode(b.p, b.n, 0, &hook, &c, &ow2, &oh2), PC_JPEG_OK);
    CHECK(s_polls >= oh / 8);                                /* at least one per MCU row */
    CHECK(ow == ow2 && oh == oh2 && !memcmp(a, c, (size_t)ow * oh * 2));
    t_free(a); t_free(c);
}

static void test_alloc_failures(void)
{
    blob_t b = load("enc_440", ".jpg");
    pc_jpeg_mem_t mem = { t_alloc, t_free, NULL };
    s_calls = 0; s_fail_at = -1;
    uint16_t *px = NULL;
    int ow, oh;
    CHECK_EQ_INT(pc_jpeg_decode(b.p, b.n, 0, &mem, &px, &ow, &oh), PC_JPEG_OK);
    int total = s_calls;
    CHECK(total >= 2);
    CHECK_EQ_INT(s_live, 1);                                 /* only the picture */
    t_free(px);
    for (int f = 1; f <= total; f++) {
        s_calls = 0; s_fail_at = f; s_live = 0;
        px = (uint16_t *)1;
        CHECK_EQ_INT(pc_jpeg_decode(b.p, b.n, 0, &mem, &px, &ow, &oh), PC_JPEG_E_NOMEM);
        CHECK(px == NULL);
        CHECK_EQ_INT(s_live, 0);
    }
    free(b.p);
}

/* the SOF's height and width patched */
static void patch_dims(blob_t *b, int w, int h)
{
    size_t s = sof_at(b);
    b->p[s + 5] = (uint8_t)(h >> 8); b->p[s + 6] = (uint8_t)h; b->p[s + 7] = (uint8_t)(w >> 8); b->p[s + 8] = (uint8_t)w;
}
static double now_s(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }

/* review I-1: a few-KB file in a layout only our decoder takes, declaring a huge picture, is refused at once (too_big, marked bad, never
 * tried again); a common layout keeps its route (fix round 2: esp_new_jpeg scales a 48 Mpx photo as before 8d) and only our decoder refuses
 * it, also at once; one inside the cap whose data runs out does not run the transform for every missing block */
static void test_size_cap(void)
{
    const char *names[] = { "enc_440", "pil_420" };
    for (int f = 0; f < 2; f++) {
        blob_t b = load(names[f], ".jpg");
        static const int dims[][2] = { { 16000, 16000 }, { 65535, 65535 }, { 8193, 100 }, { 6001, 6000 } };
        bool common = f == 1;                               /* pil_420: the chip's decoders scale while they decode - no cap on the route (fix round 2) */
        for (int i = 0; i < 4; i++) {
            patch_dims(&b, dims[i][0], dims[i][1]);
            pc_jpeg_info_t in;
            CHECK_EQ_INT(pc_jpeg_probe(b.p, b.n, &in), common ? PC_JPEG_NATIVE : PC_JPEG_TOO_BIG);
            uint16_t *px = (uint16_t *)1;
            int ow, oh;
            double t0 = now_s();
            CHECK_EQ_INT(pc_jpeg_decode(b.p, b.n, 3, NULL, &px, &ow, &oh), PC_JPEG_E_TOO_BIG);
            CHECK(px == NULL && now_s() - t0 < 0.05);
        }
        CHECK_STR(pc_jpeg_route_name(PC_JPEG_TOO_BIG), "too_big");
        patch_dims(&b, 8000, 6000);                          /* a 48 Mpx phone photo */
        pc_jpeg_info_t in48;
        CHECK_EQ_INT(pc_jpeg_probe(b.p, b.n, &in48), common ? PC_JPEG_NATIVE : PC_JPEG_TOO_BIG);
        patch_dims(&b, 6000, 6000);                          /* inside the cap: the data ends in the first MCU row */
        pc_jpeg_info_t in;
        CHECK(pc_jpeg_probe(b.p, b.n, &in) <= PC_JPEG_OWN);
        uint16_t *px = (uint16_t *)1;
        int ow, oh;
        double t0 = now_s();
        CHECK_EQ_INT(pc_jpeg_decode(b.p, b.n, pc_jpeg_shift_for(6000, 6000, 240), NULL, &px, &ow, &oh), PC_JPEG_E_FORMAT);
        CHECK(px == NULL);
        double dt = now_s() - t0;
        printf("  %s 6000x6000 with %zu bytes of data: %.3f s\n", names[f], b.n, dt);
        CHECK(dt < 1.0);
        patch_dims(&b, 400, 3000);                           /* a few rows decode, the rest is missing: grey, fast */
        t0 = now_s();
        CHECK_EQ_INT(pc_jpeg_decode(b.p, b.n, 0, NULL, &px, &ow, &oh), PC_JPEG_OK);
        dt = now_s() - t0;
        CHECK(px[(size_t)(oh - 1) * ow + ow / 2] == (uint16_t)((128 >> 3) << 11 | (128 >> 2) << 5 | 128 >> 3));
        free(px);
        CHECK(dt < 1.0);
        free(b.p);
    }
}

/* review M-1: the DC prediction of a crafted file (16-bit quantiser 65535, +2047 every block) stays in int (UBSan in the ASan build) */
static void test_dc_overflow(void)
{
    uint8_t f[1200];
    size_t n = 0;
#define PUT(...) do { const uint8_t _b[] = { __VA_ARGS__ }; memcpy(f + n, _b, sizeof _b); n += sizeof _b; } while (0)
    PUT(0xFF, 0xD8, 0xFF, 0xDB, 0x00, 131, 0x10);
    for (int i = 0; i < 64; i++) PUT(0xFF, 0xFF);
    PUT(0xFF, 0xC0, 0, 11, 8, 0, 8, 2, 0, 1, 1, 0x11, 0);          /* 512 x 8 gray */
    PUT(0xFF, 0xC4, 0, 20, 0x00, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 11);
    PUT(0xFF, 0xC4, 0, 20, 0x10, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    PUT(0xFF, 0xDA, 0, 8, 1, 1, 0x00, 0, 63, 0);
    uint64_t acc = 0;
    int nb = 0;
    for (int blk = 0; blk < 64; blk++)                          /* "0" (DC category 11), 2047, "0" (end of block): 13 bits */
        for (int i = 0; i < 13; i++) {
            acc = acc << 1 | (i >= 1 && i <= 11);
            if (++nb == 8) { f[n++] = (uint8_t)acc; if ((uint8_t)acc == 0xFF) f[n++] = 0; acc = 0; nb = 0; }
        }
    if (nb) { f[n++] = (uint8_t)(acc << (8 - nb) | ((1u << (8 - nb)) - 1)); }
    PUT(0xFF, 0xD9);
    CHECK(n < sizeof f);
    pc_jpeg_info_t in;
    CHECK_EQ_INT(pc_jpeg_probe(f, n, &in), PC_JPEG_NATIVE);
    uint16_t *px = NULL;
    int ow, oh;
    CHECK_EQ_INT(pc_jpeg_decode(f, n, 0, NULL, &px, &ow, &oh), PC_JPEG_OK);
    CHECK_EQ_INT(ow, 512);
    free(px);
#undef PUT
}

/* review M-5: a component id given twice is not a frame header */
static void test_duplicate_ids(void)
{
    blob_t b = load("pil_444", ".jpg");
    size_t s = sof_at(&b);
    b.p[s + 13] = b.p[s + 10];
    pc_jpeg_info_t in;
    CHECK_EQ_INT(pc_jpeg_probe(b.p, b.n, &in), PC_JPEG_BAD);
    uint16_t *px;
    int ow, oh;
    CHECK_EQ_INT(pc_jpeg_decode(b.p, b.n, 0, NULL, &px, &ow, &oh), PC_JPEG_E_FORMAT);
    free(b.p);
}

/* review M-2: damage in the middle of a restart-coded file costs only up to the next restart marker; damage in the first MCU row is a
 * bad picture (not an all-grey cover) */
static void test_restart_resync(void)
{
    blob_t b = load("enc_420_rst", ".jpg"), ref = load("enc_420_rst", ".rgb");
    size_t sos = 0;
    for (size_t i = 2; i + 4 < b.n;) {
        size_t len = (size_t)b.p[i + 2] << 8 | b.p[i + 3];
        if (b.p[i + 1] == 0xDA) { sos = i + 2 + len; break; }
        i += 2 + len;
    }
    CHECK(sos);
    size_t mid = sos + (b.n - sos) * 45 / 100;
    while (b.p[mid - 1] == 0xFF) mid++;
    size_t end = mid;
    while (end < mid + 8 && b.p[end] != 0xFF && b.p[end + 1] != 0xFF) { b.p[end++] = 0xFF; b.p[end++] = 0x00; }   /* stuffed 0xFF bytes: 16+ ones, no valid code */
    CHECK(end >= mid + 4);
    uint16_t *px = NULL;
    int ow, oh;
    CHECK_EQ_INT(pc_jpeg_decode(b.p, b.n, 0, NULL, &px, &ow, &oh), PC_JPEG_OK);
    int half = oh / 2 + 16;                                   /* well past the damage */
    double e = mae(px + (size_t)(half + 16) * ow, ow, oh - half - 16, ref.p + (size_t)(half + 16) * ow * 3, ow, 0);
    printf("  enc_420_rst damaged at 45%%: mae of the bottom rows %.2f\n", e);
    CHECK(e < 4.0);
    free(px);
    free(b.p);
    b = load("ff_444", ".jpg");
    for (size_t i = 2; i + 4 < b.n;) {
        size_t len = (size_t)b.p[i + 2] << 8 | b.p[i + 3];
        if (b.p[i + 1] == 0xDA) { sos = i + 2 + len; break; }
        i += 2 + len;
    }
    CHECK_EQ_INT(pc_jpeg_decode(b.p, sos + 12, 0, NULL, &px, &ow, &oh), PC_JPEG_E_FORMAT);   /* 12 bytes of data */
    CHECK(px == NULL);
    memset(b.p + sos, 0xA5, 64);                              /* garbage from the first block on */
    int rc = pc_jpeg_decode(b.p, b.n, 0, NULL, &px, &ow, &oh);
    CHECK(rc == PC_JPEG_E_FORMAT || rc == PC_JPEG_OK);
    free(px);
    free(b.p);
    free(ref.p);
}

/* fix round 3 (re-review N-1): the engine's output size and the smallest possible output, from crafted headers, in 64 bits - on the
 * 32-bit S31 65535 x 32769 wrapped a size_t product below the 8 MB limit */
static void test_hw_sizes(void)
{
    static const struct { const char *name; uint32_t bpp, mx, my; } f[] = { { "pil_444", 2, 8, 8 }, { "pil_420", 2, 16, 16 }, { "pil_422", 2, 16, 8 }, { "pil_gray", 1, 8, 8 } };
    for (int i = 0; i < 4; i++) {
        blob_t b = load(f[i].name, ".jpg");
        pc_jpeg_info_t in;
        uint32_t pw, ph;
        CHECK_EQ_INT(pc_jpeg_probe(b.p, b.n, &in), PC_JPEG_NATIVE);
        uint64_t n = pc_jpeg_hw_bytes(&in, &pw, &ph);
        CHECK_EQ_INT(pw, (203 + f[i].mx - 1) / f[i].mx * f[i].mx);
        CHECK_EQ_INT(ph, (141 + f[i].my - 1) / f[i].my * f[i].my);
        CHECK(n == (uint64_t)pw * ph * f[i].bpp);
        CHECK(pc_jpeg_min_bytes(&in) == (uint64_t)(203 / 8) * (141 / 8) * 2);
        patch_dims(&b, 65535, 32769);                        /* the re-review's header */
        CHECK_EQ_INT(pc_jpeg_probe(b.p, b.n, &in), PC_JPEG_NATIVE);   /* the route stays (the chip's decoders scale), ... */
        n = pc_jpeg_hw_bytes(&in, &pw, &ph);
        uint64_t want = (uint64_t)((65535 + f[i].mx - 1) / f[i].mx * f[i].mx) * ((32769 + f[i].my - 1) / f[i].my * f[i].my) * f[i].bpp;
        CHECK(n == want);
        CHECK(n > PC_JPEG_BUDGET_BYTES);
        if (f[i].bpp == 2) CHECK((uint32_t)want < PC_JPEG_BUDGET_BYTES);   /* ... where a 32-bit product wraps below the limit */
        CHECK(pc_jpeg_min_bytes(&in) > PC_JPEG_BUDGET_BYTES);   /* never fits: too_big, not "memory" */
        patch_dims(&b, 8000, 6000);                          /* a 48 Mpx photo: too big for the engine, fine scaled */
        CHECK_EQ_INT(pc_jpeg_probe(b.p, b.n, &in), PC_JPEG_NATIVE);
        CHECK(pc_jpeg_hw_bytes(&in, &pw, &ph) > PC_JPEG_BUDGET_BYTES);
        CHECK(pc_jpeg_min_bytes(&in) <= PC_JPEG_BUDGET_BYTES);
        free(b.p);
    }
}

/* fix round 3 (re-review N-3): damage early in the first MCU row that a restart marker in the same row recovers is not a bad picture */
static void test_row0_recovered(void)
{
    blob_t b = load("enc_420_rst", ".jpg");
    size_t sos = 0;
    for (size_t i = 2; i + 4 < b.n;) {
        size_t len = (size_t)b.p[i + 2] << 8 | b.p[i + 3];
        if (b.p[i + 1] == 0xDA) { sos = i + 2 + len; break; }
        i += 2 + len;
    }
    size_t mid = sos + 2;                                    /* inside the first restart interval (MCUs 0..2 of 13 in the row) */
    while (b.p[mid - 1] == 0xFF) mid++;
    size_t end = mid;
    while (end < mid + 6 && b.p[end] != 0xFF && b.p[end + 1] != 0xFF) { b.p[end++] = 0xFF; b.p[end++] = 0x00; }
    CHECK(end >= mid + 4);
    uint16_t *px = NULL;
    int ow, oh;
    CHECK_EQ_INT(pc_jpeg_decode(b.p, b.n, 0, NULL, &px, &ow, &oh), PC_JPEG_OK);
    CHECK(px != NULL);
    free(px);
    free(b.p);
}

int main(void)
{
    read_manifest();
    RUN(test_routes);
    RUN(test_board_layouts);
    RUN(test_decode_all);
    RUN(test_progressive_refused);
    RUN(test_crafted_headers);
    RUN(test_multi_scan_refused);
    RUN(test_shift_for);
    RUN(test_damaged);
    RUN(test_poll_hook);
    RUN(test_alloc_failures);
    RUN(test_size_cap);
    RUN(test_dc_overflow);
    RUN(test_duplicate_ids);
    RUN(test_restart_resync);
    RUN(test_hw_sizes);
    RUN(test_row0_recovered);
    return 0;
}
