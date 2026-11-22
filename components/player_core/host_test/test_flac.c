// SPDX-License-Identifier: LGPL-2.1-or-later
#include <math.h>
#include "pc/pc_backend.h"
#include "pc/pc_id3.h"
#include "pc_test.h"
#include "flac_gen.h"

static uint8_t *load(const char *name, uint32_t *len)
{
    char p[256];
    snprintf(p, sizeof p, "%s/%s", FIXTURES, name);
    FILE *f = fopen(p, "rb");
    CHECK(f != NULL);
    fseek(f, 0, SEEK_END);
    *len = (uint32_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc(*len);
    CHECK(fread(b, 1, *len, f) == *len);
    fclose(f);
    return b;
}

/* decodes everything; returns frames, fills zero-crossing count of the left channel and the peak */
static long drain(void *h, int *zc, float *peak)
{
    float buf[2 * 256], prev = 0;
    long total = 0;
    *zc = 0; *peak = 0;
    for (;;) {
        int n = pc_backend_flac.render(h, buf, 256);
        CHECK(n >= 0);
        if (n == 0) break;
        for (int i = 0; i < n; i++) {
            float l = buf[2 * i];
            CHECK(l >= -1.0f && l <= 1.0f);
            CHECK(fabsf(buf[2 * i] - buf[2 * i + 1]) < 1e-6f);   /* both fixtures carry the same tone in L and R */
            if ((prev < 0) != (l < 0)) (*zc)++;
            if (fabsf(l) > *peak) *peak = fabsf(l);
            prev = l;
        }
        total += n;
    }
    return total;
}

static void test_flac_mono_44k(void)
{
    uint32_t len;
    uint8_t *d = load("tone44.flac", &len);
    pc_reader_t r;
    pc_reader_mem(&r, d, len);
    void *h = NULL;
    CHECK_EQ_INT(pc_backend_flac.open(&h, &r, 0), 0);
    CHECK_EQ_INT(pc_backend_flac.rate(h), 44100);
    CHECK_NEAR(pc_backend_flac.length_ms(h), 500, 2);
    int zc; float pk;
    CHECK_EQ_INT(drain(h, &zc, &pk), 22050);              /* mono duplicated to both channels */
    CHECK_NEAR(zc, 1000, 4);                              /* 1 kHz for 0.5 s */
    CHECK(pk > 0.1f);
    CHECK_NEAR(pc_backend_flac.position_ms(h), 500, 2);
    pc_backend_flac.close(h);
    free(d);
}

static void test_flac_96k_native(void)
{
    uint32_t len;
    uint8_t *d = load("tone96.flac", &len);
    pc_reader_t r;
    pc_reader_mem(&r, d, len);
    void *h = NULL;
    CHECK_EQ_INT(pc_backend_flac.open(&h, &r, 0), 0);
    CHECK_EQ_INT(pc_backend_flac.rate(h), 96000);         /* hires T5: <= PC_SRC_MAX_RATE is native, the audio task plans the pipeline */
    int zc; float pk;
    long n = drain(h, &zc, &pk);
    CHECK_NEAR(n, 28800, 2);                              /* 0.3 s at 96k */
    CHECK_NEAR(zc, 600, 2);                               /* 1 kHz, no filter start-up */
    CHECK_NEAR(pk, 0.0884, 0.002);                        /* the file peaks at -21.07 dB */
    pc_backend_flac.close(h);
    free(d);
}

/* a generated 1 kHz tone at rate/24 bit, secs long: opens and returns the backend's rate, *frames = everything rendered */
static uint32_t gen_rate(uint32_t rate, double secs, long *frames)
{
    int fr = (int)(rate * secs);
    float *lr = malloc((size_t)fr * 2 * sizeof(float));
    for (int i = 0; i < fr; i++) lr[2 * i] = lr[2 * i + 1] = 0.25f * sinf(2 * (float)M_PI * 1000 * (float)i / (float)rate);
    size_t cap = 42 + (size_t)fr * 2 * 4 + 4096;
    uint8_t *g = malloc(cap);
    size_t n = flac_gen(g, cap, rate, 24, 2, 4096, lr, fr);
    CHECK(n > 0);
    pc_reader_t r;
    pc_reader_mem(&r, g, (uint32_t)n);
    void *h = NULL;
    CHECK_EQ_INT(pc_backend_flac.open(&h, &r, 0), 0);
    uint32_t got = pc_backend_flac.rate(h);
    float buf[2 * 480];
    int k;
    *frames = 0;
    while ((k = pc_backend_flac.render(h, buf, 480)) > 0) *frames += k;
    pc_backend_flac.close(h);
    free(g);
    free(lr);
    return got;
}

static void test_flac_192k_native(void)
{
    long n;
    CHECK_EQ_INT(gen_rate(192000, 0.1, &n), 192000);
    CHECK_EQ_INT(n, 19200);
}

static void test_flac_384k_native(void)                   /* hires T13: native at every rate, the backend never resamples (pc_halve does) */
{
    long n;
    CHECK_EQ_INT(gen_rate(384000, 0.05, &n), 384000);
    CHECK_EQ_INT(n, 19200);
}

static void test_flac_resume_position(void)
{
    uint32_t len;
    uint8_t *d = load("tone44.flac", &len);
    pc_reader_t r;
    pc_reader_mem(&r, d, len);
    void *h = NULL;
    float buf[2 * 256];
    CHECK_EQ_INT(pc_backend_flac.open(&h, &r, 0), 0);
    long got = 0;
    while (got < 11025) got += pc_backend_flac.render(h, buf, 256);
    uint32_t pos = pc_backend_flac.byte_pos(h);           /* opaque resume token */
    pc_backend_flac.close(h);
    CHECK_EQ_INT(pc_backend_flac.open(&h, &r, pos), 0);
    CHECK_NEAR(pc_backend_flac.position_ms(h), got * 1000 / 44100, 2);
    int zc; float pk;
    CHECK_NEAR(drain(h, &zc, &pk), 22050 - got, 1);
    pc_backend_flac.close(h);
    free(d);
}

static void test_flac_rejects_garbage(void)
{
    static const uint8_t junk[64] = { 'n', 'o', 't', ' ', 'f', 'l', 'a', 'c' };
    pc_reader_t r;
    pc_reader_mem(&r, junk, sizeof junk);
    void *h = NULL;
    CHECK(pc_backend_flac.open(&h, &r, 0) != 0);
}

static void test_flac_tags_via_id3_parse(void)
{
    uint32_t len;
    uint8_t *d = load("tone44.flac", &len);
    pc_reader_t r;
    pc_reader_mem(&r, d, len);
    pc_id3_t t;
    memset(&t, 0, sizeof t);
    CHECK_EQ_INT(pc_id3_parse(&r, &t), 0);
    CHECK_STR(t.title, "測試曲 Title");
    CHECK_STR(t.artist, "Artist A");
    CHECK_STR(t.album, "Album B");
    CHECK_EQ_INT(t.date, 20240517);
    CHECK_EQ_INT(t.track, 3);
    CHECK(t.apic_len > 100 && t.apic_off + t.apic_len <= len);
    CHECK(d[t.apic_off] == 0xFF && d[t.apic_off + 1] == 0xD8);   /* the JPEG itself */
    pc_id3_free(&t);
    free(d);
}

static void test_ogg_tags_via_id3_parse(void)
{
    uint32_t len;
    pc_reader_t r;
    pc_id3_t t;
    uint8_t *d = load("tone.opus", &len);
    pc_reader_mem(&r, d, len);
    CHECK_EQ_INT(pc_id3_parse(&r, &t), 0);
    CHECK_STR(t.title, "Opus 歌");
    CHECK_STR(t.artist, "Op Artist");
    CHECK_EQ_INT(t.date, 20210101);
    CHECK_EQ_INT(t.track, 7);
    pc_id3_free(&t);
    free(d);
    d = load("tone.ogg", &len);
    pc_reader_mem(&r, d, len);
    CHECK_EQ_INT(pc_id3_parse(&r, &t), 0);
    CHECK_STR(t.title, "Vorbis 歌");
    CHECK_STR(t.album, "V Album");
    CHECK_EQ_INT(t.date, 20191231);
    pc_id3_free(&t);
    free(d);
}

static void test_m4a_tags_via_id3_parse(void)
{
    uint32_t len;
    pc_reader_t r;
    pc_id3_t t;
    uint8_t *d = load("tone_aac.m4a", &len);
    pc_reader_mem(&r, d, len);
    CHECK_EQ_INT(pc_id3_parse(&r, &t), 0);
    CHECK_STR(t.title, "M4A 歌");
    CHECK_STR(t.artist, "M Artist");
    CHECK_STR(t.album, "M Album");
    CHECK_EQ_INT(t.date, 20180101);
    CHECK_EQ_INT(t.track, 5);
    pc_id3_free(&t);
    free(d);
    d = load("tone_alac.m4a", &len);
    pc_reader_mem(&r, d, len);
    CHECK_EQ_INT(pc_id3_parse(&r, &t), 0);
    CHECK_STR(t.title, "ALAC 歌");
    pc_id3_free(&t);
    free(d);
}

static void test_flac_members(void)
{
    uint32_t len;
    uint8_t *d = load("tone96.flac", &len);
    pc_reader_t r;
    pc_reader_mem(&r, d, len);
    void *h = NULL;
    CHECK_EQ_INT(pc_backend_flac.open(&h, &r, 0), 0);
    CHECK_STR(pc_backend_flac.name, "FLAC");
    CHECK_EQ_INT(pc_backend_flac.bits(h), (((d[20] & 1) << 4) | (d[21] >> 4)) + 1);   /* STREAMINFO bits per sample (the fixture is 24 bit) */
    CHECK_EQ_INT(pc_backend_flac.burst(h), (d[10] << 8) | d[11]);   /* STREAMINFO max block size */
    CHECK(pc_backend_flac.burst(h) > 0);
    pc_backend_flac.close(h);
    free(d);
    /* a generated 96k / 24 bit file with 16384-frame blocks */
    enum { FR = 20000 };
    static float lr[2 * FR];
    for (int i = 0; i < FR; i++) lr[2 * i] = lr[2 * i + 1] = 0.5f * sinf(2 * (float)M_PI * 1000 * i / 96000);
    size_t cap = 42 + (size_t)FR * 2 * 4 + 64, n;
    uint8_t *g = malloc(cap);
    n = flac_gen(g, cap, 96000, 24, 2, 16384, lr, FR);
    CHECK(n > 0);
    pc_reader_mem(&r, g, (uint32_t)n);
    CHECK_EQ_INT(pc_backend_flac.open(&h, &r, 0), 0);
    CHECK_EQ_INT(pc_backend_flac.bits(h), 24);
    CHECK_EQ_INT(pc_backend_flac.burst(h), 16384);
    CHECK_EQ_INT(pc_backend_flac.rate(h), 96000);       /* native since hires T5 */
    float buf[2 * 480];
    long total = 0;
    int k;
    pc_backend_flac.close(h);
    /* decoded samples against the input (a 44.1k twin) */
    n = flac_gen(g, cap, 44100, 24, 2, 16384, lr, FR);
    CHECK(n > 0);
    pc_reader_mem(&r, g, (uint32_t)n);
    CHECK_EQ_INT(pc_backend_flac.open(&h, &r, 0), 0);
    CHECK_EQ_INT(pc_backend_flac.rate(h), 44100);
    long at = 0;
    while (at < 1000) {
        k = pc_backend_flac.render(h, buf, 480);
        CHECK(k > 0);
        for (int i = 0; i < k && at < 1000; i++, at++) {
            CHECK_NEAR(buf[2 * i], lr[2 * at], 1.5 / 8388608.0);
            CHECK_NEAR(buf[2 * i + 1], lr[2 * at + 1], 1.5 / 8388608.0);
        }
    }
    pc_backend_flac.close(h);
    n = flac_gen(g, cap, 96000, 24, 2, 16384, lr, FR);
    pc_reader_mem(&r, g, (uint32_t)n);
    CHECK_EQ_INT(pc_backend_flac.open(&h, &r, 0), 0);
    while ((k = pc_backend_flac.render(h, buf, 480)) > 0) total += k;
    CHECK_EQ_INT(total, FR);                            /* native: every frame */
    pc_backend_flac.close(h);
    free(g);
}

int main(void)
{
    RUN(test_flac_members);
    RUN(test_flac_mono_44k);
    RUN(test_flac_96k_native);
    RUN(test_flac_192k_native);
    RUN(test_flac_384k_native);
    RUN(test_flac_resume_position);
    RUN(test_flac_rejects_garbage);
    RUN(test_flac_tags_via_id3_parse);
    RUN(test_ogg_tags_via_id3_parse);
    RUN(test_m4a_tags_via_id3_parse);
    return 0;
}
