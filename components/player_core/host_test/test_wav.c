// SPDX-License-Identifier: LGPL-2.1-or-later
#include <stdbool.h>
#include <math.h>
#include "pc/pc_backend.h"
#include "pc/pc_io.h"
#include "pc_test.h"

/* ---- builders: every WAV here is synthesised in memory ---- */
typedef struct { uint8_t *b; uint32_t n, cap; } buf_t;
static void put(buf_t *w, const void *p, uint32_t n)
{
    if (w->n + n > w->cap) { w->cap = (w->n + n) * 2; w->b = realloc(w->b, w->cap); CHECK(w->b != NULL); }
    memcpy(w->b + w->n, p, n);
    w->n += n;
}
static void put16(buf_t *w, uint16_t v) { uint8_t t[2] = { (uint8_t)v, (uint8_t)(v >> 8) }; put(w, t, 2); }
static void put32(buf_t *w, uint32_t v) { put16(w, (uint16_t)v); put16(w, (uint16_t)(v >> 16)); }
static void riff(buf_t *w) { put(w, "RIFF", 4); put32(w, 0); put(w, "WAVE", 4); }
/* KSDATAFORMAT_SUBTYPE_xxx: <sub> 00 00 | 00 00 10 00 80 00 00 AA 00 38 9B 71 (the 14 bytes after the 16-bit sub-format) */
static void guid(buf_t *w, uint16_t sub, bool good)
{
    uint8_t t[14] = { 0, 0, 0, 0, 0x10, 0, 0x80, 0, 0, 0xAA, 0, 0x38, 0x9B, 0x71 };
    if (!good) t[4] = 0x55;
    put16(w, sub);
    put(w, t, 14);
}
static void fmt_chunk(buf_t *w, uint16_t tag, uint16_t ch, uint32_t rate, uint16_t bits, uint16_t balign, uint16_t sub, bool goodguid)
{
    bool ext = tag == 0xFFFE;
    put(w, "fmt ", 4);
    put32(w, ext ? 40 : 16);
    put16(w, tag); put16(w, ch); put32(w, rate); put32(w, rate * balign); put16(w, balign); put16(w, bits);
    if (ext) { put16(w, 22); put16(w, bits); put32(w, 3); guid(w, sub, goodguid); }
}
static void data_head(buf_t *w, uint32_t size) { put(w, "data", 4); put32(w, size); }

typedef struct { uint16_t tag, ch, bits, balign, sub; uint32_t rate; int frames; bool goodguid; uint32_t data_size; } spec_t;
static uint8_t *build(const spec_t *s, uint32_t *len, uint32_t *data_off)
{
    buf_t w = { 0 };
    riff(&w);
    fmt_chunk(&w, s->tag, s->ch, s->rate, s->bits, s->balign, s->sub, s->goodguid);
    uint32_t bytes = (uint32_t)s->frames * s->balign;
    data_head(&w, s->data_size ? s->data_size : bytes);
    *data_off = w.n;
    uint8_t *z = calloc(1, bytes + 1);
    put(&w, z, bytes);
    free(z);
    *len = w.n;
    return w.b;
}
/* the brief's helper: header + zeroed frames (the tests write their ramp with set_s) */
static uint8_t *wav_make(uint16_t tag, uint16_t ch, uint32_t rate, uint16_t bits, int frames, bool extensible, uint32_t *len, uint32_t *data_off)
{
    spec_t s = { .tag = extensible ? 0xFFFE : tag, .ch = ch, .bits = bits, .balign = (uint16_t)(ch * bits / 8), .rate = rate, .frames = frames,
                 .sub = tag, .goodguid = true };
    return build(&s, len, data_off);
}
static void set_i(uint8_t *d, int idx, int bits, int32_t v)   /* sample idx (interleaved) as little-endian int */
{
    uint32_t u = (uint32_t)v;
    for (int k = 0; k < bits / 8; k++) d[idx * bits / 8 + k] = (uint8_t)(u >> (8 * k));
}
static void set_f(uint8_t *d, int idx, float v) { memcpy(d + idx * 4, &v, 4); }

