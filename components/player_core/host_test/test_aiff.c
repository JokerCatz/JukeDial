// SPDX-License-Identifier: LGPL-2.1-or-later
/* AIFF / AIFF-C backend: every file is synthesised in memory (no audio in git). */
#include <math.h>
#include <stdbool.h>
#include "pc/pc_backend.h"
#include "pc/pc_io.h"
#include "pc_test.h"

typedef struct { uint8_t *b; uint32_t n, cap; } buf_t;
static void put(buf_t *w, const void *p, uint32_t n)
{
    if (w->n + n > w->cap) { w->cap = (w->n + n) * 2; w->b = realloc(w->b, w->cap); CHECK(w->b != NULL); }
    memcpy(w->b + w->n, p, n);
    w->n += n;
}
static void put16(buf_t *w, uint16_t v) { uint8_t t[2] = { (uint8_t)(v >> 8), (uint8_t)v }; put(w, t, 2); }
static void put32(buf_t *w, uint32_t v) { put16(w, (uint16_t)(v >> 16)); put16(w, (uint16_t)v); }
/* 80-bit extended for an integer rate */
static void put80(buf_t *w, uint32_t rate)
{
    int e = 31;
    while (e > 0 && !(rate >> e & 1)) e--;
    uint64_t m = (uint64_t)rate << (63 - e);
    put16(w, (uint16_t)(0x3FFF + e));
    put32(w, (uint32_t)(m >> 32));
    put32(w, (uint32_t)m);
}

typedef struct { bool aifc; const char *comp; uint16_t ch, bits; uint32_t rate; int frames; uint32_t ssnd_pad; } spec_t;
/* sample idx (interleaved) written as an int of the container width, big- or little-endian */
static void put_sample(buf_t *w, int bits, int32_t v, bool le)
{
    int by = (bits + 7) / 8;
    uint8_t t[8] = { 0 };
    for (int i = 0; i < by && i < 4; i++) t[le ? i : by - 1 - i] = (uint8_t)((uint32_t)v >> (8 * i));
    put(w, t, (uint32_t)by);
}
static int32_t ramp(int i, int bits) { int by = (bits + 7) / 8; if (by > 4) by = 4; return (int32_t)(((int64_t)(i % 200 - 100) * ((1ll << (8 * by - 1)) - 1)) / 100); }

static uint8_t *build(const spec_t *s, uint32_t *len)
{
    buf_t w = { 0 };
    bool le = s->comp && !memcmp(s->comp, "sowt", 4);
    put(&w, "FORM", 4); put32(&w, 0); put(&w, s->aifc ? "AIFC" : "AIFF", 4);
    if (s->aifc) { put(&w, "FVER", 4); put32(&w, 4); put32(&w, 0xA2805140u); }
    put(&w, "COMM", 4); put32(&w, s->aifc ? 24 : 18);
    put16(&w, s->ch); put32(&w, (uint32_t)s->frames); put16(&w, s->bits); put80(&w, s->rate);
    if (s->aifc) { put(&w, s->comp, 4); put(&w, "\0\0", 2); }
    uint32_t by = (uint32_t)(s->bits + 7) / 8, bytes = (uint32_t)s->frames * s->ch * by;
    put(&w, "SSND", 4); put32(&w, 8 + s->ssnd_pad + bytes); put32(&w, s->ssnd_pad); put32(&w, 0);
    for (uint32_t i = 0; i < s->ssnd_pad; i++) put(&w, "\x55", 1);
    for (int i = 0; i < s->frames * s->ch; i++) put_sample(&w, s->bits, ramp(i, s->bits), le);
    *len = w.n;
    return w.b;
}

static int play_all(const pc_backend_t *be, void *h, float *out, int max)
{
    int n = 0;
    while (n < max) {
        int g = be->render(h, out + 2 * n, max - n < 100 ? max - n : 100);
        CHECK(g >= 0);
        if (!g) break;
        n += g;
    }
    return n;
}

static void check_ramp(const spec_t *s, uint32_t start_off, int first_frame)
{
    uint32_t len;
    uint8_t *d = build(s, &len);
    pc_reader_t r;
    pc_reader_mem(&r, d, len);
    void *h;
    CHECK_EQ_INT(pc_backend_aiff.open(&h, &r, start_off), 0);
    CHECK_EQ_INT(pc_backend_aiff.rate(h), s->rate);
    CHECK_EQ_INT(pc_backend_aiff.bits(h), s->bits);
    CHECK_EQ_INT(pc_backend_aiff.length_ms(h), (int64_t)s->frames * 1000 / s->rate);
    float *out = malloc(sizeof(float) * 2 * (size_t)s->frames);
    int n = play_all(&pc_backend_aiff, h, out, s->frames);
    CHECK_EQ_INT(n, s->frames - first_frame);
    float full = (float)(1ll << (8 * ((s->bits + 7) / 8) - 1));
    for (int i = 0; i < n; i++) {
        int f = first_frame + i;
        float l = (float)ramp(f * s->ch, s->bits) / full;
        float rr = s->ch > 1 ? (float)ramp(f * s->ch + 1, s->bits) / full : l;
        CHECK_NEAR(out[2 * i], l, 1e-6);
        CHECK_NEAR(out[2 * i + 1], rr, 1e-6);
    }
    CHECK_EQ_INT(pc_backend_aiff.position_ms(h), (int64_t)s->frames * 1000 / s->rate);
    pc_backend_aiff.close(h);
    free(out);
    free(d);
}

static void test_depths(void)
{
    static const uint16_t bits[] = { 8, 16, 24, 32 };
    for (int b = 0; b < 4; b++)
        for (int ch = 1; ch <= 2; ch++) {
            spec_t s = { .ch = (uint16_t)ch, .bits = bits[b], .rate = 44100, .frames = 1000 };
            check_ramp(&s, 0, 0);
        }
}

