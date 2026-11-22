// SPDX-License-Identifier: LGPL-2.1-or-later
/* ADX / VAG / BRSTM backends: streams are built in memory (deterministic pseudo-random ADPCM bytes, no audio file in git). The expected checksums of
 * the decoded PCM were made once with ffmpeg's decoders on the same bytes (test files written with STREAM_DUMP=<dir>; BRSTM: see below); the internal chip bench
 * repeats the comparison on the real files. Also: resume, hostile input (every truncation, random flips) under ASan. */
#include <stdbool.h>
#include "pc/pc_backend.h"
#include "pc/pc_id3.h"
#include <math.h>
#include "pc_test.h"

static uint32_t rng_state = 12345;
static uint8_t rnd8(void) { rng_state = rng_state * 1664525u + 1013904223u; return (uint8_t)(rng_state >> 24); }
static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
static void put16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }

static uint8_t *make_adx(int ch, int rate, int blocks, uint32_t *len)
{
    const int bsz = 18, cr = 0x2c;
    uint32_t n = (uint32_t)(cr + 4 + blocks * ch * bsz);
    uint8_t *f = calloc(1, n);
    rng_state = 777;
    f[0] = 0x80; put16(f + 2, cr); f[4] = 3; f[5] = bsz; f[6] = 4; f[7] = (uint8_t)ch;
    put32(f + 8, (uint32_t)rate); put32(f + 12, (uint32_t)(blocks * 32)); put16(f + 16, 500); f[18] = 3;
    memcpy(f + cr - 2, "(c)CRI", 6);
    for (int b = 0; b < blocks * ch; b++) {
        uint8_t *p = f + cr + 4 + b * bsz;
        put16(p, (uint32_t)(rnd8() % 24));                         /* small scales: the filter does not saturate */
        for (int i = 2; i < bsz; i++) p[i] = rnd8();
    }
    *len = n;
    return f;
}

static uint8_t *make_vag(int rate, int frames, uint32_t *len)
{
    uint32_t n = 48 + (uint32_t)frames * 16;
    uint8_t *f = calloc(1, n);
    rng_state = 4242;
    memcpy(f, "VAGp", 4); put32(f + 4, 0x20); put32(f + 12, (uint32_t)frames * 16); put32(f + 16, (uint32_t)rate); memcpy(f + 32, "test", 4);
    for (int k = 0; k < frames; k++) {
        uint8_t *p = f + 48 + k * 16;
        p[0] = (uint8_t)((rnd8() % 5) << 4 | (rnd8() % 10 + 3));  /* filter 0-4, shift 3-12 */
        p[1] = 0;
        for (int i = 2; i < 16; i++) p[i] = rnd8();
    }
    *len = n;
    return f;
}