static void *open_mem(const uint8_t *b, uint32_t len, pc_reader_t *r, uint32_t start, int *rc)
{
    void *h = NULL;
    pc_reader_mem(r, b, len);
    *rc = pc_backend_wav.open(&h, r, start);
    return *rc == 0 ? h : NULL;
}

static const int32_t V16[4] = { 0, 1, 32767, -32768 };
static const int32_t V24[4] = { 0x7FFFFF, -0x800000, 1, -1 };

static void test_wav_16(void)
{
    uint32_t len, off;
    uint8_t *b = wav_make(1, 2, 44100, 16, 44100, false, &len, &off);
    for (int i = 0; i < 44100 * 2; i++) set_i(b + off, i, 16, V16[(i + (i >> 3)) & 3]);
    pc_reader_t r; int rc;
    void *h = open_mem(b, len, &r, 0, &rc);
    CHECK(rc == 0 && h);
    CHECK(pc_backend_wav.rate(h) == 44100);
    CHECK(pc_backend_wav.length_ms(h) == 1000);
    CHECK(pc_backend_wav.position_ms(h) == 0);
    float out[2 * 100];
    int frames = 0;
    for (;;) {
        int n = pc_backend_wav.render(h, out, 100);
        CHECK(n >= 0);
        if (!n) break;
        for (int i = 0; i < 2 * n; i++) {
            int idx = frames * 2 + i;
            CHECK(out[i] == (float)V16[(idx + (idx >> 3)) & 3] / 32768.0f);
        }
        frames += n;
    }
    CHECK(frames == 44100);
    CHECK(pc_backend_wav.position_ms(h) == 1000);
    CHECK(pc_backend_wav.byte_pos(h) == 44100u * 4);
    pc_backend_wav.close(h);
    free(b);
}

static void check_24(const uint8_t *b, uint32_t len, uint32_t off)
{
    (void)off;
    pc_reader_t r; int rc;
    void *h = open_mem(b, len, &r, 0, &rc);
    CHECK(rc == 0 && h);
    CHECK(pc_backend_wav.rate(h) == 96000);
    float out[2 * 8];
    int n = pc_backend_wav.render(h, out, 8);
    CHECK(n == 8);
    for (int i = 0; i < 16; i++) CHECK(out[i] == (float)V24[i & 3] / 8388608.0f);   /* == : 24 bits fit a float mantissa */
    pc_backend_wav.close(h);
}
static void test_wav_24_exact(void)
{
    uint32_t len, off;
    uint8_t *b = wav_make(1, 2, 96000, 24, 64, false, &len, &off);
    for (int i = 0; i < 128; i++) set_i(b + off, i, 24, V24[i & 3]);
    check_24(b, len, off);
    free(b);
}

static void test_wav_32_and_float(void)
{
    uint32_t len, off;
    uint8_t *b = wav_make(1, 2, 48000, 32, 2, false, &len, &off);
    set_i(b + off, 0, 32, 0x40000000); set_i(b + off, 1, 32, INT32_MIN); set_i(b + off, 2, 32, 0x7FFFFFFF); set_i(b + off, 3, 32, -0x40000000);
    pc_reader_t r; int rc;
    void *h = open_mem(b, len, &r, 0, &rc);
    CHECK(rc == 0 && h);
    float out[8];
    CHECK(pc_backend_wav.render(h, out, 4) == 2);
    CHECK(out[0] == 0.5f && out[1] == -1.0f && out[2] <= 1.0f && out[2] > 0.999999f && out[3] == -0.5f);
    CHECK(pc_backend_wav.render(h, out, 4) == 0);
    pc_backend_wav.close(h);
    free(b);

    b = wav_make(3, 2, 48000, 32, 2, false, &len, &off);
    set_f(b + off, 0, 1.5f); set_f(b + off, 1, -0.25f); set_f(b + off, 2, -7.0f); set_f(b + off, 3, NAN);
    h = open_mem(b, len, &r, 0, &rc);
    CHECK(rc == 0 && h);
    CHECK(pc_backend_wav.render(h, out, 4) == 2);
    CHECK(out[0] == 1.0f && out[1] == -0.25f && out[2] == -1.0f && out[3] == 0.0f);   /* clipped to +-1; NaN -> silence */
    pc_backend_wav.close(h);
    free(b);
}

