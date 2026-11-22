// SPDX-License-Identifier: LGPL-2.1-or-later
/* True Audio backend over libtta: the files are made in memory by libtta's own encoder (built for the host tests only), so no audio is
 * committed. TTA is lossless: what comes out is what went in, to the bit. */
#include <stdbool.h>
#include "libtta.h"
#include "pc/pc_backend.h"
#include "pc/pc_io.h"
#include "pc_test.h"

typedef struct { TTA_io_callback io; uint8_t *b; uint32_t n, cap, pos; } mem_t;
static TTAint32 w_write(TTA_io_callback *io, TTAuint8 *src, TTAuint32 n)
{
    mem_t *m = (mem_t *)io;
    if (m->pos + n > m->cap) { m->cap = (m->pos + n) * 2; m->b = realloc(m->b, m->cap); CHECK(m->b != NULL); }
    memcpy(m->b + m->pos, src, n);
    m->pos += n;
    if (m->pos > m->n) m->n = m->pos;
    return (TTAint32)n;
}
static TTAint32 w_read(TTA_io_callback *io, TTAuint8 *dst, TTAuint32 n) { (void)io; (void)dst; (void)n; return 0; }
static TTAint64 w_seek(TTA_io_callback *io, TTAint64 off) { ((mem_t *)io)->pos = (uint32_t)off; return off; }

static int32_t sample(int i, int bps)
{
    int64_t amp = (1ll << (bps - 1)) - 1;
    return (int32_t)(amp * ((((int64_t)i * 7919) % 2001) - 1000) / 1000);    /* a noisy ramp: not a pattern the predictor learns away */
}

static uint8_t *make_tta(uint32_t rate, int ch, int bps, int frames, uint32_t *len)
{
    mem_t m = { .io = { .read = w_read, .write = w_write, .seek = w_seek } };
    tta_encoder_new(&m.io);
    TTA_info info = { .format = TTA_FORMAT_SIMPLE, .nch = (uint32_t)ch, .bps = (uint32_t)bps, .sps = rate, .samples = (uint32_t)frames };
    CHECK_EQ_INT(tta_encoder_init_set_info(&info, 0), 0);
    int depth = (bps + 7) / 8;
    uint32_t bytes = (uint32_t)frames * (uint32_t)ch * (uint32_t)depth;
    uint8_t *pcm = calloc(1, bytes + 4);
    for (int i = 0; i < frames * ch; i++) {
        int32_t v = sample(i, bps);
        for (int k = 0; k < depth; k++) pcm[i * depth + k] = (uint8_t)((uint32_t)v >> (8 * k));
    }
    CHECK_EQ_INT(tta_encoder_process_stream(pcm, bytes, NULL), 0);
    CHECK_EQ_INT(tta_encoder_finalize(), 0);
    tta_encoder_done();
    free(pcm);
    *len = m.n;
    return m.b;
}

static void check_all(uint32_t rate, int ch, int bps, int frames, uint32_t start)
{
    uint32_t len;
    uint8_t *d = make_tta(rate, ch, bps, frames, &len);
    pc_reader_t r;
    pc_reader_mem(&r, d, len);
    void *h;
    CHECK_EQ_INT(pc_backend_tta.open(&h, &r, start), 0);
    CHECK_EQ_INT(pc_backend_tta.rate(h), rate);
    CHECK_EQ_INT(pc_backend_tta.bits(h), bps);
    CHECK_EQ_INT(pc_backend_tta.length_ms(h), (int64_t)frames * 1000 / rate);
    float *out = malloc(sizeof(float) * 2 * 1000);
    int64_t n = 0;
    float scale = 1.0f / (float)(1u << (bps - 1));
    for (;;) {
        int g = pc_backend_tta.render(h, out, 1000);
        CHECK(g >= 0);
        if (!g) break;
        for (int i = 0; i < g; i++) {
            int64_t f = start + n + i;
            float l = (float)sample((int)f * ch, bps) * scale, rr = ch > 1 ? (float)sample((int)f * ch + 1, bps) * scale : l;
            CHECK_NEAR(out[2 * i], l, 1e-9);
            CHECK_NEAR(out[2 * i + 1], rr, 1e-9);
        }
        n += g;
    }
    CHECK_EQ_INT(n, frames - (int)start);
    CHECK_EQ_INT(pc_backend_tta.position_ms(h), (int64_t)frames * 1000 / rate);
    pc_backend_tta.close(h);
    free(out);
    free(d);
}