/* RSTM with DSP-ADPCM: HEAD at 0x40 (three entries: stream info, tracks, channels), ADPC (unused here), DATA; blocks of `bsz` bytes per channel */
static uint8_t *make_brstm(int ch, int rate, int blocks, int bsz, int last_frames, uint32_t *len)
{
    const uint32_t head = 0x40, hsz = (8 + 0x70 + 0x38 * (uint32_t)ch + 31) & ~31u, adpc = head + hsz, adsz = (8 + (uint32_t)blocks * (uint32_t)ch * 4 + 31) & ~31u, data = adpc + adsz, audio = data + 0x20;
    const uint32_t fsz = (uint32_t)last_frames * 8, fpad = (fsz + 31) & ~31u;
    uint32_t dsz = (uint32_t)(blocks - 1) * (uint32_t)bsz * (uint32_t)ch + fpad * (uint32_t)ch;
    uint32_t n = audio + dsz;
    uint8_t *f = calloc(1, n);
    rng_state = 99;
    memcpy(f, "RSTM", 4); f[4] = 0xFE; f[5] = 0xFF; put16(f + 6, 0x100); put32(f + 8, n); put16(f + 12, 0x40); put16(f + 14, 2);
    put32(f + 0x10, head); put32(f + 0x14, hsz); put32(f + 0x18, adpc); put32(f + 0x1c, adsz); put32(f + 0x20, data); put32(f + 0x24, dsz + 0x20);
    memcpy(f + head, "HEAD", 4); put32(f + head + 4, hsz);
    memcpy(f + adpc, "ADPC", 4); put32(f + adpc + 4, adsz);
    uint8_t *b = f + head + 8;                                      /* offsets inside HEAD count from b */
    put32(b + 0, 1); put32(b + 4, 0x18); put32(b + 8, 1); put32(b + 12, 0x4c); put32(b + 16, 1); put32(b + 20, 0x5c);
    uint8_t *si = b + 0x18;
    si[0] = 2; si[1] = 0; si[2] = (uint8_t)ch; put16(si + 4, (uint32_t)rate);
    uint32_t total = (uint32_t)(blocks - 1) * ((uint32_t)bsz / 8) * 14 + (uint32_t)last_frames * 14;
    put32(si + 0xc, total); put32(si + 0x10, audio); put32(si + 0x14, (uint32_t)blocks); put32(si + 0x18, (uint32_t)bsz); put32(si + 0x1c, (uint32_t)bsz / 8 * 14);
    put32(si + 0x20, fsz); put32(si + 0x24, (uint32_t)last_frames * 14); put32(si + 0x28, fpad);
    uint8_t *tr = b + 0x4c; tr[0] = 1;
    uint8_t *cn = b + 0x5c;                                         /* channel count, then (marker, offset -> (marker, offset -> coefficients)) per channel */
    cn[0] = (uint8_t)ch;
    for (int c = 0; c < ch; c++) {
        uint32_t e1 = 0x70 + 0x38 * (uint32_t)c, e2 = e1 + 8;
        put32(cn + 4 + 8 * c, 1); put32(cn + 8 + 8 * c, e1);
        put32(b + e1, 1); put32(b + e1 + 4, e2);
        static const int16_t co[16] = { 0, 0, 0x780, 0, 0xe60, -0x680, 0xc40, -0x6e0, 0xf40, -0x780, 0x800, 0, 0xfc0, -0x7e0, 0xe00, -0x640 };
        for (int i = 0; i < 16; i++) put16(b + e2 + 2 * i, (uint32_t)(uint16_t)(co[i] + (c ? 5 : 0)));
        put32(b + e1 + 4, e2);
    }
    memcpy(f + data, "DATA", 4); put32(f + data + 4, dsz + 0x20);
    for (uint32_t i = 0; i < dsz; i += 8) {
        uint8_t *p = f + audio + i;
        p[0] = (uint8_t)((rnd8() % 3) << 4 | (rnd8() % 5));        /* coefficient set 0-2, scale 2^0..2^4 */
        for (int k = 1; k < 8; k++) p[k] = rnd8();
    }
    *len = n;
    return f;
}

static uint32_t fnv_pcm(const float *out, int n)
{
    uint32_t h = 2166136261u;
    for (int i = 0; i < n; i++) {
        float v = out[i] * 32768.0f;
        int s = (int)(v < 0 ? v - 0.5f : v + 0.5f);
        h = (h ^ (uint8_t)s) * 16777619u;
        h = (h ^ (uint8_t)(s >> 8)) * 16777619u;
    }
    return h;
}

/* all frames of a stream: returns the frame count, *sum = FNV of the int16 stereo PCM; *pcm (malloc'd, caller frees) when not NULL */
static int play(const pc_backend_t *be, const uint8_t *d, uint32_t len, uint32_t start, uint32_t *sum, float **pcm)
{
    pc_reader_t r;
    pc_reader_mem(&r, d, len);
    void *h;
    CHECK_EQ_INT(be->open(&h, &r, start), 0);
    float *all = NULL;
    int tot = 0, cap = 0;
    float out[2 * 100];
    for (;;) {
        int g = be->render(h, out, 100);
        CHECK(g >= 0);
        if (g == 0) break;
        if (tot + g > cap) { cap = (tot + g) * 2; all = realloc(all, sizeof(float) * 2 * (size_t)cap); }
        memcpy(all + 2 * tot, out, sizeof(float) * 2 * (size_t)g);
        tot += g;
    }
    *sum = fnv_pcm(all ? all : out, 2 * tot);
    be->close(h);
    if (pcm) *pcm = all; else free(all);
    return tot;
}

static void maybe_dump(const char *name, const uint8_t *d, uint32_t len)
{
    const char *dir = getenv("STREAM_DUMP");
    if (!dir) return;
    char p[512];
    snprintf(p, sizeof p, "%s/%s", dir, name);
    FILE *f = fopen(p, "wb");
    CHECK(f && fwrite(d, 1, len, f) == len);
    fclose(f);
}

#define CHECK_SUM(a, b) do { if (getenv("STREAM_DUMP")) printf("%s = %08x\n", #a, (unsigned)(a)); else CHECK_EQ_INT(a, b); } while (0)