static void test_wav_extensible(void)
{
    uint32_t len, off;
    uint8_t *b = wav_make(1, 2, 96000, 24, 64, true, &len, &off);   /* tag 0xFFFE, sub-format PCM */
    CHECK(b[20] == 0xFE && b[21] == 0xFF);
    for (int i = 0; i < 128; i++) set_i(b + off, i, 24, V24[i & 3]);
    check_24(b, len, off);
    free(b);

    b = wav_make(3, 2, 48000, 32, 2, true, &len, &off);              /* sub-format FLOAT */
    set_f(b + off, 0, 0.75f); set_f(b + off, 1, -0.125f); set_f(b + off, 2, 2.0f); set_f(b + off, 3, 0.0f);
    pc_reader_t r; int rc;
    void *h = open_mem(b, len, &r, 0, &rc);
    CHECK(rc == 0 && h);
    float out[4];
    CHECK(pc_backend_wav.render(h, out, 2) == 2);
    CHECK(out[0] == 0.75f && out[1] == -0.125f && out[2] == 1.0f && out[3] == 0.0f);
    pc_backend_wav.close(h);
    free(b);
}

static void test_wav_mono(void)
{
    uint32_t len, off;
    uint8_t *b = wav_make(1, 1, 44100, 16, 300, false, &len, &off);
    for (int i = 0; i < 300; i++) set_i(b + off, i, 16, (i * 100) - 15000);
    pc_reader_t r; int rc;
    void *h = open_mem(b, len, &r, 0, &rc);
    CHECK(rc == 0 && h);
    CHECK(pc_backend_wav.length_ms(h) == 300 * 1000 / 44100);
    float out[2 * 300];
    int n = pc_backend_wav.render(h, out, 300);
    CHECK(n == 300);
    for (int i = 0; i < 300; i++) {
        CHECK(out[2 * i] == out[2 * i + 1]);
        CHECK(out[2 * i] == (float)((i * 100) - 15000) / 32768.0f);
    }
    CHECK(pc_backend_wav.byte_pos(h) == 600);   /* mono frame = 2 bytes */
    pc_backend_wav.close(h);
    free(b);
}

/* 4096 % 6 = 4: every refill of a 24-bit stereo file carries a split frame; every sample over several refills is compared, in uneven pieces */
static void test_wav_24_refill(void)
{
    enum { FR = 1500, PIECE = 37 };
    uint32_t len, off;
    uint8_t *b = wav_make(1, 2, 96000, 24, FR, false, &len, &off);
    for (int i = 0; i < 2 * FR; i++) set_i(b + off, i, 24, i * 997 - 1500000);   /* distinct in every sample, both channels */
    pc_reader_t r; int rc;
    void *h = open_mem(b, len, &r, 0, &rc);
    CHECK(rc == 0 && h);
    float out[2 * PIECE];
    int done = 0, n;
    while ((n = pc_backend_wav.render(h, out, PIECE)) > 0) {
        CHECK(n == PIECE || done + n == FR);
        for (int i = 0; i < 2 * n; i++) CHECK(out[i] == (float)((2 * done + i) * 997 - 1500000) / 8388608.0f);
        done += n;
        CHECK(pc_backend_wav.byte_pos(h) == (uint32_t)done * 6);
    }
    CHECK(done == FR);
    pc_backend_wav.close(h);
    free(b);
}

