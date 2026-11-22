// SPDX-License-Identifier: LGPL-2.1-or-later
/* Musepack backend over libmpcdec: an SV7 stream of silent frames is built bit by bit in memory (no audio in git). SV7 = 32-bit little-endian
 * words read most significant bit first; a frame is a 20-bit size, then its bits. With max_band 1, no band used, a frame is 10 bits. */
#include "pc/pc_backend.h"
#include "pc/pc_io.h"
#include "pc_test.h"

typedef struct { uint8_t *b; uint32_t nbits, cap; } bits_t;
static void putbits(bits_t *w, uint32_t v, int n)
{
    for (int i = n - 1; i >= 0; i--) {
        if ((w->nbits >> 3) + 1 > w->cap) { w->cap = w->cap * 2 + 64; w->b = realloc(w->b, w->cap); CHECK(w->b != NULL); memset(w->b + w->nbits / 8, 0, w->cap - w->nbits / 8); }
        if (v >> i & 1) w->b[w->nbits >> 3] |= (uint8_t)(0x80 >> (w->nbits & 7));
        w->nbits++;
    }
}

static uint8_t *build_sv7(int nframes, int freq_idx, uint32_t last_samples, uint32_t *len)
{
    bits_t w = { 0 };
    putbits(&w, (uint32_t)nframes, 32);
    putbits(&w, 0, 1); putbits(&w, 0, 1); putbits(&w, 1, 6); putbits(&w, 0, 4); putbits(&w, 0, 2); putbits(&w, (uint32_t)freq_idx, 2);
    for (int i = 0; i < 5; i++) putbits(&w, 0, 16);                    /* peak, gains */
    putbits(&w, 1, 1); putbits(&w, last_samples, 11); putbits(&w, 0, 1); putbits(&w, 0, 19); putbits(&w, 100, 8);
    for (int f = 0; f < nframes; f++) {
        putbits(&w, 10, 20);
        putbits(&w, 0, 4); putbits(&w, 0, 4);                           /* band 0: resolution 0 on both channels */
        putbits(&w, 1, 1); putbits(&w, 1, 1);                           /* band 1: Huffman symbol '1' = +0 on both */
        if (f == nframes - 1) putbits(&w, last_samples, 11);            /* the last frame carries its sample count */
    }
    uint32_t bytes = (w.nbits + 7) / 8, words = (bytes + 3) / 4 + 4;    /* zero padding: the demuxer reads ahead */
    uint8_t *f = calloc(1, 4 + words * 4);
    memcpy(f, "MP+\x07", 4);
    for (uint32_t i = 0; i < words * 4; i++) {                          /* byte-swap every 32-bit group */
        uint32_t src = (i & ~3u) + (3 - (i & 3));
        f[4 + i] = src < w.cap ? w.b[src] : 0;
    }
    free(w.b);
    *len = 4 + words * 4;
    return f;
}

static void test_play(void)
{
    uint32_t len;
    uint8_t *d = build_sv7(20, 0, 600, &len);                           /* 19 full frames + 600 samples */
    pc_reader_t r;
    pc_reader_mem(&r, d, len);
    void *h;
    CHECK_EQ_INT(pc_backend_mpc.open(&h, &r, 0), 0);
    CHECK_EQ_INT(pc_backend_mpc.rate(h), 44100);
    int64_t total = 19 * 1152 + 600;
    CHECK_EQ_INT(pc_backend_mpc.length_ms(h), total * 1000 / 44100);
    float out[2 * 441];
    int64_t n = 0;
    for (;;) {
        int g = pc_backend_mpc.render(h, out, 441);
        CHECK(g >= 0);
        if (!g) break;
        for (int i = 0; i < 2 * g; i++) CHECK_NEAR(out[i], 0.0, 1e-6);
        n += g;
    }
    CHECK_EQ_INT(n, total);
    CHECK_EQ_INT(pc_backend_mpc.position_ms(h), total * 1000 / 44100);
    pc_backend_mpc.close(h);
    free(d);
}