static void test_aifc_and_rates(void)
{
    spec_t s = { .aifc = true, .comp = "NONE", .ch = 2, .bits = 16, .rate = 48000, .frames = 500 };
    check_ramp(&s, 0, 0);
    s.comp = "sowt"; s.bits = 24; s.rate = 96000;
    check_ramp(&s, 0, 0);
    s.aifc = false; s.comp = NULL; s.rate = 176400; s.ssnd_pad = 6;     /* an SSND offset field: padding before the first frame */
    check_ramp(&s, 0, 0);
    s.rate = 22050; s.bits = 12;                                        /* a 12-bit sample size is left-justified in 16 bits */
    check_ramp(&s, 0, 0);
}

static void test_resume(void)
{
    spec_t s = { .ch = 2, .bits = 16, .rate = 44100, .frames = 1000 };
    uint32_t len;
    uint8_t *d = build(&s, &len);
    pc_reader_t r;
    pc_reader_mem(&r, d, len);
    void *h;
    CHECK_EQ_INT(pc_backend_aiff.open(&h, &r, 400 * 4 + 3), 0);        /* a token not on a frame boundary rounds down */
    CHECK_EQ_INT(pc_backend_aiff.byte_pos(h), 400 * 4);
    float *out = malloc(sizeof(float) * 2000);
    CHECK_EQ_INT(play_all(&pc_backend_aiff, h, out, 1000), 600);
    CHECK_NEAR(out[0], (double)ramp(400 * 2, 16) / 32768.0, 1e-6);
    CHECK_EQ_INT(pc_backend_aiff.byte_pos(h), 1000 * 4);
    pc_backend_aiff.close(h);
    CHECK_EQ_INT(pc_backend_aiff.open(&h, &r, 0x7fffffff), 0);         /* past the end: from the start, as MPC and TTA do */
    CHECK_EQ_INT(pc_backend_aiff.byte_pos(h), 0);
    CHECK_EQ_INT(play_all(&pc_backend_aiff, h, out, 1000), 1000);
    pc_backend_aiff.close(h);
    free(out);
    free(d);
}

/* the spec allows SSND before COMM (wave-6 review, LOW 22): the same ramp must play */
static void test_ssnd_before_comm(void)
{
    spec_t s = { .ch = 2, .bits = 16, .rate = 44100, .frames = 300 };
    uint32_t len;
    uint8_t *d = build(&s, &len);
    uint8_t *e = malloc(len);
    const uint32_t comm_len = 8 + 18;                                   /* the plain AIFF COMM chunk, right after the 12-byte FORM header */
    memcpy(e, d, 12);
    memcpy(e + 12, d + 12 + comm_len, len - 12 - comm_len);             /* SSND first */
    memcpy(e + len - comm_len, d + 12, comm_len);                       /* COMM last */
    pc_reader_t r;
    pc_reader_mem(&r, e, len);
    void *h;
    CHECK_EQ_INT(pc_backend_aiff.open(&h, &r, 0), 0);
    float *out = malloc(sizeof(float) * 2 * 300);
    CHECK_EQ_INT(play_all(&pc_backend_aiff, h, out, 300), 300);
    CHECK_NEAR(out[2 * 5], (double)ramp(5 * 2, 16) / 32768.0, 1e-6);
    pc_backend_aiff.close(h);
    free(out);
    memcpy(e + len - comm_len, "XXXX", 4);                              /* no COMM at all: refused */
    pc_reader_mem(&r, e, len);
    CHECK(pc_backend_aiff.open(&h, &r, 0) != 0);
    free(e);
    free(d);
}

static void test_refused(void)
{
    spec_t s = { .ch = 2, .bits = 16, .rate = 44100, .frames = 100 };
    uint32_t len;
    void *h;
    pc_reader_t r;
    uint8_t *d;
    s.aifc = true; s.comp = "ima4";                                     /* compressed */
    d = build(&s, &len); pc_reader_mem(&r, d, len);
    CHECK(pc_backend_aiff.open(&h, &r, 0) != 0); free(d);
    s.aifc = false; s.ch = 3;
    d = build(&s, &len); pc_reader_mem(&r, d, len);
    CHECK(pc_backend_aiff.open(&h, &r, 0) != 0); free(d);
    s.ch = 2; s.rate = 1000;
    d = build(&s, &len); pc_reader_mem(&r, d, len);
    CHECK(pc_backend_aiff.open(&h, &r, 0) != 0); free(d);
    s.rate = 44100; s.bits = 64;
    d = build(&s, &len); pc_reader_mem(&r, d, len);
    CHECK(pc_backend_aiff.open(&h, &r, 0) != 0); free(d);
    /* truncated file: the size fields lie; what the file holds plays, never past its end */
    s.bits = 16;
    d = build(&s, &len); pc_reader_mem(&r, d, len - 50);
    CHECK_EQ_INT(pc_backend_aiff.open(&h, &r, 0), 0);
    float out[2 * 200];
    CHECK_EQ_INT(play_all(&pc_backend_aiff, h, out, 200), (100 * 4 - 50) / 4);
    pc_backend_aiff.close(h); free(d);
    /* no SSND, junk, short */
    uint8_t junk[40] = "FORM";
    pc_reader_mem(&r, junk, sizeof junk);
    CHECK(pc_backend_aiff.open(&h, &r, 0) != 0);
    pc_reader_mem(&r, junk, 5);
    CHECK(pc_backend_aiff.open(&h, &r, 0) != 0);
}

int main(void)
{
    RUN(test_depths);
    RUN(test_aifc_and_rates);
    RUN(test_resume);
    RUN(test_ssnd_before_comm);
    RUN(test_refused);
    return 0;
}