static void test_wav_resume(void)
{
    uint32_t len, off;
    uint8_t *b = wav_make(1, 2, 96000, 24, 3000, false, &len, &off);
    for (int i = 0; i < 6000; i++) set_i(b + off, i, 24, i * 997 - 3000000);
    pc_reader_t r; int rc;
    void *h = open_mem(b, len, &r, 0, &rc);
    CHECK(rc == 0 && h);
    static float out[2 * 1000];
    CHECK(pc_backend_wav.render(h, out, 1000) == 1000);
    for (int i = 0; i < 2000; i++) CHECK(out[i] == (float)(i * 997 - 3000000) / 8388608.0f);   /* all of it, across the refill at byte 4096 */
    CHECK(pc_backend_wav.byte_pos(h) == 6000);
    uint32_t bp = pc_backend_wav.byte_pos(h);
    float next[2];
    CHECK(pc_backend_wav.render(h, next, 1) == 1);
    pc_backend_wav.close(h);

    h = open_mem(b, len, &r, bp, &rc);
    CHECK(rc == 0 && h);
    CHECK(pc_backend_wav.byte_pos(h) == 6000);
    CHECK(pc_backend_wav.position_ms(h) == 1000 * 1000 / 96000);
    float again[2];
    CHECK(pc_backend_wav.render(h, again, 1) == 1);
    CHECK(again[0] == next[0] && again[1] == next[1]);
    CHECK(again[0] == (float)(2000 * 997 - 3000000) / 8388608.0f);   /* frame 1000, left */
    pc_backend_wav.close(h);

    h = open_mem(b, len, &r, 6003, &rc);   /* a token inside a frame is aligned down, never mid-frame */
    CHECK(rc == 0 && h && pc_backend_wav.byte_pos(h) == 6000);
    pc_backend_wav.close(h);
    h = open_mem(b, len, &r, 0xFFFFFFF0u, &rc);   /* past the end: clamped, plays nothing */
    CHECK(rc == 0 && h);
    CHECK(pc_backend_wav.render(h, out, 10) == 0);
    pc_backend_wav.close(h);
    free(b);
}

static void test_wav_cut(void)
{
    uint32_t len, off;
    spec_t s = { .tag = 1, .ch = 2, .bits = 24, .balign = 6, .rate = 48000, .frames = 600, .data_size = 10000 };   /* header says 10,000 */
    uint8_t *b = build(&s, &len, &off);
    uint32_t cut = off + 3001;                       /* the file ends after 3,001 data bytes */
    uint8_t *c = malloc(cut);                        /* exact size: ASan sees any read past it */
    memcpy(c, b, cut);
    free(b);
    pc_reader_t r; int rc;
    void *h = open_mem(c, cut, &r, 0, &rc);
    CHECK(rc == 0 && h);
    static float out[2 * 700];
    int total = 0, n;
    while ((n = pc_backend_wav.render(h, out, 64)) > 0) total += n;
    CHECK(n == 0 && total == 500);
    CHECK(pc_backend_wav.render(h, out, 64) == 0);
    CHECK(pc_backend_wav.byte_pos(h) == 3000);
    CHECK(pc_backend_wav.length_ms(h) == 500 * 1000 / 48000);
    pc_backend_wav.close(h);
    free(c);
}