static void test_formats(void)
{
    check_all(44100, 2, 16, 100000, 0);                 /* two and a bit TTA frames (46080 samples each) */
    check_all(44100, 1, 16, 30000, 0);
    check_all(48000, 2, 24, 120000, 0);
    check_all(96000, 1, 24, 50000, 0);
}

static void test_resume(void)
{
    check_all(44100, 2, 16, 150000, 100000);            /* inside the third frame: a seek, then 8000-odd samples dropped */
    check_all(44100, 2, 16, 150000, 46080);             /* exactly on a frame start */
    check_all(44100, 1, 24, 90000, 12345);
}

static void test_one_at_a_time_and_bad(void)
{
    uint32_t len;
    uint8_t *d = make_tta(44100, 2, 16, 20000, &len);
    pc_reader_t r;
    pc_reader_mem(&r, d, len);
    void *h1, *h2;
    CHECK_EQ_INT(pc_backend_tta.open(&h1, &r, 0), 0);
    CHECK(pc_backend_tta.open(&h2, &r, 0) != 0);        /* the state is global: a second decoder is refused */
    pc_backend_tta.close(h1);
    CHECK_EQ_INT(pc_backend_tta.open(&h2, &r, 0), 0);   /* and free again after the close */
    pc_backend_tta.close(h2);
    uint8_t junk[100];
    memset(junk, 0x33, sizeof junk);
    pc_reader_mem(&r, junk, sizeof junk);
    CHECK(pc_backend_tta.open(&h1, &r, 0) != 0);
    for (uint32_t cut = 30; cut < len; cut += 997) {    /* truncated: never a crash, never a hang */
        pc_reader_mem(&r, d, cut);
        if (pc_backend_tta.open(&h1, &r, 0) == 0) {
            float out[2 * 441];
            int g, guard = 0;
            while ((g = pc_backend_tta.render(h1, out, 441)) > 0 && ++guard < 1000) {}
            pc_backend_tta.close(h1);
        }
    }
    d[len / 2] ^= 0xff;                                 /* a flipped byte: the frame CRC fails, the decoder carries on or stops */
    pc_reader_mem(&r, d, len);
    if (pc_backend_tta.open(&h1, &r, 0) == 0) {
        float out[2 * 441];
        int g, guard = 0;
        while ((g = pc_backend_tta.render(h1, out, 441)) > 0 && ++guard < 1000) {}
        pc_backend_tta.close(h1);
    }
    free(d);
    d = make_tta(44100, 2, 16, 20000, &len);
    memcpy(d + 4, "\x02\x00", 2);                       /* format 2 = password protected (the header CRC no longer fits either) */
    pc_reader_mem(&r, d, len);
    CHECK(pc_backend_tta.open(&h1, &r, 0) != 0);
    free(d);
}

/* ---- hostile headers (wave-6 review: HIGH 2, MED 6/7): the header CRC is trivial to forge ---- */
/* wave-6 review #19 (FIX7): without a usable seek table the run-up decodes from the start inside open(); a resume further in than 60 s of audio
 * starts the track over instead (a resume inside the bound is exact). With a good table the seek works at any distance. */