static void test_resume_and_rates(void)
{
    static const int rates[4] = { 44100, 48000, 37800, 32000 };
    for (int fi = 0; fi < 4; fi++) {
        uint32_t len;
        uint8_t *d = build_sv7(41, fi, 481, &len)        /* the last frame holds the 481-sample synthesis delay's worth, as an encoder writes it */;
        pc_reader_t r;
        pc_reader_mem(&r, d, len);
        void *h;
        CHECK_EQ_INT(pc_backend_mpc.open(&h, &r, 20000), 0);
        CHECK_EQ_INT(pc_backend_mpc.rate(h), rates[fi]);
        CHECK_EQ_INT(pc_backend_mpc.byte_pos(h), 20000);
        float out[2 * 480];
        int64_t n = 0;
        for (;;) {
            int g = pc_backend_mpc.render(h, out, 480);
            CHECK(g >= 0);
            if (!g) break;
            n += g;
        }
        CHECK_EQ_INT(n, 40 * 1152 + 481 - 20000);
        pc_backend_mpc.close(h);
        free(d);
    }
}

static void test_bad(void)
{
    uint8_t junk[200];
    memset(junk, 0x5a, sizeof junk);
    pc_reader_t r;
    void *h;
    pc_reader_mem(&r, junk, sizeof junk);
    CHECK(pc_backend_mpc.open(&h, &r, 0) != 0);
    memcpy(junk, "MP+\x06", 4);                                          /* an old stream version */
    CHECK(pc_backend_mpc.open(&h, &r, 0) != 0);
    memcpy(junk, "MPCK", 4);                                            /* SV8 magic, garbage blocks */
    CHECK(pc_backend_mpc.open(&h, &r, 0) != 0);
    uint32_t len;
    uint8_t *d = build_sv7(5, 0, 1152, &len);
    for (uint32_t cut = 6; cut < len; cut += 7) {                       /* truncated at every few bytes: never a crash */
        pc_reader_mem(&r, d, cut);
        if (pc_backend_mpc.open(&h, &r, 0) == 0) {
            float out[2 * 441];
            while (pc_backend_mpc.render(h, out, 441) > 0) {}
            pc_backend_mpc.close(h);
        }
    }
    free(d);
}

/* ---- SV8 hostile streams (wave-6 review: HIGH 3, MED 8/9). Blocks are built the way mksv8 of the review did: key, size varint, payload. ---- */
typedef struct { uint8_t *b; size_t n; } buf_t;
static void bput(buf_t *w, const void *p, size_t n) { w->b = realloc(w->b, w->n + n + 1); CHECK(w->b != NULL); if (n) memcpy(w->b + w->n, p, n); w->n += n; }
static size_t vint(uint8_t *o, uint64_t v)
{
    uint8_t t[10];
    int k = 0;
    t[k++] = v & 0x7f;
    for (v >>= 7; v; v >>= 7) t[k++] = (uint8_t)(0x80 | (v & 0x7f));
    for (int i = 0; i < k; i++) o[i] = t[k - 1 - i];
    return (size_t)k;
}
static uint32_t crc32_std(const uint8_t *p, size_t n)
{
    uint32_t c = 0xffffffffu;
    for (size_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++) c = c & 1 ? (c >> 1) ^ 0xEDB88320u : c >> 1;
    }
    return ~c;
}
static void block(buf_t *w, const char *key, const uint8_t *pay, size_t n)
{
    uint8_t v[10];
    for (size_t hl = 3; hl < 12; hl++) {                /* the size field counts the header itself */
        size_t k = vint(v, n + hl);
        if (k + 2 == hl) { bput(w, key, 2); bput(w, v, k); bput(w, pay, n); return; }
    }
    CHECK(0);
}
static void sv8_sh(buf_t *w, uint64_t samples)
{
    uint8_t pay[40];
    size_t n = 0;
    pay[n++] = 8;
    n += vint(pay + n, samples);
    n += vint(pay + n, 0);
    pay[n++] = 0x00;                                    /* 44.1 kHz, max_band 1 */
    pay[n++] = 0x10;                                    /* 2 channels, no MS, block_pwr 0 */
    uint8_t blk[48];
    uint32_t c = crc32_std(pay, n);
    blk[0] = (uint8_t)(c >> 24); blk[1] = (uint8_t)(c >> 16); blk[2] = (uint8_t)(c >> 8); blk[3] = (uint8_t)c;
    memcpy(blk + 4, pay, n);
    block(w, "SH", blk, n + 4);
}
static void sv8_open_and_run(const uint8_t *d, size_t len, uint32_t start, int max_blocks)
{
    pc_reader_t r;
    pc_reader_mem(&r, d, (uint32_t)len);
    void *h = NULL;
    if (pc_backend_mpc.open(&h, &r, start) == 0) {
        float out[2 * 441];
        int g, guard = 0;
        while ((g = pc_backend_mpc.render(h, out, 441)) > 0 && ++guard < max_blocks) {}
        pc_backend_mpc.close(h);
    }
}