static int try_open(const uint8_t *b, uint32_t len)
{
    pc_reader_t r; int rc;
    void *h = open_mem(b, len, &r, 0, &rc);
    if (h) pc_backend_wav.close(h);
    return rc;
}
/* opens, renders to the end in odd pieces and returns the frames played (-1: open refused) */
static int play_buf(const uint8_t *b, uint32_t len)
{
    pc_reader_t r; int rc;
    void *h = open_mem(b, len, &r, 0, &rc);
    if (!h) return -1;
    float out[2 * 50];
    int total = 0, n;
    while ((n = pc_backend_wav.render(h, out, 50)) > 0) total += n;
    CHECK(n == 0);
    pc_backend_wav.close(h);
    return total;
}
static int play_spec(const spec_t *s)
{
    uint32_t len, off;
    uint8_t *b = build(s, &len, &off);
    uint8_t *c = malloc(len);   /* exact-size block: ASan polices every read */
    memcpy(c, b, len);
    free(b);
    int n = play_buf(c, len);
    free(c);
    return n;
}
static int try_spec(const spec_t *s)
{
    uint32_t len, off;
    uint8_t *b = build(s, &len, &off);
    /* copy to an exact-size block so ASan polices every read */
    uint8_t *c = malloc(len);
    memcpy(c, b, len);
    free(b);
    int rc = try_open(c, len);
    free(c);
    return rc;
}