/* checksums of ffmpeg's decode of the same bytes (see the header); filled by the internal chip bench -style runs */
#define SUM_ADX_MONO 0xf39d8285u
#define SUM_ADX_STEREO 0x91380e49u
#define SUM_VAG 0x5ceb47d5u
/* BRSTM: ffmpeg's DSP-ADPCM decoder leaves out the +1024 rounding term of Nintendo's, so its output differs by a few LSB: this one is a golden value of our
 * own decoder (ffmpeg decodes the same file to the same length, and to bit-identical PCM when this decoder's rounding term is taken out: checked once). */
#define SUM_BRSTM_STEREO 0x0e41e7fdu

static void test_adx(void)
{
    uint32_t len, sum;
    uint8_t *d = make_adx(1, 16000, 20, &len);
    maybe_dump("m.adx", d, len);
    CHECK_EQ_INT(play(&pc_backend_adx, d, len, 0, &sum, NULL), 20 * 32);
    CHECK_SUM(sum, SUM_ADX_MONO);
    free(d);
    d = make_adx(2, 22050, 30, &len);
    maybe_dump("s.adx", d, len);
    CHECK_EQ_INT(play(&pc_backend_adx, d, len, 0, &sum, NULL), 30 * 32);
    CHECK_SUM(sum, SUM_ADX_STEREO);
    pc_reader_t r;
    pc_reader_mem(&r, d, len);
    void *h;
    CHECK_EQ_INT(pc_backend_adx.open(&h, &r, 0), 0);
    CHECK_EQ_INT(pc_backend_adx.rate(h), 22050);
    CHECK_EQ_INT(pc_backend_adx.length_ms(h), 30 * 32 * 1000 / 22050);
    pc_backend_adx.close(h);
    d[4] = 4;                                                       /* an exponential-scale ADX: refused with a reason */
    pc_reader_mem(&r, d, len);
    CHECK(pc_backend_adx.open(&h, &r, 0) != 0);
    CHECK_EQ_INT(pc_stream_check(&r, ".adx"), 1);
    d[4] = 3;
    d[5] = 36;                                                      /* block size 36 (wave-6 review): only 32 samples a block were decoded, the length and pitch were wrong: refused */
    pc_reader_mem(&r, d, len);
    CHECK(pc_backend_adx.open(&h, &r, 0) != 0);
    d[5] = 18;
    d[19] = 8;                                                      /* the encryption flag: it would play as noise */
    pc_reader_mem(&r, d, len);
    CHECK(pc_backend_adx.open(&h, &r, 0) != 0);
    d[19] = 0;
    pc_reader_mem(&r, d, len);
    CHECK_EQ_INT(pc_backend_adx.open(&h, &r, 0), 0);
    pc_backend_adx.close(h);
    free(d);
}

static void test_vag(void)
{
    uint32_t len, sum;
    uint8_t *d = make_vag(22050, 40, &len);
    maybe_dump("t.vag", d, len);
    CHECK_EQ_INT(play(&pc_backend_vag, d, len, 0, &sum, NULL), 40 * 28);
    CHECK_SUM(sum, SUM_VAG);
    free(d);
}

static void test_tag(void)
{
    uint32_t len;
    uint8_t *d = make_vag(22050, 4, &len);
    pc_reader_t r;
    pc_reader_mem(&r, d, len);
    pc_id3_t t;
    memset(&t, 0, sizeof t);
    CHECK_EQ_INT(pc_id3_parse(&r, &t), 0);
    CHECK_STR(t.title, "test");
    pc_id3_free(&t);
    free(d);
}

static void test_brstm(void)
{
    uint32_t len, sum;
    uint8_t *d = make_brstm(2, 32000, 4, 0x100, 10, &len);        /* 3 full blocks of 32 frames + a last block of 10 frames */
    maybe_dump("s.brstm", d, len);
    int want = 3 * 32 * 14 + 10 * 14;
    CHECK_EQ_INT(play(&pc_backend_brstm, d, len, 0, &sum, NULL), want);
    CHECK_SUM(sum, SUM_BRSTM_STEREO);
    free(d);
    d = make_brstm(4, 32000, 3, 0x80, 7, &len);                    /* four channels fold to stereo */
    CHECK_EQ_INT(play(&pc_backend_brstm, d, len, 0, &sum, NULL), 2 * 16 * 14 + 7 * 14);
    free(d);
}