static void test_sv8_seek_table(void)
{
    /* ST claims 60000 entries but its block holds 8 bytes: the table reader walked off the demux buffer (heap-buffer-overflow); and a Golomb
     * run of zero bytes had no end. Opening may fail or succeed without a table, it must not touch anything outside the block. */
    static const uint8_t zeros[16] = { 0 };
    for (int variant = 0; variant < 3; variant++) {
        buf_t w = { 0 };
        bput(&w, "MPCK", 4);
        sv8_sh(&w, 1152ull * 64 * 60000);
        uint8_t st[40];
        size_t n = vint(st, variant == 2 ? 65536 : 60000);
        st[n++] = 0x60;
        memset(st + n, variant == 1 ? 0xff : 0, 8);
        block(&w, "ST", st, n + 8);
        block(&w, "AP", zeros, sizeof zeros);
        block(&w, "SE", NULL, 0);
        sv8_open_and_run(w.b, w.n, 0, 50);
        sv8_open_and_run(w.b, w.n, 100000, 50);          /* a resume seeks through the (dropped or short) table */
        free(w.b);
    }
}

static void test_sv8_zero_samples(void)
{
    /* samples = 0: the generic seek table has 2 entries and the decode never ends by count: the append past them overflowed after ~128 frames */
    static const uint8_t ap[16] = { 0 };
    for (size_t apn = 1; apn <= 4; apn++) {              /* the frame is a few bits: one of these block lengths is exactly a silent frame */
        buf_t w = { 0 };
        bput(&w, "MPCK", 4);
        sv8_sh(&w, 0);
        for (int i = 0; i < 600; i++) block(&w, "AP", ap, apn);
        block(&w, "SE", NULL, 0);
        sv8_open_and_run(w.b, w.n, 0, 100000);
        free(w.b);
    }
}

static void test_sv7_requant_bounds(void)
{
    /* SV7 band resolutions are delta coded: a stream that walks them outside -1..17 indexed Cc[] / Dc[] (19 entries) out of bounds. Random frame
     * bytes (a fixed seed) hit it within a few hundred frames; under ASan/UBSan any out-of-range read fails the run. */
    uint32_t len;
    uint8_t *d = build_sv7(60, 0, 1152, &len);
    uint32_t seed = 12345;
    for (int round = 0; round < 300; round++) {
        uint8_t *m = malloc(len);
        memcpy(m, d, len);
        for (uint32_t i = 20; i < len; i++) { seed = seed * 1664525u + 1013904223u; if ((seed >> 24) < 90) m[i] = (uint8_t)(seed >> 16); }
        pc_reader_t r;
        pc_reader_mem(&r, m, len);
        void *h;
        if (pc_backend_mpc.open(&h, &r, 0) == 0) {
            float out[2 * 441];
            int guard = 0;
            while (pc_backend_mpc.render(h, out, 441) > 0 && ++guard < 400) {}
            pc_backend_mpc.close(h);
        }
        free(m);
    }
    free(d);
}

int main(void)
{
    RUN(test_sv8_seek_table);
    RUN(test_sv8_zero_samples);
    RUN(test_sv7_requant_bounds);
    RUN(test_play);
    RUN(test_resume_and_rates);
    RUN(test_bad);
    return 0;
}