static void test_wav_garbage(void)
{
    spec_t ok = { .tag = 1, .ch = 2, .bits = 24, .balign = 6, .rate = 48000, .frames = 100 };
    CHECK(try_spec(&ok) == 0);
    spec_t s;
    s = ok; s.rate = 0;           CHECK(try_spec(&s) == -1);
    s = ok; s.rate = 10000000;    CHECK(try_spec(&s) == -1);
    s = ok; s.rate = 7999;        CHECK(try_spec(&s) == -1);
    s = ok; s.rate = 768000;      CHECK(try_spec(&s) == 0);   /* parse accepts it; the rate guard is elsewhere */
    s = ok; s.ch = 7; s.balign = 42; CHECK(try_spec(&s) == -1);
    s = ok; s.ch = 0; s.balign = 0;  CHECK(try_spec(&s) == -1);
    s = ok; s.bits = 20; s.balign = 6; CHECK(try_spec(&s) == -1);
    s = ok; s.balign = 8;         CHECK(try_spec(&s) == -1);   /* bits 24 with block_align 8 */
    s = ok; s.balign = 0;         CHECK(try_spec(&s) == -1);
    s = ok; s.tag = 2;            CHECK(try_spec(&s) == -1);   /* ADPCM */
    s = ok; s.tag = 3; s.bits = 24; CHECK(try_spec(&s) == -1);  /* float must be 32-bit */
    s = ok; s.frames = 0;         CHECK(try_spec(&s) == -1);   /* no audio at all */
    s = ok; s.tag = 0xFFFE; s.sub = 1; s.goodguid = false; CHECK(try_spec(&s) == -1);   /* unknown GUID */
    s = ok; s.tag = 0xFFFE; s.sub = 6; s.goodguid = true;  CHECK(try_spec(&s) == -1);   /* A-law sub-format */
    s = ok; s.tag = 0xFFFE; s.sub = 1; s.goodguid = true;  CHECK(try_spec(&s) == 0);
    s = ok; s.ch = 3; s.balign = 9;      CHECK(try_spec(&s) == -1);
    s = ok; s.ch = 65535; s.balign = 65535; CHECK(try_spec(&s) == -1);   /* the 16-bit block_align cannot even hold ch * 3 */
    s = ok; s.bits = 0; s.balign = 0;    CHECK(try_spec(&s) == -1);
    s = ok; s.bits = 8; s.balign = 2;    CHECK(try_spec(&s) == -1);
    s = ok; s.bits = 12; s.balign = 3;   CHECK(try_spec(&s) == -1);
    s = ok; s.bits = 64; s.balign = 16;  CHECK(try_spec(&s) == -1);
    s = ok; s.tag = 3; s.bits = 64; s.balign = 16; CHECK(try_spec(&s) == -1);   /* 64-bit float */
    s = ok; s.rate = 0xFFFFFFFFu;        CHECK(try_spec(&s) == -1);
    s = ok; s.data_size = 0xFFFFFFFFu;   CHECK(play_spec(&s) == 100);   /* clipped to the file: all 100 frames, no more */
    s = ok; s.data_size = 597;           CHECK(play_spec(&s) == 99);    /* 99 frames and 3 bytes: the partial frame is not delivered */
    s = ok; s.data_size = 5;             CHECK(play_spec(&s) == -1);    /* less than one frame */
    s = ok;                              CHECK(play_spec(&s) == 100);

    buf_t w = { 0 };
    riff(&w);   /* data before fmt */
    data_head(&w, 600);
    uint8_t z[600] = { 0 };
    put(&w, z, 600);
    fmt_chunk(&w, 1, 2, 48000, 24, 6, 0, true);
    CHECK(try_open(w.b, w.n) == -1);
    w.n = 0;

    riff(&w);   /* an odd-sized unknown chunk (size 3 + pad byte) before fmt */
    put(&w, "junk", 4); put32(&w, 3); put(&w, "abc\0", 4);
    fmt_chunk(&w, 1, 2, 48000, 24, 6, 0, true);
    data_head(&w, 600);
    put(&w, z, 600);
    CHECK(try_open(w.b, w.n) == 0);
    w.n = 0;

    riff(&w);   /* a chunk size near 2^32 must not wrap the offset */
    put(&w, "junk", 4); put32(&w, 0xFFFFFFFFu);
    fmt_chunk(&w, 1, 2, 48000, 24, 6, 0, true);
    data_head(&w, 600);
    put(&w, z, 600);
    CHECK(try_open(w.b, w.n) == -1);
    w.n = 0;

    riff(&w);   /* fmt chunk shorter than 16 */
    put(&w, "fmt ", 4); put32(&w, 14); put(&w, z, 14);
    data_head(&w, 600);
    put(&w, z, 600);
    CHECK(try_open(w.b, w.n) == -1);
    w.n = 0;

    riff(&w);   /* extensible fmt cut short (size says 40, file ends) */
    put(&w, "fmt ", 4); put32(&w, 40); put16(&w, 0xFFFE); put16(&w, 2); put32(&w, 48000); put32(&w, 0); put16(&w, 6); put16(&w, 24);
    CHECK(try_open(w.b, w.n) == -1);
    w.n = 0;

    riff(&w);   /* extensible with size 16 (no sub-format at all) */
    fmt_chunk(&w, 1, 2, 48000, 24, 6, 0, true);
    w.b[20] = 0xFE; w.b[21] = 0xFF;
    data_head(&w, 600);
    put(&w, z, 600);
    CHECK(try_open(w.b, w.n) == -1);
    w.n = 0;

    riff(&w);   /* data header present but no payload byte */
    fmt_chunk(&w, 1, 2, 48000, 24, 6, 0, true);
    data_head(&w, 600);
    CHECK(try_open(w.b, w.n) == -1);
    w.n = 0;

    riff(&w);   /* a LIST chunk claiming more than the file holds, and no data chunk: refused */
    fmt_chunk(&w, 1, 2, 48000, 24, 6, 0, true);
    put(&w, "LIST", 4); put32(&w, 100000);
    CHECK(try_open(w.b, w.n) == -1);
    w.n = 0;

    riff(&w);   /* a LIST chunk (even size), then data: plays all 100 frames */
    fmt_chunk(&w, 1, 2, 48000, 24, 6, 0, true);
    put(&w, "LIST", 4); put32(&w, 10); put(&w, z, 10);
    data_head(&w, 600);
    put(&w, z, 600);
    CHECK(play_buf(w.b, w.n) == 100);
    w.n = 0;

    riff(&w);   /* a LIST chunk after data, outside the stated data length: not played */
    fmt_chunk(&w, 1, 2, 48000, 24, 6, 0, true);
    data_head(&w, 600);
    put(&w, z, 600);
    put(&w, "LIST", 4); put32(&w, 12); put(&w, "INFOabcdefgh", 12);
    CHECK(play_buf(w.b, w.n) == 100);
    w.n = 0;

    riff(&w);   /* RF64: refused (its magic is not RIFF) */
    fmt_chunk(&w, 1, 2, 48000, 24, 6, 0, true);
    data_head(&w, 600);
    put(&w, z, 600);
    memcpy(w.b, "RF64", 4);
    CHECK(try_open(w.b, w.n) == -1);
    w.n = 0;

    riff(&w);   /* EXTENSIBLE with cbSize 21 (< 22) in a 40-byte chunk: refused */
    fmt_chunk(&w, 0xFFFE, 2, 48000, 24, 6, 1, true);
    data_head(&w, 600);
    put(&w, z, 600);
    CHECK(w.b[36] == 22);
    w.b[36] = 21;
    CHECK(try_open(w.b, w.n) == -1);
    w.b[36] = 22;
    CHECK(try_open(w.b, w.n) == 0);   /* the same bytes with cbSize 22 open */
    w.n = 0;

    riff(&w);   /* a plain 18-byte fmt with a huge cbSize: cbSize is not used for PCM, plays */
    put(&w, "fmt ", 4); put32(&w, 18);
    put16(&w, 1); put16(&w, 2); put32(&w, 48000); put32(&w, 48000 * 6); put16(&w, 6); put16(&w, 24); put16(&w, 0xFFFF);
    data_head(&w, 600);
    put(&w, z, 600);
    CHECK(play_buf(w.b, w.n) == 100);
    w.n = 0;

    riff(&w);   /* the same 18-byte fmt claiming EXTENSIBLE: no room for the GUID, refused */
    put(&w, "fmt ", 4); put32(&w, 18);
    put16(&w, 0xFFFE); put16(&w, 2); put32(&w, 48000); put32(&w, 48000 * 6); put16(&w, 6); put16(&w, 24); put16(&w, 0xFFFF);
    data_head(&w, 600);
    put(&w, z, 600);
    CHECK(try_open(w.b, w.n) == -1);
    w.n = 0;

    /* EXTENSIBLE with valid bits 20 in a 24-bit container: the decoder ignores the valid-bits field and plays the full
     * 24-bit container value (left-justified samples keep their level); it does not refuse and does not rescale */
    riff(&w);
    fmt_chunk(&w, 0xFFFE, 2, 48000, 24, 6, 1, true);
    CHECK(w.b[38] == 24);
    w.b[38] = 20;
    data_head(&w, 12);
    uint8_t smp[12] = { 0x56, 0x34, 0x12, 0x00, 0x00, 0x80, 0xF0, 0xFF, 0x7F, 0x01, 0x00, 0x00 };
    put(&w, smp, 12);
    void *vh = NULL;
    pc_reader_t vr;
    pc_reader_mem(&vr, w.b, w.n);
    CHECK(pc_backend_wav.open(&vh, &vr, 0) == 0);
    float vo[4];
    CHECK(pc_backend_wav.render(vh, vo, 2) == 2);
    CHECK(vo[0] == (float)0x123456 / 8388608.0f && vo[1] == -1.0f && vo[2] == (float)0x7FFFF0 / 8388608.0f && vo[3] == 1.0f / 8388608.0f);
    pc_backend_wav.close(vh);
    free(w.b);

    uint8_t eleven[11];
    memcpy(eleven, "RIFF\0\0\0\0WAVE", 11);   /* a RIFF/WAVE header cut after 11 bytes */
    CHECK(try_open(eleven, 11) == -1);
    uint8_t notwav[44] = "RIFX\0\0\0\0WAVE";
    CHECK(try_open(notwav, 44) == -1);
    CHECK(try_open(eleven, 0) == -1);
}

int main(void)
{
    RUN(test_wav_16);
    RUN(test_wav_24_exact);
    RUN(test_wav_32_and_float);
    RUN(test_wav_extensible);
    RUN(test_wav_mono);
    RUN(test_wav_24_refill);
    RUN(test_wav_resume);
    RUN(test_wav_cut);
    RUN(test_wav_garbage);
    return 0;
}