static void test_resume(void)
{
    const struct { const pc_backend_t *be; int kind; } cases[] = { { &pc_backend_adx, 0 }, { &pc_backend_vag, 1 }, { &pc_backend_brstm, 2 } };
    for (int k = 0; k < 3; k++) {
        uint32_t len, s1, s2;
        uint8_t *d = k == 0 ? make_adx(2, 16000, 25, &len) : k == 1 ? make_vag(16000, 50, &len) : make_brstm(2, 16000, 5, 0x100, 12, &len);
        float *all;
        int n = play(cases[k].be, d, len, 0, &s1, &all);
        for (int start = 1; start < n; start += n / 7 + 1) {
            float *tail;
            int m = play(cases[k].be, d, len, (uint32_t)start, &s2, &tail);
            CHECK_EQ_INT(m, n - start);
            CHECK(!memcmp(tail, all + 2 * start, sizeof(float) * 2 * (size_t)m));   /* a resume continues exactly where the stream was */
            free(tail);
        }
        CHECK_EQ_INT(play(cases[k].be, d, len, 1000000000u, &s2, NULL), n);      /* a token past the end plays from the start (as MPC, TTA and AIFF do) */
        free(all);
        free(d);
    }
}

/* wave-6 review #19 (FIX7): a resume far into a long stream jumps near the target (a bounded run-up) instead of decoding everything before it.
 * The result is the same audio once the filters settled: compared with the full decode it differs by a few LSB at most, from the first frame. */
static void test_resume_far(void)
{
    const struct { const pc_backend_t *be; int kind; } cases[] = { { &pc_backend_adx, 0 }, { &pc_backend_vag, 1 }, { &pc_backend_brstm, 2 } };
    for (int k = 0; k < 3; k++) {
        uint32_t len, s1, s2;
        uint8_t *d = k == 0 ? make_adx(2, 16000, 20000, &len) : k == 1 ? make_vag(16000, 40000, &len) : make_brstm(2, 16000, 2400, 0x1000, 12, &len);
        float *all;
        int n = play(cases[k].be, d, len, 0, &s1, &all);
        CHECK(n > 560000);                                          /* longer than EXACT_UNITS units of every kind */
        const int starts[] = { n / 2, n - 40000, n - 1 };
        for (size_t i = 0; i < sizeof starts / sizeof *starts; i++) {
            float *tail;
            int m = play(cases[k].be, d, len, (uint32_t)starts[i], &s2, &tail);
            CHECK_EQ_INT(m, n - starts[i]);
            float worst = 0;
            for (int j = 0; j < 2 * m; j++) { float e = fabsf(tail[j] - all[2 * starts[i] + j]); if (e > worst) worst = e; }
            CHECK(worst < 16.0f / 32768.0f);
            free(tail);
        }
        free(all);
        free(d);
    }
}

static void try_open(const pc_backend_t *be, const uint8_t *d, uint32_t len)
{
    pc_reader_t r;
    pc_reader_mem(&r, d, len);
    void *h;
    if (be->open(&h, &r, 0)) return;
    float out[2 * 200];
    for (int i = 0; i < 400; i++) if (be->render(h, out, 200) <= 0) break;
    be->close(h);
}

static void test_hostile(void)
{
    uint32_t len;
    uint8_t *src[3] = { make_adx(2, 16000, 6, &len), NULL, NULL };
    uint32_t lens[3] = { len, 0, 0 };
    src[1] = make_vag(16000, 8, &len); lens[1] = len;
    src[2] = make_brstm(2, 16000, 3, 0x40, 4, &len); lens[2] = len;
    const pc_backend_t *be[3] = { &pc_backend_adx, &pc_backend_vag, &pc_backend_brstm };
    srand(5);
    for (int k = 0; k < 3; k++) {
        for (uint32_t cut = 0; cut < lens[k]; cut += (cut < 0x200 ? 1 : 7)) {                    /* every truncation of the header, then every 7th */
            uint8_t *c = malloc(cut ? cut : 1);
            memcpy(c, src[k], cut);
            try_open(be[k], c, cut);
            free(c);
        }
        for (int i = 0; i < 1500; i++) {
            uint8_t *c = malloc(lens[k]);
            memcpy(c, src[k], lens[k]);
            int flips = 1 + rand() % 4;
            for (int q = 0; q < flips; q++) c[rand() % (int)(i % 2 ? lens[k] : (lens[k] < 0x100 ? lens[k] : 0x100))] = (uint8_t)rand();
            try_open(be[k], c, lens[k]);
            free(c);
        }
        free(src[k]);
    }
}

int main(void)
{
    RUN(test_adx);
    RUN(test_vag);
    RUN(test_tag);
    RUN(test_brstm);
    RUN(test_resume);
    RUN(test_resume_far);
    RUN(test_hostile);
    return 0;
}