static int count_from(const uint8_t *d, uint32_t len, uint32_t start)
{
    pc_reader_t r;
    pc_reader_mem(&r, d, len);
    void *h;
    CHECK_EQ_INT(pc_backend_tta.open(&h, &r, start), 0);
    float out[2 * 1000];
    int g, n = 0;
    while ((g = pc_backend_tta.render(h, out, 1000)) > 0) n += g;
    pc_backend_tta.close(h);
    return n;
}
static void test_resume_without_table(void)
{
    const int frames = 90 * 8000;                           /* 90 s at the lowest rate */
    uint32_t len;
    uint8_t *d = make_tta(8000, 1, 16, frames, &len);
    CHECK_EQ_INT(count_from(d, len, 600000), frames - 600000);   /* the good table seeks */
    d[22] ^= 1;                                             /* the seek table's first entry: its CRC no longer matches */
    CHECK_EQ_INT(count_from(d, len, 30000), frames - 30000);     /* inside the bound: dropped exactly */
    CHECK_EQ_INT(count_from(d, len, 60 * 8000 - 1), frames - (60 * 8000 - 1));
    CHECK_EQ_INT(count_from(d, len, 600000), frames);            /* beyond it: from the start */
    free(d);
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
static void put_le(uint8_t *p, uint32_t v, int n) { for (int i = 0; i < n; i++) p[i] = (uint8_t)(v >> (8 * i)); }
static void forge(uint8_t *d, uint32_t sps, uint32_t samples)
{
    put_le(d + 10, sps, 4);
    put_le(d + 14, samples, 4);
    put_le(d + 18, crc32_std(d, 18), 4);
}
/* the 32-bit size_t simulation: libtta's seek-table malloc (aligned_alloc) sees the size truncated to 32 bits while the flag is set */
int tta_test_trunc32;
void *__real_aligned_alloc(size_t al, size_t n);
void *__wrap_aligned_alloc(size_t al, size_t n) { return __real_aligned_alloc(al, tta_test_trunc32 ? (size_t)(uint32_t)n : n); }

static void try_open(const uint8_t *d, uint32_t len, bool want_ok)
{
    pc_reader_t r;
    pc_reader_mem(&r, d, len);
    void *h = NULL;
    int rc = pc_backend_tta.open(&h, &r, 0);
    CHECK_EQ_INT(rc == 0, want_ok);
    if (rc == 0) {
        float out[2 * 441];
        int g, guard = 0;
        while ((g = pc_backend_tta.render(h, out, 441)) > 0 && ++guard < 200) {}
        pc_backend_tta.close(h);
    }
}

static void test_hostile_header(void)
{
    uint32_t len;
    uint8_t *d = make_tta(44100, 2, 16, 20000, &len);
    forge(d, 44100, 20000);                             /* the forging itself keeps a good file good */
    try_open(d, len, true);
    static const uint32_t sps_bad[] = { 0, 1, 7999, 384001, 0x01000001u, 0xFFFFFFFFu };
    for (unsigned i = 0; i < sizeof sps_bad / sizeof sps_bad[0]; i++) {
        forge(d, sps_bad[i], 20000);                    /* rate 0: samples % 0 (SIGFPE); tiny rates: a huge frame count; 256 * rate wrapping in 32 bits */
        try_open(d, len, false);
    }
    tta_test_trunc32 = 1;                               /* 32-bit size_t: frames * 8 wraps (sps 1, samples 2^29 + 1: an 8-byte table the reader then overran) */
    forge(d, 1, (1u << 29) + 1);
    try_open(d, len, false);
    forge(d, 1, 0xFFFFFFFFu);
    try_open(d, len, false);
    forge(d, 8000, 0xFFFFFFFFu);                        /* the largest legal frame count: 513 k frames = 4 MB, no wrap, must not crash */
    try_open(d, len, false);
    tta_test_trunc32 = 0;
    free(d);
}

/* the Rice parameter k must stay below 32: 0xFF-heavy data pushes it up (bit_mask[k] / shift_16[k + 1] read past the tables) */
static void test_rice_bounds(void)
{
    uint32_t len;
    uint8_t *d = make_tta(44100, 2, 16, 30000, &len);
    uint32_t hdr = 22 + 4 * 1 + 4;                       /* header + a one-frame seek table + its CRC: the data starts here */
    for (uint32_t i = hdr; i < len - 8; i++) d[i] = (i & 3) == 3 ? 0x00 : 0xFF;
    pc_reader_t r;
    pc_reader_mem(&r, d, len);
    void *h;
    if (pc_backend_tta.open(&h, &r, 0) == 0) {
        float out[2 * 441];
        int g, guard = 0;
        while ((g = pc_backend_tta.render(h, out, 441)) > 0 && ++guard < 2000) {}
        pc_backend_tta.close(h);
    }
    free(d);
}

int main(void)
{
    RUN(test_hostile_header);
    RUN(test_rice_bounds);
    RUN(test_formats);
    RUN(test_resume);
    RUN(test_resume_without_table);
    RUN(test_one_at_a_time_and_bad);
    return 0;
}
